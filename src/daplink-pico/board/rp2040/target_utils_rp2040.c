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

#include "target_utils_rp2040.h"

#include "swd_host.h"
#include "raspberry/target_utils_raspberry.h"



// Read 16-bit word from target memory.
static uint8_t swd_read_word16(uint32_t addr, uint16_t *val)
{
    uint8_t v1, v2;

    if ( !swd_read_byte(addr+0, &v1))
        return 0;
    if ( !swd_read_byte(addr+1, &v2))
        return 0;

    *val = ((uint16_t)v2 << 8) + v1;
    return 1;
}   // swd_read_word16



// this is 'M' 'u', 1 (version)
#define RP2040_BOOTROM_MAGIC 0x01754d
#define RP2040_BOOTROM_MAGIC_ADDR 0x00000010


//
// find a function in the bootrom, see RP2040 datasheet, chapter 2.8
//
static uint32_t rp2040_target_find_rom_func(char ch1, char ch2)
{
    uint16_t tag = (ch2 << 8) | ch1;

    // First read the bootrom magic value...
    uint32_t magic;

    if ( !swd_read_word(RP2040_BOOTROM_MAGIC_ADDR, &magic))
        return 0;
    if ((magic & 0x00ffffff) != RP2040_BOOTROM_MAGIC)
        return 0;

    // Now find the start of the table...
    uint16_t v;
    uint32_t tabaddr;
    if ( !swd_read_word16(RP2040_BOOTROM_MAGIC_ADDR+4, &v))
        return 0;
    tabaddr = v;

    // Now try to find our function...
    uint16_t value;
    do {
        if ( !swd_read_word16(tabaddr, &value))
            return 0;
        if (value == tag) {
            if ( !swd_read_word16(tabaddr+2, &value))
                return 0;
            return (uint32_t)value;
        }
        tabaddr += 4;
    } while (value != 0);
    return 0;
}   // rp2040_target_find_rom_func



///
/// Call function on the target device at address \a addr.
/// Arguments are in \a args[] / \a argc, result of the called function (from r0) will be
/// put to \a *result (if != NULL).
///
/// \pre
///    - target MCU must be connected
///    - code must be already uploaded to target
///
/// \note
///    The called function could end with __breakpoint(), but with the help of the used trampoline
///    functions in ROM, the functions can be fetched.
///
bool rp2040_target_call_function(uint32_t addr, uint32_t args[], int argc, uint32_t *result)
{
    static uint32_t trampoline_addr = 0;  // trampoline is fine to get the return value of the callee
    static uint32_t trampoline_end;
    bool interrupted = false;

    if ( !target_core_halt())
        return false;

    assert(argc <= 4);

    // First get the trampoline address...  (actually not required, because the functions reside in RAM...)
    if (trampoline_addr == 0) {
        trampoline_addr = rp2040_target_find_rom_func('D', 'T');
        trampoline_end = rp2040_target_find_rom_func('D', 'E');
        if (trampoline_addr == 0  ||  trampoline_end == 0)
            return false;
    }

    // Set the registers for the trampoline call...
    // function in r7, args in r0, r1, r2, and r3, end in lr?
    for (int i = 0;  i < argc;  ++i) {
        if ( !swd_write_core_register(i, args[i]))
            return false;
    }
    if ( !swd_write_core_register(7, addr))
        return false;

    // Set the stack pointer to something sensible... (MSP)
    if ( !swd_write_core_register(13, TARGET_RP2040_STACK))
        return false;

    // Now set the PC to go to our address
    if ( !swd_write_core_register(15, trampoline_addr))
        return false;

    // Set xPSR for the thumb thingy...
    if ( !swd_write_core_register(16, (1 << 24)))
        return false;

    if ( !target_core_halt())
        return false;

#if DEBUG_MODULE
    for (int i = 0;  i < 18;  ++i)
        display_reg(i);
#endif

    // start execution
//    picoprobe_info(".................... execute\n");
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
                picoprobe_error("rp2040_target_call_function: execution timed out after %u ms\n",
                                (unsigned)(dt_us / 1000));
                interrupted = true;
            }
        }
        if ( !interrupted) {
            uint32_t dt_ms = (time_us_32() - start_us) / 1000;
            if (dt_ms > 10) {
                picoprobe_debug("rp2040_target_call_function: execution finished after %lu ms\n", dt_ms);
            }
        }
    }

#if DEBUG_MODULE
    for (int i = 0;  i < 18;  ++i)
        display_reg(i);
#endif

    if (result != NULL  &&  !interrupted) {
        // fetch result of function (r0)
        if ( !swd_read_core_register(0, result)) {
            picoprobe_error("rp2040_target_call_function: cannot read core register 0\n");
            return false;
        }
    }

    {
        uint32_t r15;

        if ( !swd_read_core_register(15, &r15)) {
            picoprobe_error("rp2040_target_call_function: cannot read core register 15\n");
            return false;
        }

        if (r15 != (trampoline_end & 0xfffffffe)) {
            picoprobe_error("rp2040_target_call_function: invoked target function did not run til end: 0x%0x != 0x%0x\n",
                            (unsigned)r15, (unsigned)trampoline_end);
            return false;
        }
    }
    return true;
}   // rp2040_target_call_function
