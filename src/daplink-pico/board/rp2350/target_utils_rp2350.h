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

#ifndef _TARGET_UTILS_RP2350_H
#define _TARGET_UTILS_RP2350_H


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
    extern "C" {
#endif

#define TARGET_RP2350_FLASH_START     0x10000000
#define TARGET_RP2350_FLASH_MAX_SIZE  0x10000000
#define TARGET_RP2350_RAM_START       0x20000000
#define TARGET_RP2350_RAM_SIZE        (512*1024)

#define TARGET_RP2350_STACK           (TARGET_RP2350_RAM_START + 0x20000) //TARGET_RP2350_RAM_SIZE - 32768)

typedef int   (*rp2350_rom_get_sys_info_fn)(uint32_t *out_buffer, uint32_t out_buffer_word_size, uint32_t flags);
typedef void  (*rp2350_rom_connect_internal_flash_fn)(void);

uint32_t rp2350_target_find_rom_func(char ch1, char ch2);
bool rp2350_target_call_function(uint32_t addr, uint32_t args[], int argc, uint32_t breakpoint, uint32_t *result);


#ifdef __cplusplus
    }
#endif

#endif
