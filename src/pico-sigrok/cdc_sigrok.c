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

#include <stdio.h>
#include "pico/stdlib.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "hardware/sync.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "stream_buffer.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "sigrok_int.h"
#include "cdc_sigrok.h"



#define STREAM_SIGROK_SIZE    8192
#define STREAM_SIGROK_TRIGGER 32

#define EV_RX                 0x01
#define EV_TX                 0x02
#define EV_STREAM             0x04

static TaskHandle_t           task_cdc_sigrok;
static EventGroupHandle_t     events;
static StreamBufferHandle_t   stream_sigrok;

static volatile bool m_connected = false;



void cdc_sigrok_tx_complete_cb(void)
{
    xEventGroupSetBits(events, EV_TX);
}   // cdc_sigrok_tx_complete_cb



void cdc_sigrok_line_state_cb(bool dtr, bool rts)
/**
 * Flush tinyusb buffers on connect/disconnect.
 * This seems to be necessary to survive e.g. a restart of the host (Linux)
 */
{
    tud_cdc_n_write_clear(CDC_SIGROK_N);
    tud_cdc_n_read_flush(CDC_SIGROK_N);
    m_connected = (dtr  ||  rts);;
    xEventGroupSetBits(events, EV_STREAM);
}   // cdc_uart_line_state_cb



void cdc_sigrok_write(const char *buf, int length)
{
#if 0
    int i;
    static int bytecnt;
    static int linecnt;
    char cc[128];

    i = 0;
    while (i < length) {
        int n;

        if (length - i > 32)
            n = 32;
        else
            n = length - i;

        for (int j = 0;  j < n;  ++j) {
            sprintf(cc + 3*j, "%02x ", buf[i+j]);
        }
        Dprintf("%4d/%5d: %s\n", ++linecnt, bytecnt, cc);

        bytecnt += n;
        i += n;
    }
#endif

    xStreamBufferSend(stream_sigrok, buf, length, portMAX_DELAY);
    xEventGroupSetBits(events, EV_STREAM);
}   // cdc_sigrok_write



//Process incoming character stream
//Return true if the device rspstr has a response to send to host
//Be sure that rspstr does not have \n  or \r.
static bool process_char(sr_device_t *d, char charin)
{
    int tmpint, tmpint2;
    bool ret = false;

    //set default rspstr for all commands that have a dataless ack
    d->rspstr[0] = '*';
    d->rspstr[1] = '\0';

    //the reset character works by itself
    if (charin == '*') {
        sigrok_reset(d);
        Dprintf("sigrok cmd '*' -> RESET %d\n", d->sample_and_send);
        return false;
    }
    else if (charin == '\r'  ||  charin == '\n') {
        d->cmdstr[d->cmdstr_ndx] = 0;
        switch (d->cmdstr[0]) {
            case 'i':
                // identification
                // SRPICO,AxxyDzz,02 - num analog, analog size, num digital,version
                sprintf(d->rspstr, "SRPICO,A%02d1D%02d,02", SR_NUM_A_CHAN, SR_NUM_D_CHAN);
                ret = true;
                break;

            case 'R':
                // sampling rate
                extern uint32_t probe_get_cpu_freq_khz(void);

                tmpint = atol(&(d->cmdstr[1]));
                if (tmpint >= 5000  &&  tmpint <= 1000 * probe_get_cpu_freq_khz() + 16) { //Add 16 to support cfg_bits
                    d->sample_rate = tmpint;
//                    Dprintf("SMPRATE= %lu\n", d->sample_rate);
                    ret = true;
                }
                else {
//                    Dprintf("unsupported smp rate %s\n", d->cmdstr);
                    ret = false;
                }
                break;

            case 'L':
                // sample limit
                tmpint = atol(&(d->cmdstr[1]));
                if (tmpint > 0) {
                    d->num_samples = tmpint;
//                    Dprintf("NUMSMP=%lu\n", d->num_samples);
                    ret = true;
                }
                else {
//                    Dprintf("bad num samples %s\n", d->cmdstr);
                    ret = false;
                }
                break;

            case 'a':
                // get analog scale
                tmpint = atoi(&(d->cmdstr[1])); //extract channel number
                if (tmpint >= 0) {
                    //scale and offset are both in integer uVolts
                    //separated by x
                    sprintf(d->rspstr, "25700x0");  //3.3/(2^7) and 0V offset
//                    Dprintf("ASCL%d\n", tmpint);
                    ret = true;
                }
                else {
//                    Dprintf("bad ascale %s\n", d->cmdstr);
                    ret = false; //this will return a '*' causing the host to fail
                }
                break;

            case 'F':
                // fixed set of samples
//                Dprintf("STRT_FIX\n");
                d->continuous = false;
                sigrok_tx_init(d);
                ret = false;
                break;

            case 'C':
                // continuous mode
//                Dprintf("STRT_CONT\n");
                d->continuous = true;
                sigrok_tx_init(d);
                ret = false;
                break;

            case 't':
                // trigger -format tvxx where v is value and xx is two digit channel
                /* HW trigger deprecated */
                ret = true;
                break;

            case 'p':
                // pretrigger count, this is a nop
                tmpint = atoi(&(d->cmdstr[1]));
//                Dprintf("Pre-trigger samples %d cmd %s\n", tmpint, d->cmdstr);
                ret = true;
                break;

            case 'A':
                // enable analog channel always a set
                // format is Axyy where x is 0 for disabled, 1 for enabled and yy is channel #
                tmpint = d->cmdstr[1] - '0'; //extract enable value
                tmpint2 = atoi(&(d->cmdstr[2])); //extract channel number
                if (tmpint >= 0  &&  tmpint <= 1  &&  tmpint2 >= 0  &&  tmpint2 <= 31) {  // TODO 31 is max bits
                    d->a_mask = d->a_mask & ~(1 << tmpint2);
                    d->a_mask = d->a_mask | (tmpint << tmpint2);
//                    Dprintf("A%d EN %d Msk 0x%lX\n", tmpint2, tmpint, d->a_mask);
                    ret = true;
                }
                else {
                    ret = false;
                }
                break;

            case 'D':
                // enable digital channel always a set
                // format is Dxyy where x is 0 for disabled, 1 for enabled and yy is channel #
                tmpint = d->cmdstr[1] - '0'; //extract enable value
                tmpint2 = atoi(&(d->cmdstr[2])); //extract channel number
                if (tmpint >= 0  &&  tmpint <= 1  &&  tmpint2 >= 0  &&  tmpint2 < SR_NUM_D_CHAN) {
                    d->d_mask = d->d_mask & ~(1 << tmpint2);
                    d->d_mask = d->d_mask | (tmpint << tmpint2);

                    d->d_mask_D4 = 0;
                    for (int i = 0;  i < 8;  ++i) {
                        d->d_mask_D4 |= (d->d_mask & 0x0f) << (4 * i);
                    }
//                    Dprintf("D%d EN %d Msk 0x%lX 0x%lX\n", tmpint2, tmpint, d->d_mask, d->d_mask_D4);
                    ret = true;
                }
                else {
                    ret = false;
                }
                break;

            case 'N':
                // return channel name
                // format is N[AD]yy, A=analog, D=digital, yy is channel #
                ret = false;
                if (d->cmdstr_ndx >= 4) {
                    tmpint = atoi(d->cmdstr + 2);
                    if (d->cmdstr[1] == 'A'  &&  tmpint >= 0  &&  tmpint < SR_NUM_A_CHAN) {
                        sprintf(d->rspstr, "ADC%d", tmpint);
                        ret = true;
                    }
                    else if (d->cmdstr[1] == 'D'  &&  tmpint >= 0  &&  tmpint < SR_NUM_D_CHAN) {
                        sprintf(d->rspstr, "GP%d", tmpint + SR_BASE_D_CHAN);
                        ret = true;
                    }
                }
                else if (d->cmdstr[1] == '?') {
                    strcpy(d->rspstr, "ok");
                    ret = true;
                }
                break;

            default:
                Dprintf("bad command %s\n", d->cmdstr);
                ret = false;
                break;
        }

        if (ret) {
            Dprintf("sigrok cmd '%s' -> '%s' [OK]\n", d->cmdstr, d->rspstr);
        }
        else {
            Dprintf("sigrok cmd '%s' -> '%s'\n", d->cmdstr, d->rspstr);
        }

        d->cmdstr_ndx = 0;
    }
    else {
        //no CR/LF
        if (d->cmdstr_ndx >= sizeof(d->cmdstr) - 1) {
            d->cmdstr[sizeof(d->cmdstr) - 2] = 0;
            Dprintf("Command overflow %s\n", d->cmdstr);
            d->cmdstr_ndx = 0;
        }
        d->cmdstr[d->cmdstr_ndx++] = charin;
        ret = false;
    }
    //default return 0 means to not send any kind of response
    return ret;
}   // process_char



void cdc_sigrok_rx_cb(void)
{
    xEventGroupSetBits(events, EV_RX);
}   // cdc_sigrok_rx_cb



void cdc_sigrok_thread()
{

    for (;;) {
        static uint8_t cdc_tx_buf[CFG_TUD_CDC_TX_BUFSIZE];
        EventBits_t ev = EV_RX | EV_TX | EV_STREAM;
        size_t cdc_rx_chars;

        if ( !m_connected) {
            // wait here until connected (and until my terminal program is ready)
            while ( !m_connected) {
                xEventGroupWaitBits(events, EV_RX | EV_TX | EV_STREAM, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        cdc_rx_chars = tud_cdc_n_available(CDC_SIGROK_N);
        if (cdc_rx_chars == 0  &&  xStreamBufferIsEmpty(stream_sigrok)) {
            // -> nothing left to do: sleep for a long time
            tud_cdc_n_write_flush(CDC_SIGROK_N);
            ev = xEventGroupWaitBits(events, EV_RX | EV_TX | EV_STREAM, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        }
        else if (cdc_rx_chars != 0) {
            // wait a short period if there are characters host -> probe -> target
            ev = xEventGroupWaitBits(events, EV_RX | EV_TX | EV_STREAM, pdTRUE, pdFALSE, pdMS_TO_TICKS(1));
        }
        else {
            // wait until transmission via USB has finished
            ev = xEventGroupWaitBits(events, EV_RX | EV_TX | EV_STREAM, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        }

        if (ev & EV_RX) {
            for (;;) {
                uint32_t n;
                uint8_t  ch;

                n = tud_cdc_n_available(CDC_SIGROK_N);
                if (n == 0) {
                    break;
                }

                n = tud_cdc_n_read(CDC_SIGROK_N, &ch, sizeof(ch));
                if (n == 0) {
                    break;
                }

                // The '+' is the only character we track during normal sampling because it can end
                // a continuous trace.  A reset '*' should only be seen after we have completed normally
                // or hit an error condition.
                if (ch == '+') {
                    sr_dev.sample_and_send = false;
                    sr_dev.aborted         = false;        // clear the abort so we stop sending !!
                }
                else {
                    if (process_char(&sr_dev, (char)ch)) {
                        sr_dev.send_resp = true;
                    }
                }
                sigrok_notify();
            }
        }

        while ( !xStreamBufferIsEmpty(stream_sigrok)) {
            //
            // transmit characters target -> picoprobe -> host
            //
            size_t cnt;
            size_t max_cnt;

            max_cnt = tud_cdc_n_write_available(CDC_SIGROK_N);
            if (max_cnt == 0) {
                break;
            }

            max_cnt = MIN(sizeof(cdc_tx_buf), max_cnt);
            cnt = xStreamBufferReceive(stream_sigrok, cdc_tx_buf, max_cnt, pdMS_TO_TICKS(500));
            if (cnt != 0) {
                tud_cdc_n_write(CDC_SIGROK_N, cdc_tx_buf, cnt);
            }
        }
        tud_cdc_n_write_flush(CDC_SIGROK_N);
    }
}   // cdc_sigrok_thread



void cdc_sigrok_init(uint32_t task_prio)
{
    events = xEventGroupCreate();
    stream_sigrok = xStreamBufferCreate(STREAM_SIGROK_SIZE, STREAM_SIGROK_TRIGGER);
    xTaskCreate(cdc_sigrok_thread, "CDC_SIGROK", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_cdc_sigrok);
}   // cdc_sigrok_init
