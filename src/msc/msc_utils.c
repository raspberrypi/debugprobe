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


#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <pico/stdlib.h>

#include "boot/uf2.h"                // this is the Pico variant of the UF2 header

#include "msc_utils.h"
#include "sw_lock.h"
#include "led.h"

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "target_family.h"
#include "target_config.h"
#include "swd_host.h"
#include "error.h"
#include "flash_intf.h"
#include "flash_manager.h"
#include "rp2040/target_utils_rp2040.h"
#include "rp2350/target_utils_rp2350.h"
#include "raspberry/target_utils_raspberry.h"


#define DEBUG_MODULE    0

// DAPLink needs bigger buffer
#define TARGET_WRITER_THREAD_MSGBUFF_SIZE   (16 * sizeof(struct uf2_block) + 100)

#define USE_RP2040()          (UF2_ID(0) == RP2040_FAMILY_ID)
#define USE_RP2350()          (UF2_ID(0) == RP2350_ARM_S_FAMILY_ID  ||  UF2_ID(1) == RP2350_ARM_S_FAMILY_ID  ||  UF2_ID(2) == RP2350_ARM_S_FAMILY_ID  ||  UF2_ID(3) == RP2350_ARM_S_FAMILY_ID)
#define UF2_ID(NDX)           (g_board_info.target_cfg->rt_uf2_id[NDX])
#define UF2_ID_LEN()          (sizeof(g_board_info.target_cfg->rt_uf2_id) / sizeof(g_board_info.target_cfg->rt_uf2_id[0]))
#define UF2_ID_IS_PRESENT()   (UF2_ID(0) != 0)

#define TARGET_FLASH_START    (g_board_info.target_cfg->flash_regions[0].start)
#define TARGET_FLASH_END      (g_board_info.target_cfg->flash_regions[0].end)

static TaskHandle_t           task_target_writer_thread;
static MessageBufferHandle_t  msgbuff_target_writer_thread;
static SemaphoreHandle_t      sema_swd_in_use;
static TimerHandle_t          timer_disconnect;
static void                  *timer_disconnect_id;
static bool                   have_lock;
static bool                   must_initialize = true;
static bool                   had_write;
static volatile bool          is_connected;
static uint32_t               transferred_bytes;
static uint64_t               transfer_start_us;


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

#define FOR_TARGET_RP2040_CODE        __attribute__((noinline, section("for_target_msc_rp2040"), target("arch=armv6-m"), optimize("-Og")))

#define TARGET_RP2040_CODE            (TARGET_RP2040_RAM_START + 0x10000)
#define TARGET_RP2040_FLASH_BLOCK     ((uint32_t)rp2040_flash_block - (uint32_t)__start_for_target_msc_rp2040 + TARGET_RP2040_CODE)
#define TARGET_RP2040_BOOT2           (TARGET_RP2040_RAM_START + 0x20000)
#define TARGET_RP2040_BOOT2_SIZE      256
#define TARGET_RP2040_ERASE_MAP       (TARGET_RP2040_BOOT2 + TARGET_RP2040_BOOT2_SIZE)
#define TARGET_RP2040_ERASE_MAP_SIZE  256
#define TARGET_RP2040_DATA            (TARGET_RP2040_RAM_START + 0x00000)


extern char __start_for_target_msc_rp2350[];
extern char __stop_for_target_msc_rp2350[];

#define FOR_TARGET_RP2350_CODE        __attribute__((noinline, section("for_target_msc_rp2350")))

#define TARGET_RP2350_CODE            (TARGET_RP2350_RAM_START + 0x10000)
#define TARGET_RP2350_FLASH_BLOCK     ((uint32_t)rp2350_flash_block    - (uint32_t)__start_for_target_msc_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_BREAKPOINT      ((uint32_t)rp2350_msc_breakpoint - (uint32_t)__start_for_target_msc_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_RCP_INIT        ((uint32_t)rp2350_msc_rcp_init   - (uint32_t)__start_for_target_msc_rp2350 + TARGET_RP2350_CODE)
#define TARGET_RP2350_ERASE_MAP       (TARGET_RP2350_RAM_START + 0x20000)
#define TARGET_RP2350_ERASE_MAP_SIZE  256
#define TARGET_RP2350_DATA            (TARGET_RP2350_RAM_START + 0x00000)



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

    rp2040_rom_void_fn         _connect_internal_flash = rom_table_lookup(function_table, fn('I', 'F'));
    rp2040_rom_void_fn         _flash_exit_xip         = rom_table_lookup(function_table, fn('E', 'X'));
    rp2040_rom_flash_erase_fn  _flash_range_erase      = rom_table_lookup(function_table, fn('R', 'E'));
    rp2040_rom_flash_prog_fn   _flash_range_program    = rom_table_lookup(function_table, fn('R', 'P'));
    rp2040_rom_void_fn         _flash_flush_cache      = rom_table_lookup(function_table, fn('F', 'C'));
    rp2040_rom_void_fn         _flash_enter_cmd_xip    = rom_table_lookup(function_table, fn('C', 'X'));

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
            RP2040_FLASH_RANGE_ERASE(offset, erase_block_size, erase_block_size, 0xD8);     // 64K erase
            res |= 0x0001;
        }
        *erase_map_entry = 0xff;
    }

    if (src != NULL  &&  length != 0) {
        RP2040_FLASH_RANGE_PROGRAM(offset, (uint8_t *)src, length);
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


// -----------------------------------------------------------------------------------


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


#define _FN(a, b)        (uint32_t)((b << 8) | a)

FOR_TARGET_RP2350_CODE uint32_t rp2350_flash_block(uint32_t addr, uint32_t *src, int length)
{
    typedef void *(*rp2350_rom_table_lookup_fn)(uint32_t code, uint32_t mask);
    const uint32_t BOOTROM_TABLE_LOOKUP_OFFSET  = 0x16;
    const uint16_t RT_FLAG_FUNC_ARM_SEC         = 0x0004;
    rp2350_rom_table_lookup_fn rom_table_lookup = (rp2350_rom_table_lookup_fn) (uintptr_t)(*(uint16_t*) (BOOTROM_TABLE_LOOKUP_OFFSET));

    rp2040_rom_void_fn         _connect_internal_flash = rom_table_lookup(_FN('I', 'F'), RT_FLAG_FUNC_ARM_SEC);
    rp2040_rom_void_fn         _flash_exit_xip         = rom_table_lookup(_FN('E', 'X'), RT_FLAG_FUNC_ARM_SEC);
    rp2040_rom_flash_erase_fn  _flash_range_erase      = rom_table_lookup(_FN('R', 'E'), RT_FLAG_FUNC_ARM_SEC);
    rp2040_rom_flash_prog_fn   _flash_range_program    = rom_table_lookup(_FN('R', 'P'), RT_FLAG_FUNC_ARM_SEC);
    rp2040_rom_void_fn         _flash_flush_cache      = rom_table_lookup(_FN('F', 'C'), RT_FLAG_FUNC_ARM_SEC);
    rp2040_rom_void_fn         _flash_enter_cmd_xip    = rom_table_lookup(_FN('C', 'X'), RT_FLAG_FUNC_ARM_SEC);

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
            RP2040_FLASH_RANGE_ERASE(offset, erase_block_size, erase_block_size, 0xD8);     // 64K erase
            res |= 0x0001;
        }
        *erase_map_entry = 0xff;
    }

    if (src != NULL  &&  length != 0) {
        RP2040_FLASH_RANGE_PROGRAM(offset, (uint8_t *)src, length);
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


// -----------------------------------------------------------------------------------


#if DEBUG_MODULE
static bool display_reg(uint8_t num)
{
	uint32_t val;
	bool rc;

    rc = swd_read_core_register(num, &val);
    if ( !rc)
    	return rc;
    printf("xx %d r%d=0x%lx\n", __LINE__, num, val);
    return true;
}   // display_reg
#endif



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



/**
 * Disconnect probe from the target and start the target.
 * Called by software timer.
 *
 * \pre
 *    must have sw_lock()
 */
static void target_disconnect(TimerHandle_t xTimer)
{
    if (xSemaphoreTake(sema_swd_in_use, 0)) {
        if (is_connected) {
            uint32_t dt_ms = (uint32_t)((time_us_64() - transfer_start_us) / 1000);
            uint32_t t_bps = (100 * transferred_bytes) / (dt_ms / 10);
            picoprobe_info("=================================== MSC disconnect target: %d bytes transferred, %d bytes/s\n",
                           (int)transferred_bytes, (int)t_bps);
            led_state(LS_MSC_DISCONNECTED);
            if (had_write) {
                if (USE_RP2040()) {
                }
                else if (USE_RP2350()) {
                }
                else {
                    printf("xxxxxxxxxxxxxx\n");
                    flash_manager_uninit();
                }
                bool r1 = target_set_state(RESET_PROGRAM);
                bool r2 = target_set_state(RESET_RUN);
                printf("<<<<<<<<<<<<<<<<< %d %d\n", r1, r2);

            }
            is_connected = false;
        }
        have_lock = false;
        xSemaphoreGive(sema_swd_in_use);
        sw_unlock("MSC");
    }
    else {
        xTimerReset(xTimer, pdMS_TO_TICKS(1000));
    }
}   // target_disconnect



///
/// Connect the probe to the target.
/// This function must be called on every read/write to retrigger the disconnect functionality.
/// Disconnecting is done after a certain delay without calling msc_target_connect().
///
bool msc_target_connect(bool write_mode)
{
    static uint64_t last_trigger_us;
    uint64_t now_us;
    bool ok;

    if (have_lock  ||  sw_lock("MSC", true)) {
        xSemaphoreTake(sema_swd_in_use, portMAX_DELAY);
        have_lock = true;
        now_us = time_us_64();
        ok = true;
        if ( !is_connected  ||  now_us - last_trigger_us > 1000*1000) {
            picoprobe_info("=================================== MSC connect target\n");
            led_state(LS_MSC_CONNECTED);

            ok = target_set_state(ATTACH);
            printf("---------------------------------- %d\n", ok);

            must_initialize = ok;
            is_connected = true;                   // disconnect must be issued!
            had_write = false;
            transferred_bytes = 0;
            transfer_start_us = now_us;
        }
        last_trigger_us = now_us;
        xTimerReset(timer_disconnect, pdMS_TO_TICKS(1000));
        xSemaphoreGive(sema_swd_in_use);
    }
    else {
        ok = false;
    }
    return ok;
}   // msc_target_connect



static void setup_uf2_record(struct uf2_block *uf2, uint32_t target_addr, uint32_t payload_size, uint32_t block_no, uint32_t num_blocks)
{
    uf2->magic_start0 = UF2_MAGIC_START0;
    uf2->magic_start1 = UF2_MAGIC_START1;
    uf2->flags        = UF2_ID_IS_PRESENT() ? UF2_FLAG_FAMILY_ID_PRESENT : 0;
    uf2->target_addr  = target_addr;
    uf2->payload_size = payload_size;
    uf2->block_no     = block_no;
    uf2->num_blocks   = num_blocks;
    uf2->file_size    = UF2_ID(0);
    uf2->magic_end    = UF2_MAGIC_END;
}   // setup_uf2_record



bool msc_is_uf2_record(const void *sector, uint32_t sector_size)
/**
 * Check if the data really contains a UF2 record.
 */
{
    const uint32_t payload_size = 256;
    bool r = false;

//    printf("   sector_size: %ld\n", sector_size);

    if (sector_size >= sizeof(struct uf2_block)) {
        const struct uf2_block *uf2 = (const struct uf2_block *)sector;

#if 0
        printf("   uf2->magic_start0:  0x%08lx\n", uf2->magic_start0);
        printf("   uf2->magic_start1:  0x%08lx\n", uf2->magic_start1);
        printf("   uf2->magic_end:     0x%08lx\n", uf2->magic_end);
        printf("   uf2->block_no:      0x%08lx\n", uf2->block_no);
        printf("   uf2->num_blocks:    0x%08lx\n", uf2->num_blocks);
        printf("   uf2->payload_size:  0x%08lx\n", uf2->payload_size);
        printf("   uf2->target_addr:   0x%08lx\n", uf2->target_addr);
        printf("   uf2->flags:         0x%08lx\n", uf2->flags);
        printf("   uf2->file_size:     0x%08lx\n", uf2->file_size);
        printf("   UF2_ID:             0x%08lx\n", UF2_ID(0));
        printf("   TARGET_FLASH_START: 0x%08lx\n", TARGET_FLASH_START);
        printf("   TARGET_FLASH_END:   0x%08lx\n", TARGET_FLASH_END);
#endif

        if (    uf2->magic_start0 == UF2_MAGIC_START0
            &&  uf2->magic_start1 == UF2_MAGIC_START1
            &&  uf2->magic_end == UF2_MAGIC_END
            &&  uf2->block_no < uf2->num_blocks
            &&  uf2->payload_size == payload_size
            &&  uf2->target_addr >= TARGET_FLASH_START
            &&  uf2->target_addr + payload_size <= TARGET_FLASH_END
           ) {
            if ((uf2->flags & UF2_FLAG_FAMILY_ID_PRESENT) != 0) {
                for (int i = 0;  i < UF2_ID_LEN();  ++i) {
                    if (uf2->file_size == UF2_ID(i)) {
                        r = true;
                        break;
                    }
                }
            }
            else {
                r = true;
            }
        }

    }
    return r;
}   // msc_is_uf2_record



//
// send the UF2 block to \a target_writer_thread()
//
bool msc_target_write_memory(const struct uf2_block *uf2)
{
    xMessageBufferSend(msgbuff_target_writer_thread, uf2, sizeof(*uf2), portMAX_DELAY);
    return true;
}   // msc_target_write_memory



bool msc_target_read_memory(struct uf2_block *uf2, uint32_t target_addr, uint32_t block_no, uint32_t num_blocks)
{
    const uint32_t payload_size = 256;
    bool ok;

    static_assert(payload_size <= sizeof(uf2->data), "UF2 payload is too big");

    xSemaphoreTake(sema_swd_in_use, portMAX_DELAY);
    setup_uf2_record(uf2, target_addr, payload_size, block_no, num_blocks);
    ok = swd_read_memory(target_addr, uf2->data, payload_size);
    transferred_bytes += payload_size;
    xSemaphoreGive(sema_swd_in_use);
    return ok;
}   // msc_target_read_memory



void target_writer_thread(void *ptr)
{
    static struct uf2_block uf2;
    size_t   len;

    for (;;) {
        len = xMessageBufferReceive(msgbuff_target_writer_thread, &uf2, sizeof(uf2), portMAX_DELAY);
        assert(len == 512);
        transferred_bytes += uf2.payload_size;

//        printf("target_writer_thread(0x%lx, %ld, %ld), %u\n", uf2.target_addr, uf2.block_no, uf2.num_blocks, len);

        xSemaphoreTake(sema_swd_in_use, portMAX_DELAY);

        if (must_initialize) {
            if (USE_RP2040()) {
                bool ok;

                ok = target_set_state(RESET_PROGRAM);
                if (ok) {
                    must_initialize = false;
                    rp2040_target_copy_flash_code();
                }
            }
            else if (USE_RP2350()) {
                bool ok;

                ok = target_set_state(RESET_PROGRAM);
                if (ok) {
                    must_initialize = false;
                    rp2350_target_copy_flash_code();
                    rp2350_target_call_function(TARGET_RP2350_RCP_INIT, NULL, 0, TARGET_RP2350_RCP_INIT+24, NULL);
                }
            }
            else {
                error_t sts;

//              flash_manager_set_page_erase(false);
                sts = flash_manager_init(flash_intf_target);
                printf("flash_manager_init = %d\n", sts);
                if (sts == ERROR_SUCCESS) {
                    must_initialize = false;
                }
            }
            had_write = true;
        }

        if (USE_RP2040()) {
            uint32_t arg[3];
            uint32_t res;

            arg[0] = uf2.target_addr;
            arg[1] = TARGET_RP2040_DATA;
            arg[2] = uf2.payload_size;

//            printf("   xxx  0x%lx, 0x%lx, 0x%lx, %ld\n", TARGET_RP2040_FLASH_BLOCK, arg[0], arg[1], arg[2]);

            if (swd_write_memory(TARGET_RP2040_DATA, (uint8_t *)uf2.data, uf2.payload_size)) {
                rp2040_target_call_function(TARGET_RP2040_FLASH_BLOCK, arg, sizeof(arg) / sizeof(arg[0]), &res);
                if (res & 0xf0000000) {
                    picoprobe_error("target_writer_thread: target operation returned 0x%x\n", (unsigned)res);
                }
            }
            else {
                picoprobe_error("target_writer_thread: failed to write to 0x%x/%d\n", (unsigned)uf2.target_addr, (unsigned)uf2.payload_size);
            }
        }
        else if (USE_RP2350()) {
#if 1
            uint32_t arg[3];
            uint32_t res;

            arg[0] = uf2.target_addr;
            arg[1] = TARGET_RP2350_DATA;
            arg[2] = uf2.payload_size;

//            printf("   yyy  0x%lx, 0x%lx, 0x%lx, %ld\n", TARGET_RP2350_FLASH_BLOCK, arg[0], arg[1], arg[2]);

            if (swd_write_memory(TARGET_RP2350_DATA, (uint8_t *)uf2.data, uf2.payload_size)) {
//                rp2350_target_call_function(TARGET_RP2350_RCP_INIT,   NULL, 0, TARGET_RP2350_RCP_INIT+24, NULL);
                rp2350_target_call_function(TARGET_RP2350_FLASH_BLOCK, arg, sizeof(arg) / sizeof(arg[0]), TARGET_RP2350_BREAKPOINT, &res);
                if (res & 0xf0000000) {
                    picoprobe_error("target_writer_thread: target operation returned 0x%x\n", (unsigned)res);
                }
            }
            else {
                picoprobe_error("target_writer_thread: failed to write to 0x%x/%d\n", (unsigned)uf2.target_addr, (unsigned)uf2.payload_size);
            }
#endif
        }
        else {
            flash_manager_data(uf2.target_addr, uf2.data, uf2.payload_size);
        }

        xTimerReset(timer_disconnect, pdMS_TO_TICKS(10));    // the above operation could take several 100ms!
        xSemaphoreGive(sema_swd_in_use);
    }
}   // target_writer_thread



bool msc_target_is_writable(void)
{
    return UF2_ID_IS_PRESENT();
}   // msc_target_is_writable



void msc_init(uint32_t task_prio)
{
    printf("msc_init()\n");

    sema_swd_in_use = xSemaphoreCreateMutex();
    if (sema_swd_in_use == NULL) {
        panic("msc_init: cannot create sema_swd_in_use\n");
    }

    timer_disconnect = xTimerCreate("timer_disconnect", pdMS_TO_TICKS(100), pdFALSE, timer_disconnect_id, target_disconnect);
    if (timer_disconnect == NULL) {
        panic("msc_init: cannot create timer_disconnect\n");
    }

    msgbuff_target_writer_thread = xMessageBufferCreate(TARGET_WRITER_THREAD_MSGBUFF_SIZE);
    if (msgbuff_target_writer_thread == NULL) {
        panic("msc_init: cannot create msgbuff_target_writer_thread\n");
    }
    if (xTaskCreate(target_writer_thread, "MSC Writer", configMINIMAL_STACK_SIZE,
                    NULL, task_prio, &task_target_writer_thread) != pdPASS) {
        panic("msc_init: cannot create task_target_writer_thread\n");
    }
}   // msc_init
