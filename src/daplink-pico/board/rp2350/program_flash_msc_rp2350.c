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
#include "program_flash_msc_rp2350.h"
#include "target_utils_rp2350.h"
#include "rp2040/target_utils_rp2040.h"

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


extern char __start_for_target_msc_rp2350[];
extern char __stop_for_target_msc_rp2350[];

// Attributes for RP2350 target code - DO NOT CHANGE THIS (but doesn't matter if code is compiled for cortex-m0 or m33, executes both on target)
// Note that there is also compile option setup in CMakeLists.txt
#define FOR_TARGET_RP2350_CODE        __attribute__((noinline, section("for_target_msc_rp2350")))

#define TARGET_RP2350_CODE            (TARGET_RP2350_RAM_START + 0x10000)
#define TARGET_RP2350_FLASH_BLOCK     ((uint32_t)rp2350_flash_block    - (uint32_t)__start_for_target_msc_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_BREAKPOINT      ((uint32_t)rp2350_msc_breakpoint - (uint32_t)__start_for_target_msc_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_RCP_INIT        ((uint32_t)rp2350_msc_rcp_init   - (uint32_t)__start_for_target_msc_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_ERASE_MAP       (TARGET_RP2350_RAM_START + 0x20000)
#define TARGET_RP2350_ERASE_MAP_SIZE  256
#define TARGET_RP2350_DATA            (TARGET_RP2350_RAM_START + 0x00000)



FOR_TARGET_RP2350_CODE __attribute__((naked)) void rp2350_msc_rcp_init(void)
/**
 * Just enable the RCP which is fine if it already was (we assume no other
 * co-processors are enabled at this point to save space)
 *
 * \note
 *    stolen from https://github.com/raspberrypi/openocd/blob/sdk-2.0.0/src/flash/nor/rp2040.c
 */
{
    __asm volatile(".byte 0x06, 0x48            "); // ldr r0, = PPB_BASE + M33_CPACR_OFFSET
    __asm volatile(".byte 0x5f, 0xf4, 0x40, 0x41"); // movs r1, #M33_CPACR_CP7_BITS
    __asm volatile(".byte 0x01, 0x60            "); // str r1, [r0]
                                                     // Only initialize canary seeds if they haven't been (as to do so twice is a fault)
    __asm volatile(".byte 0x30, 0xee, 0x10, 0xf7"); // mrc p7, #1, r15, c0, c0, #0
    __asm volatile(".byte 0x04, 0xd4            "); // bmi 1f
                                                     // Todo should we use something random here and pass it into the algorithm?
    __asm volatile(".byte 0x40, 0xec, 0x80, 0x07"); // mcrr p7, #8, r0, r0, c0
    __asm volatile(".byte 0x40, 0xec, 0x81, 0x07"); // mcrr p7, #8, r0, r0, c1
                                                     // Let other core know
    __asm volatile(".byte 0x40, 0xbf            "); // sev
                                                     // 1:
    __asm volatile(".byte 0x00, 0xbe            "); // bkpt (end of algorithm)
    __asm volatile(".byte 0x00, 0x00            "); // pad
    __asm volatile(".byte 0x88, 0xed, 0x00, 0xe0"); // PPB_BASE + M33_CPACR_OFFSET
}   // rp2350_msc_rcp_init



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
FOR_TARGET_RP2350_CODE uint32_t rp2350_flash_block(uint32_t addr, uint32_t *src, int length)
{
    const uint32_t BOOTROM_TABLE_LOOKUP_OFFSET  = 0x16;
    const uint16_t RT_FLAG_FUNC_ARM_SEC         = 0x0004;
    rp2350_rom_table_lookup_fn rom_table_lookup = (rp2350_rom_table_lookup_fn) (uintptr_t)(*(uint16_t*) (BOOTROM_TABLE_LOOKUP_OFFSET));

    rp2xxx_rom_void_fn         _connect_internal_flash = rom_table_lookup(ROM_FN('I', 'F'), RT_FLAG_FUNC_ARM_SEC);
    rp2xxx_rom_void_fn         _flash_exit_xip         = rom_table_lookup(ROM_FN('E', 'X'), RT_FLAG_FUNC_ARM_SEC);
    rp2xxx_rom_flash_erase_fn  _flash_range_erase      = rom_table_lookup(ROM_FN('R', 'E'), RT_FLAG_FUNC_ARM_SEC);
    rp2xxx_rom_flash_prog_fn   _flash_range_program    = rom_table_lookup(ROM_FN('R', 'P'), RT_FLAG_FUNC_ARM_SEC);
    rp2xxx_rom_void_fn         _flash_flush_cache      = rom_table_lookup(ROM_FN('F', 'C'), RT_FLAG_FUNC_ARM_SEC);
    rp2xxx_rom_void_fn         _flash_enter_cmd_xip    = rom_table_lookup(ROM_FN('C', 'X'), RT_FLAG_FUNC_ARM_SEC);

    const uint32_t erase_block_size = 0x10000;               // 64K - if this is changed, then some logic below has to be changed as well
    uint32_t offset = addr - TARGET_RP2350_FLASH_START;      // this is actually the physical flash address
    uint32_t erase_map_offset = (offset >> 16);              // 64K per map entry
    uint8_t *erase_map_entry = ((uint8_t *)TARGET_RP2350_ERASE_MAP) + erase_map_offset;
    uint32_t res = 0;

    if (offset > TARGET_RP2350_FLASH_MAX_SIZE)
        return 0x40000000;

    // We want to make sure the flash is connected so that we can check its current content
    RP2350_FLASH_ENTER_CMD_XIP();

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

    RP2350_FLASH_ENTER_CMD_XIP();

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
}   // rp2350_flash_block



FOR_TARGET_RP2350_CODE __attribute__((naked)) void rp2350_msc_breakpoint(void)
{
    __asm volatile ("bkpt 0");
}   // rp2350_msc_breakpoint



static bool rp2350_target_copy_flash_code(void)
{
    const uint32_t code_len = (__stop_for_target_msc_rp2350 - __start_for_target_msc_rp2350);

    picoprobe_info("FLASH: Copying custom flash code to 0x%08x (%d bytes)\n", TARGET_RP2350_CODE, (int)code_len);
    if ( !swd_write_memory(TARGET_RP2350_CODE, (uint8_t *)__start_for_target_msc_rp2350, code_len))
        return false;

    // clear TARGET_RP2350_ERASE_MAP
    for (uint32_t i = 0;  i < TARGET_RP2350_ERASE_MAP_SIZE;  i += sizeof(uint32_t)) {
        if ( !swd_write_word(TARGET_RP2350_ERASE_MAP + i, 0)) {
            return false;
        }
    }
    return true;
}   // rp2350_target_copy_flash_code



bool target_rp2350_msc_copy_flash_code(void)
{
    bool ok;

    ok = target_set_state(RESET_PROGRAM);
    if (ok) {
        ok = rp2350_target_copy_flash_code();
        rp2350_target_call_function(TARGET_RP2350_RCP_INIT, NULL, 0, TARGET_RP2350_RCP_INIT+24, NULL);
    }
    return ok;
}   // target_rp2350_msc_copy_flash_code



uint32_t target_rp2350_msc_flash(uint32_t addr, const uint8_t *data, uint32_t size)
{
    uint32_t arg[3];
    uint32_t res = 0;

    arg[0] = addr;
    arg[1] = TARGET_RP2350_DATA;
    arg[2] = size;

//            printf("   zzz  0x%lx, 0x%lx, 0x%lx, %ld\n", TARGET_RP2350_FLASH_BLOCK, arg[0], arg[1], arg[2]);

    if (swd_write_memory(TARGET_RP2350_DATA, (uint8_t *)data, size)) {
        rp2350_target_call_function(TARGET_RP2350_FLASH_BLOCK, arg, sizeof(arg) / sizeof(arg[0]), TARGET_RP2350_BREAKPOINT, &res);
        if (res & 0xf0000000) {
            picoprobe_error("target_rp2350_msc_flash: target operation returned 0x%x\n", (unsigned)res);
        }
    }
    else {
        picoprobe_error("target_rp2350_msc_flash: failed to write to 0x%x/%d\n", (unsigned)addr, (unsigned)size);
    }
    return res;
}   // target_rp2350_msc_flash
