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

#include "daplink_addr.h"
#include "target_board.h"



/**
* List of start and size for each size of flash sector
* The size will apply to all sectors between the listed address and the next address
* in the list.
* The last pair in the list will have sectors starting at that address and ending
* at address start + size.
*/
static const sector_info_t sectors_info[] = {
    {0x10000000, 4096},
};


static target_cfg_t target_device_rp2040 = {
    .version                    = kTargetConfigVersion,
    .sectors_info               = sectors_info,
    .sector_info_length         = (sizeof(sectors_info))/(sizeof(sector_info_t)),
    .flash_regions[0].start     = 0x10000000,
    .flash_regions[0].end       = 0x10000000 + KB(2048),
    .flash_regions[0].flags     = kRegionIsDefault,
    .ram_regions[0].start       = 0x20000000,
    .ram_regions[0].end         = 0x20000000 + KB(256),
};

const board_info_t g_board_info = {
    .info_version        = kBoardInfoVersion,
    .board_id            = "0000",                // see e.g. https://github.com/pyocd/pyOCD/blob/main/pyocd/board/board_ids.py and https://os.mbed.com/request-board-id
    .family_id           = CREATE_FAMILY_ID(127, 1),
    .daplink_url_name    = "-unknown-",
    .daplink_drive_name  = "-unknown-",
    .daplink_target_url  = "https://daplink.io",
    .target_cfg          = &target_device_rp2040,
};
