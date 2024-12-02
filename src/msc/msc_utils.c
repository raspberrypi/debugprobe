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
#include "target_utils_raspberry.h"


#define DEBUG_MODULE    0

// DAPLink needs bigger buffer
#define TARGET_WRITER_THREAD_MSGBUFF_SIZE   (16 * sizeof(struct uf2_block) + 100)

#define USE_DAPLINK()         (UF2_ID != RP2040_FAMILY_ID)
#define UF2_ID                (g_board_info.target_cfg->rt_uf2_id)
#define UF2_ID_IS_PRESENT()   (UF2_ID != 0)

extern target_cfg_t target_device_rp2040;

// these constants are for range checking on the probe
#define RP2040_FLASH_START    (target_device_rp2040.flash_regions[0].start)
#define RP2040_FLASH_END      (target_device_rp2040.flash_regions[0].end)

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


extern char __start_for_target[];
extern char __stop_for_target[];

#define FOR_TARGET_RP2040_CODE        __attribute__((noinline, section("for_target")))

#define TARGET_RP2040_CODE            (TARGET_RP2040_RAM_START + 0x10000)
#define TARGET_RP2040_FLASH_BLOCK     ((uint32_t)rp2040_flash_block - (uint32_t)__start_for_target + TARGET_RP2040_CODE)
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
    rom_table_lookup_fn rom_table_lookup = (rom_table_lookup_fn)rom_hword_as_ptr(0x18);
    uint16_t            *function_table = (uint16_t *)rom_hword_as_ptr(0x14);

    rom_void_fn         _connect_internal_flash = rom_table_lookup(function_table, fn('I', 'F'));
    rom_void_fn         _flash_exit_xip         = rom_table_lookup(function_table, fn('E', 'X'));
    rom_flash_erase_fn  _flash_range_erase      = rom_table_lookup(function_table, fn('R', 'E'));
    rom_flash_prog_fn   _flash_range_program    = rom_table_lookup(function_table, fn('R', 'P'));
    rom_void_fn         _flash_flush_cache      = rom_table_lookup(function_table, fn('F', 'C'));
    rom_void_fn         _flash_enter_cmd_xip    = rom_table_lookup(function_table, fn('C', 'X'));

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
    int code_len = (__stop_for_target - __start_for_target);

    picoprobe_info("FLASH: Copying custom flash code to 0x%08x (%d bytes)\n", TARGET_RP2040_CODE, code_len);
    if ( !swd_write_memory(TARGET_RP2040_CODE, (uint8_t *)__start_for_target, code_len))
        return false;

    // clear TARGET_RP2040_ERASE_MAP
    for (int i = 0;  i < TARGET_RP2040_ERASE_MAP_SIZE;  i += sizeof(uint32_t)) {
        if ( !swd_write_word(TARGET_RP2040_ERASE_MAP + i, 0)) {
            return false;
        }
    }

    // copy BOOT2 code (TODO make it right)
#if 1
    // this works only if target and probe have the same BOOT2 code
    picoprobe_info("FLASH: Copying BOOT2 code to 0x%08x (%d bytes)\n", TARGET_RP2040_BOOT2, TARGET_RP2040_BOOT2_SIZE);
    if ( !swd_write_memory(TARGET_RP2040_BOOT2, (uint8_t *)RP2040_FLASH_START, TARGET_RP2040_BOOT2_SIZE))
        return false;
#else
    // TODO this means, that the target function fetches the code from the image (actually this could be done here...)
    if ( !swd_write_word(TARGET_RP2040_BOOT2, 0xffffffff))
        return false;
#endif

    return true;
}   // rp2040_target_copy_flash_code



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
            picoprobe_info("=================================== MSC disconnect target\n");
            led_state(LS_MSC_DISCONNECTED);
            if (had_write) {
                if (USE_DAPLINK()) {
                    flash_manager_uninit();
                }
                target_set_state(RESET_RUN);
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
//            picoprobe_debug("---------------------------------- %d\n", ok);

            must_initialize = ok;
            is_connected = true;                   // disconnect must be issued!
            had_write = false;
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
    uf2->file_size    = UF2_ID;
    uf2->magic_end    = UF2_MAGIC_END;
}   // setup_uf2_record



bool msc_is_uf2_record(const void *sector, uint32_t sector_size)
{
    const uint32_t payload_size = 256;
    bool r = false;

    if (sector_size >= sizeof(struct uf2_block)) {
        const struct uf2_block *uf2 = (const struct uf2_block *)sector;

        if (    uf2->magic_start0 == UF2_MAGIC_START0
            &&  uf2->magic_start1 == UF2_MAGIC_START1
            &&  uf2->magic_end == UF2_MAGIC_END
            &&  uf2->block_no < uf2->num_blocks
            &&  uf2->payload_size == payload_size
            &&  uf2->target_addr >= TARGET_FLASH_START
            &&  uf2->target_addr - payload_size * uf2->block_no >= TARGET_FLASH_START             // could underflow
            &&  uf2->target_addr - payload_size * uf2->block_no + payload_size * uf2->num_blocks
                        <= TARGET_FLASH_END) {
            if ((uf2->flags & UF2_FLAG_FAMILY_ID_PRESENT) != 0) {
                if (uf2->file_size == UF2_ID) {
                    r = true;
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

//        picoprobe_info("target_writer_thread(0x%lx, %ld, %ld), %u\n", uf2.target_addr, uf2.block_no, uf2.num_blocks, len);

        xSemaphoreTake(sema_swd_in_use, portMAX_DELAY);

        if (must_initialize) {
            if (USE_DAPLINK()) {
                error_t sts;

//              flash_manager_set_page_erase(false);
                sts = flash_manager_init(flash_intf_target);
//                picoprobe_info("flash_manager_init = %d\n", sts);
                if (sts == ERROR_SUCCESS) {
                    must_initialize = false;
                }
            }
            else {
                bool ok;

                ok = target_set_state(RESET_PROGRAM);
                if (ok) {
                    must_initialize = false;
                    rp2040_target_copy_flash_code();
                }
            }
            had_write = true;
        }

        if (USE_DAPLINK()) {
            flash_manager_data(uf2.target_addr, uf2.data, uf2.payload_size);
        }
        else {
            uint32_t arg[3];
            uint32_t res;

            arg[0] = uf2.target_addr;
            arg[1] = TARGET_RP2040_DATA;
            arg[2] = uf2.payload_size;

//          picoprobe_info("     0x%lx, 0x%lx, 0x%lx, %ld\n", TARGET_RP2040_FLASH_BLOCK, arg[0], arg[1], arg[2]);

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
    picoprobe_debug("msc_init()\n");

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
