/**
 * @file    flash_blob.c
 * @brief   Flash algorithm for the nRF54 family
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

#include "flash_blob.h"

// TODO unverified

// Taken from https://github.com/pyocd/pyOCD/blob/main/pyocd/target/builtin/target_nRF54L15.py
static const uint32_t nRF54_flash_algo[] = {
        0xE00ABE00,
        0xf8d24a02, 0x2b013400, 0x4770d1fb, 0x5004b000, 0x47702000, 0x47702000, 0x49072001, 0xf8c1b508,
        0xf7ff0500, 0xf8c1ffed, 0x20000540, 0xffe8f7ff, 0x0500f8c1, 0xbf00bd08, 0x5004b000, 0x2301b508,
        0xf8c14906, 0xf7ff3500, 0xf04fffdb, 0x600333ff, 0xf7ff2000, 0xf8c1ffd5, 0xbd080500, 0x5004b000,
        0x2301b538, 0x4d0c4614, 0x0103f021, 0x3500f8c5, 0xffc6f7ff, 0x44214622, 0x42911b00, 0x2000d105,
        0xffbef7ff, 0x0500f8c5, 0x4613bd38, 0x4b04f853, 0x461a5014, 0xbf00e7f1, 0x5004b000, 0x00000000
};

/**
* List of start and size for each size of flash sector
* The size will apply to all sectors between the listed address and the next address
* in the list.
* The last pair in the list will have sectors starting at that address and ending
* at address start + size.
*/
static const sector_info_t sectors_info_nrf54[] = {
    {0, 4},
};

static const program_target_t flash_nrf54 = {
    .init = 0x20000015,
    .uninit = 0x20000019,
    .erase_chip = 0x2000001d,
    .erase_sector = 0x20000041,
    .program_page = 0x20000065,
    .verify = 0x0,
    {
        .breakpoint = 0x20000001,
        .static_base = 0x20000000 + 0x00000004 + 0x000000a0,
        .stack_pointer = 0x20000300
    },
    .program_buffer = 0x20000000 + 0x00000A00,
    .algo_start = 0x20000000,
    .algo_size = sizeof(nRF54_flash_algo),
    .algo_blob = nRF54_flash_algo,
    .program_buffer_size = 512 // should be USBD_MSC_BlockSize
};
