/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// ---------------------------------------------------------------------------------------------------------------------
// YAPicoprobe definitions
//

#include <stdio.h>

#include "swd_host.h"

#include "raspberry/target_utils_raspberry.h"
#include "target_utils_rp2350.h"

extern char __start_for_target_connect_rp2350[];
extern char __stop_for_target_connect_rp2350[];

// Attributes for RP2350 target code - DO NOT CHANGE THIS (but doesn't matter if code is compiled for cortex-m0 or m33, executes both on target)
// Note that there is also compile option setup in CMakeLists.txt
#define FOR_TARGET_RP2350_CODE        __attribute__((noinline, section("for_target_connect_rp2350")))

#define TARGET_RP2350_CODE            (TARGET_RP2350_RAM_START + 0x10000)
#define TARGET_RP2350_BREAKPOINT      ((uint32_t)rp2350_breakpoint - (uint32_t)__start_for_target_connect_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_FLASH_SIZE      ((uint32_t)rp2350_flash_size - (uint32_t)__start_for_target_connect_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_RCP_INIT        ((uint32_t)rp2350_rcp_init   - (uint32_t)__start_for_target_connect_rp2350 + TARGET_RP2350_CODE)

//
// ---------------------------------------------------------------------------------------------------------------------

// this are the bootrom functions
// created with rp2350_target_find_rom_func()

#if 0
0.126 (  1) - - e: 31956
0.126 (  0) -      S R, flags 0x0005
0.127 (  1) -        0xbc1d
0.127 (  0) -        0x18c1
0.127 (  0) - - e: 31964
0.128 (  1) -      G S, flags 0x0017
0.128 (  0) -        0xf76f
0.129 (  1) -        0x81bf
0.129 (  0) -        0x09c1
0.130 (  1) -        0x7e15
0.130 (  0) - - e: 31976
0.130 (  0) -      S C, flags 0x0010
0.131 (  1) -        0x7e35
0.131 (  0) - - e: 31982
0.132 (  1) -      L P, flags 0x0007
0.132 (  0) -        0xf76f
0.133 (  1) -        0x809f
0.133 (  0) -        0x0eb9
0.133 (  0) - - e: 31992
0.134 (  1) -      G P, flags 0x0017
0.134 (  0) -        0xf76f
0.135 (  1) -        0xffef
0.135 (  0) -        0x0acd
0.135 (  0) -        0x7e2d
0.136 (  1) - - e: 32004
0.136 (  0) -      G B, flags 0x0017
0.137 (  1) -        0xf76f
0.137 (  0) -        0xff2f
0.137 (  0) -        0x0b85
0.138 (  1) -        0x7e4d
0.138 (  0) - - e: 32016
0.139 (  1) -      F A, flags 0x0017
0.139 (  0) -        0xf76f
0.139 (  0) -        0xfe6f
0.140 (  1) -        0x0bc5
0.140 (  0) -        0x7e25
0.140 (  0) - - e: 32028
0.141 (  1) -      R B, flags 0x0017
0.142 (  1) -        0xf76f
0.142 (  0) -        0xfdaf
0.142 (  0) -        0x0571
0.143 (  1) -        0x7e45
0.143 (  0) - - e: 32040
0.144 (  1) -      O A, flags 0x0017
0.144 (  0) -        0xf76f
0.144 (  0) -        0xfcef
0.145 (  1) -        0x09b9
0.145 (  0) -        0x7e3d
0.145 (  0) - - e: 32052
0.146 (  1) -      I F, flags 0x0007
0.146 (  0) -        0xf76f
0.147 (  1) -        0xfc2f
0.147 (  0) -        0x0c1d
0.147 (  0) - - e: 32062
0.148 (  1) -      R A, flags 0x0007
0.148 (  0) -        0xf76f
0.149 (  1) -        0xfb8f
0.149 (  0) -        0x0ca1
0.149 (  0) - - e: 32072
0.150 (  1) -      E X, flags 0x0007
0.150 (  0) -        0xf76f
0.151 (  1) -        0xfaef
0.151 (  0) -        0x0d65
0.151 (  0) - - e: 32082
0.152 (  1) -      R E, flags 0x0007
0.152 (  0) -        0xf76f
0.153 (  1) -        0xfa4f
0.153 (  0) -        0x0d0d
0.153 (  0) - - e: 32092
0.154 (  1) -      R P, flags 0x0007
0.154 (  0) -        0xf76f
0.155 (  1) -        0xf9af
0.155 (  0) -        0x0cd1
0.155 (  0) - - e: 32102
0.156 (  1) -      F C, flags 0x0007
0.156 (  0) -        0xf76f
0.157 (  1) -        0xf90f
0.157 (  0) -        0x3711
0.158 (  1) - - e: 32112
0.158 (  0) -      C X, flags 0x0007
0.159 (  1) -        0xf76f
0.159 (  0) -        0xf86f
0.159 (  0) -        0x04f1
0.160 (  1) - - e: 32122
0.160 (  0) -      X M, flags 0x0007
0.161 (  1) -        0xf76f
0.161 (  0) -        0xf7cf
0.161 (  0) -        0x04f5
0.162 (  1) - - e: 32132
0.162 (  0) -      F O, flags 0x0017
0.163 (  1) -        0xf76f
0.163 (  0) -        0xf72f
0.163 (  0) -        0x06e3
0.164 (  1) -        0x7e1d
0.164 (  0) - - e: 32144
0.165 (  1) -      R C, flags 0x0007
0.165 (  0) -        0xf76f
0.165 (  0) -        0xf66f
0.166 (  1) -        0x0b61
0.166 (  0) - - e: 32154
0.167 (  1) -      V B, flags 0x0004
0.167 (  0) -        0x03d5
0.167 (  0) - - e: 32160
0.168 (  1) -      S P, flags 0x0004
0.168 (  0) -        0x0361
0.169 (  1) - - e: 32166
0.169 (  0) -      A B, flags 0x0007
0.170 (  1) -        0xf76f
0.170 (  0) -        0xf50f
0.170 (  0) -        0x0e55
0.171 (  1) - - e: 32176
0.171 (  0) -      E B, flags 0x0007
0.172 (  1) -        0xf76f
0.172 (  0) -        0xf46f
0.172 (  0) -        0x0f9d
0.173 (  1) - - e: 32186
0.173 (  0) -      C I, flags 0x0007
0.174 (  1) -        0xf76f
0.174 (  0) -        0xf3cf
0.174 (  0) -        0x0f11
0.175 (  1) - - e: 32196
0.175 (  0) -      G U, flags 0x0007
0.176 (  1) -        0xf76f
0.176 (  0) -        0xf32f
0.177 (  1) -        0x0ed7
0.177 (  0) - - e: 32206
0.177 (  0) -      S S, flags 0x0001
0.178 (  1) -        0xb495
0.178 (  0) - - e: 32212
0.179 (  1) -      G R, flags 0x0040
0.179 (  0) -        0x7df0
0.179 (  0) - - e: 32218
0.180 (  1) -      P T, flags 0x0040
0.180 (  0) -        0x46bc
0.180 (  0) - - e: 32224
0.181 (  1) -      X F, flags 0x0040
0.181 (  0) -        0x46c4
0.182 (  1) - - e: 32230
0.182 (  0) -      F D, flags 0x0040
0.183 (  1) -        0x46c0
0.183 (  0) - - e: 32236
#endif

//----------------------------------------------------------------------------------------------------------------------
//
// Functions running in the target
//

FOR_TARGET_RP2350_CODE __attribute__((naked)) void rp2350_rcp_init(void)
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
}   // rp2350_rcp_init


FOR_TARGET_RP2350_CODE __attribute__((naked)) void rp2350_breakpoint(void)
{
    __asm volatile ("bkpt 0");
}   // rp2350_breakpoint


// ---------------------------------------------------------------------------------------------------------------------
//
// Parts of the following code has been stolen from pico-bootrom-rp2350/src/main/arm/varm_generic_flash.c
//                                              and pico-sdk2/src/rp2_common/hardware_flash/flash.c
//
// ---------------------------------------------------------------------------------------------------------------------


// These are supported by almost any SPI flash
#define FLASHCMD_READ_SFDP        0x5a
#define FLASHCMD_READ_JEDEC_ID    0x9f



FOR_TARGET_RP2350_CODE static void flash_put_get(uint8_t cs, const uint8_t *tx, uint8_t *rx, size_t count, size_t rx_skip)
{
}


// returns its first argument to allow it to be preserved across calls without
// a callee save register
FOR_TARGET_RP2350_CODE static void flash_do_cmd(uint8_t cs, uint8_t cmd, const uint8_t *tx, uint8_t *rx, size_t count)
{
    //qmi_hw->direct_tx = cmd | QMI_DIRECT_TX_NOPUSH_BITS;
    flash_put_get(cs, tx, rx, count, 0);
    rx[2] = 22;
}


#if 0
// Timing of this one is critical, so do not expose the symbol to debugger etc
FOR_TARGET_RP2350_CODE static void flash_put_cmd_addr(uint8_t cmd, uint32_t addr)
{
    flash_cs_force(OUTOVER_LOW);
    addr |= cmd << 24;
    for (int i = 0; i < 4; ++i) {
        ssi->dr0 = addr >> 24;
        addr <<= 8;
    }
}
#endif


// ----------------------------------------------------------------------------
// Size determination via SFDP or JEDEC ID (best effort)
// Relevant XKCD: https://xkcd.com/927/

FOR_TARGET_RP2350_CODE static void flash_read_sfdp(uint32_t addr, uint8_t *rx, size_t count)
{
#if 0
    assert(addr < 0x1000000);
    flash_put_cmd_addr(FLASHCMD_READ_SFDP, addr);
    ssi->dr0 = 0; // dummy byte
    flash_put_get(NULL, rx, count, 5);
#else
    // TODO this is a dummy...
    for (int i = 0;  i < count;  ++i)
        rx[i] = 0;
#endif
}


FOR_TARGET_RP2350_CODE static uint32_t bytes_to_u32le(const uint8_t *b)
{
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}


// Return value >= 0: log 2 of flash size in bytes.
// Return value < 0: unable to determine size.
FOR_TARGET_RP2350_CODE static int flash_size_log2(void)
{
    uint8_t rxbuf[16];

    // Check magic
    flash_read_sfdp(0, rxbuf, 16);
    if (bytes_to_u32le(rxbuf) != ('S' | ('F' << 8) | ('D' << 16) | ('P' << 24)))
        goto sfdp_fail;
    // Skip NPH -- we don't care about nonmandatory parameters.
    // Offset 8 is header for mandatory parameter table
    // | ID | MinRev | MajRev | Length in words | ptr[2] | ptr[1] | ptr[0] | unused|
    // ID must be 0 (JEDEC) for mandatory PTH
    if (rxbuf[8] != 0)
        goto sfdp_fail;

    uint32_t param_table_ptr = bytes_to_u32le(rxbuf + 12) & 0xffffffu;
    flash_read_sfdp(param_table_ptr, rxbuf, 8);
    uint32_t array_size_word = bytes_to_u32le(rxbuf + 4);
    // MSB set: array >= 2 Gbit, encoded as log2 of number of bits
    // MSB clear: array < 2 Gbit, encoded as direct bit count
    if (array_size_word & (1u << 31)) {
        array_size_word &= ~(1u << 31);
    } else {
        uint32_t ctr = 0;
        array_size_word += 1;
        while (array_size_word >>= 1)
            ++ctr;
        array_size_word = ctr;
    }
    // Sanity check... 2kbit is minimum for 2nd stage, 128 Gbit is 1000x bigger than we can XIP
    if (array_size_word < 11 || array_size_word > 37)
        goto sfdp_fail;
    return array_size_word - 3;

sfdp_fail:
    // If no SFDP, it's common to encode log2 of main array size in second
    // byte of JEDEC ID
    flash_do_cmd(0, FLASHCMD_READ_JEDEC_ID, NULL, rxbuf, 3);
    uint8_t array_size_byte = rxbuf[2];
    // Confusingly this is log2 of size in bytes, not bits like SFDP. Sanity check:
    if (array_size_byte < 8 || array_size_byte > 34)
        goto jedec_id_fail;
    return array_size_byte;

jedec_id_fail:
    return -1;
}   // flash_size_log2


FOR_TARGET_RP2350_CODE static void *rp2350_rom_table_lookup(char c1, char c2)
/**
 * Lookup ROM table.
 * This seems to have one more indirection as documented.
 */
{
    const uint32_t BOOTROM_TABLE_LOOKUP_OFFSET  = 0x16;
    const uint16_t RT_FLAG_FUNC_ARM_SEC         = 0x0004;
    //const uint16_t RT_FLAG_FUNC_ARM_NONSEC      = 0x0010;
    rp2350_rom_table_lookup_fn rom_table_lookup = (rp2350_rom_table_lookup_fn) (uintptr_t)(*(uint16_t*) (BOOTROM_TABLE_LOOKUP_OFFSET));

    uint16_t code = (c2 << 8) | c1;
    return (void *)rom_table_lookup(code, RT_FLAG_FUNC_ARM_SEC);
}  // rp2350_rom_table_lookup


FOR_TARGET_RP2350_CODE static uint32_t rp2350_flash_size(void)
{

    rp2xxx_rom_void_fn  _flash_exit_xip         = rp2350_rom_table_lookup('E', 'X');
    rp2xxx_rom_void_fn  _flash_flush_cache      = rp2350_rom_table_lookup('F', 'C');
    rp2xxx_rom_void_fn  _flash_enter_cmd_xip    = rp2350_rom_table_lookup('C', 'X');

    _flash_exit_xip();
    int r = flash_size_log2();
    _flash_flush_cache();
    _flash_enter_cmd_xip();

    return (r < 0) ? 0 : (1UL << r);
}   // rp2350_flash_size


//----------------------------------------------------------------------------------------------------------------------
//
// Actual functions
//

static bool rp2350_target_copy_flash_code(void)
{
    int code_len = (__stop_for_target_connect_rp2350 - __start_for_target_connect_rp2350);

    picoprobe_info("FLASH: Copying custom flash code to 0x%08x (%d bytes)\n", TARGET_RP2350_CODE, code_len);

    if ( !swd_write_memory(TARGET_RP2350_CODE, (uint8_t *)__start_for_target_connect_rp2350, code_len))
        return false;
    return true;
}   // rp2350_target_copy_flash_code


uint32_t target_rp2350_get_external_flash_size(void)
{
    uint32_t res = 0;
    bool ok;

    ok = target_set_state(RESET_PROGRAM);
    if (ok) {
        ok = rp2350_target_copy_flash_code();
        ok = rp2350_target_call_function(TARGET_RP2350_RCP_INIT,   NULL, 0, TARGET_RP2350_RCP_INIT+24, NULL);
        ok = rp2350_target_call_function(TARGET_RP2350_FLASH_SIZE, NULL, 0, TARGET_RP2350_BREAKPOINT, &res);
        target_set_state(RESET_PROGRAM);
    }

    return res;
}   // target_rp2350_get_external_flash_size
