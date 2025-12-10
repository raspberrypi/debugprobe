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
//#include <string.h>
#include <stdio.h>

#include <pico/stdlib.h>

#include "boot/uf2.h"                // this is the Pico variant of the UF2 header

#include "picoprobe_config.h"

#include "msc_utils.h"
#include "sw_lock.h"
#include "led.h"

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "target_board.h"
#include "target_family.h"
#include "swd_host.h"
#include "flash_manager.h"
#include "rp2040/program_flash_msc_rp2040.h"
#include "rp2350/program_flash_msc_rp2350.h"


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
                    flash_manager_uninit();
                }
                target_set_state(RESET_PROGRAM);
                target_set_state(RESET_RUN);
            }
            is_connected = false;
        }
        have_lock = false;
        xSemaphoreGive(sema_swd_in_use);
        sw_unlock(E_SWLOCK_MSC);
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

    if (have_lock  ||  sw_lock(E_SWLOCK_MSC)) {
        xSemaphoreTake(sema_swd_in_use, portMAX_DELAY);
        have_lock = true;
        now_us = time_us_64();
        ok = true;
        if ( !is_connected  ||  now_us - last_trigger_us > 1000*1000) {
            picoprobe_info("=================================== MSC connect target\n");
            led_state(LS_MSC_CONNECTED);

            ok = target_set_state(ATTACH);
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
    for (;;) {
        static struct uf2_block uf2;
        size_t   len;

        len = xMessageBufferReceive(msgbuff_target_writer_thread, &uf2, sizeof(uf2), portMAX_DELAY);
        assert(len == 512);
        transferred_bytes += uf2.payload_size;

//        printf("target_writer_thread(0x%lx, %ld, %ld), %u\n", uf2.target_addr, uf2.block_no, uf2.num_blocks, len);

        xSemaphoreTake(sema_swd_in_use, portMAX_DELAY);

        if (must_initialize) {
            if (USE_RP2040()) {
                if (target_rp2040_msc_copy_flash_code()) {
                    must_initialize = false;
                }
                else {
                    picoprobe_error("target_writer_thread: copy rp2040 code failed\n");
                }
            }
            else if (USE_RP2350()) {
                if (target_rp2350_msc_copy_flash_code()) {
                    must_initialize = false;
                }
                else {
                    picoprobe_error("target_writer_thread: copy rp2350 code failed\n");
                }
            }
            else {
                error_t sts;

//              flash_manager_set_page_erase(false);
                sts = flash_manager_init(flash_intf_target);
                if (sts == ERROR_SUCCESS) {
                    must_initialize = false;
                }
            }
            had_write = true;
        }

        if (USE_RP2040()) {
            target_rp2040_msc_flash(uf2.target_addr, uf2.data, uf2.payload_size);
        }
        else if (USE_RP2350()) {
            target_rp2350_msc_flash(uf2.target_addr, uf2.data, uf2.payload_size);
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
