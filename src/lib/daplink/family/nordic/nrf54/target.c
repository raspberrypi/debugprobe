/**
 * @file    target.c
 * @brief   Target information for the nRF54 Family
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2009-2021, Arm Limited, All Rights Reserved
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

#include "target_config.h"

// The file flash_blob.c must only be included in target.c
#include "flash_blob.c"

target_cfg_t target_device_nrf54l05 = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_nrf54,
    .sector_info_length             = (sizeof(sectors_info_nrf54))/(sizeof(sector_info_t)),
    .flash_regions[0].start         = 0,
    .flash_regions[0].end           = KB(500),
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *) &flash_nrf54,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(96),
    .erase_reset                    = 1,
    .target_vendor                  = "NordicSemiconductor",
    .target_part_number             = "nRF54L05",
};

target_cfg_t target_device_nrf54l10 = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_nrf54,
    .sector_info_length             = (sizeof(sectors_info_nrf54))/(sizeof(sector_info_t)),
    .flash_regions[0].start         = 0,
    .flash_regions[0].end           = KB(1012),
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *) &flash_nrf54,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(192),
    .erase_reset                    = 1,
    .target_vendor                  = "NordicSemiconductor",
    .target_part_number             = "nRF54L10",
};

target_cfg_t target_device_nrf54l15 = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_nrf54,
    .sector_info_length             = (sizeof(sectors_info_nrf54))/(sizeof(sector_info_t)),
    .flash_regions[0].start         = 0,
    .flash_regions[0].end           = KB(1524),
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *) &flash_nrf54,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(256),
    .erase_reset                    = 1,
    .target_vendor                  = "NordicSemiconductor",
    .target_part_number             = "nRF54L15",
};
