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
#include <stdbool.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"

#include "tusb.h"

#include "picoprobe_config.h"


#define CDC_DEBUG_N           1

#define STREAM_PRINTF_SIZE    30000
#define STREAM_PRINTF_TRIGGER 32

static TaskHandle_t           task_printf = NULL;
static SemaphoreHandle_t      sema_printf;
static StreamBufferHandle_t   stream_printf;

static uint8_t cdc_debug_buf[CFG_TUD_CDC_TX_BUFSIZE];



void cdc_debug_thread(void *ptr)
/**
 * Transmit debug output via CDC
 */
{
    bool was_connected = false;

    for (;;) {
        if (tud_cdc_n_connected(CDC_DEBUG_N)) {
            size_t cnt;
            size_t max_cnt;

            if ( !was_connected) {
                // wait here some time (until my terminal program is ready)
                was_connected = true;
                vTaskDelay(pdMS_TO_TICKS(2000));
            }

            max_cnt = tud_cdc_n_write_available(CDC_DEBUG_N);
            if (max_cnt == 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            else {
                max_cnt = MIN(sizeof(cdc_debug_buf), max_cnt);
                cnt = xStreamBufferReceive(stream_printf, cdc_debug_buf, max_cnt, pdMS_TO_TICKS(50));
                if (cnt != 0) {
                    tud_cdc_n_write(CDC_DEBUG_N, cdc_debug_buf, cnt);
                    tud_cdc_n_write_flush(CDC_DEBUG_N);
                }
            }
        }
        else {
            if (was_connected) {
                tud_cdc_n_write_clear(CDC_DEBUG_N);
                was_connected = false;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}   // cdc_debug_thread



int cdc_debug_printf(const char* format, ...)
/**
 * Debug printf()
 * At the beginning of each output line a timestamp is inserted.
 */
{
    static uint32_t prev_ms;
    static uint32_t base_ms;
    static bool newline = true;
    uint32_t now_ms;
    uint32_t d_ms;
    char buf[128];
    char tbuf[30];
    int cnt;
    int ndx = 0;
    const char *p;

    if (task_printf == NULL)
        return -1;

    xSemaphoreTake(sema_printf, portMAX_DELAY);

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
                // - reset time if there has been no activity for 5s
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
            xStreamBufferSend(stream_printf, tbuf, strnlen(tbuf, sizeof(tbuf)), 1);
        }

        p = memchr(buf + ndx, '\n', total_cnt - ndx);
        if (p == NULL) {
            cnt = total_cnt - ndx;
        } else {
            cnt = (p - (buf + ndx)) + 1;
            newline = true;
        }

        xStreamBufferSend(stream_printf, buf + ndx, cnt, 1);

        ndx += cnt;
    }
    xSemaphoreGive(sema_printf);

    return total_cnt;
} // cdc_debug_printf



void cdc_debug_init(UBaseType_t task_prio)
{
    stream_printf = xStreamBufferCreate(STREAM_PRINTF_SIZE, STREAM_PRINTF_TRIGGER);
    if (stream_printf == NULL) {
        panic("cdc_debug_init: cannot create stream_printf\n");
    }

    sema_printf = xSemaphoreCreateBinary();
    if (sema_printf == NULL) {
        panic("cdc_debug_init: cannot create sema_printf\n");
    }
    else {
        xSemaphoreGive(sema_printf);
    }

    xTaskCreate(cdc_debug_thread, "CDC_DEB", configMINIMAL_STACK_SIZE+1024, NULL, task_prio, &task_printf);
}   // cdc_debug_init
