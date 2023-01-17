/**
 * @file    target.c
 * @brief   Target information for the STM32L151CC
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

#include "target_config.h"



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



// TODO this is not correct, taken from nRF52

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
    {0, 4096},
};


// target information
target_cfg_t target_device = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info_rp2040,
    .sector_info_length             = (sizeof(sectors_info_rp2040))/(sizeof(sectors_info_rp2040)),
    .flash_regions[0].start         = 0x10000000,
    .flash_regions[0].end           = 0x10000000 + MB(2),
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *) &flash_rp2040,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20000000 + KB(256),
    .erase_reset                    = 1,
    .target_vendor                  = "RaspberryPi",
    .target_part_number             = "RP2040",
};
