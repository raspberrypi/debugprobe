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


#include <stdint.h>
#include <string.h>

#include <pico/stdlib.h>

#include "msc_utils.h"

#include "target_family.h"
#include "swd_host.h"
#include "DAP_config.h"
#include "DAP.h"


#define ADWORD(X)       (X) & 0xff, ((X) & 0xff00) >> 8, ((X) & 0xff0000) >> 16, ((X) & 0xff000000) >> 24
#define AWORD(X)        (X) & 0xff, ((X) & 0xff00) >> 8

/*
 * The following recordings are taken by modifying DAP.c
 * Take care that the output FIFO of cdc_debug_printf() is big enough to hold all output.
 */
#if 0
            uint32_t DAP_ProcessCommand(const uint8_t *request, uint8_t *response)
            {
            uint32_t num;

            num = _DAP_ProcessCommand(request, response);

            {
                uint32_t req_len = (num >> 16);
                uint32_t resp_len = (num & 0xffff);
                const char *s;

                switch (*request) {
                case ID_DAP_Info              : s = "ID_DAP_Info              "; break;
                case ID_DAP_HostStatus        : s = "ID_DAP_HostStatus        "; break;
                case ID_DAP_Connect           : s = "ID_DAP_Connect           "; break;    
                case ID_DAP_Disconnect        : s = "ID_DAP_Disconnect        "; break;
                case ID_DAP_TransferConfigure : s = "ID_DAP_TransferConfigure "; break;
                case ID_DAP_Transfer          : s = "ID_DAP_Transfer          "; break;
                case ID_DAP_TransferBlock     : s = "ID_DAP_TransferBlock     "; break;
                case ID_DAP_TransferAbort     : s = "ID_DAP_TransferAbort     "; break;
                case ID_DAP_WriteABORT        : s = "ID_DAP_WriteABORT        "; break;
                case ID_DAP_Delay             : s = "ID_DAP_Delay             "; break;
                case ID_DAP_ResetTarget       : s = "ID_DAP_ResetTarget       "; break;
                case ID_DAP_SWJ_Pins          : s = "ID_DAP_SWJ_Pins          "; break;
                case ID_DAP_SWJ_Clock         : s = "ID_DAP_SWJ_Clock         "; break;
                case ID_DAP_SWJ_Sequence      : s = "ID_DAP_SWJ_Sequence      "; break;
                case ID_DAP_SWD_Configure     : s = "ID_DAP_SWD_Configure     "; break;
                case ID_DAP_SWD_Sequence      : s = "ID_DAP_SWD_Sequence      "; break;
                case ID_DAP_JTAG_Sequence     : s = "ID_DAP_JTAG_Sequence     "; break;
                case ID_DAP_JTAG_Configure    : s = "ID_DAP_JTAG_Configure    "; break;
                case ID_DAP_JTAG_IDCODE       : s = "ID_DAP_JTAG_IDCODE       "; break;
                case ID_DAP_SWO_Transport     : s = "ID_DAP_SWO_Transport     "; break;
                case ID_DAP_SWO_Mode          : s = "ID_DAP_SWO_Mode          "; break;
                case ID_DAP_SWO_Baudrate      : s = "ID_DAP_SWO_Baudrate      "; break;
                case ID_DAP_SWO_Control       : s = "ID_DAP_SWO_Control       "; break;
                case ID_DAP_SWO_Status        : s = "ID_DAP_SWO_Status        "; break;
                case ID_DAP_SWO_ExtendedStatus: s = "ID_DAP_SWO_ExtendedStatus"; break;
                case ID_DAP_SWO_Data          : s = "ID_DAP_SWO_Data          "; break;       
                default                       : s = "unknown                  "; break;
                }

            #if 1
                cdc_debug_printf("    /* len */ %2ld, /* %s */ 0x%02x, ", req_len, s, *request);
                for (uint32_t u = 1;  u < req_len;  ++u) {
                cdc_debug_printf("0x%02x, ", request[u]);
                }
                cdc_debug_printf("\n");
            #endif
            #if 0
                cdc_debug_printf("                                                                                               /* --> len=0x%02lx: ", resp_len);
                for (uint32_t u = 0;  u < MIN(resp_len, 32);  ++u) {
                cdc_debug_printf("0x%02x, ", response[u]);
                }
                cdc_debug_printf(" */\n");
            #endif
            }

            return num;
            }
#endif



#define SEQ_SWJ_FROM_DORMANT_1   0xff, 0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19
#define SEQ_SWJ_FROM_DORMANT_2   0xa0, 0x01
#define SEQ_SWJ_RESET_1          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
#define SEQ_SWJ_RESET_2          0x00
#define SEQ_SWJ_CORE0            0x27, 0x29, 0x00, 0x01, 0x00
#define SEQ_SWJ_CORE1            0x27, 0x29, 0x00, 0x11, 0x01

/// Halt the target.
static const uint8_t cmd_halt_target[] = {
    /* len */ 13, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0d, 0x03, 0x00, 0x5f, 0xa0,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    0
};


/// Continue the target.
static const uint8_t cmd_continue_target[] = {
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 23, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x05, 0x30, 0xed, 0x00, 0xe0, 0x0d, 0x1f, 0x00, 0x00, 0x00, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0d, 0x01, 0x00, 0x5f, 0xa0,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    0
};


/// Reset target
static const uint8_t cmd_reset_target[] = {
    /* len */ 23, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x12, 0x00, 0x00, 0x03, 0x05, 0x0c, 0xed, 0x00, 0xe0, 0x0d, 0x04, 0x00, 0xfa, 0x05,
    /* len */  5, /* ID_DAP_TransferBlock      */ 0x06, 0x00, 0x01, 0x00, 0x06,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    0
};


/// Reset target and keep it halted
static const uint8_t cmd_reset_halt_target[] = {
    /* len */ 13, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0d, 0x03, 0x00, 0x5f, 0xa0,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xfc, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 33, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x06, 0x05, 0xfc, 0xed, 0x00, 0xe0, 0x0d, 0x01, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x12, 0x00, 0x00, 0x03, 0x05, 0x0c, 0xed, 0x00, 0xe0, 0x0d, 0x04, 0x00, 0xfa, 0x05,
    /* len */  5, /* ID_DAP_TransferBlock      */ 0x06, 0x00, 0x01, 0x00, 0x06,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 19, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x12, 0x00, 0x00, 0x03, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 25, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x06, 0x05, 0xf4, 0xed, 0x00, 0xe0, 0x0d, 0x10, 0x00, 0x00, 0x00, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f, 0x05, 0xf8, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 19, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x05, 0xfc, 0xed, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    0
};


/// Init DP and power up debug
static const uint8_t cmd_initdp_target[] = {
    /* len */  5, /* ID_DAP_TransferBlock      */ 0x06, 0x00, 0x01, 0x00, 0x02,
    /* len */ 19, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x50, 0x06,
    0
};


static const uint8_t cmd_wakeup_target[] = {
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0xfe,
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0x04,
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0xff,
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0xf0,
    /* len */  5, /* ID_DAP_SWJ_Clock          */ 0x11, ADWORD(DAP_DEFAULT_SWJ_CLOCK),
    /* len */  2, /* ID_DAP_Connect            */ 0x02, 0x01,
    /* len */  5, /* ID_DAP_SWJ_Clock          */ 0x11, ADWORD(DAP_DEFAULT_SWJ_CLOCK),
    /* len */  6, /* ID_DAP_TransferConfigure  */ 0x04, 0x02, 0x50, 0x00, 0x00, 0x00,
    /* len */  2, /* ID_DAP_SWD_Configure      */ 0x13, 0x00,
    /* len */ 19, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x88, SEQ_SWJ_FROM_DORMANT_1,
    /* len */  4, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x0c, SEQ_SWJ_FROM_DORMANT_2,
};


/// Connect to the target.  After this sequence the target is ready to receive "standard" swd_host commands
static const uint8_t cmd_setup_target[] = {
    // this was recorded from: pyocd cmd --target=rp2040
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0xfe,
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0x04,
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0xff,
    /* len */  2, /* ID_DAP_Info               */ 0x00, 0xf0,
    /* len */  5, /* ID_DAP_SWJ_Clock          */ 0x11, ADWORD(DAP_DEFAULT_SWJ_CLOCK),
    /* len */  2, /* ID_DAP_Connect            */ 0x02, 0x01,
    /* len */  5, /* ID_DAP_SWJ_Clock          */ 0x11, ADWORD(DAP_DEFAULT_SWJ_CLOCK),
    /* len */  6, /* ID_DAP_TransferConfigure  */ 0x04, 0x02, 0x50, 0x00, 0x00, 0x00,
    /* len */  2, /* ID_DAP_SWD_Configure      */ 0x13, 0x00,
    /* len */ 19, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x88, SEQ_SWJ_FROM_DORMANT_1,
    /* len */  4, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x0c, SEQ_SWJ_FROM_DORMANT_2,
    /* len */  9, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x33, SEQ_SWJ_RESET_1,
    /* len */  3, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x02, SEQ_SWJ_RESET_2,
    /* len */  9, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x33, SEQ_SWJ_RESET_1,
    /* len */  3, /* ID_DAP_SWJ_Sequence       */ 0x12, 0x02, SEQ_SWJ_RESET_2,
    /* len */ 13, /* ID_DAP_SWD_Sequence       */ 0x1d, 0x04, 0x08, 0x99, 0x85, 0x21, SEQ_SWJ_CORE0, 0x02, 0x00,
    /* len */  5, /* ID_DAP_TransferBlock      */ 0x06, 0x00, 0x01, 0x00, 0x02,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0x06,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0x03, 0x00, 0x00, 0x00, 0x06,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02,
    /* len */ 19, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x50, 0x06,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x00, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x01, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x02, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x03, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x00, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x03,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x00, 0x07,
    /* len */ 14, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x52, 0x00, 0x00, 0x4f, 0x03,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x08, 0xf0, 0x00, 0x00, 0x00, 0x0b,
    /* len */ 24, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x05, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x52, 0x00, 0x00, 0x03, 0x01, 0x12, 0x00, 0x00, 0x03, 0x05, 0xfc, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 13, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xfc, 0xed, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x01,
    /* len */  5, /* ID_DAP_TransferBlock      */ 0x06, 0x00, 0x01, 0x00, 0x03,
    /* len */ 20, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x0d, 0x05, 0xd0, 0xff, 0x0f, 0xe0, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    /* len */ 17, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x0a, 0x05, 0x00, 0xf0, 0x0f, 0xe0, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    /* len */ 20, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x0d, 0x05, 0xd0, 0xef, 0x00, 0xe0, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    /* len */ 20, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x0d, 0x05, 0xd0, 0x1f, 0x00, 0xe0, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    /* len */ 20, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x0d, 0x05, 0xd0, 0x2f, 0x00, 0xe0, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    /* len */ 19, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x04, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0d, 0x01, 0x00, 0x5f, 0xa0, 0x05, 0x00, 0xed, 0x00, 0xe0, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0xfc, 0xed, 0x00, 0xe0, 0x0f,
    /* len */  9, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x02, 0x05, 0x00, 0x10, 0x00, 0xe0, 0x0f,
    /* len */ 39, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x08, 0x05, 0x28, 0x10, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x05, 0x38, 0x10, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x10, 0x00, 0xe0, 0x0d, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x20, 0x00, 0xe0, 0x0f,
    /* len */ 59, /* ID_DAP_Transfer           */ 0x05, 0x00, 0x0c, 0x05, 0x00, 0x20, 0x00, 0xe0, 0x0d, 0x02, 0x00, 0x00, 0x00, 0x05, 0x08, 0x20, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0c, 0x20, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x05, 0x10, 0x20, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x05, 0x14, 0x20, 0x00, 0xe0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x05, 0xf0, 0xed, 0x00, 0xe0, 0x0f,
    0
};



static bool swd_send_recorded_data(const uint8_t *cmds)
{
    bool sts = true;

    for (;;) {
        uint8_t len;
        uint8_t response[64];
        uint32_t r;

        len = *cmds;
        if (len == 0) {
            break;
        }

        r = DAP_ProcessCommand(cmds + 1, response);
        if ((r >> 16) != len) {
            // there is a problem in the recording
            sts = false;
            break;
        }
        cmds += len + 1;
    }
    return sts;
}   // swd_send_recorded_data



bool swd_connect_target(bool write_mode)
{
    static uint64_t last_trigger_us;
    uint64_t now_us = time_us_64();
    bool ok = true;

    if (now_us - last_trigger_us > 1000*1000) {
    	picoprobe_info("========================================================================\n");

#if 0
    	// so funktioniert das Lesen des Speichers :-/
        picoprobe_info("---------------------------------- swd_init\n");
        swd_init();
        picoprobe_info("---------------------------------- swd_send_recorded_data(cmd_setup_target)\n");
        ok = swd_send_recorded_data(cmd_setup_target);
#endif

//        picoprobe_info("---------------------------------- swd_send_recorded_data(cmd_wakeup_target)\n");
//        ok = swd_send_recorded_data(cmd_wakeup_target);

//        picoprobe_info("---------------------------------- JTAG2SWD()\n");
//        ok = JTAG2SWD();
//        ok = JTAG2SWD();

//        picoprobe_info("---------------------------------- swd_send_recorded_data(cmd_setup_target)\n");
//        ok = swd_send_recorded_data(cmd_setup_target);


#if 0
        // das funktioniert prinzipiell, Speicher kann man trotzdem nicht lesen :-/
        picoprobe_info("---------------------------------- swd_init_debug\n");
        swd_init_debug();

        picoprobe_info("---------------------------------- %d\n", ok);

        {
        	uint32_t val;

        	ok = swd_read_dp(DP_CTRL_STAT, &val);
        	picoprobe_info("                1 !!!!!!!! %d %lx\n", ok, val);

        	ok = swd_write_dp(DP_CTRL_STAT, 0x50000000);
        	picoprobe_info("                2 !!!!!!!! %d %lx\n", ok, val);

        	for (int i = 0;  i < 10;  ++i)
        	{
        		ok = swd_read_dp(DP_CTRL_STAT, &val);
        		picoprobe_info("                 !!!!!!!! %d %lx\n", ok, val);
        	}
        }
#endif

        // cmd_halt_target -> flash/RAM read successful

        // ok = swd_send_recorded_data(cmd_halt_target);
        // picoprobe_info("---------------------------------- %d\n", ok);

        // vorher immer swd_init_debug()
        // RESET_HOLD               -> ok, Flash nok
        // RESET_RUN                -> bleibt natürlich wegen dem swd_off() stehen
        // RESET_PROGRAM            -> bleibt bei "Wait until core is halted" hängen (wenn man das überspringt, läuft er durch, der Speicher ist aber 0)
        // NO_DEBUG                 -> ok, Flash nok
        // DEBUG                    -> ok, Flash nok
        // HALT                     -> bleibt ebenfalls bei "Wait until core is halted" hängen
        // RUN                      -> bleibt dann hinterher irgendwo hängen (bei einem Zugriff aufs Ziel)

#if 1
        swd_init_debug();
#else
        swd_init();
        g_target_family->target_before_init_debug();
#endif
        ok = target_set_state(RESET_PROGRAM);
        picoprobe_info("---------------------------------- %d\n", ok);

        //
        // cheap test to read some memory
        //
        if (1) {
        	uint8_t buff[32];
        	uint8_t r;

        	picoprobe_info("---------------------------------- buff:\n");
        	memset(buff, 0xaa, sizeof(buff));
        	r = swd_read_memory(0x10000000, buff,  sizeof(buff));
        	picoprobe_info("   %u -", r);
        	for (int i = 0;  i < sizeof(buff);  ++i)
        		picoprobe_info(" %02x", buff[i]);
        	picoprobe_info("\n");
        	r = swd_read_memory(0x10000000, buff,  sizeof(buff));
        	picoprobe_info("   %u -", r);
        	for (int i = 0;  i < sizeof(buff);  ++i)
        		picoprobe_info(" %02x", buff[i]);
        	picoprobe_info("\n");
            picoprobe_info("----------------------------------\n");
        }
    }
    last_trigger_us = now_us;

    return ok;
}   // swd_connect_target



void setup_uf2_record(struct uf2_block *uf2, uint32_t target_addr, uint32_t payload_size, uint32_t block_no, uint32_t num_blocks)
{
    uf2->magic_start0 = UF2_MAGIC_START0;
    uf2->magic_start1 = UF2_MAGIC_START1;
    uf2->flags        = UF2_FLAG_FAMILY_ID_PRESENT;
    uf2->target_addr  = target_addr;
    uf2->payload_size = payload_size;
    uf2->block_no     = block_no;
    uf2->num_blocks   = num_blocks;
    uf2->file_size    = RP2040_FAMILY_ID;
    uf2->magic_end    = UF2_MAGIC_END;
}   // setup_uf2_record



/**
 * The following is old code, keeping for further fiddling
 */
#if 0
/// taken from pico_debug and output of pyODC
static uint8_t swd_from_dormant(void)
{
    const uint8_t in1[] = {0xff, 0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19};
    const uint8_t in2[] = {0xa0, 0x01};

    SWJ_Sequence(136, in1);
    SWJ_Sequence( 12, in2);
    return 1;
}


/// taken from pico_debug and output of pyODC
static uint8_t swd_reset(void)
{
    const uint8_t in1[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t in2[] = {0x00};

    SWJ_Sequence( 51, in1);
    SWJ_Sequence(  2, in2);
    return 1;
}


/// taken from output of pyODC
static uint8_t swd_read_idcode(uint32_t *id)
{
    return swd_read_dp(DP_IDCODE, id);
}


static void swd_target_select(uint8_t core)
{
    static const uint8_t out1[]  = {0x99};
    static const uint8_t core0[] = {0x27, 0x29, 0x00, 0x01, 0x00 };
    static const uint8_t core1[] = {0x27, 0x29, 0x00, 0x11, 0x01 };
    static const uint8_t out2[]  = {0x00};
    static uint8_t input;

    vTaskDelay(200);
    SWD_Sequence(8, out1, NULL);
    SWD_Sequence(0x85, NULL, &input);
    SWD_Sequence(33, (core == 0) ? core0 : core1, NULL);
    SWD_Sequence(2, out2, NULL);
}


static bool initialized = false;

        if ( !initialized  &&  time_us_64() > 5 * 1000000) {
            uint32_t r;
            uint32_t data[256];

            initialized = true;
            DAP_Setup();
            // if (g_board_info.prerun_board_config) {
            //     g_board_info.prerun_board_config();
            // }

            swd_init(); // this also starts the target

#if 1
            picoprobe_info("----------------------------------\n");
            swd_send_recorded_data(cmd_setup_target);
            picoprobe_info("----------------------------------\n");
#endif
#if 0
            // vTaskDelay(200);
            swd_from_dormant();
            swd_reset();
            swd_reset();
            swd_target_select(0);
            swd_read_idcode( &r);
            picoprobe_info("   swd_read_idcode: 0x%lx\n", r);

            swd_write_dp(DP_SELECT, 2);
            swd_read_dp(DP_CTRL_STAT, &r);
            picoprobe_info("   swd_read_dp(DP_CTRL_STAT): 0x%lx\n", r);

            r = swd_clear_errors();
            picoprobe_info("   swd_clear_errors: %lu\n", r);
            swd_write_dp(DP_SELECT, 0);
            swd_read_dp(DP_CTRL_STAT, &r);
            picoprobe_info("   swd_read_dp(DP_CTRL_STAT): 0x%lx\n", r);
#endif

#if 0
            // das überlebt er
            for (int i = 0;  i < 16;  ++i) {
                swd_read_core_register(i, &r);
                picoprobe_info("   swd_read_core_register(%d): 0x%lx\n", i, r);
            }
#endif

#if 1
            // das überlebt er NICHT
            picoprobe_info("----------------------------------\n");
            memset(data, 0xff, sizeof(data));
            swd_read_memory(0x20000000, (uint8_t *)data, sizeof(data));
            picoprobe_info("   swd_read_memory: %lu\n", r);
            for (int i = 0;  i < sizeof(data) / 4;  ++i) {
                picoprobe_info(" %08lx", data[i]);
            }
            picoprobe_info("\n");
            picoprobe_info("----------------------------------\n");
#endif

#if 0
            // ok: RESET_HOLD, RUN (but does nothing)
            // nok: RESET_RUN, RESET_PROGRAM, NO_DEBUG, HALT
            // half: DEBUG
            picoprobe_info("----------------------------------\n");
            swd_set_target_state_sw(RUN);
            picoprobe_info("----------------------------------\n");

            memset(data, 0xff, sizeof(data));
            for (uint32_t offs = 0;  offs < 32;  ++offs) {
                swd_read_byte(0x10000000 + offs, data);
                picoprobe_info("   swd_read_byte: %02x\n", data[0]);
            }

            memset(data, 0xff, sizeof(data));
            r = swd_read_memory(0x10000000, data, sizeof(data));
            picoprobe_info("   swd_read_block: %lu\n", r);
            for (int i = 0;  i < sizeof(data);  ++i) {
                picoprobe_info(" %02x", data[i]);
            }
            picoprobe_info("\n");

            memset(data, 0xff, sizeof(data));
            r = swd_read_memory(0x10000000, data, sizeof(data));
            picoprobe_info("   swd_read_block: %lu\n", r);
            for (int i = 0;  i < sizeof(data);  ++i) {
                picoprobe_info(" %02x", data[i]);
            }
            picoprobe_info("\n");

            // swd_init_debug();
            // r = swd_set_target_state_hw(HALT);
            // if (target_set_state(HALT) == 0) {
            //     target_set_state(RESET_PROGRAM);
            // }
#endif
#endif
