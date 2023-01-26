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
#include "event_groups.h"

#include "tusb.h"

#include "picoprobe_config.h"


#define STREAM_PRINTF_SIZE    2048
#define STREAM_PRINTF_TRIGGER 32

static TaskHandle_t           task_printf = NULL;
static SemaphoreHandle_t      sema_printf;
static StreamBufferHandle_t   stream_printf;

#define EV_TX_COMPLETE        0x01
#define EV_STREAM             0x02

/// event flags
static EventGroupHandle_t events;

static uint8_t cdc_debug_buf[CFG_TUD_CDC_TX_BUFSIZE];

static bool was_connected = false;



void cdc_debug_thread(void *ptr)
/**
 * Transmit debug output via CDC
 */
{
    for (;;) {
        if ( !was_connected) {
            // wait here some time (until my terminal program is ready)
            was_connected = true;
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (xStreamBufferIsEmpty(stream_printf)) {
            // -> end of transmission: flush and sleep for a long time
            tud_cdc_n_write_flush(CDC_DEBUG_N);
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        }
        else {
            size_t cnt;
            size_t max_cnt;

            max_cnt = tud_cdc_n_write_available(CDC_DEBUG_N);
            if (max_cnt == 0) {
                // -> sleep for a short time, actually wait until data transmitted via USB
                xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
            }
            else {
                max_cnt = MIN(sizeof(cdc_debug_buf), max_cnt);
                cnt = xStreamBufferReceive(stream_printf, cdc_debug_buf, max_cnt, pdMS_TO_TICKS(500));
                if (cnt != 0) {
                    tud_cdc_n_write(CDC_DEBUG_N, cdc_debug_buf, cnt);
                }
            }
        }
    }
}   // cdc_debug_thread



void cdc_debug_line_state_cb(bool dtr, bool rts)
{
    /* CDC drivers use linestate as a bodge to activate/deactivate the interface.
     * Resume our UART polling on activate, stop on deactivate */
    if (!dtr  &&  !rts) {
        vTaskSuspend(task_printf);
        tud_cdc_n_write_clear(CDC_DEBUG_N);
        was_connected = false;
    }
    else {
        vTaskResume(task_printf);
    }
}   // cdc_debug_line_state_cb



void cdc_debug_tx_complete_cb(void)
{
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_debug_tx_complete_cb



int cdc_debug_printf(const char* format, ...)
/**
 * Debug printf()
 * At the beginning of each output line a timestamp is inserted.
 */
{
    static uint32_t prev_ms;
    static uint32_t base_ms;
    static bool newline = true;
    static char buf[128];            // buffers can be put into static so that it does not stress the task stack
    static char tbuf[30];
    int ndx = 0;

    if (task_printf == NULL)
        return -1;

    if (portCHECK_IF_IN_ISR()) {
        // we suppress those messages silently
        return -1;
    }

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
        const char *p;
        int cnt;

        if (newline) {
            newline = false;

            if (tbuf[0] == 0) {
                //
                // more or less intelligent time stamp which allows better measurements:
                // - show delta
                // - reset time if there has been no activity for 5s
                //
                uint32_t now_ms;
                uint32_t d_ms;

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

    xEventGroupSetBits(events, EV_STREAM);

    return total_cnt;
} // cdc_debug_printf



void cdc_debug_init(uint32_t task_prio)
{
    events = xEventGroupCreate();

    stream_printf = xStreamBufferCreate(STREAM_PRINTF_SIZE, STREAM_PRINTF_TRIGGER);
    if (stream_printf == NULL) {
        panic("cdc_debug_init: cannot create stream_printf\n");
    }

    sema_printf = xSemaphoreCreateMutex();
    if (sema_printf == NULL) {
        panic("cdc_debug_init: cannot create sema_printf\n");
    }

    xTaskCreate(cdc_debug_thread, "CDC_DEB", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_printf);
    cdc_debug_line_state_cb(false, false);
}   // cdc_debug_init
