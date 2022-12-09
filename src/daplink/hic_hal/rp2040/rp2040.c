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
#include "target_board.h"
#include "target_family.h"

#include "cmsis_os2.h"



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
target_cfg_t target_device = {
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



static void rp2040_swd_set_target_reset(uint8_t asserted)
{
    cdc_debug_printf("----- rp2040_swd_set_target_reset(%d)\n", asserted);
}

static void rp2040_prerun_board_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_board_config()\n");
}

void board_bootloader_init(void)
{
    cdc_debug_printf("----- board_bootloader_init()\n");
}



const board_info_t g_board_info = {
    .info_version = kBoardInfoVersion,
    .board_id = "0000",
    .daplink_url_name =   "HELP_FAQHTM",
    .daplink_drive_name = "BOOTLOADER",
    .daplink_target_url = "https://daplink.io",
    .swd_set_target_reset = &rp2040_swd_set_target_reset,
    .prerun_board_config = &rp2040_prerun_board_config,
    .target_cfg = &target_device,
};

static const target_family_descriptor_t g_rp2040_family = {
    .family_id = 0,
    .default_reset_type = kSoftwareReset,
    .swd_set_target_reset = &rp2040_swd_set_target_reset,
};

const target_family_descriptor_t *g_target_family = &g_rp2040_family;
