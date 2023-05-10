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
#if OPT_TARGET_UART
    #include "cdc_uart.h"
#endif
#if OPT_NET_SYSVIEW_SERVER
    #include "net/net_sysview.h"
#endif
#include "led.h"


typedef void (*rtt_data_to_host)(const uint8_t *buf, uint32_t cnt);

#define TARGET_RAM_START        g_board_info.target_cfg->ram_regions[0].start
#define TARGET_RAM_END          g_board_info.target_cfg->ram_regions[0].end

#define STREAM_RTT_SIZE         128
#define STREAM_RTT_TRIGGER      1

#define RTT_CHANNEL_CONSOLE     0
#define RTT_POLL_INT_MS         10

#define EV_RTT_TO_TARGET        0x01

static const uint32_t           segger_alignment = 4;
static const uint8_t            seggerRTT[16] = "SEGGER RTT\0\0\0\0\0\0";
static uint32_t                 prev_rtt_cb = 0;
static bool                     rtt_console_running = false;
static bool                     ok_console_from_target = false;
static bool                     ok_console_to_target = false;

static TaskHandle_t             task_rtt_console = NULL;
static StreamBufferHandle_t     stream_rtt_console_to_target;                  // small stream for host->probe->target console communication
static EventGroupHandle_t       events;

#if OPT_NET_SYSVIEW_SERVER
    #define RTT_CHANNEL_SYSVIEW 1
    #undef  RTT_POLL_INT_MS
    #define RTT_POLL_INT_MS     1                                              // faster polling
    static StreamBufferHandle_t stream_rtt_sysview_to_target;                  // small stream for host->probe->target sysview communication
    static bool ok_sysview_from_target = false;
    static bool ok_sysview_to_target = false;
#endif



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



static bool rtt_check_channel_from_target(uint32_t rtt_cb, uint16_t channel, SEGGER_RTT_BUFFER_UP *aUp)
{
    bool ok;
    int32_t buff_cnt;

    ok = (rtt_cb >= TARGET_RAM_START  &&  rtt_cb <= TARGET_RAM_END);
    ok = ok  &&  swd_read_word(rtt_cb + offsetof(SEGGER_RTT_CB, MaxNumUpBuffers), (uint32_t *)&(buff_cnt));
    ok = ok  &&  (buff_cnt > channel);
    ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[channel]), (uint8_t *)aUp, sizeof(SEGGER_RTT_BUFFER_UP));
    ok = ok  &&  (aUp->SizeOfBuffer > 0  &&  aUp->SizeOfBuffer < TARGET_RAM_END - TARGET_RAM_START);
    ok = ok  &&  ((uint32_t)aUp->pBuffer >= TARGET_RAM_START  &&  (uint32_t)aUp->pBuffer + aUp->SizeOfBuffer <= TARGET_RAM_END);
    if (ok) {
        picoprobe_info("rtt_check_channel_from_target: %u %p %u %u %u\n", channel, aUp->pBuffer, aUp->SizeOfBuffer, aUp->RdOff, aUp->WrOff);
    }
    return ok;
}   // rtt_check_channel_from_target



static bool rtt_check_channel_to_target(uint32_t rtt_cb, uint16_t channel, SEGGER_RTT_BUFFER_DOWN *aDown)
{
    bool ok;
    int32_t buff_cnt;

    ok = (rtt_cb >= TARGET_RAM_START  &&  rtt_cb <= TARGET_RAM_END);
    ok = ok  &&  swd_read_word(rtt_cb + offsetof(SEGGER_RTT_CB, MaxNumDownBuffers), (uint32_t *)&(buff_cnt));
    ok = ok  &&  (buff_cnt > channel);
    ok = ok  &&  swd_read_memory(rtt_cb + offsetof(SEGGER_RTT_CB, aDown[channel]), (uint8_t *)aDown, sizeof(SEGGER_RTT_BUFFER_DOWN));
    ok = ok  &&  (aDown->SizeOfBuffer > 0  &&  aDown->SizeOfBuffer < TARGET_RAM_END - TARGET_RAM_START);
    ok = ok  &&  ((uint32_t)aDown->pBuffer >= TARGET_RAM_START  &&  (uint32_t)aDown->pBuffer + aDown->SizeOfBuffer <= TARGET_RAM_END);
    if (ok) {
        picoprobe_info("rtt_check_channel_to_target: %u %p %u %u %u\n", channel, aDown->pBuffer, aDown->SizeOfBuffer, aDown->RdOff, aDown->WrOff);
    }
    return ok;
}   // rtt_check_channel_to_target



static unsigned rtt_get_write_space(SEGGER_RTT_BUFFER_DOWN *pRing)
/**
 * Return the number of space left in the target buffer
 */
{
    unsigned rd_off;
    unsigned wr_off;
    unsigned r;

    rd_off = pRing->RdOff;
    wr_off = pRing->WrOff;
    if (rd_off <= wr_off) {
        r = pRing->SizeOfBuffer - 1u - wr_off + rd_off;
    }
    else {
        r = rd_off - wr_off - 1u;
    }
    return r;
}   // rtt_get_write_space



static bool rtt_from_target(uint32_t rtt_cb, uint16_t channel, SEGGER_RTT_BUFFER_UP *aUp,
                            rtt_data_to_host data_to_host, bool *worked)
{
    bool ok;
    uint8_t buf[256];

    ok = swd_read_word(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[channel].WrOff), (uint32_t *)&(aUp->WrOff));

    if (ok  &&  aUp->WrOff != aUp->RdOff) {
        //
        // fetch characters from target
        //
        uint32_t cnt;

        if (aUp->WrOff > aUp->RdOff) {
            cnt = aUp->WrOff - aUp->RdOff;
        }
        else {
            cnt = aUp->SizeOfBuffer - aUp->RdOff;
        }
        cnt = MIN(cnt, sizeof(buf));

        memset(buf, 0, sizeof(buf));
        ok = ok  &&  swd_read_memory((uint32_t)aUp->pBuffer + aUp->RdOff, buf, cnt);
        aUp->RdOff = (aUp->RdOff + cnt) % aUp->SizeOfBuffer;
        ok = ok  &&  swd_write_word(rtt_cb + offsetof(SEGGER_RTT_CB, aUp[channel].RdOff), aUp->RdOff);

        if (data_to_host != NULL)
        {
            // direct received data to host
            data_to_host(buf, cnt);
        }

        led_state(LS_RTT_RX_DATA);
        *worked = true;
    }
    return ok;
}   // rtt_from_target



static bool rtt_to_target(uint32_t rtt_cb, StreamBufferHandle_t stream, uint16_t channel,
                          SEGGER_RTT_BUFFER_DOWN *aDown, bool *worked)
{
    bool ok = true;
    uint8_t buf[16];
    unsigned num_bytes;

    if ( !xStreamBufferIsEmpty(stream)) {
        //
        // send data to target
        //
        ok = ok  &&  swd_read_word(rtt_cb + offsetof(SEGGER_RTT_CB, aDown[channel].RdOff), (uint32_t *)&(aDown->RdOff));

        num_bytes = rtt_get_write_space(aDown);
        if (num_bytes > 0) {
            //printf("a cnt: %u -> ", num_bytes);

            num_bytes = MIN(num_bytes, sizeof(buf));
            num_bytes = xStreamBufferReceive(stream, &buf, num_bytes, 0);
        }

        if (num_bytes > 0) {
            unsigned wr_off;
            unsigned remaining;

            wr_off = aDown->WrOff;
            remaining = aDown->SizeOfBuffer - wr_off;

            //printf("%u %u %u %u", channel, aDown->WrOff, num_bytes, remaining);

            if (remaining > num_bytes) {
                //
                // All data fits before wrap around
                //
                ok = ok  &&  swd_write_memory((uint32_t)aDown->pBuffer + wr_off, buf, num_bytes);
                aDown->WrOff = wr_off + num_bytes;
            }
            else {
                //
                // We reach the end of the buffer, so need to wrap around
                //
                unsigned num_bytes_at_once;

                num_bytes_at_once = remaining;
                ok = ok  &&  swd_write_memory((uint32_t)aDown->pBuffer + wr_off, buf, num_bytes_at_once);
                num_bytes_at_once = num_bytes - remaining;
                ok = ok  &&  swd_write_memory((uint32_t)aDown->pBuffer, buf + remaining, num_bytes_at_once);
                aDown->WrOff = num_bytes_at_once;
            }

            ok = ok  &&  swd_write_word(rtt_cb + offsetof(SEGGER_RTT_CB, aDown[channel].WrOff), aDown->WrOff);

            //printf(" -> %u\n", aDown->WrOff);
        }

        *worked = true;
    }
    return ok;
}   // rtt_to_target



static void do_rtt_io(uint32_t rtt_cb)
{
#if OPT_TARGET_UART
    SEGGER_RTT_BUFFER_UP   aUpConsole;       // Up buffer, transferring information up from target via debug probe to host
    SEGGER_RTT_BUFFER_DOWN aDownConsole;     // Down buffer, transferring information from host via debug probe to target
    ok_console_from_target = false;
    ok_console_to_target = false;
#endif
#if OPT_NET_SYSVIEW_SERVER
    SEGGER_RTT_BUFFER_UP   aUpSysView;       // Up buffer, transferring information up from target via debug probe to host
    SEGGER_RTT_BUFFER_DOWN aDownSysView;     // Down buffer, transferring information from host via debug probe to target
    ok_sysview_from_target = false;
    ok_sysview_to_target = false;
#endif
    bool ok = true;

    static_assert(sizeof(uint32_t) == sizeof(unsigned int));    // why doesn't segger use uint32_t?

    if (rtt_cb < TARGET_RAM_START  ||  rtt_cb >= TARGET_RAM_END) {
        return;
    }

    // do operations
    rtt_console_running = true;
    while (ok  &&  !sw_unlock_requested()) {
        bool worked;

        worked = false;

        //
        // transfer RTT from target to host
        //
        #if OPT_TARGET_UART
            if (ok_console_from_target)
                ok = ok  &&  rtt_from_target(rtt_cb, RTT_CHANNEL_CONSOLE, &aUpConsole, cdc_uart_write, &worked);
        #endif
        #if OPT_NET_SYSVIEW_SERVER
            if (ok_sysview_from_target)
                ok = ok  &&  rtt_from_target(rtt_cb, RTT_CHANNEL_SYSVIEW, &aUpSysView, net_sysview_send, &worked);
        #endif

        //
        // transfer RTT data from host to target
        //
        #if OPT_TARGET_UART
            if (ok_console_to_target)
                ok = ok  &&  rtt_to_target(rtt_cb, stream_rtt_console_to_target, RTT_CHANNEL_CONSOLE, &aDownConsole, &worked);
        #endif
        #if OPT_NET_SYSVIEW_SERVER
            if (ok_sysview_to_target)
                ok = ok  &&  rtt_to_target(rtt_cb, stream_rtt_sysview_to_target, RTT_CHANNEL_SYSVIEW, &aDownSysView, &worked);
        #endif

        if ( !worked)
        {
            // did nothing -> check if RTT channels appeared
            #if OPT_TARGET_UART
                if ( !ok_console_from_target)
                    ok_console_from_target = rtt_check_channel_from_target(rtt_cb, RTT_CHANNEL_CONSOLE, &aUpConsole);
                if ( !ok_console_to_target)
                    ok_console_to_target = rtt_check_channel_to_target(rtt_cb, RTT_CHANNEL_CONSOLE, &aDownConsole);
            #endif
            #if OPT_NET_SYSVIEW_SERVER
                if ( !ok_sysview_from_target)
                    ok_sysview_from_target = rtt_check_channel_from_target(rtt_cb, RTT_CHANNEL_SYSVIEW, &aUpSysView);
                if ( !ok_sysview_to_target)
                    ok_sysview_to_target = rtt_check_channel_to_target(rtt_cb, RTT_CHANNEL_SYSVIEW, &aDownSysView);
            #endif

            // -> delay
            xEventGroupWaitBits(events, EV_RTT_TO_TARGET, pdTRUE, pdFALSE, pdMS_TO_TICKS(RTT_POLL_INT_MS));
        }
    }
    rtt_console_running = false;
}   // do_rtt_io



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



void rtt_io_thread(void *ptr)
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

        vTaskDelay(pdMS_TO_TICKS(100));            // give the target some time for startup
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
                do_rtt_io(rtt_cb);
            }

            target_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));        // some guard time after disconnect
        }
        sw_unlock("RTT");
        vTaskDelay(pdMS_TO_TICKS(300));            // give the other task the opportunity to catch sw_lock();
    }
}   // rtt_io_thread



bool rtt_console_cb_exists(void)
{
    return rtt_console_running  &&  ok_console_to_target;
}   // rtt_console_cb_exists



void rtt_send_byte(StreamBufferHandle_t stream, uint16_t channel, uint8_t ch, bool allow_drop)
/**
 * Write a byte into the RTT stream.
 * If there is no space left in the stream, wait 10ms and then try again.
 * If there is still no space, then drop a byte from the stream.
 */
{
    size_t available = xStreamBufferSpacesAvailable(stream);
    if ( !allow_drop  &&  available < sizeof(ch)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        available = xStreamBufferSpacesAvailable(stream);
        if (available < sizeof(ch)) {
            uint8_t dummy;
            xStreamBufferReceive(stream, &dummy, sizeof(dummy), 0);
            picoprobe_error("rtt_console_send_byte: drop byte on channel %d\n", channel);
        }
    }
    xStreamBufferSend(stream, &ch, sizeof(ch), 0);
    xEventGroupSetBits(events, EV_RTT_TO_TARGET);
}   // rtt_send_byte



void rtt_console_send_byte(uint8_t ch)
{
    rtt_send_byte(stream_rtt_console_to_target, RTT_CHANNEL_CONSOLE, ch, false);
}   // rtt_console_send_byte



#if OPT_NET_SYSVIEW_SERVER
void rtt_sysview_send_byte(uint8_t ch)
/**
 * Send a byte to the SysView channel of the target
 *
 * TODO currently this is disabled because this aborts SysView operation.  Has to be investigated.
 */
{
#if 0
    rtt_send_byte(stream_rtt_sysview_to_target, RTT_CHANNEL_SYSVIEW, ch, true);
#endif
}   // rtt_sysview_send_byte
#endif



void rtt_console_init(uint32_t task_prio)
{
    picoprobe_debug("rtt_console_init()\n");

    events = xEventGroupCreate();

    stream_rtt_console_to_target = xStreamBufferCreate(STREAM_RTT_SIZE, STREAM_RTT_TRIGGER);
    if (stream_rtt_console_to_target == NULL) {
        picoprobe_error("rtt_console_init: cannot create stream_rtt_console_to_target\n");
    }

#if OPT_NET_SYSVIEW_SERVER
    stream_rtt_sysview_to_target = xStreamBufferCreate(STREAM_RTT_SIZE, STREAM_RTT_TRIGGER);
    if (stream_rtt_sysview_to_target == NULL) {
        picoprobe_error("rtt_console_init: cannot create stream_rtt_sysview_to_target\n");
    }
#endif

    xTaskCreateAffinitySet(rtt_io_thread, "RTT", configMINIMAL_STACK_SIZE, NULL, task_prio, 1, &task_rtt_console);
    if (task_rtt_console == NULL)
    {
        picoprobe_error("rtt_console_init: cannot create task_rtt_console\n");
    }
}   // rtt_console_init
