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


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include "task.h"

#include "target_board.h"
#include "swd_host.h"

#include "picoprobe_config.h"
#include "rtt_console.h"
#include "sw_lock.h"
#include "RTT/SEGGER_RTT.h"
#include "cdc_uart.h"
#include "led.h"



#define TARGET_RAM_START      g_board_info.target_cfg->ram_regions[0].start
#define TARGET_RAM_END        g_board_info.target_cfg->ram_regions[0].end

#define STREAM_RTT_SIZE       128
#define STREAM_RTT_TRIGGER    1

#define EV_RTT_TO_TARGET      0x01

static const uint32_t         segger_alignment = 4;
static const uint8_t          seggerRTT[16] = "SEGGER RTT\0\0\0\0\0\0";
static uint32_t               prev_rtt_cb = 0;
static bool                   rtt_console_running = false;

static TaskHandle_t           task_rtt_console = NULL;
static StreamBufferHandle_t   stream_rtt;
static EventGroupHandle_t     events;



static uint32_t check_buffer_for_rtt_cb(uint8_t *buf, uint32_t buf_size, uint32_t base_addr)
{
    uint32_t rtt_cb = 0;

    for (uint32_t ndx = 0;  ndx <= buf_size - sizeof(seggerRTT);  ndx += segger_alignment) {
        if (memcmp(buf + ndx, seggerRTT, sizeof(seggerRTT)) == 0) {
            rtt_cb = base_addr + ndx;
            break;
        }
    }
    return rtt_cb;
}   // check_buffer_for_rtt_cb



/**
 * Search for the RTT control block.
 *
 * \return 0 -> nothing found, otherwise beginning of control block
 *
 * \note
 *    - a small block at the end of RAM is not searched
 *    - searching all 256KByte RAM of the RP2040 takes 600ms (at 12.5MHz interface clock)
 */
static uint32_t search_for_rtt_cb(void)
{
    uint8_t buf[1024];
    bool ok;
    uint32_t rtt_cb = 0;

    picoprobe_debug("searching RTT_CB in 0x%08lx..0x%08lx, prev: 0x%08lx\n", TARGET_RAM_START, TARGET_RAM_END - 1, prev_rtt_cb);

    if (prev_rtt_cb != 0) {
        // fast search, saves a little SW traffic and a few ms
        ok = swd_read_memory(prev_rtt_cb, buf, sizeof(seggerRTT));
        if (ok) {
            rtt_cb = check_buffer_for_rtt_cb(buf, sizeof(seggerRTT), prev_rtt_cb);
        }
    }

    if (rtt_cb == 0) {
        // note that searches must somehow overlap to find (unaligned) control blocks at the border of read chunks
        for (uint32_t addr = TARGET_RAM_START;  addr <= TARGET_RAM_END - sizeof(buf);  addr += sizeof(buf) - sizeof(seggerRTT)) {
            ok = swd_read_memory(addr, buf, sizeof(buf));
            if ( !ok  ||  sw_unlock_requested()) {
                break;
            }

            rtt_cb = check_buffer_for_rtt_cb(buf, sizeof(buf), addr);
            if (rtt_cb != 0) {
                break;
            }
        }
    }

    if (rtt_cb != 0) {
        picoprobe_info("RTT_CB found at 0x%lx\n", rtt_cb);
    }
    else {
        picoprobe_debug("no RTT_CB found\n");
    }
    prev_rtt_cb = rtt_cb;
    return rtt_cb;
}   // search_for_rtt_cb



static void do_rtt_console(uint32_t rtt_cb)
{
    SEGGER_RTT_BUFFER_UP   aUp;       // Up buffer, transferring information up from target via debug probe to host
    SEGGER_RTT_BUFFER_DOWN aDown;     // Down buffer, transferring information from host via debug probe to target
    uint8_t buf[128];
    bool ok = true;

    static_assert(sizeof(uint32_t) == sizeof(unsigned int));    // why doesn't segger use uint32_t?

    if (rtt_cb < TARGET_RAM_START  ||  rtt_cb >= TARGET_RAM_END) {
        return;
    }

    ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aUp),
                                 (uint8_t *)&aUp, sizeof(aUp));
    ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aDown),
                                 (uint8_t *)&aDown, sizeof(aDown));

    // do operations
    rtt_console_running = true;
    while (ok  &&  !sw_unlock_requested()) {
        ok = ok  &&  swd_read_word(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[0].WrOff), (uint32_t *)&aUp.WrOff);

        if (aUp.WrOff == aUp.RdOff) {
            // -> no characters available
            xEventGroupWaitBits(events, EV_RTT_TO_TARGET, pdTRUE, pdFALSE, pdMS_TO_TICKS(10));
        }
        else {
            //
            // fetch characters from target
            //
            uint32_t cnt;

            if (aUp.WrOff > aUp.RdOff) {
                cnt = aUp.WrOff - aUp.RdOff;
            }
            else {
                cnt = aUp.SizeOfBuffer - aUp.RdOff;
            }
            cnt = MIN(cnt, sizeof(buf));

            memset(buf, 0, sizeof(buf));
            ok = ok  &&  swd_read_memory((uint32_t)aUp.pBuffer + aUp.RdOff, buf, cnt);
            aUp.RdOff = (aUp.RdOff + cnt) % aUp.SizeOfBuffer;
            ok = ok  &&  swd_write_word(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[0].RdOff), aUp.RdOff);

            // put received data into CDC UART
            cdc_uart_write(buf, cnt);

            led_state(LS_RTT_RX_DATA);
        }

        if ( !xStreamBufferIsEmpty(stream_rtt)) {
            //
            // send data to target
            //
            ok = ok  &&  swd_read_word(rtt_cb + offsetof(SEGGER_RTT_CB, aDown[0].RdOff), (uint32_t *)&(aDown.RdOff));
            if ((aDown.WrOff + 1) % aDown.SizeOfBuffer != aDown.RdOff) {
                // -> space left in RTT buffer on target
                uint32_t cnt;
                size_t r;

                if (aDown.WrOff >= aDown.RdOff) {
                    cnt = aDown.SizeOfBuffer - aDown.WrOff;
                }
                else {
                    cnt = (aDown.RdOff - aDown.WrOff) - 1;
                }
                cnt = MIN(cnt, sizeof(buf));

                r = xStreamBufferReceive(stream_rtt, &buf, cnt, 0);
                if (r > 0) {
                    ok = ok  &&  swd_write_memory((uint32_t)aDown.pBuffer + aDown.WrOff, buf, r);
                    aDown.WrOff = (aDown.WrOff + r) % aDown.SizeOfBuffer;
                    ok = ok  &&  swd_write_word(rtt_cb + offsetof(SEGGER_RTT_CB, aDown[0].WrOff), aDown.WrOff);

                    led_state(LS_UART_TX_DATA);
                }
            }
        }
    }
    rtt_console_running = false;
}   // do_rtt_console



/**
 * Connect to the target, but let the target run
 * \return true -> connected to target
 */
static bool target_connect(void)
{
    bool r = false;

//    picoprobe_debug("=================================== RTT connect target\n");
//    target_set_state(RESET_PROGRAM);
    if (target_set_state(ATTACH)) {
        r = true;
    }
    return r;
}   // target_connect



static void target_disconnect(void)
{
//    picoprobe_debug("=================================== RTT disconnect target\n");
//    target_set_state(RESET_RUN);
}   // target_disconnect



void rtt_console_thread(void *ptr)
{
    uint32_t rtt_cb;
    bool target_online = false;

    for (;;) {
        sw_lock("RTT", false);
        // post: we have the interface

        if ( !target_online) {
            if (g_board_info.prerun_board_config != NULL) {
                g_board_info.prerun_board_config();
            }
            if (g_board_info.target_cfg->rt_board_id != NULL) {
                picoprobe_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
                //picoprobe_info("Target family     : 0x%04x\n", g_target_family->family_id);
                picoprobe_info("Target vendor     : %s\n", g_board_info.target_cfg->target_vendor);
                picoprobe_info("Target part       : %s\n", g_board_info.target_cfg->target_part_number);
                //picoprobe_info("Board vendor      : %s\n", g_board_info.board_vendor);
                //picoprobe_info("Board name        : %s\n", g_board_info.board_name);
                picoprobe_info("Flash             : 0x%08lx..0x%08lx (%ldK)\n", g_board_info.target_cfg->flash_regions[0].start,
                               g_board_info.target_cfg->flash_regions[0].end - 1,
                               (g_board_info.target_cfg->flash_regions[0].end - g_board_info.target_cfg->flash_regions[0].start) / 1024);
                picoprobe_info("RAM               : 0x%08lx..0x%08lx (%ldK)\n",
                               g_board_info.target_cfg->ram_regions[0].start,
                               g_board_info.target_cfg->ram_regions[0].end - 1,
                               (g_board_info.target_cfg->ram_regions[0].end - g_board_info.target_cfg->ram_regions[0].start) / 1024);
                picoprobe_info("SWD frequency     : %ukHz\n", g_board_info.target_cfg->rt_swd_khz);
                picoprobe_info("SWD max frequency : %ukHz\n", g_board_info.target_cfg->rt_max_swd_khz);
                picoprobe_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        if ( !target_connect()) {
            led_state(LS_NO_TARGET);
            target_online = false;

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else {
            led_state(LS_TARGET_FOUND);
            target_online = true;

            rtt_cb = search_for_rtt_cb();
            if (rtt_cb != 0) {
                led_state(LS_RTT_CB_FOUND);
                do_rtt_console(rtt_cb);
            }

            target_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));        // some guard time after disconnect
        }
        sw_unlock("RTT");
        vTaskDelay(pdMS_TO_TICKS(300));            // give the other task the opportunity to catch sw_lock();
    }
}   // rtt_console_thread



bool rtt_console_cb_exists(void)
{
    return rtt_console_running;
}   // rtt_console_cb_exists



void rtt_console_send_byte(uint8_t ch)
/**
 * Write a byte into the RTT stream.
 * If there is no space left in the stream, wait 10ms and then try again.
 * If there is still no space, then drop a byte from the stream.
 */
{
    size_t available = xStreamBufferSpacesAvailable(stream_rtt);
    if (available < sizeof(ch)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        available = xStreamBufferSpacesAvailable(stream_rtt);
        if (available < sizeof(ch)) {
            uint8_t dummy;
            xStreamBufferReceive(stream_rtt, &dummy, sizeof(dummy), 0);
            picoprobe_error("rtt_console_send_byte: drop byte\n");
        }
    }
    xStreamBufferSend(stream_rtt, &ch, sizeof(ch), 0);
    xEventGroupSetBits(events, EV_RTT_TO_TARGET);
}   // rtt_console_send_byte



void rtt_console_init(uint32_t task_prio)
{
    picoprobe_debug("rtt_console_init()\n");

    events = xEventGroupCreate();

    stream_rtt = xStreamBufferCreate(STREAM_RTT_SIZE, STREAM_RTT_TRIGGER);
    if (stream_rtt == NULL) {
        picoprobe_error("rtt_console_init: cannot create stream_rtt\n");
    }

    xTaskCreateAffinitySet(rtt_console_thread, "RTT_CONSOLE", configMINIMAL_STACK_SIZE, NULL, task_prio, 1, &task_rtt_console);
    if (task_rtt_console == NULL)
    {
        picoprobe_error("rtt_console_init: cannot create task_rtt_console\n");
    }
}   // rtt_console_init
