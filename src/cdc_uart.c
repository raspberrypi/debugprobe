/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <pico/stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

#include "tusb.h"

#include "picoprobe_config.h"

TaskHandle_t uart_taskhandle;
TickType_t last_wake, interval = 100;

static uint8_t tx_buf[CDC_UARTS][CFG_TUD_CDC_TX_BUFSIZE];
static uint8_t rx_buf[CDC_UARTS][CFG_TUD_CDC_RX_BUFSIZE];
static int was_connected[CDC_UARTS];

// Actually s^-1 so 25ms
#define DEBOUNCE_MS 40
static uint debounce_ticks = 5;

#ifdef PICOPROBE_UART_TX_LED
static uint tx_led_debounce;
#endif

#ifdef PICOPROBE_UART_RX_LED
static uint rx_led_debounce;
#endif

void cdc_uart_init(void) {
    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    gpio_set_pulls(PICOPROBE_UART_TX, 1, 0);
    gpio_set_pulls(PICOPROBE_UART_RX, 1, 0);
    uart_init(PICOPROBE_UART_INTERFACE, PICOPROBE_UART_BAUDRATE);

#if CDC_UARTS == 2
    gpio_set_function(PICOPROBE_EXTRA_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_EXTRA_UART_RX, GPIO_FUNC_UART);
    gpio_set_pulls(PICOPROBE_EXTRA_UART_TX, 1, 0);
    gpio_set_pulls(PICOPROBE_EXTRA_UART_RX, 1, 0);
    uart_init(PICOPROBE_EXTRA_UART_INTERFACE, PICOPROBE_EXTRA_UART_BAUDRATE);
#endif
    for (int n = 0; n < CDC_UARTS; n++) { was_connected[n] = 0; }
}

void cdc_task(uint8_t tty)
{
    uint rx_len = 0;

    uart_inst_t* uart_ptr = PICOPROBE_UART_INTERFACE;
#if CDC_UARTS == 2
    if (tty) { uart_ptr = PICOPROBE_EXTRA_UART_INTERFACE; }
#endif

    // Consume uart fifo regardless even if not connected
    while(uart_is_readable(uart_ptr) && (rx_len < sizeof(rx_buf[tty]))) {
        rx_buf[tty][rx_len++] = uart_getc(uart_ptr);
    }

    if (tud_cdc_n_connected(tty)) {
        was_connected[tty] = 1;
        int written = 0;
        /* Implicit overflow if we don't write all the bytes to the host.
         * Also throw away bytes if we can't write... */
        if (rx_len) {
          if (!tty) {
#ifdef PICOPROBE_UART_RX_LED
            gpio_put(PICOPROBE_UART_RX_LED, 1);
            rx_led_debounce = debounce_ticks;
#endif
          }
          written = MIN(tud_cdc_n_write_available(tty), rx_len);
          if (written > 0) {
            tud_cdc_n_write(tty, rx_buf[tty], written);
            tud_cdc_n_write_flush(tty);
          }
        } else {
          if (!tty) {
#ifdef PICOPROBE_UART_RX_LED
            if (rx_led_debounce)
              rx_led_debounce--;
            else
              gpio_put(PICOPROBE_UART_RX_LED, 0);
#endif
          }
        }

      /* Reading from a firehose and writing to a FIFO. */
      size_t watermark = MIN(tud_cdc_n_available(tty), sizeof(tx_buf[tty]));
      if (watermark > 0) {
        size_t tx_len;
        if (!tty) {
#ifdef PICOPROBE_UART_TX_LED
          gpio_put(PICOPROBE_UART_TX_LED, 1);
          tx_led_debounce = debounce_ticks;
#endif
        }
        /* Batch up to half a FIFO of data - don't clog up on RX */
        watermark = MIN(watermark, 16);
        tx_len = tud_cdc_n_read(tty, tx_buf[tty], watermark);
        uart_write_blocking(uart_ptr, tx_buf[tty], tx_len);
      } else {
        if (!tty) {
#ifdef PICOPROBE_UART_TX_LED
          if (tx_led_debounce)
            tx_led_debounce--;
          else
            gpio_put(PICOPROBE_UART_TX_LED, 0);
#endif
        }
      }
    } else if (was_connected[tty]) {
      tud_cdc_n_write_clear(tty);
      was_connected[tty] = 0;
    }
}

void cdc_tasks(void) {
    for (int tty = 0; tty < CDC_UARTS; tty++) { cdc_task(tty); }
}

void cdc_thread(void *ptr)
{
  BaseType_t delayed;
  last_wake = xTaskGetTickCount();
  /* Threaded with a polling interval that scales according to linerate */
  while (1) {
    cdc_tasks();
    delayed = xTaskDelayUntil(&last_wake, interval);
    if (delayed == pdFALSE)
      last_wake = xTaskGetTickCount();
  }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding)
{
  uint8_t tty = itf;
  /* Set the tick thread interval to the amount of time it takes to
   * fill up half a FIFO. Millis is too coarse for integer divide.
   */
  uint32_t micros = (1000 * 1000 * 16 * 10) / MAX(line_coding->bit_rate, 1);
  /* Modifying state, so park the thread before changing it. */
  vTaskSuspend(uart_taskhandle);
  interval = MAX(1, micros / ((1000 * 1000) / configTICK_RATE_HZ));
  debounce_ticks = MAX(1, configTICK_RATE_HZ / (interval * DEBOUNCE_MS));
  picoprobe_info("New baud rate %ld micros %ld interval %lu\n",
                  line_coding->bit_rate, micros, interval);

  uart_inst_t* uart_ptr = PICOPROBE_UART_INTERFACE;
#if CDC_UARTS == 2
  if (tty) { uart_ptr = PICOPROBE_EXTRA_UART_INTERFACE; }
#endif

  uart_deinit(uart_ptr);
  tud_cdc_n_write_clear(tty);
  tud_cdc_n_read_flush(tty);
  uart_init(uart_ptr, line_coding->bit_rate);
  vTaskResume(uart_taskhandle);
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  uint8_t tty = itf;
  /* CDC drivers use linestate as a bodge to activate/deactivate the interface.
   * Resume our UART polling on activate, stop on deactivate */
  if (!dtr && !rts) {
    vTaskSuspend(uart_taskhandle);
    if (!tty) {
#ifdef PICOPROBE_UART_RX_LED
      gpio_put(PICOPROBE_UART_RX_LED, 0);
      rx_led_debounce = 0;
#endif
#ifdef PICOPROBE_UART_RX_LED
      gpio_put(PICOPROBE_UART_TX_LED, 0);
      tx_led_debounce = 0;
#endif
    }
  } else
    vTaskResume(uart_taskhandle);
}
