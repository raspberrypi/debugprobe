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

#include "target_family.h"
#include "swd_host.h"
#include "daplink_addr.h"

#include "picoprobe_config.h"
#include "rtt_console.h"
#include "sw_lock.h"
#include "RTT/SEGGER_RTT.h"


#define DO_TARGET_DISCONNECT  1


static const uint8_t    seggerRTT[16] = "SEGGER RTT\0\0\0\0\0\0";

static TaskHandle_t           task_rtt_console = NULL;



static uint32_t search_for_rtt_cb(void)
{
    uint8_t buf[1024];
    bool ok;
    uint32_t rttAddr = 0;

    for (uint32_t addr = DAPLINK_RAM_START;  addr < DAPLINK_RAM_START + DAPLINK_RAM_SIZE;  addr += sizeof(buf)) {
        ok = swd_read_memory(addr, buf, sizeof(buf));
        if ( !ok  ||  sw_unlock_requested()) {
            break;
        }

        for (uint32_t ndx = 0;  ndx < sizeof(buf) - sizeof(seggerRTT);  ndx += sizeof(seggerRTT)) {
            if (memcmp(buf + ndx, seggerRTT, sizeof(seggerRTT)) == 0) {
                rttAddr = addr + ndx;
                break;
            }
        }
        if (rttAddr != 0) {
            break;
        }
    }

    if (rttAddr != 0) {
        picoprobe_info("RTT_CB found at 0x%lx\n", rttAddr);
    }
    else {
        picoprobe_info("no RTT_CB found\n");
    }
    return rttAddr;
}   // search_for_rtt_cb



static void do_rtt_console(uint32_t rtt_cb)
{
    SEGGER_RTT_BUFFER_UP  aUp;       // Up buffer, transferring information up from target via debug probe to host
    uint8_t buf[100];
    bool ok = true;

    ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aUp),
                                 (uint8_t *)&aUp, sizeof(aUp));

    // do operations
    while (ok  &&  !sw_unlock_requested()) {
        ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[0].WrOff), (uint8_t *)&(aUp.WrOff), 2*sizeof(unsigned));

        if (aUp.WrOff == aUp.RdOff) {
//            vTaskDelay(pdMS_TO_TICKS(100));
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
//            picoprobe_info("RTT_CB: p:%p size:%d wr:%d rd:%d cnt:%lu\n", aUp.pBuffer, aUp.SizeOfBuffer, aUp.WrOff, aUp.RdOff, cnt);

            memset(buf, 0, sizeof(buf));
            ok = ok  &&  swd_read_memory((uint32_t)aUp.pBuffer + aUp.RdOff, buf, cnt);
            ok = ok  &&  swd_write_word(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[0].RdOff), (aUp.RdOff + cnt) % aUp.SizeOfBuffer);

#if 0
            for (int i = 0;  i < cnt;  ++i) {
                picoprobe_debug(" %02x", buf[i]);
            }
            picoprobe_debug("\n");
#else
            picoprobe_debug(" %.100s", buf);
#endif
        }
    }
}   // do_rtt_console



/**
 * Connect to the target, but let the target run
 */
static void target_connect(void)
{
    picoprobe_debug("=================================== RTT connect target\n");
//    target_set_state(RESET_PROGRAM);
    target_set_state(ATTACH);
}   // target_connect



#if DO_TARGET_DISCONNECT
static void target_disconnect(void)
{
    picoprobe_debug("=================================== RTT disconnect target\n");
    target_set_state(RESET_RUN);
}   // target_disconnect
#endif



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

#if DO_TARGET_DISCONNECT
        target_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));        // TODO after disconnect some guard time seems to be required??
#endif
        vTaskDelay(pdMS_TO_TICKS(100));        // TODO after disconnect some guard time seems to be required??

        sw_unlock("RTT");
    }
}   // rtt_console_thread



void rtt_console_init(uint32_t task_prio)
{
    picoprobe_debug("rtt_console_init()\n");

    xTaskCreate(rtt_console_thread, "RTT_CONSOLE", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_rtt_console);
}   // rtt_console_init
