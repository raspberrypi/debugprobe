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

/*
 * Code/ideas are partially taken from pico_probe.  Other parts from DAPLink.
 *
 * Note that handling of the rescue DP has been dropped (no idea how to test this).
 */

#include "DAP_config.h"
#include "DAP.h"
#include "swd_host.h"

#include "target_family.h"
#include "target_board.h"

#include "probe.h"



target_cfg_t target_device;
static char board_vendor[30];
static char board_name[30];



// TODO this is not correct, taken from nRF52

static const uint32_t RP2040_FLM[] = {
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040, 0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
    0x47702000, 0x47702000, 0x4c2bb570, 0x60202002, 0x20014929, 0x60083108, 0x68284d28, 0xd00207c0,
    0x60202000, 0xf000bd70, 0xe7f6f833, 0x4c22b570, 0x60212102, 0x2f10f1b0, 0x491fd303, 0x31102001,
    0x491de001, 0x60081d09, 0xf0004d1c, 0x6828f821, 0xd0fa07c0, 0x60202000, 0xe92dbd70, 0xf8df41f0,
    0x088e8058, 0x46142101, 0xf8c84605, 0x4f131000, 0xc501cc01, 0x07c06838, 0x1e76d007, 0x2100d1f8,
    0x1000f8c8, 0xe8bd4608, 0xf00081f0, 0xe7f1f801, 0x6800480b, 0x00fff010, 0x490ad00c, 0x29006809,
    0x4908d008, 0x31fc4a08, 0xd00007c3, 0x1d09600a, 0xd1f90840, 0x00004770, 0x4001e504, 0x4001e400,
    0x40010404, 0x40010504, 0x6e524635, 0x00000000,
};



// TODO this is not correct, taken from nRF52 (above)

static const program_target_t flash_rp2040 = {
    .init              = 0x20000021,
    .uninit            = 0x20000025,
    .erase_chip        = 0x20000029,
    .erase_sector      = 0x2000004D,
    .program_page      = 0x2000007B,
    .verify            = 0x0,
    {
        .breakpoint    = 0x20000001,
        .static_base   = 0x20000020 + 0x00000150,
        .stack_pointer = 0x20001000
    },
    .program_buffer    = 0x20000200,
    .algo_start        = 0x20000000,
    .algo_size         = 0x00000150,
    .algo_blob         = RP2040_FLM,
    .program_buffer_size = 512 // should be USBD_MSC_BlockSize
};



// TODO this is not correct, taken from nRF52

static const sector_info_t sectors_info_rp2040[] = {     // actually the external QSPI flash
    {0x10000000, 4096},
};



// target information for RP2040 (actually Pico), must be global
target_cfg_t target_device_rp2040 = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_rp2040,
    .sector_info_length             = (sizeof(sectors_info_rp2040))/(sizeof(sectors_info_rp2040)),
    .flash_regions[0].start         = 0x10000000,
    .flash_regions[0].end           = 0x10000000 + MB(2),
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *)&flash_rp2040,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(256),
    .erase_reset                    = 1,
    .target_vendor                  = "RaspberryPi",
    .target_part_number             = "RP2040",
    .rt_family_id                   = CREATE_FAMILY_ID(127, 1),         // must fit g_raspberry_rp2040_family.family_id
    .rt_board_id                    = "7f01",                           // TODO whatfor?
};


// target information for a generic device which allows at least RTT (if connected)
target_cfg_t target_device_generic = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = NULL,
    .sector_info_length             = 0,
    .flash_regions[0].start         = 0x00000000,
    .flash_regions[0].end           = 0x00000000 + MB(1),
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = NULL,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(256),
    .erase_reset                    = 1,
    .target_vendor                  = "Generic",
    .target_part_number             = "cortex_m",
    .rt_family_id                   = kStub_SWSysReset_FamilyID,
    .rt_board_id                    = "ffff",
};



extern target_cfg_t target_device_nrf52;
extern target_cfg_t target_device_nrf52833;
extern target_cfg_t target_device_nrf52840;

const char *board_id_nrf52832_dk = "1101";    // TODO what for?
const char *board_id_nrf52833_dk = "1101";
const char *board_id_nrf52840_dk = "1102";

const uint32_t id_rp2040   = (0x927) + (0x0002 << 12);    // taken from RP2040 SDK platform.c
const uint32_t id_nrf52832 = 0x00052832;
const uint32_t id_nrf52833 = 0x00052833;
const uint32_t id_nrf52840 = 0x00052840;



static void search_family(void)
{
    // force search of family
    g_target_family = NULL;

    // search family
    init_family();
}   // search_family



/**
 * Search the correct board / target / family.
 * Currently nRF52840 and RP2040 are auto detected.
 *
 * Global outputs are \a g_board_info, \a g_target_family.  These are the only variables that should be (read) accessed
 * externally.
 *
 * \note
 *    I'm not sure if the usage of board_vendor/name is correct here.
 */
void pico_prerun_board_config(void)
{
    bool r;
    uint32_t id = 0;

    probe_set_swclk_freq(1500);                            // slow down during target probing

    if (id == 0) {
        // check for RP2040
        target_device = target_device_rp2040;
        search_family();
        if (target_set_state(ATTACH)) {
            uint32_t chip_id;

            r = swd_read_word(0x40000000, &chip_id);
            if (r  &&  (chip_id & 0x0fffffff) == id_rp2040) {
                id = id_rp2040;
                strcpy(board_vendor, "RaspberryPi");
                strcpy(board_name, "Pico");
                probe_set_swclk_freq(15000);
            }
        }
    }

    if (id == 0) {
        // check for nRF52832, nRF52833 or nRF52840
        // DK names taken from https://infocenter.nordicsemi.com/topic/ug_gsg_ses/UG/gsg/chips_and_sds.html
        target_device = target_device_nrf52840;
        target_device.rt_family_id = kNordic_Nrf52_FamilyID;
        target_device.rt_board_id = board_id_nrf52840_dk;
        target_device.target_part_number = "nRF52840";
        search_family();
        if (target_set_state(ATTACH)) {
            uint32_t info_part;
            uint32_t info_ram;
            uint32_t info_flash;

            r = swd_read_word(0x10000100, &info_part)  &&  swd_read_word(0x1000010c, &info_ram)  &&  swd_read_word(0x10000110, &info_flash);
            if (r  &&  info_part == id_nrf52832) {
                id = id_nrf52832;
                target_device = target_device_nrf52;
                target_device.rt_family_id = kNordic_Nrf52_FamilyID;
                target_device.rt_board_id = board_id_nrf52832_dk;
                target_device.target_part_number = "nRF52832";
                target_device.flash_regions[0].end = target_device.flash_regions[0].start + 1024 * info_flash;
                target_device.ram_regions[0].end   = target_device.ram_regions[0].start + 1024 * info_ram;
                strcpy(board_vendor, "NordicSemiconductor");
                strcpy(board_name, "PCA10040");
                probe_set_swclk_freq(8000);
            }
            else if (r  &&  info_part == id_nrf52833) {
                id = id_nrf52833;
                target_device = target_device_nrf52833;
                target_device.rt_family_id = kNordic_Nrf52_FamilyID;
                target_device.rt_board_id = board_id_nrf52833_dk;
                target_device.target_part_number = "nRF52833";
                target_device.flash_regions[0].end = target_device.flash_regions[0].start + 1024 * info_flash;
                target_device.ram_regions[0].end   = target_device.ram_regions[0].start + 1024 * info_ram;
                strcpy(board_vendor, "NordicSemiconductor");
                strcpy(board_name, "PCA10100");
                probe_set_swclk_freq(8000);
            }
            else if (r  &&  info_part == id_nrf52840) {
                id = id_nrf52840;
                target_device.flash_regions[0].end = target_device.flash_regions[0].start + 1024 * info_flash;
                target_device.ram_regions[0].end   = target_device.ram_regions[0].start + 1024 * info_ram;
                strcpy(board_vendor, "NordicSemiconductor");
                strcpy(board_name, "PCA10056");
                probe_set_swclk_freq(8000);
            }
        }
    }

    if (id == 0) {
        // set generic device
        target_device = target_device_generic;
        search_family();
        strcpy(board_vendor, "Generic");
        strcpy(board_name, "Generic");
    }
}   // pico_prerun_board_config



const board_info_t g_board_info = {
    .info_version        = kBoardInfoVersion,
    .board_id            = "0000",                // see e.g. https://github.com/pyocd/pyOCD/blob/main/pyocd/board/board_ids.py and https://os.mbed.com/request-board-id
    .daplink_url_name    = "-unknown-",
    .daplink_drive_name  = "-unknown-",
    .daplink_target_url  = "https://daplink.io",
    .target_cfg          = &target_device,
    .board_vendor        = board_vendor,
    .board_name          = board_name,
    .prerun_board_config = pico_prerun_board_config,
};
