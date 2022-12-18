/**
 * @file    rp2040.c
 * @brief   board ID for the Raspberry Pi Pico board
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2009-2019, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DAP_config.h"
#include "DAP.h"

#include "daplink_addr.h"
#include "swd_host.h"
#include "target_board.h"
#include "target_family.h"

#include "cmsis_os2.h"

#define DBG_Addr     (0xe000edf0)
#include "debug_cm.h"

#include "FreeRTOS.h"
#include "task.h"



#define DP_DLPIDR                       0x34    // (RD)

//
// Control/Status Register Defines
//
#define SWDERRORS           (STICKYORUN|STICKYCMP|STICKYERR)


#define SWD_OK              0
#define SWD_WAIT            1
#define SWD_ERROR           3

#define CHECK_OK(func)      { int rc = func; if (rc != SWD_OK) return rc; }
#define CHECK_OK_BOOL(func) { bool ok = func; if ( !ok) return SWD_ERROR; }


// Core will point at whichever one is current...
static uint8_t core;



void osDelay(uint32_t ticks)
{
    vTaskDelay(10 * ticks);
}   // osDelay



#if 0
static void swd_set_target_reset_nrf(uint8_t asserted)
{
    uint32_t ap_index_return;

    if (asserted) {
        swd_init_debug();

        swd_read_ap(0x010000FC, &ap_index_return);
        if (ap_index_return == 0x02880000) {
            // Have CTRL-AP
            swd_write_ap(0x01000000, 1);  // CTRL-AP reset hold
        }
        else {
            // No CTRL-AP - Perform a soft reset
            // 0x05FA0000 = VECTKEY, 0x4 = SYSRESETREQ
            uint32_t swd_mem_write_data = 0x05FA0000 | 0x4;
            swd_write_memory(0xE000ED0C, (uint8_t *) &swd_mem_write_data, 4);
        }
        if(g_board_info.swd_set_target_reset){ //aditional reset
            g_board_info.swd_set_target_reset(asserted);
        }
    } else {
        swd_read_ap(0x010000FC, &ap_index_return);
        if (ap_index_return == 0x02880000) {
            // Device has CTRL-AP
            swd_write_ap(0x01000000, 0);  // CTRL-AP reset release
        }
        else {
            // No CTRL-AP - Soft reset has been performed
        }
        if(g_board_info.swd_set_target_reset){
            g_board_info.swd_set_target_reset(asserted);
        }
    }
}

const target_family_descriptor_t g_nordic_nrf52 = {
    .family_id = kNordic_Nrf52_FamilyID,
    .default_reset_type = kSoftwareReset,
    .soft_reset_type = SYSRESETREQ,
    .swd_set_target_reset = swd_set_target_reset_nrf,
};
#endif


/**
* List of start and size for each size of flash sector
* The size will apply to all sectors between the listed address and the next address
* in the list.
* The last pair in the list will have sectors starting at that address and ending
* at address start + size.
*/
static const sector_info_t sectors_info[] = {
    {DAPLINK_ROM_IF_START, DAPLINK_SECTOR_SIZE},
 };

// k26f target information
target_cfg_t rp2040_target_device = {
    .version                    = kTargetConfigVersion,
    .sectors_info               = sectors_info,
    .sector_info_length         = (sizeof(sectors_info))/(sizeof(sector_info_t)),
    .flash_regions[0].start     = DAPLINK_ROM_IF_START,
    .flash_regions[0].end       = DAPLINK_ROM_IF_START + DAPLINK_ROM_IF_SIZE,
    .flash_regions[0].flags     = kRegionIsDefault,
    .ram_regions[0].start       = DAPLINK_RAM_APP_START,
    .ram_regions[0].end         = DAPLINK_RAM_APP_START + DAPLINK_RAM_APP_SIZE,
    /* .flash_algo not needed for bootloader */
};



/*************************************************************************************************/

/// taken from pico_debug and output of pyODC
static uint8_t swd_from_dormant(void)
{
    const uint8_t ones_seq[] = {0xff};
    const uint8_t zero_seq[] = {0x00};
    const uint8_t selection_alert_seq[] = {0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19};
    const uint8_t act_seq[] = { 0x1a };

	cdc_debug_printf("---swd_from_dormant()\n");

	SWJ_Sequence(  8, ones_seq);
    SWJ_Sequence(128, selection_alert_seq);
    SWJ_Sequence(  4, zero_seq);
    SWJ_Sequence(  8, act_seq);
    return SWD_OK;
}


/// taken from pico_debug and output of pyODC
static uint8_t swd_line_reset(void)
{
#if 1
    const uint8_t reset_seq[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03};

    cdc_debug_printf("---swd_line_reset()\n");

    SWJ_Sequence( 52, reset_seq);
#elif 1
    const uint8_t reset_seq_0[] = {0x00};
    const uint8_t reset_seq_1[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    cdc_debug_printf("---swd_line_reset() - alternative\n");

    SWJ_Sequence( 51, reset_seq_1);
    SWJ_Sequence(  2, reset_seq_0);
#else
    cdc_debug_printf("---swd_line_reset() - alternative 2\n");
    JTAG2SWD();
#endif
    return SWD_OK;
}


static void swd_targetsel(uint8_t core)
{
    static const uint8_t out1[]        = {0x99};
    static const uint8_t core_0[]      = {0x27, 0x29, 0x00, 0x01, 0x00};
    static const uint8_t core_1[]      = {0x27, 0x29, 0x00, 0x11, 0x01};
    static const uint8_t core_rescue[] = {0x27, 0x29, 0x00, 0xf1, 0x00};
    static const uint8_t out2[]        = {0x00};
    static uint8_t input;

	cdc_debug_printf("---swd_targetsel(%u)\n", core);

	SWD_Sequence(8, out1, NULL);
    SWD_Sequence(0x80 + 5, NULL, &input);
    if (core == 0)
        SWD_Sequence(33, core_0, NULL);
    else if (core == 1)
        SWD_Sequence(33, core_1, NULL);
    else
        SWD_Sequence(33, core_rescue, NULL);
    SWD_Sequence(2, out2, NULL);
}


/**
 * @brief Use the rescue dp to perform a hardware reset
 *
 * @return int
 */
static int dp_rescue_reset()
{
    int rc;
    uint32_t rv;
    static const uint8_t zero[] = { 0, 0, 0, 0 };

    cdc_debug_printf("---dp_rescue_reset()\n");

    swd_from_dormant();
    swd_line_reset();
    swd_targetsel(0xff);
    rc = swd_read_dp(DP_IDCODE, &rv);
    if ( !rc) {
        cdc_debug_printf("---rescue failed (DP_IDR read rc=%d)\n", rc);
        return SWD_ERROR;
    }

    // Now toggle the power request which will cause the reset...
    rc = swd_write_dp(DP_CTRL_STAT, CDBGPWRUPREQ);
    cdc_debug_printf("---RESET rc=%d\n", rc);
    rc = swd_write_dp(DP_CTRL_STAT, 0);
    cdc_debug_printf("---RESET rc=%d\n", rc);

    // Make sure the write completes...
    SWD_Sequence(8, zero, NULL);

    // And delay a bit... no idea how long we need, but we need something.
    for (int i=0; i < 2; i++) {
        SWD_Sequence(32, zero, NULL);
    }
    return SWD_OK;
}   // dp_rescue_reset


/**
 * @brief Does the basic core select and then reads DP_IDCODE as required
 *
 * @param num
 * @return int
 */
static int dp_core_select(uint8_t core)
{
    uint32_t rv;

	cdc_debug_printf("---dp_core_select(%u)\n", core);

	swd_line_reset();
    swd_targetsel(core);

    CHECK_OK_BOOL(swd_read_dp(DP_IDCODE, &rv));
    cdc_debug_printf("---  id(%u)=0x%08lx\n", core, rv);
    return SWD_OK;
}   // dp_core_select


/**
 * @brief Select the core, but also make sure we can properly read
 *        from it. Used in the initialisation routine.
 *
 * @param num
 * @return int
 */
static int dp_core_select_and_confirm(uint8_t core)
{
    uint32_t rv;

	cdc_debug_printf("---dp_core_select_and_confirm(%u)\n", core);

	CHECK_OK(dp_core_select(core));
    CHECK_OK_BOOL(swd_clear_errors());
    CHECK_OK_BOOL(swd_write_dp(DP_SELECT, 0));
    CHECK_OK_BOOL(swd_read_dp(DP_CTRL_STAT, &rv));

    return SWD_OK;
}   // dp_core_select_and_confirm


/**
 * @brief Do everything we need to be able to utilise to the AP's
 *
 * This powers on the needed subdomains so that we can access the
 * other AP's.
 *
 * @return int
 */
static int dp_power_on()
{
    uint32_t    rv;

	for (int i=0; i < 10; i++) {
		cdc_debug_printf("---dp_power_on() %d\n", i);
        // Attempt to power up...
        if ( !swd_write_dp(DP_CTRL_STAT, CDBGPWRUPREQ|CSYSPWRUPREQ))
        	continue;
        if ( !swd_read_dp(DP_CTRL_STAT, &rv))
        	continue;
        if (rv & SWDERRORS) {
        	swd_clear_errors();
        	continue;
        }
        if ( !(rv & CDBGPWRUPACK))
        	continue;
        if ( !(rv & CSYSPWRUPACK))
        	continue;
        return SWD_OK;
    }
    return SWD_ERROR;
}   // dp_power_on


static const uint32_t bp_reg[4] = { 0xE0002008, 0xE000200C, 0xE0002010, 0xE0002014 };

static int core_enable_debug()
{
	cdc_debug_printf("---core_enable_debug()\n");

	// Enable debug
    CHECK_OK_BOOL(swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN));

    // Clear each of the breakpoints...
    for (int i = 0;  i < 4;  ++i) {
        CHECK_OK_BOOL(swd_write_word(bp_reg[i], 0));
    }
    return SWD_OK;
}   // core_enable_debug


static int core_select(uint8_t num)
{
	cdc_debug_printf("---core_select(%u)\n", num);

	// See if we are already selected...
    if (core == num)
    	return SWD_OK;

    CHECK_OK(dp_core_select(num));

    // Need to switch the core here for dp_read to work...
    core = num;

    // TODO
#if 0
    // If that was ok we can validate the switch by checking the TINSTANCE part of
    // DLPIDR
    {
    	uint32_t dlpidr = 0;

    	CHECK_OK(dp_read(DP_DLPIDR, &dlpidr));
    }
#endif

    // TODO: shouldn't we validate DPIDR with DLPIDR?
    return SWD_OK;
}   // core_select


/**
 * @brief Send the required sequence to reset the line and start SWD ops
 *
 * This routine needs to try to connect to each core and make sure it
 * responds, it also powers up the relevant bits and sets debug enabled.
 */
static int dp_initialize(void)
{
	cdc_debug_printf("---dp_initialize()\n");

	core = 0xff;

    swd_from_dormant();

#if 0
    cdc_debug_printf("JTAG2SWD()\n");
    JTAG2SWD();
    cdc_debug_printf("JTAG2SWD() finished\n");
#endif

#if 1
    int have_reset = 0;

    // Now try to connect to each core and setup power and debug status...
    for (int c = 0; c < 2; c++) {
        while (1) {
            if (dp_core_select_and_confirm(c) != SWD_OK) {
                if (dp_core_select_and_confirm(c) != SWD_OK) {
                    // If we've already reset, then this is fatal...
                    if (have_reset)
                        return SWD_ERROR;
                    dp_rescue_reset();
                    swd_from_dormant();     // seem to need this?
                    have_reset = 1;
                    continue;
                }
            }

            // Make sure we can use dp_xxx calls...
            core = c;
#if 0
            if (dp_power_on() != SWD_OK) 
                continue;

#if 1
            // Now we can enable debugging... (and remove breakpoints)
            if (core_enable_debug() != SWD_OK)
                continue;
#endif
#endif

            // If we get here, then this core is fine...
            break;
        }
    }
#else
    dp_core_select_and_confirm(0);
    core = 0;
    dp_power_on();

#if 1
    cdc_debug_printf("second init....................................\n");
    JTAG2SWD();
#if 0
    swd_clear_errors();
    swd_write_dp(DP_SELECT, 0);
#else
    dp_core_select_and_confirm(0);
#endif
    dp_power_on();
    {
    	uint32_t tmp;
		swd_read_ap(0xfc, &tmp);
    }
#endif
#endif

#if 1
    // And lets make sure we end on core 0
    if (core_select(0) != SWD_OK) {
        return SWD_ERROR;
    }
    core = 0;
#endif

#if 0
    // Now try to read DP_DLIDR (bank 3)
    {
    	uint8_t rc;
    	uint32_t rv;

    	cdc_debug_printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
		rc = swd_write_dp(DP_SELECT, 0x3);
		if (rc != SWD_OK) {
			cdc_debug_printf("rc=%d\r\n", rc);
		}
		rc = swd_read_dp(0x4, &rv);
		cdc_debug_printf("DP_DLIDR rc=%d val=0x%08lx\r\n", rc, rv);
    }
#endif

    return SWD_OK;
}   // dp_initialize


/*************************************************************************************************/


#if 0
extern void probe_assert_reset(bool);

static void rp2040_swd_set_target_reset(uint8_t asserted)
{
    // TODO set HW signal accordingly, asserted means "active"
    cdc_debug_printf("----- rp2040_swd_set_target_reset(%d)\n", asserted);
    probe_assert_reset(asserted);
}
#endif

static void rp2040_prerun_board_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_board_config()\n");
}

void board_bootloader_init(void)
{
    cdc_debug_printf("----- board_bootloader_init()\n");
}

#if 0
static uint8_t rp2040_target_set_state(target_state_t state)
{
    cdc_debug_printf("----- rp2040_target_set_state(%d)\n", state);
    return swd_set_target_state_hw(state);
}
#endif

static void rp2040_target_before_init_debug(void)
{
	int r;

    cdc_debug_printf("----- rp2040_target_before_init_debug()                               BEGIN\n");
    r = dp_initialize();
    {
    	uint32_t tmp;
    	swd_read_ap(0xfc, &tmp);
    	swd_write_dp(DP_SELECT, 0);
    }
    cdc_debug_printf("----- rp2040_target_before_init_debug()                               dp_initialize: %d\n", r);
#if 0
    r = core_select(0);
    cdc_debug_printf("----- rp2040_target_before_init_debug()                               core_select: %d\n", r);
#endif
}

static void rp2040_prerun_target_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_target_config()\n");
}

#if 1
static uint8_t rp2040_target_unlock_sequence(void)
{
    cdc_debug_printf("----- rp2040_target_unlock_sequence()                                 BEGIN\n");
#if 1
    // das funktioniert, wenn in swd_init_debug() auskommentiert wird
    dp_core_select_and_confirm(core);
    dp_power_on();
#else
    // und das hier crasht so richtig
    dp_initialize();
#endif
    cdc_debug_printf("----- rp2040_target_unlock_sequence()                                 END\n");
    return 1;
}   // rp2040_target_unlock_sequence
#endif


const board_info_t g_board_info = {
    .info_version = kBoardInfoVersion,
    .board_id = "0000",
    .daplink_url_name =   "HELP_FAQHTM",
    .daplink_drive_name = "BOOTLOADER",
    .daplink_target_url = "https://daplink.io",
    // .swd_set_target_reset = &rp2040_swd_set_target_reset,
    .prerun_board_config = &rp2040_prerun_board_config,
    .target_cfg = &rp2040_target_device,
};

static const target_family_descriptor_t g_rp2040_family = {
    .family_id = 0,
    .default_reset_type = kSoftwareReset,
    .soft_reset_type = SYSRESETREQ,
//    .swd_set_target_reset = &rp2040_swd_set_target_reset,
    // .target_set_state = &rp2040_target_set_state,
    .target_before_init_debug = &rp2040_target_before_init_debug,
    .prerun_target_config = &rp2040_prerun_target_config,
//	.target_unlock_sequence = &rp2040_target_unlock_sequence,
};

const target_family_descriptor_t *g_target_family = &g_rp2040_family;

