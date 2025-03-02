/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hardware/helper.h"
#include "hardware/structs/ssi.h"
#include "hardware/regs/io_qspi.h"

// ---------------------------------------------------------------------------------------------------------------------
// YAPicoprobe definitions
//

#include <stdio.h>

#include "swd_host.h"

#include "raspberry/target_utils_raspberry.h"
#include "target_utils_rp2040.h"

extern char __start_for_target_connect_rp2040[];
extern char __stop_for_target_connect_rp2040[];

// Attributes for RP2040 target code - DO NOT CHANGE THIS
// Note that there is also compile option setup in CMakeLists.txt
#if defined(__clang__)
    #define FOR_TARGET_RP2040_CODE        __attribute__((noinline, section("for_target_connect_rp2040"), target("arch=cortex-m0")))
#else
    #define FOR_TARGET_RP2040_CODE        __attribute__((noinline, section("for_target_connect_rp2040"), target("arch=armv6-m")))
#endif

#define TARGET_RP2040_CODE            (TARGET_RP2040_RAM_START + 0x10000)
#define TARGET_RP2040_FLASH_SIZE      ((uint32_t)rp2040_flash_size - (uint32_t)__start_for_target_connect_rp2040 + TARGET_RP2040_CODE)


// ---------------------------------------------------------------------------------------------------------------------
//
// Parts of the following code has been stolen from pico-bootrom/bootrom/program_flash_generic.c
//                                              and pico-sdk2/src/rp2_common/hardware_flash/flash.c
//
// ---------------------------------------------------------------------------------------------------------------------


// These are supported by almost any SPI flash
#define FLASHCMD_READ_SFDP        0x5a
#define FLASHCMD_READ_JEDEC_ID    0x9f

// Annoyingly, structs give much better code generation, as they re-use the base
// pointer rather than doing a PC-relative load for each constant pointer.

static ssi_hw_t *const ssi = (ssi_hw_t *) XIP_SSI_BASE;

// Sanity check
#undef static_assert
#define static_assert(cond, x) extern int static_assert[(cond)?1:-1]
check_hw_layout(ssi_hw_t, ssienr, SSI_SSIENR_OFFSET);
check_hw_layout(ssi_hw_t, spi_ctrlr0, SSI_SPI_CTRLR0_OFFSET);

typedef enum {
    OUTOVER_NORMAL = 0,
    OUTOVER_INVERT,
    OUTOVER_LOW,
    OUTOVER_HIGH
} outover_t;

// Flash code may be heavily interrupted (e.g. if we are running USB MSC
// handlers concurrently with flash programming) so we control the CS pin
// manually
FOR_TARGET_RP2040_CODE static void flash_cs_force(outover_t over) {
    io_rw_32 *reg = (io_rw_32 *) (IO_QSPI_BASE + IO_QSPI_GPIO_QSPI_SS_CTRL_OFFSET);
#ifndef GENERAL_SIZE_HACKS
    *reg = (*reg & ~IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS)
        | (over << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB);
#else
    // The only functions we want are FSEL (== 0 for XIP) and OUTOVER!
    *reg = over << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB;
#endif
    // Read to flush async bridge
    (void) *reg;
}

// Put bytes from one buffer, and get bytes into another buffer.
// These can be the same buffer.
// If tx is NULL then send zeroes.
// If rx is NULL then all read data will be dropped.
//
// If rx_skip is nonzero, this many bytes will first be consumed from the FIFO,
// before reading a further count bytes into *rx.
// E.g. if you have written a command+address just before calling this function.
FOR_TARGET_RP2040_CODE static void flash_put_get(const uint8_t *tx, uint8_t *rx, size_t count, size_t rx_skip) {
    // Make sure there is never more data in flight than the depth of the RX
    // FIFO. Otherwise, when we are interrupted for long periods, hardware
    // will overflow the RX FIFO.
    const uint max_in_flight = 16 - 2; // account for data internal to SSI
    size_t tx_count = count;
    size_t rx_count = count;
    while (tx_count || rx_skip || rx_count) {
        // NB order of reads, for pessimism rather than optimism
        uint32_t tx_level = ssi_hw->txflr;
        uint32_t rx_level = ssi_hw->rxflr;
        bool did_something = false; // Expect this to be folded into control flow, not register
        if (tx_count && tx_level + rx_level < max_in_flight) {
            ssi->dr0 = (uint32_t) (tx ? *tx++ : 0);
            --tx_count;
            did_something = true;
        }
        if (rx_level) {
            uint8_t rxbyte = ssi->dr0;
            did_something = true;
            if (rx_skip) {
                --rx_skip;
            } else {
                if (rx)
                    *rx++ = rxbyte;
                --rx_count;
            }
        }
        if (!did_something)
            break;
    }
    flash_cs_force(OUTOVER_HIGH);
}

// Convenience wrapper for above
// (And it's hard for the debug host to get the tight timing between
// cmd DR0 write and the remaining data)
FOR_TARGET_RP2040_CODE static void flash_do_cmd(uint8_t cmd, const uint8_t *tx, uint8_t *rx, size_t count) {
    flash_cs_force(OUTOVER_LOW);
    ssi->dr0 = cmd;
    flash_put_get(tx, rx, count, 1);
}


// Timing of this one is critical, so do not expose the symbol to debugger etc
FOR_TARGET_RP2040_CODE static void flash_put_cmd_addr(uint8_t cmd, uint32_t addr) {
    flash_cs_force(OUTOVER_LOW);
    addr |= cmd << 24;
    for (int i = 0; i < 4; ++i) {
        ssi->dr0 = addr >> 24;
        addr <<= 8;
    }
}


// ----------------------------------------------------------------------------
// Size determination via SFDP or JEDEC ID (best effort)
// Relevant XKCD: https://xkcd.com/927/

FOR_TARGET_RP2040_CODE static void flash_read_sfdp(uint32_t addr, uint8_t *rx, size_t count) {
    assert(addr < 0x1000000);
    flash_put_cmd_addr(FLASHCMD_READ_SFDP, addr);
    ssi->dr0 = 0; // dummy byte
    flash_put_get(NULL, rx, count, 5);
}

FOR_TARGET_RP2040_CODE static uint32_t bytes_to_u32le(const uint8_t *b) {
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

// Return value >= 0: log 2 of flash size in bytes.
// Return value < 0: unable to determine size.
FOR_TARGET_RP2040_CODE static int __noinline flash_size_log2() {
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
    flash_do_cmd(FLASHCMD_READ_JEDEC_ID, NULL, rxbuf, 3);
    uint8_t array_size_byte = rxbuf[2];
    // Confusingly this is log2 of size in bytes, not bits like SFDP. Sanity check:
    if (array_size_byte < 8 || array_size_byte > 34)
        goto jedec_id_fail;
    return array_size_byte;

jedec_id_fail:
    return -1;
}



// ---------------------------------------------------------------------------------------------------------------------
// YAPicoprobe definitions
//

FOR_TARGET_RP2040_CODE static uint32_t rp2040_flash_size(void)
{
    // Fill in the rom functions...
    rp2040_rom_table_lookup_fn rom_table_lookup = (rp2040_rom_table_lookup_fn)rom_hword_as_ptr(0x18);
    uint16_t            *function_table = (uint16_t *)rom_hword_as_ptr(0x14);

    rp2xxx_rom_void_fn  _flash_exit_xip         = rom_table_lookup(function_table, ROM_FN('E', 'X'));
    rp2xxx_rom_void_fn  _flash_flush_cache      = rom_table_lookup(function_table, ROM_FN('F', 'C'));
    rp2xxx_rom_void_fn  _flash_enter_cmd_xip    = rom_table_lookup(function_table, ROM_FN('C', 'X'));

    _flash_exit_xip();
    int r = flash_size_log2();
    _flash_flush_cache();
    _flash_enter_cmd_xip();

    return (r < 0) ? 0 : (1UL << r);
}   // rp2040_flash_size



static bool rp2040_target_copy_flash_code(void)
{
    int code_len = (__stop_for_target_connect_rp2040 - __start_for_target_connect_rp2040);

    picoprobe_info("FLASH: Copying custom flash code to 0x%08x (%d bytes)\r\n", TARGET_RP2040_CODE, code_len);

    if ( !swd_write_memory(TARGET_RP2040_CODE, (uint8_t *)__start_for_target_connect_rp2040, code_len))
        return false;
    return true;
}   // rp2040_target_copy_flash_code



uint32_t target_rp2040_get_external_flash_size(void)
{
    uint32_t res = 0;
    bool ok;

    ok = target_set_state(RESET_PROGRAM);
    if (ok) {
        rp2040_target_copy_flash_code();
        rp2040_target_call_function(TARGET_RP2040_FLASH_SIZE, NULL, 0, &res);
        target_set_state(RESET_PROGRAM);
    }

    return res;
}   // target_rp2040_get_external_flash_size
