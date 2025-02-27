/**
 * @file    pico.c
 * @brief   board code for Pico
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
 *
 * Two important global variables:
 * - \a g_board_info contains information about the probe and target, e.g. how to perform
 *   probe initialization, send a HW reset to the target, etc
 * - \a g_target_family tells things like: what is the actual reset sequence for
 *   the target (family), how to set the state of the target (family), etc.
 *   This may differ really from target to target although all have "standard" DAPs
 *   included.  See RP2040 with dual cores and dormant sequence which does not
 *   like the JTAG2SWD sequence.  Others like the nRF52840 have other reset
 *   sequences.
 */

#include <stdio.h>
#include "boot/uf2.h"

#include "DAP_config.h"
#include "DAP.h"
#include "swd_host.h"

#include "target_family.h"
#include "target_board.h"
#include "raspberry/target_utils_raspberry.h"
#include "rp2040/program_flash_generic_rp2040.h"
#include "rp2350/program_flash_generic_rp2350.h"

#include "probe.h"
#include "minIni/minIni.h"

// include only here!
#include "raspberry/flash_blob.c"


// these are IDs for target identification, required registers to identify may/do differ
const uint32_t swd_id_rp2040    = (0x927) + (0x0002 << 12);    // taken from RP2040 SDK platform.c
const uint32_t swd_id_rp2350    = (0x927) + (0x0004 << 12);    // taken from RP2350 SDK platform.c
const uint32_t swd_id_nrf52832  = 0x00052832;                  // see FICR.INFO.PART
const uint32_t swd_id_nrf52833  = 0x00052833;
const uint32_t swd_id_nrf52840  = 0x00052840;

// IDs for UF2 identification, use the following command to obtain recent list:
// curl https://raw.githubusercontent.com/microsoft/uf2/master/utils/uf2families.json | jq -r '.[] | "\(.id)\t\(.description)"' | sort -k 2
const uint32_t uf2_id_nrf52          = 0x1b57745f;
const uint32_t uf2_id_nrf52833       = 0x621e937a;
const uint32_t uf2_id_nrf52840       = 0xada52840;
const uint32_t uf2_id_rp2040         = RP2040_FAMILY_ID;
const uint32_t uf2_id_rp2            = ABSOLUTE_FAMILY_ID;
const uint32_t uf2_id_rp2350_nonsec  = RP2350_ARM_NS_FAMILY_ID;    // Non-secure Arm image
const uint32_t uf2_id_rp2350_sec_rv  = RP2350_RISCV_FAMILY_ID;     // RISC-V image
const uint32_t uf2_id_rp2350_sec_arm = RP2350_ARM_S_FAMILY_ID;     // Secure Arm image

// IDs for board identification (but whatfor?)
#define board_id_nrf52832_dk      "1101"
#define board_id_nrf52833_dk      "1101"
#define board_id_nrf52840_dk      "1102"
#define board_id_rp2040_pico      "7f01"          // see TARGET_RP2040_FAMILY_ID
#define board_id_rp2350_pico2     "7f02"          // see TARGET_RP2350_FAMILY_ID

// here we can modify the otherwise constant board/target information
target_cfg_t target_device;
static char board_vendor[30];
static char board_name[30];



// target information for RP2040 (actually Pico), must be global
// because a special algo is used for flashing, corresponding fields below are empty.
target_cfg_t target_device_rp2040 = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_rp2040,
    .sector_info_length             = (sizeof(sectors_info_rp2040))/(sizeof(sector_info_t)),
    .flash_regions[0].start         = 0x10000000,
    .flash_regions[0].end           = 0x10000000,
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *)&flash_rp2040,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(256),
    .target_vendor                  = "RaspberryPi",
    .target_part_number             = "RP2040",
    .rt_family_id                   = TARGET_RP2040_FAMILY_ID,
    .rt_board_id                    = board_id_rp2040_pico,
    .rt_uf2_id[0]                   = uf2_id_rp2040,
    .rt_uf2_id[1]                   = 0,
    .rt_max_swd_khz                 = 25000,
    .rt_swd_khz                     = 10000,
};


// target information for RP2350 (actually Pico2), must be global
// because a special algo is used for flashing, corresponding fields below are empty.
target_cfg_t target_device_rp2350 = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_rp2350,
    .sector_info_length             = (sizeof(sectors_info_rp2350))/(sizeof(sector_info_t)),
    .flash_regions[0].start         = 0x10000000,
    .flash_regions[0].end           = 0x10000000,
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *)&flash_rp2350,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(512),
    .target_vendor                  = "RaspberryPi",
    .target_part_number             = "RP2350",
    .rt_family_id                   = TARGET_RP2350_FAMILY_ID,
    .rt_board_id                    = board_id_rp2350_pico2,
    .rt_uf2_id[0]                   = uf2_id_rp2350_sec_arm,
    .rt_uf2_id[1]                   = uf2_id_rp2350_nonsec,
    .rt_uf2_id[2]                   = uf2_id_rp2350_sec_rv,
    .rt_uf2_id[3]                   = uf2_id_rp2,
    .rt_uf2_id[4]                   = 0,
    .rt_max_swd_khz                 = 25000,
    .rt_swd_khz                     = 10000,
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
    .rt_uf2_id[0]                   = 0,                               // this also implies no write operation
    .rt_max_swd_khz                 = 10000,
    .rt_swd_khz                     = 2000,
};


// target information for SWD not connected
target_cfg_t target_device_disconnected = {
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
    .target_vendor                  = "Disconnected",
    .target_part_number             = "Disconnected",
    .rt_family_id                   = kStub_SWSysReset_FamilyID,
    .rt_board_id                    = NULL,                            // indicates not connected
    .rt_uf2_id[0]                   = 0,                               // this also implies no write operation
    .rt_max_swd_khz                 = 10000,
    .rt_swd_khz                     = 2000,
};



extern target_cfg_t target_device_nrf52;
extern target_cfg_t target_device_nrf52833;
extern target_cfg_t target_device_nrf52840;



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
 *
 * TODO create a manual configuration possibility via INI
 */
void pico_prerun_board_config(void)
{
    bool r;
    bool target_found = false;

    target_device_generic.ram_regions[0].start = ini_getl(MININI_SECTION, MININI_VAR_RSTART,
                                                          target_device_generic.ram_regions[0].start,
                                                          MININI_FILENAME);
    target_device_generic.ram_regions[0].end   = ini_getl(MININI_SECTION, MININI_VAR_REND,
                                                          target_device_generic.ram_regions[0].end,
                                                          MININI_FILENAME);

    target_device = target_device_generic;
    probe_set_swclk_freq_khz(target_device.rt_swd_khz, false);                            // slow down during target probing

    if ( !target_found) {
        // check for RP2040
        target_device = target_device_rp2040;
        search_family();
        if (target_set_state(ATTACH)) {
            uint32_t chip_id;

            r = swd_read_word(0x40000000, &chip_id);
            if (r  &&  (chip_id & 0x0fffffff) == swd_id_rp2040) {
                target_found = true;
                strcpy(board_vendor, "RaspberryPi");
                strcpy(board_name, "Pico");

                // get size of targets flash
                uint32_t size = target_rp2040_get_external_flash_size();
                if (size > 0) {
                    target_device.flash_regions[0].end = target_device.flash_regions[0].start + size;
                }
            }
        }
    }

    if ( !target_found) {
        // check for RP2350
        target_device = target_device_rp2350;
        search_family();
        if (target_set_state(ATTACH)) {
            uint32_t chip_id;

            r = swd_read_word(0x40000000, &chip_id);
            if (r  &&  (chip_id & 0x0fffffff) == swd_id_rp2350) {
                target_found = true;
                strcpy(board_vendor, "RaspberryPi");
                strcpy(board_name, "Pico2");

                // get size of targets flash
                uint32_t size = target_rp2350_get_external_flash_size();
                if (size > 0) {
                    target_device.flash_regions[0].end = target_device.flash_regions[0].start + size;
                }
            }
        }
    }

    if ( !target_found) {
        // check for nRF52832, nRF52833 or nRF52840
        // DK names taken from https://infocenter.nordicsemi.com/topic/ug_gsg_ses/UG/gsg/chips_and_sds.html
        target_device = target_device_nrf52840;
        target_device.rt_family_id   = kNordic_Nrf52_FamilyID;
        target_device.rt_board_id    = board_id_nrf52840_dk;
        target_device.rt_uf2_id[0]   = uf2_id_nrf52840;
        target_device.rt_uf2_id[1]   = 0;
        target_device.rt_max_swd_khz = 10000;
        target_device.rt_swd_khz     = 6000;
        target_device.target_part_number = "nRF52840";
        strcpy(board_vendor, "NordicSemiconductor");
        strcpy(board_name, "Generic nRF52840");                 // e.g. PCA10056

        search_family();
        if (target_set_state(ATTACH)) {
            uint32_t info_part;
            uint32_t info_ram;
            uint32_t info_flash;

            // reading flash/RAM size is Nordic special
            r = swd_read_word(0x10000100, &info_part)  &&  swd_read_word(0x1000010c, &info_ram)  &&  swd_read_word(0x10000110, &info_flash);
            if (r  &&  info_part == swd_id_nrf52832) {
                target_found = true;
                target_device = target_device_nrf52;
                target_device.rt_family_id   = kNordic_Nrf52_FamilyID;
                target_device.rt_board_id    = board_id_nrf52832_dk;
                target_device.rt_uf2_id[0]   = uf2_id_nrf52;
                target_device.rt_uf2_id[1]   = 0;
                target_device.rt_max_swd_khz = 10000;
                target_device.rt_swd_khz     = 6000;
                target_device.target_part_number = "nRF52832";
                strcpy(board_vendor, "NordicSemiconductor");
                strcpy(board_name, "Generic nRF52832");         // e.g. PCA10040
                target_device.flash_regions[0].end = target_device.flash_regions[0].start + 1024 * info_flash;
                target_device.ram_regions[0].end   = target_device.ram_regions[0].start + 1024 * info_ram;
            }
            else if (r  &&  info_part == swd_id_nrf52833) {
                target_found = true;
                target_device = target_device_nrf52833;
                target_device.rt_family_id   = kNordic_Nrf52_FamilyID;
                target_device.rt_board_id    = board_id_nrf52833_dk;
                target_device.rt_uf2_id[0]   = uf2_id_nrf52833;
                target_device.rt_uf2_id[1]   = 0;
                target_device.rt_max_swd_khz = 10000;
                target_device.rt_swd_khz     = 6000;
                target_device.target_part_number = "nRF52833";
                strcpy(board_vendor, "NordicSemiconductor");
                strcpy(board_name, "Generic nRF52833");         // e.g. PCA10100
                target_device.flash_regions[0].end = target_device.flash_regions[0].start + 1024 * info_flash;
                target_device.ram_regions[0].end   = target_device.ram_regions[0].start + 1024 * info_ram;
            }
            else if (r  &&  info_part == swd_id_nrf52840) {
                target_found = true;
                target_device.flash_regions[0].end = target_device.flash_regions[0].start + 1024 * info_flash;
                target_device.ram_regions[0].end   = target_device.ram_regions[0].start + 1024 * info_ram;
            }
        }
    }

    if ( !target_found) {
        target_device = target_device_generic;             // holds already all values
        search_family();
        if (target_set_state(ATTACH)) {
            // set generic device
            strcpy(board_vendor, "Generic");
            strcpy(board_name, "Generic");
        }
        else {
            // Disconnected!
            // Note that .rt_board_id is set to NULL to show the disconnect state.
            // This is actually a hack to provide other layers with some dummy g_board_info.target_cfg
            target_device = target_device_disconnected;    // holds already all values
            search_family();
            strcpy(board_vendor, "Disconnected");
            strcpy(board_name, "Disconnected");
        }
    }

    // set the SWCLK either to configured value or from the target structure
    {
        int32_t f_khz;

        f_khz = ini_getl(MININI_SECTION, MININI_VAR_FSWD, 0, MININI_FILENAME);
        if (f_khz < PROBE_MIN_FREQ_KHZ  ||  f_khz > target_device.rt_max_swd_khz) {
            f_khz = target_device.rt_swd_khz;
        }
        probe_set_swclk_freq_khz(f_khz, true);
    }

    target_set_state(RESET_RUN);
}   // pico_prerun_board_config



/**
 * This is the global variable holding information about probe and target.
 */
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
