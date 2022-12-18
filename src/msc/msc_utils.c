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



#if 0
	/*
	 * For lowlevel debugging (or better recording), DAP.c can be modified as follows:
	 * (but take care that the output FIFO of cdc_debug_printf() is big enough to hold all output)
	 */
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



bool swd_connect_target(bool write_mode)
{
    static uint64_t last_trigger_us;
    uint64_t now_us = time_us_64();
    bool ok = true;

    if (now_us - last_trigger_us > 1000*1000) {
    	picoprobe_info("========================================================================\n");

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

//        ok = target_set_state(RESET_RUN);
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
