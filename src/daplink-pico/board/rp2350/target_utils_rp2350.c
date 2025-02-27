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

#include <stdio.h>

#include "target_utils_rp2350.h"

#include "swd_host.h"
#include "raspberry/target_utils_raspberry.h"


// this is 'M' 'u', 2 (version)
#define RP2350_BOOTROM_MAGIC       0x02754d
#define RP2350_BOOTROM_MAGIC_ADDR  0x00000010



static uint8_t swd_read_word16(uint32_t addr, uint16_t *val)
/**
 * Read 16-bit word from target memory.
 */
{
    uint8_t v1, v2;

    if ( !swd_read_byte(addr+0, &v1))
        return 0;
    if ( !swd_read_byte(addr+1, &v2))
        return 0;

    *val = ((uint16_t)v2 << 8) + v1;
    return 1;
}   // swd_read_word16



uint32_t rp2350_target_find_rom_func(char ch1, char ch2)
/**
 * find a function in the bootrom, see RP2350 datasheet, chapter 2.8
 */
{
    uint16_t flags = 0x0004;   // RT_FLAG_FUNC_ARM_SEC
    uint16_t tag = (ch2 << 8) | ch1;

    // First read the bootrom magic value...
    uint32_t magic;

//    printf("a %c %c -> %d\n", ch1, ch2, tag);
    if ( !swd_read_word(RP2350_BOOTROM_MAGIC_ADDR, &magic))
        return 0;
//    printf("b 0x%08lx\n", magic);
    if ((magic & 0x00ffffff) != RP2350_BOOTROM_MAGIC)
        return 0;
//    printf("c\n");

    // Now find the start of the table...
    uint16_t v;
    uint32_t addr;
    if ( !swd_read_word16(RP2350_BOOTROM_MAGIC_ADDR+4, &v))
        return 0;
    addr = v;
//    printf("d\n");

    // Now try to find our function...
    uint16_t entry_tag;
    uint16_t entry_flags;

    for (;;) {
//        printf("- e: %ld\n", addr);
        if ( !swd_read_word16(addr, &entry_tag))
            return 0;
        if (entry_tag == 0)
            break;

        addr += 2;
        if ( !swd_read_word16(addr, &entry_flags))
            return 0;
        addr += 2;
//        printf("     %c %c, flags 0x%04x\n", entry_tag, entry_tag >> 8, entry_flags);

        if (entry_tag == tag  &&  (entry_flags & flags) != 0) {
            uint16_t entry_addr;

            while ((flags & 0x01) == 0) {
                if ((entry_flags & 1) != 0)
                    addr += 2;
                flags >>= 1;
                entry_flags >>= 1;
            }
            if ( !swd_read_word16(addr, &entry_addr))
                return 0;

//            printf("       found: 0x%04x\n", entry_addr);
            return (uint32_t)entry_addr;
        }
        else {
            while (entry_flags != 0) {
                uint16_t dummy;

                if ( !swd_read_word16(addr, &dummy))
                    return 0;
//                printf("       0x%04x\n", dummy);

                entry_flags &= (entry_flags - 1);
                addr += 2;
            }
        }
    }
    picoprobe_error("bootrom function not found\n");
    return 0;
}   // rp2350_target_find_rom_func



///
/// Call function on the target device at address \a addr.
/// Arguments are in \a args[] / \a argc, result of the called function (from r0) will be
/// put to \a *result (if != NULL).
///
/// \pre
///    - target MCU must be connected
///    - code must be already uploaded to target
///
bool rp2350_target_call_function(uint32_t addr, uint32_t args[], int argc, uint32_t breakpoint, uint32_t *result)
{
    bool interrupted = false;

    assert(argc <= 4);

    if ( !target_core_halt())
        return false;

    // Set the registers for the trampoline call...
    // function in r7, args in r0, r1, r2, and r3, end in lr?
    for (int i = 0;  i < argc;  ++i) {
        if ( !swd_write_core_register(i, args[i]))
            return false;
    }

    // Set LR
    if ( !swd_write_core_register(9, TARGET_RP2350_STACK + 0x10000))
        return false;

    // Set the stack pointer to something sensible... (MSP)
    if ( !swd_write_core_register(13, TARGET_RP2350_STACK))
        return false;

    if ( !swd_write_core_register(14, breakpoint | 1))
        return false;

    if ( !swd_write_core_register(15, addr | 1))
        return false;

    // Set xPSR for the thumb thingy...
    if ( !swd_write_core_register(16, (1 << 24)))
        return false;

    if ( !target_core_halt())
        return false;

    // start execution
//    picoprobe_info(".................... execute  0x%08lx() -> 0x%08lx\n", addr, breakpoint);
    if ( !target_core_unhalt_with_masked_ints())
        return false;

    // check status
    {
        uint32_t status;

        if (!swd_read_dp(DP_CTRL_STAT, &status)) {
            return false;
        }
        if (status & (STICKYERR | WDATAERR)) {
            return false;
        }
    }

    // Wait until core is halted (again)
    {
        const uint32_t timeout_us = 5000000;
        uint32_t start_us = time_us_32();

        while ( !target_core_is_halted()) {
            uint32_t dt_us = time_us_32() - start_us;

            if (dt_us > timeout_us) {
                target_core_halt();
                picoprobe_error("rp2350_target_call_function: execution timed out after %u ms\n",
                                (unsigned)(dt_us / 1000));
                interrupted = true;
                break;
            }
        }
        if ( !interrupted) {
            uint32_t dt_ms = (time_us_32() - start_us) / 1000;
            if (dt_ms > 100) {
                picoprobe_debug("rp2350_target_call_function: execution finished after %lu ms\n", dt_ms);
            }
        }
//        picoprobe_info("....................   time: %lu[us]\n", time_us_32() - start_us);
    }

    if (result != NULL  &&  !interrupted) {
        // fetch result of function (r0)
        if ( !swd_read_core_register(0, result)) {
            picoprobe_error("rp2350_target_call_function: cannot read core register 0\n");
            return false;
        }
//        picoprobe_info("....................   res:  %lu\n", *result);
    }

    {
        uint32_t r15;

        if ( !swd_read_core_register(15, &r15)) {
            picoprobe_error("rp2350_target_call_function: cannot read core register 15\n");
            return false;
        }

        if (r15 != (breakpoint & 0xfffffffe)) {
            picoprobe_error("rp2350_target_call_function: invoked target function did not run til end: 0x%0x != 0x%0x\n",
                            (unsigned)r15, (unsigned)breakpoint);
            return false;
        }
    }
    return true;
}   // rp2350_target_call_function
