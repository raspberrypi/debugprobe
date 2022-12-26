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


#define DO_TARGET_DISCONNECT  1


static const uint8_t seggerRTT[16] = "SEGGER RTT\0\0\0\0\0\0";

static TaskHandle_t           task_rtt_console = NULL;



static void search_for_rtt(void)
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
}   // search_for_rtt



static void do_rtt_console(void)
{
    // do operations
    while ( !sw_unlock_requested()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}   // do_rtt_console



/**
 * Connect to the target, but let the target run
 *
 * TODO  just do an attach
 */
static void target_connect(void)
{
    picoprobe_debug("=================================== RTT connect target\n");
    target_set_state(RESET_PROGRAM);
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
    for (;;) {
        sw_lock("RTT", false);
        // post: we have the interface

        vTaskDelay(pdMS_TO_TICKS(100));
        target_connect();

        search_for_rtt();
        do_rtt_console();

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
