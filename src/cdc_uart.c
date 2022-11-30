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

#include <stdarg.h>
#include <pico/stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

#include "tusb.h"

#include "picoprobe_config.h"

TaskHandle_t uart_taskhandle;
TickType_t last_wake, interval = 100;

static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];
static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];

static uint16_t cdc_debug_fifo_read;
static uint16_t cdc_debug_fifo_write;
static uint8_t cdc_debug_fifo[4096];
static uint8_t cdc_debug_buf[256];

void cdc_uart_init(void) {
    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    gpio_set_pulls(PICOPROBE_UART_TX, 1, 0);
    gpio_set_pulls(PICOPROBE_UART_RX, 1, 0);
    uart_init(PICOPROBE_UART_INTERFACE, PICOPROBE_UART_BAUDRATE);
    cdc_debug_fifo_read = 0;
    cdc_debug_fifo_write = 0;
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
        else if (cdc_debug_fifo_read != cdc_debug_fifo_write) {
          int cnt;

          if (cdc_debug_fifo_read > cdc_debug_fifo_write) {
            cnt = sizeof(cdc_debug_fifo) - cdc_debug_fifo_read;
          }
          else {
            cnt = cdc_debug_fifo_write - cdc_debug_fifo_read;
          }
          cnt = MIN(cnt, sizeof(cdc_debug_buf));
          memcpy(cdc_debug_buf, cdc_debug_fifo + cdc_debug_fifo_read, cnt);
          cdc_debug_fifo_read = (cdc_debug_fifo_read + cnt) % sizeof(cdc_debug_fifo);
          tud_cdc_write(cdc_debug_buf, cnt);
          tud_cdc_write_flush();
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
  picoprobe_info("New baud rate %ld micros %ld interval %lu\n",
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



void cdc_to_fifo(const char *buf, int max_cnt)
{
    const char *buf_pnt;
    int cnt;

    buf_pnt = buf;
    while (max_cnt > 0 && (cdc_debug_fifo_write + 1) % sizeof(cdc_debug_fifo) != cdc_debug_fifo_read)
    {
        if (cdc_debug_fifo_read > cdc_debug_fifo_write)
        {
            cnt = (cdc_debug_fifo_read - 1) - cdc_debug_fifo_write;
        }
        else
        {
            cnt = sizeof(cdc_debug_fifo) - cdc_debug_fifo_write;
        }
        cnt = MIN(cnt, max_cnt);
        memcpy(cdc_debug_fifo + cdc_debug_fifo_write, buf_pnt, cnt);
        buf_pnt += cnt;
        max_cnt -= cnt;
        cdc_debug_fifo_write = (cdc_debug_fifo_write + cnt) % sizeof(cdc_debug_fifo);
    }
}   // cdc_to_fifo



int cdc_printf(const char* format, ...)
/**
 * Debug printf()
 * Note that at the beginning of each output a timestamp is inserted.  Which means, that each call to cdc_printf()
 * should output a line.
 */
{
    static uint32_t prev_ms;
    static uint32_t base_ms;
    static bool newline = true;
    uint32_t now_ms;
    uint32_t d_ms;
    char buf[256];
    char tbuf[30];
    int cnt;
    int ndx = 0;
    const char *p;

    //
    // print formatted text into buffer
    //
    va_list va;
    va_start(va, format);
    const int total_cnt = vsnprintf((char *)buf, sizeof(buf), format, va);
    va_end(va);

    tbuf[0] = 0;

    while (ndx < total_cnt)
    {
        if (newline)
        {
            newline = false;

            if (tbuf[0] == 0)
            {
                //
                // more or less intelligent time stamp which allows better measurements:
                // - show delta
                // - reset time if there hase been no activity for 5s
                //
                now_ms = (uint32_t)(time_us_64() / 1000) - base_ms;
                if (now_ms - prev_ms > 5000)
                {
                    base_ms = (uint32_t)(time_us_64() / 1000);
                    now_ms = 0;
                    prev_ms = 0;
                }
                d_ms = (uint32_t)(now_ms - prev_ms);
                d_ms = MIN(d_ms, 999);
                snprintf(tbuf, sizeof(tbuf), "%lu.%03lu (%3lu) - ", now_ms / 1000, now_ms % 1000, d_ms);
                prev_ms = now_ms;
            }
            cdc_to_fifo(tbuf, strnlen(tbuf, sizeof(tbuf)));
        }

        p = memchr(buf + ndx, '\n', total_cnt - ndx);
        if (p == NULL)
        {
            cnt = total_cnt - ndx;
        }
        else {
            cnt = (p - (buf + ndx)) + 1;
            newline = true;
        }

        cdc_to_fifo(buf + ndx, cnt);

        ndx += cnt;
    }

    return total_cnt;
}   // cdc_printf
