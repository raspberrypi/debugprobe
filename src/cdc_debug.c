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


#define CDC_DEBUG_N    1

TaskHandle_t cdc_debug_taskhandle;

static uint16_t cdc_debug_fifo_read;
static uint16_t cdc_debug_fifo_write;
static uint8_t cdc_debug_fifo[4096];
static uint8_t cdc_debug_buf[CFG_TUD_CDC_TX_BUFSIZE];



bool cdc_debug_task(void)
/**
 * Transmit debug output via CDC with a timestamp on each line.
 * \return true -> characters have been transmitted
 */
{
    static int was_connected = 0;
    bool chars_sent = false;

    if (tud_cdc_n_connected(CDC_DEBUG_N)) {
        was_connected = 1;
        if (cdc_debug_fifo_read != cdc_debug_fifo_write) {
            int cnt;

            if (cdc_debug_fifo_read > cdc_debug_fifo_write) {
                cnt = sizeof(cdc_debug_fifo) - cdc_debug_fifo_read;
            } else {
                cnt = cdc_debug_fifo_write - cdc_debug_fifo_read;
            }
            cnt = MIN(cnt, sizeof(cdc_debug_buf));
            cnt = MIN(tud_cdc_n_write_available(CDC_DEBUG_N), cnt);
            memcpy(cdc_debug_buf, cdc_debug_fifo + cdc_debug_fifo_read, cnt);
            cdc_debug_fifo_read = (cdc_debug_fifo_read + cnt) % sizeof(cdc_debug_fifo);
            tud_cdc_n_write(CDC_DEBUG_N, cdc_debug_buf, cnt);
            tud_cdc_n_write_flush(CDC_DEBUG_N);
            chars_sent = true;
        }
    } else if (was_connected) {
        tud_cdc_n_write_clear(CDC_DEBUG_N);
        was_connected = 0;
    }
    return chars_sent;
}   // cdc_debug_task



void cdc_debug_thread(void* ptr)
{
    for (;;) {
        if (cdc_debug_task())
            vTaskDelay(1);
        else
            vTaskDelay((10 * configTICK_RATE_HZ) / 1000);          // 10ms
    }
}   // cdc_debug_thread



static void cdc_to_fifo(const char* buf, int max_cnt)
{
    const char* buf_pnt;
    int cnt;

    buf_pnt = buf;
    while (max_cnt > 0 && (cdc_debug_fifo_write + 1) % sizeof(cdc_debug_fifo) != cdc_debug_fifo_read) {
        if (cdc_debug_fifo_read > cdc_debug_fifo_write) {
            cnt = (cdc_debug_fifo_read - 1) - cdc_debug_fifo_write;
        } else {
            cnt = sizeof(cdc_debug_fifo) - cdc_debug_fifo_write;
        }
        cnt = MIN(cnt, max_cnt);
        memcpy(cdc_debug_fifo + cdc_debug_fifo_write, buf_pnt, cnt);
        buf_pnt += cnt;
        max_cnt -= cnt;
        cdc_debug_fifo_write = (cdc_debug_fifo_write + cnt) % sizeof(cdc_debug_fifo);
    }
} // cdc_to_fifo



int cdc_debug_printf(const char* format, ...)
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
    char buf[100];
    char tbuf[30];
    int cnt;
    int ndx = 0;
    const char* p;

    //
    // print formatted text into buffer
    //
    va_list va;
    va_start(va, format);
    const int total_cnt = vsnprintf((char*)buf, sizeof(buf), format, va);
    va_end(va);

    tbuf[0] = 0;

    while (ndx < total_cnt) {
        if (newline) {
            newline = false;

            if (tbuf[0] == 0) {
                //
                // more or less intelligent time stamp which allows better measurements:
                // - show delta
                // - reset time if there hase been no activity for 5s
                //
                now_ms = (uint32_t)(time_us_64() / 1000) - base_ms;
                if (now_ms - prev_ms > 5000) {
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
        if (p == NULL) {
            cnt = total_cnt - ndx;
        } else {
            cnt = (p - (buf + ndx)) + 1;
            newline = true;
        }

        cdc_to_fifo(buf + ndx, cnt);

        ndx += cnt;
    }

    return total_cnt;
} // cdc_debug_printf
