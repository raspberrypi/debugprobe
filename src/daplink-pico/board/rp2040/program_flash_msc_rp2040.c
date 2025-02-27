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
 */

#include <stdio.h>
#include <stddef.h>

#include "picoprobe_config.h"

#include "raspberry/target_utils_raspberry.h"
#include "program_flash_msc_rp2040.h"
#include "target_utils_rp2040.h"
#include "swd_host.h"


// -----------------------------------------------------------------------------------
// THIS CODE IS DESIGNED TO RUN ON THE TARGET AND WILL BE COPIED OVER
// (hence it has it's own section)
// All constants here are used on the target!
// -----------------------------------------------------------------------------------
//
// Memory Map on target for programming:
//
// 0x2000 0000      (max) 64K incoming data buffer
// 0x2001 0000      start of code
// 0x2002 0000      stage2 bootloader copy (256 bytes)
// 0x2003 0800      top of stack
//


extern char __start_for_target_msc_rp2040[];
extern char __stop_for_target_msc_rp2040[];

#if defined(__clang__)
    #define FOR_TARGET_RP2040_CODE        __attribute__((noinline, section("for_target_msc_rp2040"), target("arch=cortex-m0"), optnone))
#else
    #define FOR_TARGET_RP2040_CODE        __attribute__((noinline, section("for_target_msc_rp2040"), target("arch=armv6-m"), optimize("-O0")))
#endif

#define TARGET_RP2040_CODE            (TARGET_RP2040_RAM_START + 0x10000)
#define TARGET_RP2040_FLASH_BLOCK     ((uint32_t)rp2040_flash_block - (uint32_t)__start_for_target_msc_rp2040 + TARGET_RP2040_CODE)
#define TARGET_RP2040_BOOT2           (TARGET_RP2040_RAM_START + 0x20000)
#define TARGET_RP2040_BOOT2_SIZE      256
#define TARGET_RP2040_ERASE_MAP       (TARGET_RP2040_BOOT2 + TARGET_RP2040_BOOT2_SIZE)
#define TARGET_RP2040_ERASE_MAP_SIZE  256
#define TARGET_RP2040_DATA            (TARGET_RP2040_RAM_START + 0x00000)


///
/// Code should be checked via "arm-none-eabi-objdump -S build/picoprobe.elf"
/// \param addr     \a TARGET_RP2040_FLASH_START....  A 64KByte block will be erased if \a addr is on a 64K boundary
/// \param src      pointer to source data
/// \param length   length of data block (256, 512, 1024, 2048 are legal (but unchecked)), packet may not overflow
///                 into next 64K block
/// \return         bit0=1 -> page erased, bit1=1->data flashed,
///                 bit31=1->data verify failed, bit30=1->illegal address
///
/// \note
///    This version is not optimized and depends on order of incoming sectors
///
FOR_TARGET_RP2040_CODE uint32_t rp2040_flash_block(uint32_t addr, uint32_t *src, int length)
{
    // Fill in the rom functions...
    rp2040_rom_table_lookup_fn rom_table_lookup = (rp2040_rom_table_lookup_fn)rom_hword_as_ptr(0x18);
    uint16_t            *function_table = (uint16_t *)rom_hword_as_ptr(0x14);

    rp2xxx_rom_void_fn         _connect_internal_flash = rom_table_lookup(function_table, ROM_FN('I', 'F'));
    rp2xxx_rom_void_fn         _flash_exit_xip         = rom_table_lookup(function_table, ROM_FN('E', 'X'));
    rp2xxx_rom_flash_erase_fn  _flash_range_erase      = rom_table_lookup(function_table, ROM_FN('R', 'E'));
    rp2xxx_rom_flash_prog_fn   _flash_range_program    = rom_table_lookup(function_table, ROM_FN('R', 'P'));
    rp2xxx_rom_void_fn         _flash_flush_cache      = rom_table_lookup(function_table, ROM_FN('F', 'C'));
    rp2xxx_rom_void_fn         _flash_enter_cmd_xip    = rom_table_lookup(function_table, ROM_FN('C', 'X'));

    const uint32_t erase_block_size = 0x10000;               // 64K - if this is changed, then some logic below has to be changed as well
    uint32_t offset = addr - TARGET_RP2040_FLASH_START;      // this is actually the physical flash address
    uint32_t erase_map_offset = (offset >> 16);              // 64K per map entry
    uint8_t *erase_map_entry = ((uint8_t *)TARGET_RP2040_ERASE_MAP) + erase_map_offset;
    uint32_t res = 0;

    if (offset > TARGET_RP2040_FLASH_MAX_SIZE)
        return 0x40000000;

    // We want to make sure the flash is connected so that we can check its current content
    RP2040_FLASH_ENTER_CMD_XIP();

    if (*erase_map_entry == 0) {
        //
        // erase 64K page if on 64K boundary
        //
        bool already_erased = true;
        uint32_t *a_64k = (uint32_t *)addr;

        for (int i = 0; i < erase_block_size / sizeof(uint32_t); ++i) {
            if (a_64k[i] != 0xffffffff) {
                already_erased = false;
                break;
            }
        }

        if ( !already_erased) {
            RP2xxx_FLASH_RANGE_ERASE(offset, erase_block_size, erase_block_size, 0xD8);     // 64K erase
            res |= 0x0001;
        }
        *erase_map_entry = 0xff;
    }

    if (src != NULL  &&  length != 0) {
        RP2xxx_FLASH_RANGE_PROGRAM(offset, (uint8_t *)src, length);
        res |= 0x0002;
    }

    RP2040_FLASH_ENTER_CMD_XIP();

    // does data match?
    {
        for (int i = 0;  i < length / 4;  ++i) {
            if (((uint32_t *)addr)[i] != src[i]) {
                res |= 0x80000000;
                break;
            }
        }
    }

    return res;
}   // rp2040_flash_block



static bool rp2040_target_copy_flash_code(void)
{
#if TARGET_RP2040_CONSIDER_BOOT2
    // BOOT2 code from a RPi Pico
    // dump sometime ago (2025-02-26)
    // this is required for connect_internal_flash()
    // different external flashs might require different BOOT2 area
    static const uint8_t boot2_rp2040[] = {
        0x00, 0xb5, 0x32, 0x4b, 0x21, 0x20, 0x58, 0x60, 0x98, 0x68, 0x02, 0x21, 0x88, 0x43, 0x98, 0x60,
        0xd8, 0x60, 0x18, 0x61, 0x58, 0x61, 0x2e, 0x4b, 0x00, 0x21, 0x99, 0x60, 0x02, 0x21, 0x59, 0x61,
        0x01, 0x21, 0xf0, 0x22, 0x99, 0x50, 0x2b, 0x49, 0x19, 0x60, 0x01, 0x21, 0x99, 0x60, 0x35, 0x20,
        0x00, 0xf0, 0x44, 0xf8, 0x02, 0x22, 0x90, 0x42, 0x14, 0xd0, 0x06, 0x21, 0x19, 0x66, 0x00, 0xf0,
        0x34, 0xf8, 0x19, 0x6e, 0x01, 0x21, 0x19, 0x66, 0x00, 0x20, 0x18, 0x66, 0x1a, 0x66, 0x00, 0xf0,
        0x2c, 0xf8, 0x19, 0x6e, 0x19, 0x6e, 0x19, 0x6e, 0x05, 0x20, 0x00, 0xf0, 0x2f, 0xf8, 0x01, 0x21,
        0x08, 0x42, 0xf9, 0xd1, 0x00, 0x21, 0x99, 0x60, 0x1b, 0x49, 0x19, 0x60, 0x00, 0x21, 0x59, 0x60,
        0x1a, 0x49, 0x1b, 0x48, 0x01, 0x60, 0x01, 0x21, 0x99, 0x60, 0xeb, 0x21, 0x19, 0x66, 0xa0, 0x21,
        0x19, 0x66, 0x00, 0xf0, 0x12, 0xf8, 0x00, 0x21, 0x99, 0x60, 0x16, 0x49, 0x14, 0x48, 0x01, 0x60,
        0x01, 0x21, 0x99, 0x60, 0x01, 0xbc, 0x00, 0x28, 0x00, 0xd0, 0x00, 0x47, 0x12, 0x48, 0x13, 0x49,
        0x08, 0x60, 0x03, 0xc8, 0x80, 0xf3, 0x08, 0x88, 0x08, 0x47, 0x03, 0xb5, 0x99, 0x6a, 0x04, 0x20,
        0x01, 0x42, 0xfb, 0xd0, 0x01, 0x20, 0x01, 0x42, 0xf8, 0xd1, 0x03, 0xbd, 0x02, 0xb5, 0x18, 0x66,
        0x18, 0x66, 0xff, 0xf7, 0xf2, 0xff, 0x18, 0x6e, 0x18, 0x6e, 0x02, 0xbd, 0x00, 0x00, 0x02, 0x40,
        0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x07, 0x00, 0x00, 0x03, 0x5f, 0x00, 0x21, 0x22, 0x00, 0x00,
        0xf4, 0x00, 0x00, 0x18, 0x22, 0x20, 0x00, 0xa0, 0x00, 0x01, 0x00, 0x10, 0x08, 0xed, 0x00, 0xe0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0xb2, 0x4e, 0x7a
    };
#endif
    const uint32_t code_len = (__stop_for_target_msc_rp2040 - __start_for_target_msc_rp2040);

    picoprobe_info("FLASH: Copying custom flash code to 0x%08x (%d bytes)\n", TARGET_RP2040_CODE, (int)code_len);
    if ( !swd_write_memory(TARGET_RP2040_CODE, (uint8_t *)__start_for_target_msc_rp2040, code_len))
        return false;

    // clear TARGET_RP2040_ERASE_MAP
    for (uint32_t i = 0;  i < TARGET_RP2040_ERASE_MAP_SIZE;  i += sizeof(uint32_t)) {
        if ( !swd_write_word(TARGET_RP2040_ERASE_MAP + i, 0)) {
            return false;
        }
    }

#if TARGET_RP2040_CONSIDER_BOOT2
    picoprobe_info("FLASH: Copying BOOT2 code to 0x%08x (%d bytes)\n", TARGET_RP2040_BOOT2, TARGET_RP2040_BOOT2_SIZE);
    if ( !swd_write_memory(TARGET_RP2040_BOOT2, (uint8_t *)boot2_rp2040, sizeof(boot2_rp2040)))
        return false;
#endif

    return true;
}   // rp2040_target_copy_flash_code



bool target_rp2040_msc_copy_flash_code(void)
{
    bool ok;

    ok = target_set_state(RESET_PROGRAM);
    if (ok) {
        ok = rp2040_target_copy_flash_code();
    }
    return ok;
}   // target_rp2040_msc_copy_flash_code



uint32_t target_rp2040_msc_flash(uint32_t addr, const uint8_t *data, uint32_t size)
{
    uint32_t arg[3];
    uint32_t res = 0;

    arg[0] = addr;
    arg[1] = TARGET_RP2040_DATA;
    arg[2] = size;

//            printf("   zzz  0x%lx, 0x%lx, 0x%lx, %ld\n", TARGET_RP2040_FLASH_BLOCK, arg[0], arg[1], arg[2]);

    if (swd_write_memory(TARGET_RP2040_DATA, (uint8_t *)data, size)) {
        rp2040_target_call_function(TARGET_RP2040_FLASH_BLOCK, arg, sizeof(arg) / sizeof(arg[0]), &res);
        if (res & 0xf0000000) {
            picoprobe_error("target_rp2040_msc_flash: target operation returned 0x%x\n", (unsigned)res);
        }
    }
    else {
        picoprobe_error("target_rp2040_msc_flash: failed to write to 0x%x/%d\n", (unsigned)addr, (unsigned)size);
    }
    return res;
}   // target_rp2040_msc_flash
