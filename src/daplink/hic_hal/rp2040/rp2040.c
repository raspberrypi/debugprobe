/**
 * @file    rp2040.c
 * @brief   board ID for the Raspberry Pi Pico board
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2009-2019, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "daplink_addr.h"
#include "swd_host.h"
#include "target_board.h"
#include "target_family.h"

#include "cmsis_os2.h"

#include "FreeRTOS.h"
#include "task.h"



//
// Debug Port Register Addresses
//
#define DP_DPIDR                        0x00U   // IDCODE Register (RD)
#define DP_ABORT                        0x00U   // Abort Register (WR)
#define DP_CTRL_STAT                    0x04U   // Control & Status
#define DP_RESEND                       0x08U   // Resend (RD)
#define DP_SELECT                       0x08U   // Select Register (WR)
#define DP_RDBUFF                       0x0CU   // Read Buffer (RD)
#define DP_TARGETSEL                    0x0CU   // Read Buffer (WR)

#define DP_DLCR                         0x14    // (RW)
#define DP_TARGETID                     0x24    // Target ID (RD)
#define DP_DLPIDR                       0x34    // (RD)
#define DP_EVENTSTAT                    0x44    // (RO)

//
// Abort Register Defines
//
#define DAP_ABORT           (1<<0)
#define STKCMPCLR           (1<<1)          
#define STKERRCLR           (1<<2)
#define WDERRCLR            (1<<3)
#define ORUNERRCLR          (1<<4)
#define ALLERRCLR           (STKCMPCLR|WDERRCLR|WDERRCLR|ORUNERRCLR)

#define SWD_OK              0
#define SWD_ERROR           3
#define CHECK_OK(func)      { int rc = func; if (rc != SWD_OK) return rc; }



void osDelay(uint32_t ticks)
{
    vTaskDelay(10 * ticks);
}   // osDelay



#if 0
static void swd_set_target_reset_nrf(uint8_t asserted)
{
    uint32_t ap_index_return;

    if (asserted) {
        swd_init_debug();

        swd_read_ap(0x010000FC, &ap_index_return);
        if (ap_index_return == 0x02880000) {
            // Have CTRL-AP
            swd_write_ap(0x01000000, 1);  // CTRL-AP reset hold
        }
        else {
            // No CTRL-AP - Perform a soft reset
            // 0x05FA0000 = VECTKEY, 0x4 = SYSRESETREQ
            uint32_t swd_mem_write_data = 0x05FA0000 | 0x4;
            swd_write_memory(0xE000ED0C, (uint8_t *) &swd_mem_write_data, 4);
        }
        if(g_board_info.swd_set_target_reset){ //aditional reset
            g_board_info.swd_set_target_reset(asserted);
        }
    } else {
        swd_read_ap(0x010000FC, &ap_index_return);
        if (ap_index_return == 0x02880000) {
            // Device has CTRL-AP
            swd_write_ap(0x01000000, 0);  // CTRL-AP reset release
        }
        else {
            // No CTRL-AP - Soft reset has been performed
        }
        if(g_board_info.swd_set_target_reset){
            g_board_info.swd_set_target_reset(asserted);
        }
    }
}

const target_family_descriptor_t g_nordic_nrf52 = {
    .family_id = kNordic_Nrf52_FamilyID,
    .default_reset_type = kSoftwareReset,
    .soft_reset_type = SYSRESETREQ,
    .swd_set_target_reset = swd_set_target_reset_nrf,
};
#endif


/**
* List of start and size for each size of flash sector
* The size will apply to all sectors between the listed address and the next address
* in the list.
* The last pair in the list will have sectors starting at that address and ending
* at address start + size.
*/
static const sector_info_t sectors_info[] = {
    {DAPLINK_ROM_IF_START, DAPLINK_SECTOR_SIZE},
 };

// k26f target information
target_cfg_t rp2040_target_device = {
    .version                    = kTargetConfigVersion,
    .sectors_info               = sectors_info,
    .sector_info_length         = (sizeof(sectors_info))/(sizeof(sector_info_t)),
    .flash_regions[0].start     = DAPLINK_ROM_IF_START,
    .flash_regions[0].end       = DAPLINK_ROM_IF_START + DAPLINK_ROM_IF_SIZE,
    .flash_regions[0].flags     = kRegionIsDefault,
    .ram_regions[0].start       = DAPLINK_RAM_APP_START,
    .ram_regions[0].end         = DAPLINK_RAM_APP_START + DAPLINK_RAM_APP_SIZE,
    /* .flash_algo not needed for bootloader */
};



/*************************************************************************************************/

/// taken from pico_debug and output of pyODC
static uint8_t swd_from_dormant(void)
{
    const uint8_t ones_seq[] = {0xff};
    const uint8_t zero_seq[] = {0x00};
    const uint8_t selection_alert_seq[] = {0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19};
    const uint8_t act_seq[] = { 0x1a };

    SWJ_Sequence(  8, ones_seq);
    SWJ_Sequence(128, selection_alert_seq);
    SWJ_Sequence(  4, zero_seq);
    SWJ_Sequence(  8, act_seq);
    return 1;
}


/// taken from pico_debug and output of pyODC
static uint8_t swd_line_reset(void)
{
    const uint8_t reset_seq[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03};
    SWJ_Sequence( 52, reset_seq);
    return 1;
}


/// taken from output of pyODC
static uint8_t swd_read_idcode(uint32_t *id)
{
    return swd_read_dp(DP_IDCODE, id);
}


static void swd_targetsel(uint8_t core)
{
    static const uint8_t out1[]        = {0x99};
    static const uint8_t core_0[]      = {0x27, 0x29, 0x00, 0x01, 0x00};
    static const uint8_t core_1[]      = {0x27, 0x29, 0x00, 0x11, 0x01};
    static const uint8_t core_rescue[] = {0x27, 0x29, 0x00, 0xf1, 0x00};
    static const uint8_t out2[]        = {0x00};
    static uint8_t input;

    SWD_Sequence(8, out1, NULL);
    SWD_Sequence(0x80 + 5, NULL, &input);
    if (core == 0)
        SWD_Sequence(33, core_0, NULL);
    else if (core == 0)
        SWD_Sequence(33, core_1, NULL);
    else
        SWD_Sequence(33, core_rescue, NULL);
    SWD_Sequence(2, out2, NULL);
}


static int dp_core_select(uint8_t core)
{
    uint32_t rv;

    swd_line_reset();
    swd_targetsel(core);

    CHECK_OK(swd_read(0, DP_DPIDR, &rv));
    cdc_debug_printf("  id(%d)=%08lx\n", core, rv);
    return SWD_OK;
}


static int dp_core_select_and_confirm(uint8_t core)
{
    uint32_t rv;

    CHECK_OK(dp_core_select(core));
    CHECK_OK(swd_write(0, DP_ABORT, ALLERRCLR));
    CHECK_OK(swd_write(0, DP_SELECT, 0));
    CHECK_OK(swd_read(0, DP_CTRL_STAT, &rv));

    return SWD_OK;
}


static int core_select(uint8_t core)
{
    uint32_t dlpidr;

    CHECK_OK(dp_core_select(core));
    CHECK_OK(dp_read(DP_DLPIDR, &dlpidr));
    return SWD_OK;
}


static int dp_initialize(void)
{
    swd_from_dormant();
    int have_reset = 0;

    // Now try to connect to each core and setup power and debug status...
    for (int c = 0; c < 2; c++) {
        while (1) {
            if (dp_core_select_and_confirm(c) != SWD_OK) {
                if (dp_core_select_and_confirm(c) != SWD_OK) {
                    // If we've already reset, then this is fatal...
                    if (have_reset)
                        return SWD_ERROR;
                    dp_rescue_reset();
                    swd_from_dormant();     // seem to need this?
                    have_reset = 1;
                    continue;
                }
            }
            // Make sure we can use dp_xxx calls...
            if (dp_power_on() != SWD_OK) 
                continue;

            // Now we can enable debugging... (and remove breakpoints)
            if (core_enable_debug() != SWD_OK)
                continue;

            // If we get here, then this core is fine...
            break;
        }
    }
    // And lets make sure we end on core 0
    if (dp_core_select(0) != SWD_OK) {
        return SWD_ERROR;
    }

    return SWD_OK;
}

/*************************************************************************************************/



extern void probe_assert_reset(bool);

void rp2040_swd_set_target_reset(uint8_t asserted)
{
    // TODO set HW signal accordingly, asserted means "active"
    cdc_debug_printf("----- rp2040_swd_set_target_reset(%d)\n", asserted);
    probe_assert_reset(asserted);
}

void rp2040_prerun_board_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_board_config()\n");
}

void board_bootloader_init(void)
{
    cdc_debug_printf("----- board_bootloader_init()\n");
}

uint8_t rp2040_target_set_state(target_state_t state)
{
    cdc_debug_printf("----- rp2040_target_set_state(%d)\n", state);
    return swd_set_target_state_hw(state);
}

void rp2040_target_before_init_debug(void)
{
    cdc_debug_printf("----- rp2040_target_before_init_debug()\n");
    dp_initialize();
    core_select(0);
}

void rp2040_prerun_target_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_target_config()\n");
}

const board_info_t g_board_info = {
    .info_version = kBoardInfoVersion,
    .board_id = "0000",
    .daplink_url_name =   "HELP_FAQHTM",
    .daplink_drive_name = "BOOTLOADER",
    .daplink_target_url = "https://daplink.io",
    // .swd_set_target_reset = &rp2040_swd_set_target_reset,
    .prerun_board_config = &rp2040_prerun_board_config,
    .target_cfg = &rp2040_target_device,
};

static const target_family_descriptor_t g_rp2040_family = {
    .family_id = 0,
    .default_reset_type = kSoftwareReset,
    .soft_reset_type = SYSRESETREQ,
    .swd_set_target_reset = &rp2040_swd_set_target_reset,
    // .target_set_state = &rp2040_target_set_state,
    .target_before_init_debug = &rp2040_target_before_init_debug,
    .prerun_target_config = &rp2040_prerun_target_config,
};

const target_family_descriptor_t *g_target_family = &g_rp2040_family;
