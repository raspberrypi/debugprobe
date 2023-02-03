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

#include "FreeRTOS.h"
#include "task.h"

#include "target_board.h"
#include "swd_host.h"

#include "picoprobe_config.h"
#include "rtt_console.h"
#include "sw_lock.h"
#include "RTT/SEGGER_RTT.h"
#include "cdc_uart.h"
#include "led.h"



#define TARGET_RAM_START   g_board_info.target_cfg->ram_regions[0].start
#define TARGET_RAM_END     g_board_info.target_cfg->ram_regions[0].end

static const uint32_t segger_alignment = 4;
static const uint8_t  seggerRTT[16] = "SEGGER RTT\0\0\0\0\0\0";
static uint32_t       prev_rtt_cb = 0;

static TaskHandle_t   task_rtt_console = NULL;



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
        led_state(LS_RTT_CB_FOUND);
    }
    else {
        picoprobe_debug("no RTT_CB found\n");
    }
    prev_rtt_cb = rtt_cb;
    return rtt_cb;
}   // search_for_rtt_cb



static void do_rtt_console(uint32_t rtt_cb)
{
    SEGGER_RTT_BUFFER_UP  aUp;       // Up buffer, transferring information up from target via debug probe to host
    uint8_t buf[100];
    bool ok = true;

    if (rtt_cb < TARGET_RAM_START  ||  rtt_cb >= TARGET_RAM_END) {
        return;
    }

    ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aUp),
                                 (uint8_t *)&aUp, sizeof(aUp));

    // do operations
    while (ok  &&  !sw_unlock_requested()) {
        ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[0].WrOff), (uint8_t *)&(aUp.WrOff), 2*sizeof(unsigned));

        if (aUp.WrOff == aUp.RdOff) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        else {
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
            ok = ok  &&  swd_write_word(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[0].RdOff), (aUp.RdOff + cnt) % aUp.SizeOfBuffer);

            // put received data into CDC UART
            cdc_uart_write(buf, cnt);

            led_state(LS_RTT_DATA);
        }
    }
}   // do_rtt_console



/**
 * Connect to the target, but let the target run
 */
static void target_connect(void)
{
//    picoprobe_debug("=================================== RTT connect target\n");
//    target_set_state(RESET_PROGRAM);
    if (target_set_state(ATTACH)) {
        led_state(LS_TARGET_FOUND);
    }
    else {
        led_state(LS_NO_TARGET);
    }
}   // target_connect



static void target_disconnect(void)
{
//    picoprobe_debug("=================================== RTT disconnect target\n");
//    target_set_state(RESET_RUN);
}   // target_disconnect



void rtt_console_thread(void *ptr)
{
    uint32_t rtt_cb;

    for (;;) {
        sw_lock("RTT", false);
        // post: we have the interface

        vTaskDelay(pdMS_TO_TICKS(100));
        target_connect();

        rtt_cb = search_for_rtt_cb();
        do_rtt_console(rtt_cb);

        target_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));        // TODO after disconnect some guard time seems to be required??

        sw_unlock("RTT");

        vTaskDelay(pdMS_TO_TICKS(300));        // give the other task the opportunity to catch sw_lock();
    }
}   // rtt_console_thread



void rtt_console_init(uint32_t task_prio)
{
    picoprobe_debug("rtt_console_init()\n");

    xTaskCreateAffinitySet(rtt_console_thread, "RTT_CONSOLE", configMINIMAL_STACK_SIZE, NULL, task_prio, 1, &task_rtt_console);
}   // rtt_console_init
