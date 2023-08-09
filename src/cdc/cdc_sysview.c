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
#include "stream_buffer.h"
#include "task.h"
#include "event_groups.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "rtt_io.h"


#define STREAM_SYSVIEW_SIZE      4096
#define STREAM_SYSVIEW_TRIGGER   32

static TaskHandle_t              task_sysview = NULL;
static StreamBufferHandle_t      stream_sysview;

static volatile bool m_connected = false;


#define EV_TX_COMPLETE           0x01
#define EV_STREAM                0x02
#define EV_RX                    0x04

/// event flags
static EventGroupHandle_t     events;



static void cdc_thread(void *ptr)
{
    static uint8_t cdc_tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

    for (;;) {
        uint32_t cdc_rx_chars;

        if ( !m_connected) {
            // wait here until connected (and until my terminal program is ready)
            while ( !m_connected) {
                xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        cdc_rx_chars = tud_cdc_n_available(CDC_SYSVIEW_N);
        if (cdc_rx_chars == 0  &&  xStreamBufferIsEmpty(stream_sysview)) {
            // -> nothing left to do: sleep for a long time
            //tud_cdc_n_write_flush(CDC_SYSVIEW_N);
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        }
        else if (cdc_rx_chars != 0) {
            // wait a short period if there are characters host -> probe -> target
            // waiting is done below
        }
        else {
            // wait until transmission via USB has finished
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        }

        //
        // (target ->) probe -> host
        //
        if ( !xStreamBufferIsEmpty(stream_sysview)) {
            //
            // transmit characters target -> picoprobe -> host
            //
            size_t cnt;
            size_t max_cnt;

            max_cnt = tud_cdc_n_write_available(CDC_SYSVIEW_N);
            if (max_cnt != 0) {
                //printf("%d\n",max_cnt);
                max_cnt = MIN(sizeof(cdc_tx_buf), max_cnt);
                cnt = xStreamBufferReceive(stream_sysview, cdc_tx_buf, max_cnt, pdMS_TO_TICKS(500));
                if (cnt != 0) {
                    //printf("%d %d\n",cnt,max_cnt);
                    tud_cdc_n_write(CDC_SYSVIEW_N, cdc_tx_buf, cnt);
                    if (cnt > 200) {
                        //tud_cdc_n_write_flush(CDC_SYSVIEW_N);
                    }
                }
            }
        }
        else {
            //tud_cdc_n_write_flush(CDC_SYSVIEW_N);
        }

        //
        // host -> probe -> target
        //
        for (;;) {
            uint8_t ch;
            uint32_t tx_len;

            cdc_rx_chars = tud_cdc_n_available(CDC_SYSVIEW_N);
            if (cdc_rx_chars == 0) {
                break;
            }

            tx_len = tud_cdc_n_read(CDC_SYSVIEW_N, &ch, sizeof(ch));
            if (tx_len == 0) {
                break;
            }

            // TODO seems that SystemView transmitts garbage on UART line
            //printf("-> %02x\n", ch);
            rtt_sysview_send_byte(ch);
        }
    }
}   // cdc_thread



void cdc_sysview_line_state_cb(bool dtr, bool rts)
/**
 * Flush tinyusb buffers on connect/disconnect.
 * This seems to be necessary to survive e.g. a restart of the host (Linux)
 */
{
    //printf("cdc_sysview_line_state_cb(%d,%d)\n", dtr, rts);

    tud_cdc_n_write_clear(CDC_SYSVIEW_N);
    tud_cdc_n_read_flush(CDC_SYSVIEW_N);
    m_connected = (dtr  ||  rts);
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_sysview_line_state_cb



void cdc_sysview_tx_complete_cb(void)
{
    //printf("cdc_sysview_tx_complete_cb()\n");
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_sysview_tx_complete_cb



void cdc_sysview_rx_cb()
{
    //printf("cdc_sysview_rx_cb()\n");
    
    xEventGroupSetBits(events, EV_RX);
}   // cdc_sysview_rx_cb



bool net_sysview_is_connected(void)
{
    return m_connected;
}   // net_sysview_is_connected



uint32_t net_sysview_send(const uint8_t *buf, uint32_t cnt)
/**
 * Send characters from SysView RTT channel into stream.
 *
 * \param buf  pointer to the buffer to be sent, if NULL then remaining space in stream is returned
 * \param cnt  number of bytes to be sent
 * \return if \buf is NULL the remaining space in stream is returned, otherwise the number of bytes
 */
{
    uint32_t r = 0;

    //printf("net_sysview_send(%p,%lu)\n", buf, cnt);
    
    if (buf == NULL) {
        r = xStreamBufferSpacesAvailable(stream_sysview);
    }
    else {
        if ( !net_sysview_is_connected()) {
            xStreamBufferReset(stream_sysview);
        }
        else {
            r = xStreamBufferSend(stream_sysview, buf, cnt, pdMS_TO_TICKS(1000));
        }

        xEventGroupSetBits(events, EV_STREAM);
    }

    return r;
}   // net_sysview_send



void cdc_sysview_init(uint32_t task_prio)
{
    events = xEventGroupCreate();

    stream_sysview = xStreamBufferCreate(STREAM_SYSVIEW_SIZE, STREAM_SYSVIEW_TRIGGER);
    if (stream_sysview == NULL) {
        picoprobe_error("cdc_sysview_init: cannot create stream_sysview\n");
    }

    xTaskCreate(cdc_thread, "CDC-SysViewUart", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_sysview);
    cdc_sysview_line_state_cb(false, false);
}   // cdc_sysview_init
