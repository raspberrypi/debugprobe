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

/* Max 1 FIFO worth of data */
static uint8_t tx_buf[32];
static uint8_t rx_buf[32];
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

#ifdef PICOPROBE_UART_RTS
    gpio_init(PICOPROBE_UART_RTS);
    gpio_set_dir(PICOPROBE_UART_RTS, GPIO_OUT);
    gpio_put(PICOPROBE_UART_RTS, 1);
#endif
#ifdef PICOPROBE_UART_DTR
    gpio_init(PICOPROBE_UART_DTR);
    gpio_set_dir(PICOPROBE_UART_DTR, GPIO_OUT);
    gpio_put(PICOPROBE_UART_DTR, 1);
#endif
}

void cdc_task(void)
{
    static int was_connected = 0;
    static uint cdc_tx_oe = 0;
    uint rx_len = 0;

    // Consume uart fifo regardless even if not connected
    while(uart_is_readable(PICOPROBE_UART_INTERFACE) && (rx_len < sizeof(rx_buf))) {
        rx_buf[rx_len++] = uart_getc(PICOPROBE_UART_INTERFACE);
    }

    if (tud_cdc_connected()) {
        was_connected = 1;
        int written = 0;
        /* Implicit overflow if we don't write all the bytes to the host.
         * Also throw away bytes if we can't write... */
        if (rx_len) {
#ifdef PICOPROBE_UART_RX_LED
          gpio_put(PICOPROBE_UART_RX_LED, 1);
          rx_led_debounce = debounce_ticks;
#endif
          written = MIN(tud_cdc_write_available(), rx_len);
          if (rx_len > written)
              cdc_tx_oe++;

          if (written > 0) {
            tud_cdc_write(rx_buf, written);
            tud_cdc_write_flush();
          }
        } else {
#ifdef PICOPROBE_UART_RX_LED
          if (rx_led_debounce)
            rx_led_debounce--;
          else
            gpio_put(PICOPROBE_UART_RX_LED, 0);
#endif
        }

      /* Reading from a firehose and writing to a FIFO. */
      size_t watermark = MIN(tud_cdc_available(), sizeof(tx_buf));
      if (watermark > 0) {
        size_t tx_len;
#ifdef PICOPROBE_UART_TX_LED
        gpio_put(PICOPROBE_UART_TX_LED, 1);
        tx_led_debounce = debounce_ticks;
#endif
        /* Batch up to half a FIFO of data - don't clog up on RX */
        watermark = MIN(watermark, 16);
        tx_len = tud_cdc_read(tx_buf, watermark);
        uart_write_blocking(PICOPROBE_UART_INTERFACE, tx_buf, tx_len);
      } else {
#ifdef PICOPROBE_UART_TX_LED
          if (tx_led_debounce)
            tx_led_debounce--;
          else
            gpio_put(PICOPROBE_UART_TX_LED, 0);
#endif
      }
    } else if (was_connected) {
      tud_cdc_write_clear();
      was_connected = 0;
      cdc_tx_oe = 0;
    }
}

void cdc_thread(void *ptr)
{
  BaseType_t delayed;
  last_wake = xTaskGetTickCount();
  /* Threaded with a polling interval that scales according to linerate */
  while (1) {
    cdc_task();
    delayed = xTaskDelayUntil(&last_wake, interval);
    if (delayed == pdFALSE)
      last_wake = xTaskGetTickCount();
  }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding)
{
  uart_parity_t parity;
  uint data_bits, stop_bits;
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
  uart_deinit(PICOPROBE_UART_INTERFACE);
  tud_cdc_write_clear();
  tud_cdc_read_flush();
  uart_init(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);

  switch (line_coding->parity) {
  case CDC_LINE_CODING_PARITY_ODD:
    parity = UART_PARITY_ODD;
    break;
  case CDC_LINE_CODING_PARITY_EVEN:
    parity = UART_PARITY_EVEN;
    break;
  default:
    picoprobe_info("invalid parity setting %u\n", line_coding->parity);
    /* fallthrough */
  case CDC_LINE_CODING_PARITY_NONE:
    parity = UART_PARITY_NONE;
    break;
  }

  switch (line_coding->data_bits) {
  case 5:
  case 6:
  case 7:
  case 8:
    data_bits = line_coding->data_bits;
    break;
  default:
    picoprobe_info("invalid data bits setting: %u\n", line_coding->data_bits);
    data_bits = 8;
    break;
  }

  /* The PL011 only supports 1 or 2 stop bits. 1.5 stop bits is translated to 2,
   * which is safer than the alternative. */
  switch (line_coding->stop_bits) {
  case CDC_LINE_CONDING_STOP_BITS_1_5:
  case CDC_LINE_CONDING_STOP_BITS_2:
    stop_bits = 2;
  break;
  default:
    picoprobe_info("invalid stop bits setting: %u\n", line_coding->stop_bits);
    /* fallthrough */
  case CDC_LINE_CONDING_STOP_BITS_1:
    stop_bits = 1;
  break;
  }

  uart_set_format(PICOPROBE_UART_INTERFACE, data_bits, stop_bits, parity);
  vTaskResume(uart_taskhandle);
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
#ifdef PICOPROBE_UART_RTS
  gpio_put(PICOPROBE_UART_RTS, !rts);
#endif
#ifdef PICOPROBE_UART_DTR
  gpio_put(PICOPROBE_UART_DTR, !dtr);
#endif

  /* CDC drivers use linestate as a bodge to activate/deactivate the interface.
   * Resume our UART polling on activate, stop on deactivate */
  if (!dtr && !rts) {
    vTaskSuspend(uart_taskhandle);
#ifdef PICOPROBE_UART_RX_LED
    gpio_put(PICOPROBE_UART_RX_LED, 0);
    rx_led_debounce = 0;
#endif
#ifdef PICOPROBE_UART_RX_LED
    gpio_put(PICOPROBE_UART_TX_LED, 0);
    tx_led_debounce = 0;
#endif
  } else
    vTaskResume(uart_taskhandle);
}
