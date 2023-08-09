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

/**
 * Send probe debug output via CDC to host.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"
#include "event_groups.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "minIni/minIni.h"


#define STREAM_PRINTF_SIZE    4096
#define STREAM_PRINTF_TRIGGER 32

static TaskHandle_t           task_printf = NULL;
static SemaphoreHandle_t      sema_printf;
static StreamBufferHandle_t   stream_printf;

#define EV_TX_COMPLETE        0x01
#define EV_STREAM             0x02
#define EV_RX                 0x04

/// event flags
static EventGroupHandle_t events;

static uint8_t cdc_debug_buf[CFG_TUD_CDC_TX_BUFSIZE];

static volatile bool m_connected = false;

static void cdc_debug_command_if(uint8_t ch);


void cdc_debug_thread(void *ptr)
/**
 * Transmit debug output via CDC
 */
{
    for (;;) {
        uint32_t cdc_rx_chars;
        
        if ( !m_connected) {
            // wait here until connected (and until my terminal program is ready)
            while ( !m_connected) {
                xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

		cdc_rx_chars = tud_cdc_n_available(CDC_DEBUG_N);
        if (cdc_rx_chars == 0  &&  xStreamBufferIsEmpty(stream_printf)) {
            // -> end of transmission: flush and sleep for a long time (or until new data is available)
            tud_cdc_n_write_flush(CDC_DEBUG_N);
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        }
        else if (cdc_rx_chars != 0) {
        }
        else {
            size_t cnt;
            size_t max_cnt;

            max_cnt = tud_cdc_n_write_available(CDC_DEBUG_N);
            if (max_cnt == 0) {
                // -> sleep for a short time, actually wait until data transmitted via USB
                xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
            }
            else {
                max_cnt = MIN(sizeof(cdc_debug_buf), max_cnt);
                cnt = xStreamBufferReceive(stream_printf, cdc_debug_buf, max_cnt, pdMS_TO_TICKS(500));
                if (cnt != 0) {
                    tud_cdc_n_write(CDC_DEBUG_N, cdc_debug_buf, cnt);
                }
            }
        }

        if (cdc_rx_chars != 0) {
            //
            // eat receive characters (don't know if this has any effects, but who knows)
            //
            uint8_t ch;

            tud_cdc_n_read(CDC_DEBUG_N, &ch, sizeof(ch));
            cdc_debug_command_if(ch);
        }
    }
}   // cdc_debug_thread



void cdc_debug_line_state_cb(bool dtr, bool rts)
/**
 * Flush tinyusb buffers on connect/disconnect.
 * This seems to be necessary to survive e.g. a restart of the host (Linux)
 */
{
    tud_cdc_n_write_clear(CDC_DEBUG_N);
    tud_cdc_n_read_flush(CDC_DEBUG_N);
    m_connected = (dtr  ||  rts);
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_debug_line_state_cb



void cdc_debug_tx_complete_cb(void)
{
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_debug_tx_complete_cb



void cdc_debug_rx_cb(void)
{
    xEventGroupSetBits(events, EV_RX);
}   // cdc_debug_rx_cb



static void cdc_debug_command_if(uint8_t ch)
/**
 * Command interpreter.
 * Description in README.adoc.
 *
 * \note
 *    Writing / erasing config flash is somehow cumbersome.  After each write operation to the flash,
 *    the RP2040 has currently be restarted to recover.  Nevertheless, for reconfiguration a restart
 *    is required anyway, so actually no harm.
 */
{
    static char cmd[20];
    static int ch_cnt;
    static bool unlocked;
    bool echo_cmd = false;

    if (isprint(ch)) {
        // put regular characters into buffer
        if (ch_cnt < sizeof(cmd) - 1) {
            cmd[ch_cnt++] = (char)ch;
        }
        echo_cmd = true;
    }
    else if (ch == '\b') {
        // backspace
        if (ch_cnt > 0) {
            --ch_cnt;
            echo_cmd = true;
        }
    }
    else if (ch_cnt == 0) {
        // simple unlock if no pwd set
        char pwd[20];

        ini_gets(MININI_SECTION, "pwd", "", pwd, sizeof(pwd), MININI_FILENAME);
        if (pwd[0] == '\0') {
            if ( !unlocked) {
                picoprobe_info("unlocked\n");
                unlocked = true;
            }
        }
    }
    else if (ch == '\r'  ||  ch == '\n') {
        // line end
        char *p;

        picoprobe_info_out("\n");

        p = strchr(cmd, ':');
        if (p != NULL) {
            char pwd[20];

            *p = '\0';
            ++p;
            if (strcmp(cmd, "pwd") == 0) {
                ini_gets(MININI_SECTION, "pwd", "", pwd, sizeof(pwd), MININI_FILENAME);
                unlocked = (strcmp(p, pwd) == 0);
                picoprobe_info("%s\n", unlocked ? "unlocked" : "locked: wrong password");
            }
            else {
                picoprobe_error("unknown cmd: '%s'\n", cmd);
            }
        }
        else if (unlocked) {
            p = strchr(cmd, '=');
            if (p != NULL) {
                *p = '\0';
                ++p;

                if (    strcmp(cmd, "net") == 0
                    ||  strcmp(cmd, "f_cpu") == 0
                    ||  strcmp(cmd, "f_swd") == 0
                    ||  strcmp(cmd, "pwd") ==0) {
                    multicore_reset_core1();
                    taskDISABLE_INTERRUPTS();
                    if (*p == '\0') {
                        ini_puts(MININI_SECTION, cmd, NULL, MININI_FILENAME);
                    }
                    else {
                        ini_puts(MININI_SECTION, cmd, p, MININI_FILENAME);
                    }
                    watchdog_enable(0, 0);
                    for (;;) {
                    }
                }
                else {
                    picoprobe_error("unknown var: '%s'\n", cmd);
                }
            }
            else if (strcmp(cmd, "lock") == 0) {
                picoprobe_info("locked\n");
                unlocked = false;
            }
            else if (strcmp(cmd, "show") == 0) {
                ini_print_all();
            }
            else if (strcmp(cmd, "killall") == 0) {
                multicore_reset_core1();
                taskDISABLE_INTERRUPTS();
                ini_remove(MININI_FILENAME);
                watchdog_enable(0, 0);
                for (;;) {
                }
            }
            else if (strcmp(cmd, "reset") == 0) {
                watchdog_enable(0, 0);
                for (;;) {
                }
            }
            else {
                picoprobe_error("unknown cmd: '%s'\n", cmd);
            }
        }
        else {
            picoprobe_error("must be unlocked\n");
        }
        ch_cnt = 0;
    }

    cmd[ch_cnt] = '\0';
    if (echo_cmd) {
        picoprobe_info_out("                  \rcmd: %s        \b\b\b\b\b\b\b\b", cmd);
    }
}   // cdc_debug_command_if



static void cdc_debug_put_into_stream(const void *data, size_t len)
/**
 * Write into stream.  If not connected use the stream as a FIFO and drop old content.
 */
{
    if ( !m_connected) {
        size_t available = xStreamBufferSpacesAvailable(stream_printf);
        for (;;) {
            uint8_t dummy[64];
            size_t n;

            if (available >= len) {
                break;
            }
            n = xStreamBufferReceive(stream_printf, dummy, sizeof(dummy), 0);
            if (n <= 0) {
                break;
            }
            available += n;
        }
    }
    xStreamBufferSend(stream_printf, data, len, 0);
}   // cdc_debug_put_into_stream



static void cdc_debug_write(const uint8_t *buf, const size_t total_cnt)
{
    static uint32_t prev_ms;
    static uint32_t base_ms;
    static bool newline = true;
    static char tbuf[30];
    int ndx = 0;

    tbuf[0] = 0;
    while (ndx < total_cnt) {
        const uint8_t *p;
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
                    d_ms = 10000;
                }
                else {
                    d_ms = (uint32_t)(now_ms - prev_ms);
                }
                if (d_ms <= 999) {
                    snprintf(tbuf, sizeof(tbuf), "%u.%03u (%3u) - ",
                             (unsigned)(now_ms / 1000), (unsigned)(now_ms % 1000), (unsigned)d_ms);
                }
                else {
                    snprintf(tbuf, sizeof(tbuf), "%u.%03u (...) - ",
                             (unsigned)(now_ms / 1000), (unsigned)(now_ms % 1000));
                }
                prev_ms = now_ms;
            }
            cdc_debug_put_into_stream(tbuf, strnlen(tbuf, sizeof(tbuf)));
        }

        p = memchr(buf + ndx, '\n', total_cnt - ndx);
        if (p == NULL) {
            cnt = total_cnt - ndx;
        } else {
            cnt = (p - (buf + ndx)) + 1;
            newline = true;
        }

        cdc_debug_put_into_stream(buf + ndx, cnt);

        ndx += cnt;
    }
}   // cdc_debug_write



static void stdio_cdc_out_chars(const char *buf, int length)
{
    if (task_printf == NULL)
        return;

    if (portCHECK_IF_IN_ISR()) {
        // we suppress those messages silently
        return;
    }

    xSemaphoreTake(sema_printf, portMAX_DELAY);
    cdc_debug_write((const uint8_t *)buf, length);
    xSemaphoreGive(sema_printf);
    xEventGroupSetBits(events, EV_STREAM);
}   // stdio_cdc_out_chars



stdio_driver_t stdio_cdc = {
    .out_chars = stdio_cdc_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = false
#endif
};



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

    xTaskCreate(cdc_debug_thread, "CDC-ProbeUart", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_printf);
    cdc_debug_line_state_cb(false, false);

    stdio_set_driver_enabled(&stdio_cdc, true);
}   // cdc_debug_init
