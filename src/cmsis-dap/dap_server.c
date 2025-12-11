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

#include <pico/stdlib.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "dap_server.h"
#include "dap_util.h"
#include "DAP_config.h"
#include "DAP.h"
#include "led.h"
#include "sw_lock.h"
#include "minIni/minIni.h"


#if OPT_CMSIS_DAPV2
    static TaskHandle_t        dap_taskhandle = NULL;
    static EventGroupHandle_t  dap_events;
#endif


/*
 * The following is part of a hack to make DAP_PACKET_COUNT a variable.
 * CMSIS-DAPv2 has better performance with 2 packets while
 * CMSIS-DAPv1 only works with one packet, at least with openocd which throws a
 *     "CMSIS-DAP transfer count mismatch: expected 12, got 8" on flashing.
 * The correct packet count has to be set on connection.
 *
 * More notes: pyocd works with large packets only, if the packet count is one.
 * Additionally pyocd is instable if packet count > 1.  Valid for pyocd 0.34.3.
 *
 * OpenOCD 0.11: packet size of 1024 and 2 buffers ok
 * OpenOCD 0.12: 1024 no longer working, but 512 and 2 buffers is ok
 *
 * 2024-10-13 - confusing... openocd and so on work only with 1 packet
 *
 * 2025-12-11            PyOCD 0.41.0      OpenOCD 0.12.0+dev-gebec950-dirty
 *           64/1             -/-                     +/+                       (flash/debug)
 *          128/1             +/+                     +/+
 *          256/1             -/+                     +/+
 *          512/1             -/+                     +/+
 *
 *           64/2             -/-                     +/+
 *          128/2             +/+                     -/+
 *          256/2             X/+                     -/+
 *
 *           64/4              -                       -
 *          128/4             +/+                     -/+
 *          128/8             +/+                     -/+
 *          256/8             X/+                     -/+
 *
 *     - openocd uses full buffer, pyocd only ~400 bytes
 *     - pyocd transmits obscure zero byte during certain flash phases if buffer >= 256
 *     - question is if the tools are using more than one buffer for debugging
 *
 */
#define _DAP_PACKET_COUNT_OPENOCD   1
#define _DAP_PACKET_SIZE_OPENOCD    512
#define _DAP_PACKET_COUNT_PROBERS   8
#define _DAP_PACKET_SIZE_PROBERS    512
#define _DAP_PACKET_COUNT_PYOCD     4
#define _DAP_PACKET_SIZE_PYOCD      512
#define _DAP_PACKET_COUNT_UNKNOWN   1
#define _DAP_PACKET_SIZE_UNKNOWN    64

#define _DAP_PACKET_COUNT_HID       1
#define _DAP_PACKET_SIZE_HID        64

uint8_t  dap_packet_count = _DAP_PACKET_COUNT_UNKNOWN;
uint16_t dap_packet_size  = _DAP_PACKET_SIZE_UNKNOWN;

#define BUFFER_MAXSIZE_1 MAX(_DAP_PACKET_COUNT_OPENOCD*_DAP_PACKET_SIZE_OPENOCD, _DAP_PACKET_COUNT_PROBERS*_DAP_PACKET_SIZE_PROBERS)
#define BUFFER_MAXSIZE_2 MAX(_DAP_PACKET_COUNT_PYOCD  *_DAP_PACKET_SIZE_PYOCD,   _DAP_PACKET_COUNT_UNKNOWN*_DAP_PACKET_SIZE_UNKNOWN)
#define BUFFER_MAXSIZE   MAX(BUFFER_MAXSIZE_1, BUFFER_MAXSIZE_2)

#define PACKET_MAXSIZE_1 MAX(_DAP_PACKET_SIZE_OPENOCD, _DAP_PACKET_SIZE_PROBERS)
#define PACKET_MAXSIZE_2 MAX(_DAP_PACKET_SIZE_PYOCD,   _DAP_PACKET_SIZE_UNKNOWN)
#define PACKET_MAXSIZE   MAX(PACKET_MAXSIZE_1, PACKET_MAXSIZE_2)

#if (CFG_TUD_VENDOR_RX_BUFSIZE < PACKET_MAXSIZE)
    #error "increase CFG_TUD_VENDOR_RX_BUFSIZE"
#endif

#if OPT_CMSIS_DAPV1  ||  OPT_CMSIS_DAPV2
    static uint8_t TxDataBuffer[BUFFER_MAXSIZE];
#endif
#if OPT_CMSIS_DAPV2
    static uint8_t RxDataBuffer[BUFFER_MAXSIZE];

    static bool swd_connected = false;
    static uint32_t request_len;
    static uint32_t last_request_us = 0;
    static uint32_t rx_len = 0;
    static daptool_t tool = E_DAPTOOL_UNKNOWN;
#endif



#if OPT_CMSIS_DAPV2
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
//    picoprobe_info("rx: %d, %d\n", itf, bufsize);

    if (itf != 0) {
        return;
    }

    xEventGroupSetBits(dap_events, 0x01);

    rx_len += tud_vendor_read(RxDataBuffer + rx_len, sizeof(RxDataBuffer) - rx_len);

    for (;;) {
        if (rx_len == 0) {
            return;
        }

        request_len = DAP_GetCommandLength(RxDataBuffer, rx_len);
        if (rx_len < request_len) {
//            picoprobe_error("......... %d > %d\n", request_len, rx_len);
            if (rx_len == 1  &&  RxDataBuffer[0] == 0) {
                // this is a special pyocd hack
//                picoprobe_error("#################################\n");
                rx_len = 0;
            }
            return;
        }

        if (rx_len != request_len) {
            picoprobe_error("!!!!!!!!! %d != %d\n", request_len, rx_len);
        }

        //
        // now we have at least one request in the buffer
        //
        last_request_us = time_us_32();
//        picoprobe_info("<<<(%ld,%ld) %d %d\n", request_len, rx_len, RxDataBuffer[0], RxDataBuffer[1]);

#if 1
        if (tool == E_DAPTOOL_UNKNOWN) {
            //
            // try to find out which tool is connecting
            //
            uint32_t psize;
            uint32_t pcnt;

            psize = ini_getl(MININI_SECTION, MININI_VAR_DAP_PSIZE, 0, MININI_FILENAME);
            pcnt  = ini_getl(MININI_SECTION, MININI_VAR_DAP_PCNT,  0, MININI_FILENAME);
            if (psize != 0  ||  pcnt != 0)
            {
                dap_packet_count = (pcnt  != 0) ? pcnt  : _DAP_PACKET_COUNT_UNKNOWN;
                dap_packet_size  = (psize != 0) ? psize : _DAP_PACKET_SIZE_UNKNOWN;
                dap_packet_size  = MIN(dap_packet_size, PACKET_MAXSIZE);
                if (dap_packet_count * dap_packet_size > BUFFER_MAXSIZE) {
                    dap_packet_size  = MIN(dap_packet_size, BUFFER_MAXSIZE);
                    dap_packet_count = BUFFER_MAXSIZE / dap_packet_size;
                }
                tool = E_DAPTOOL_USER;
            }
            else
            {
                tool = DAP_FingerprintTool(RxDataBuffer, request_len);
                if (tool == E_DAPTOOL_OPENOCD) {
                    dap_packet_count = _DAP_PACKET_COUNT_OPENOCD;
                    dap_packet_size  = _DAP_PACKET_SIZE_OPENOCD;
                }
                else if (tool == E_DAPTOOL_PYOCD) {
                    dap_packet_count = _DAP_PACKET_COUNT_PYOCD;
                    dap_packet_size  = _DAP_PACKET_SIZE_PYOCD;
                }
                else if (tool == E_DAPTOOL_PROBERS) {
                    dap_packet_count = _DAP_PACKET_COUNT_PROBERS;
                    dap_packet_size  = _DAP_PACKET_SIZE_PROBERS;
                }
            }
        }
#endif

        //
        // initiate SWD connect / disconnect
        //
        if ( !swd_connected) {
            if ( !DAP_OfflineCommand(RxDataBuffer)) {
                if (sw_lock(E_SWLOCK_DAPV2)) {
                    swd_connected = true;
                    picoprobe_info("=================================== DAPv2 connect target, host %s, buffer: %dx%dbytes\n",
                            (tool == E_DAPTOOL_OPENOCD) ? "OpenOCD"    :
                                    (tool == E_DAPTOOL_PYOCD) ? "pyOCD"       :
                                            (tool == E_DAPTOOL_PROBERS) ? "probe-rs" :
                                                    (tool == E_DAPTOOL_USER) ? "user-set"   : "UNKNOWN", dap_packet_count, dap_packet_size);
                    led_state(LS_DAPV2_CONNECTED);
                }
                else {
                    // did not get lock!
                }
            }
            else {
                // ID_DAP_Info must/can be done without a lock
            }
        }
        else {
            // connected:
            if (RxDataBuffer[0] == ID_DAP_Disconnect) {
                swd_connected = false;
                picoprobe_info("=================================== DAPv2 disconnect target\n");
                led_state(LS_DAPV2_DISCONNECTED);
                sw_unlock(E_SWLOCK_DAPV2);

                dap_packet_count = _DAP_PACKET_COUNT_UNKNOWN;
                dap_packet_size  = _DAP_PACKET_SIZE_UNKNOWN;
                tool = DAP_FingerprintTool(NULL, 0);
            }
            else if (RxDataBuffer[0] == ID_DAP_Info  ||  RxDataBuffer[0] == ID_DAP_HostStatus) {
                // ignore these commands after an ID_DAP_Disconnect
                // e.g. openocd issues ID_DAP_HostStatus after ID_DAP_Disconnect
            }
        }

        //
        // execute request and send back response
        //
        if (swd_connected  ||  DAP_OfflineCommand(RxDataBuffer))
        {
            uint32_t resp_len;

#if 0
            // heavy debug output, set dap_packet_count=2 to stumble into the bug
            const uint16_t bufsize = 64;
            picoprobe_info("-----------------------------------------------\n");
            picoprobe_info("<< (%lx) ", request_len);
            for (int i = 0;  i < bufsize;  ++i) {
                picoprobe_info_out(" %02x", RxDataBuffer[i]);
                if (i == request_len - 1) {
                    picoprobe_info_out(" !!!!");
                }
            }
            picoprobe_info_out("\n");
            vTaskDelay(pdMS_TO_TICKS(5));
            resp_len = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
            picoprobe_info(">> (%lx) ", resp_len);
            for (int i = 0;  i < bufsize;  ++i) {
                picoprobe_info_out(" %02x", TxDataBuffer[i]);
                if (i == (resp_len & 0xffff) - 1) {
                    picoprobe_info_out(" !!!!");
                }
            }
            picoprobe_info_out("\n");
#else
            resp_len = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
#endif

            //        picoprobe_info(">>>(%d) %d %d %d %d %d\n", request_len, resp_len & 0xffff, TxDataBuffer[0], TxDataBuffer[1], TxDataBuffer[2], TxDataBuffer[3]);

            tud_vendor_write(TxDataBuffer, resp_len & 0xffff);
            tud_vendor_flush();

            if (request_len != (resp_len >> 16))
            {
                // there is a bug in CMSIS-DAP, see https://github.com/ARM-software/CMSIS_5/pull/1503
                // but we trust our own length calculation
                picoprobe_error("   !!!!!!!! request (%u) and executed length (%u) differ\n",
                        (unsigned)request_len, (unsigned)(resp_len >> 16));
            }
        }

        if (rx_len == request_len)
        {
            rx_len = 0;
        }
        else
        {
            memmove(RxDataBuffer, RxDataBuffer + request_len, rx_len - request_len);
            rx_len -= request_len;
        }
    }
}   // tud_vendor_rx_cb
#endif



#if OPT_CMSIS_DAPV2
/**
 * CMSIS-DAP task.
 * Receive DAP requests, execute them via DAP_ExecuteCommand() and transmit the response.
 *
 * Problem zones:
 * - connect / disconnect: pyOCD does not send permanently requests if in gdbserver mode, OpenOCD does.
 *   As a consequence "disconnect" has to be detected via the command stream.  If the tool on host side
 *   fails without a disconnect, the SWD connection is not freed (for MSC or RTT).  To recover from this
 *   situation either reset the probe or issue something like "pyocd reset -t rp2040"
 * - fingerprinting the host tool: this is for optimization of the OpenOCD connection, because OpenOCD
 *   can handle big DAP packets and thus transfer is faster.
 * - ID_DAP_Disconnect / ID_DAP_Info / ID_DAP_HostStatus leads to an SWD disconnect if there is no other
 *   command following.  This is required, because "pyocd list" leads to tool detection without
 *   connect/disconnect and thus otherwise tool detection would be stuck to "pyocd" for the next connection.
 */
void dap_task(void *ptr)
{
    EventBits_t xx;

    dap_packet_count = _DAP_PACKET_COUNT_UNKNOWN;
    dap_packet_size  = _DAP_PACKET_SIZE_UNKNOWN;
    for (;;) {
        xx = xEventGroupWaitBits(dap_events, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
//        picoprobe_info("vvvv %d\n", xx);
    }
}   // dap_task
#endif



#if OPT_CMSIS_DAPV2
extern uint8_t const desc_ms_os_20[];

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
/**
 * Control handshake during USB setup
 */
{
    // nothing to do with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest) {
                case 1:
                    if (request->wIndex == 7) {
                        // Get Microsoft OS 2.0 compatible descriptor
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);

                        return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
                    }
                    else {
                        return false;
                    }

                default:
                    break;
            }
            break;

        default:
            break;
    }

    // stall unknown request
    return false;
}   // tud_vendor_control_xfer_cb
#endif



#if OPT_CMSIS_DAPV1
static bool hid_swd_connected;
static bool hid_swd_disconnect_requested;
static TimerHandle_t     timer_hid_disconnect = NULL;
static void             *timer_hid_disconnect_id;


static void hid_disconnect(TimerHandle_t xTimer)
{
    if (hid_swd_disconnect_requested  &&  hid_swd_connected) {
        hid_swd_connected = false;
        picoprobe_info("=================================== DAPv1 disconnect target\n");
        led_state(LS_DAPV1_DISCONNECTED);
        sw_unlock(E_SWLOCK_DAPV1);
    }
}   // hid_disconnect



uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    // TODO not Implemented
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}



void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* RxDataBuffer, uint16_t bufsize)
{
    // This doesn't use multiple report and report ID
    (void) itf;
    (void) report_id;
    (void) report_type;

    if (timer_hid_disconnect == NULL) {
        timer_hid_disconnect = xTimerCreate("timer_hid_disconnect", pdMS_TO_TICKS(1000), pdFALSE, timer_hid_disconnect_id,
                                            hid_disconnect);
        if (timer_hid_disconnect == NULL) {
            picoprobe_error("tud_hid_set_report_cb: cannot create timer_hid_disconnect\n");
        }
    }
    else {
        xTimerReset(timer_hid_disconnect, pdMS_TO_TICKS(1000));
    }

    //
    // initiate SWD connect / disconnect
    //
    if ( !hid_swd_connected  &&  RxDataBuffer[0] != ID_DAP_Info) {
        if (sw_lock(E_SWLOCK_DAPV1)) {
            hid_swd_connected = true;
            picoprobe_info("=================================== DAPv1 connect target\n");
            led_state(LS_DAPV1_CONNECTED);
        }
    }
    if (RxDataBuffer[0] == ID_DAP_Disconnect  ||  RxDataBuffer[0] == ID_DAP_Info  ||  RxDataBuffer[0] == ID_DAP_HostStatus) {
        hid_swd_disconnect_requested = true;

        // this is the minimum version which should always work
        dap_packet_count = _DAP_PACKET_COUNT_HID;
        dap_packet_size  = _DAP_PACKET_SIZE_HID;
    }
    else {
        hid_swd_disconnect_requested = false;
    }

    //
    // execute request and send back response
    //
    if (hid_swd_connected  ||  DAP_OfflineCommand(RxDataBuffer)) {
#if 0
        // heavy debug output, set dap_packet_count=2 to stumble into the bug
        uint32_t request_len = DAP_GetCommandLength(RxDataBuffer, bufsize);
        picoprobe_info("-----------------------------------------------\n");
        picoprobe_info("< (%lx) ", request_len);
        for (int i = 0;  i < bufsize;  ++i) {
            picoprobe_info_out(" %02x", RxDataBuffer[i]);
            if (i == request_len - 1) {
                picoprobe_info_out(" !!!!");
            }
        }
        picoprobe_info_out("\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        uint32_t res = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
        picoprobe_info("> (%lx) ", res);
        for (int i = 0;  i < bufsize;  ++i) {
            picoprobe_info_out(" %02x", TxDataBuffer[i]);
            if (i == (res & 0xffff) - 1) {
                picoprobe_info_out(" !!!!");
            }
        }
        picoprobe_info_out("\n");
#else
        uint32_t res = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
#endif
        tud_hid_report(0, TxDataBuffer, res & 0xffff);
    }
}   // tud_hid_set_report_cb
#endif



void dap_server_init(uint32_t task_prio)
{
    picoprobe_debug("dap_server_init(%u)\n", (unsigned)task_prio);

#if OPT_CMSIS_DAPV2
    dap_events = xEventGroupCreate();
    xTaskCreate(dap_task, "CMSIS-DAPv2", configMINIMAL_STACK_SIZE, NULL, task_prio, &dap_taskhandle);
#endif
}   // rtt_console_init
