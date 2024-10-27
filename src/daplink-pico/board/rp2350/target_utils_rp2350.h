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

#define TARGET_RP2350_STACK           (TARGET_RP2350_RAM_START + 0x30800)


// pre: flash connected, post: generic XIP active
#define RP2350_FLASH_RANGE_ERASE(OFFS, CNT, BLKSIZE, CMD)       \
    do {                                                        \
        _flash_exit_xip();                                      \
        _flash_range_erase((OFFS), (CNT), (BLKSIZE), (CMD));    \
        _flash_flush_cache();                                   \
        _flash_enter_cmd_xip();                                 \
    } while (0)

// pre: flash connected, post: generic XIP active
#define RP2350_FLASH_RANGE_PROGRAM(ADDR, DATA, LEN)             \
    do {                                                        \
        _flash_exit_xip();                                      \
        _flash_range_program((ADDR), (DATA), (LEN));            \
        _flash_flush_cache();                                   \
        _flash_enter_cmd_xip();                                 \
    } while (0)

// post: flash connected && fast or generic XIP active
#define RP2350_FLASH_ENTER_CMD_XIP()                            \
    do {                                                        \
        _connect_internal_flash();                              \
        _flash_flush_cache();                                   \
        if (*((uint32_t *)TARGET_RP2350_BOOT2) == 0xffffffff) { \
            _flash_enter_cmd_xip();                             \
        }                                                       \
        else {                                                  \
            ((void (*)(void))TARGET_RP2350_BOOT2+1)();          \
        }                                                       \
    } while (0)


#define rom_hword_as_ptr(rom_address) (void *)(uintptr_t)(*(uint16_t *)rom_address)
#define fn(a, b)        (uint32_t)((b << 8) | a)
typedef void *(*rom_table_lookup_fn)(uint16_t *table, uint32_t code);


typedef void *(*rom_void_fn)(void);
typedef void *(*rom_flash_erase_fn)(uint32_t addr, size_t count, uint32_t block_size, uint8_t block_cmd);
typedef void *(*rom_flash_prog_fn)(uint32_t addr, const uint8_t *data, size_t count);


bool rp2350_target_call_function(uint32_t addr, uint32_t args[], int argc, uint32_t *result);


#ifdef __cplusplus
    }
#endif

#endif
