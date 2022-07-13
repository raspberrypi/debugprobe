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

static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];
static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];

void cdc_uart_init(void) {
    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    gpio_set_pulls(PICOPROBE_UART_TX, 1, 0);
    gpio_set_pulls(PICOPROBE_UART_RX, 1, 0);
    uart_init(PICOPROBE_UART_INTERFACE, PICOPROBE_UART_BAUDRATE);
}

void cdc_task(void)
{
    static int was_connected = 0;
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
          written = MIN(tud_cdc_write_available(), rx_len);
          if (written > 0) {
            tud_cdc_write(rx_buf, written);
            tud_cdc_write_flush();
          }
        }

      /* Reading from a firehose and writing to a FIFO. */
      size_t watermark = MIN(tud_cdc_available(), sizeof(tx_buf));
      if (watermark > 0) {
        size_t tx_len;
        /* Batch up to half a FIFO of data - don't clog up on RX */
        watermark = MIN(watermark, 16);
        tx_len = tud_cdc_read(tx_buf, watermark);
        uart_write_blocking(PICOPROBE_UART_INTERFACE, tx_buf, tx_len);
      }
    } else if (was_connected) {
      tud_cdc_write_clear();
      was_connected = 0;
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
  /* Set the tick thread interval to the amount of time it takes to
   * fill up half a FIFO. Millis is too coarse for integer divide.
   */
  uint32_t micros = (1000 * 1000 * 16 * 10) / MAX(line_coding->bit_rate, 1);

  /* Modifying state, so park the thread before changing it. */
  vTaskSuspend(uart_taskhandle);
  interval = MAX(1, micros / ((1000 * 1000) / configTICK_RATE_HZ));
  picoprobe_info("New baud rate %d micros %d interval %u\n",
                  line_coding->bit_rate, micros, interval);
  uart_deinit(PICOPROBE_UART_INTERFACE);
  tud_cdc_write_clear();
  tud_cdc_read_flush();
  uart_init(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);
  vTaskResume(uart_taskhandle);
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  /* CDC drivers use linestate as a bodge to activate/deactivate the interface.
   * Resume our UART polling on activate, stop on deactivate */
  if (!dtr && !rts)
    vTaskSuspend(uart_taskhandle);
  else
    vTaskResume(uart_taskhandle);
}
