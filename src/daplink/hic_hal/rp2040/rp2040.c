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

#include "FreeRTOS.h"
#include "task.h"


#define NVIC_Addr    (0xe000e000)
#define DBG_Addr     (0xe000edf0)
#include "debug_cm.h"

// Use the CMSIS-Core definition if available.
#if !defined(SCB_AIRCR_PRIGROUP_Pos)
	#define SCB_AIRCR_PRIGROUP_Pos              8U                                            /*!< SCB AIRCR: PRIGROUP Position */
	#define SCB_AIRCR_PRIGROUP_Msk             (7UL << SCB_AIRCR_PRIGROUP_Pos)                /*!< SCB AIRCR: PRIGROUP Mask */
#endif


//
// Control/Status Register Defines
//
#define SWDERRORS           (STICKYORUN|STICKYCMP|STICKYERR)

#define CHECK_OK_BOOL(func) { bool ok = func; if ( !ok) return false; }


const uint32_t  soft_reset = SYSRESETREQ;

// Core will point at whichever one is current...
static uint8_t core;



void osDelay(uint32_t ticks)
{
    vTaskDelay(10 * ticks);
}   // osDelay



/*************************************************************************************************/

/// taken from pico_debug and output of pyODC
static void swd_from_dormant(void)
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
}   // swd_from_dormant


/// taken from pico_debug and output of pyODC
static void swd_line_reset(void)
{
    const uint8_t reset_seq[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03};

    cdc_debug_printf("---swd_line_reset()\n");

    SWJ_Sequence( 52, reset_seq);
}   // swd_line_reset


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
}   // swd_targetsel


/**
 * @brief Use the rescue dp to perform a hardware reset
 *
 * @return true -> ok
 */
static bool dp_rescue_reset()
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
        return false;
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
    return true;
}   // dp_rescue_reset


/**
 * @brief Does the basic core select and then reads DP_IDCODE as required
 *
 * @param num
 * @return true -> ok
 */
static bool dp_core_select(uint8_t core)
{
    uint32_t rv;

	cdc_debug_printf("---dp_core_select(%u)\n", core);

	swd_line_reset();
    swd_targetsel(core);

    CHECK_OK_BOOL(swd_read_dp(DP_IDCODE, &rv));
    cdc_debug_printf("---  id(%u)=0x%08lx\n", core, rv);
    return true;
}   // dp_core_select


/**
 * @brief Select the core, but also make sure we can properly read
 *        from it. Used in the initialisation routine.
 *
 * @param num
 * @return true -> ok
 */
static bool dp_core_select_and_confirm(uint8_t core)
{
    uint32_t rv;

	cdc_debug_printf("---dp_core_select_and_confirm(%u)\n", core);

	CHECK_OK_BOOL(dp_core_select(core));
    CHECK_OK_BOOL(swd_clear_errors());
    CHECK_OK_BOOL(swd_write_dp(DP_SELECT, 0));
    CHECK_OK_BOOL(swd_read_dp(DP_CTRL_STAT, &rv));

    return true;
}   // dp_core_select_and_confirm


/**
 * @brief Do everything we need to be able to utilise to the AP's
 *
 * This powers on the needed subdomains so that we can access the
 * other AP's.
 *
 * @return true -> ok
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
        return true;
    }
    return false;
}   // dp_power_on


static const uint32_t bp_reg[4] = { 0xE0002008, 0xE000200C, 0xE0002010, 0xE0002014 };

static bool core_enable_debug()
{
	cdc_debug_printf("---core_enable_debug()\n");

	// Enable debug
    CHECK_OK_BOOL(swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN));

    // Clear each of the breakpoints...
    for (int i = 0;  i < 4;  ++i) {
        CHECK_OK_BOOL(swd_write_word(bp_reg[i], 0));
    }
    return true;
}   // core_enable_debug


static bool core_select(uint8_t num)
{
	cdc_debug_printf("---core_select(%u)\n", num);

	// See if we are already selected...
    if (core == num)
    	return true;

    CHECK_OK_BOOL(dp_core_select(num));

    // Need to switch the core here for dp_read to work...
    core = num;
    return true;
}   // core_select


/**
 * @brief Send the required sequence to reset the line and start SWD ops
 *
 * This routine needs to try to connect to each core and make sure it
 * responds, it also powers up the relevant bits and sets debug enabled.
 */
static bool dp_initialize(void)
{
	cdc_debug_printf("---dp_initialize()\n");

	core = 0xff;

    swd_from_dormant();

    int have_reset = 0;

    // Now try to connect to each core and setup power and debug status...
    for (int c = 0; c < 2; c++) {
        while (1) {
            if ( !dp_core_select_and_confirm(c)) {
                if ( !dp_core_select_and_confirm(c)) {
                    // If we've already reset, then this is fatal...
                    if (have_reset)
                        return false;
                    dp_rescue_reset();
                    swd_from_dormant();     // seem to need this?
                    have_reset = 1;
                    continue;
                }
            }

            // Make sure we can use dp_xxx calls...
            core = c;
#if 0
            if ( !dp_power_on())
                continue;

#if 1
            // Now we can enable debugging... (and remove breakpoints)
            if ( !core_enable_debug())
                continue;
#endif
#endif

            // If we get here, then this core is fine...
            break;
        }
    }

    // And lets make sure we end on core 0
    if ( !core_select(0)) {
        return false;
    }

    return true;
}   // dp_initialize


/*************************************************************************************************/


#define CHECK_ABORT(COND)          if ( !(COND)) { do_abort = 1; continue; }
#define CHECK_ABORT_BREAK(COND)    if ( !(COND)) { do_abort = 1; break; }

/**
 * Try very hard to initialize the target processor.
 * Code is very similar to the one in swd_host.c except that the JTAG2SWD() sequence is not used.
 *
 * \note
 *    swd_host has to be tricked in it's caching of DP_SELECT and AP_CSW
 */
static uint8_t rp2040_swd_init_debug(void)
{
    uint32_t tmp = 0;
    int i = 0;
    const int timeout = 100;
    int8_t retries = 4;
    int8_t do_abort = 0;

    do {
		cdc_debug_printf("rp2040_swd_init_debug - 0 %d\n", do_abort);
        if (do_abort) {
            //do an abort on stale target, then reset the device
            swd_write_dp(DP_ABORT, DAPABORT);
            swd_set_target_reset(1);
            osDelay(2);
            swd_set_target_reset(0);
            osDelay(2);
            do_abort = 0;
        }
        swd_init();

        // call a target dependant function
        // this function can do several stuff before really
        // initing the debug
        if (g_target_family && g_target_family->target_before_init_debug) {
            g_target_family->target_before_init_debug();
        }

        CHECK_ABORT( swd_clear_errors() );

		CHECK_ABORT( swd_write_dp(DP_SELECT, 1) );                             // force dap_state.select to "0"
		CHECK_ABORT( swd_write_dp(DP_SELECT, 0) );

        // Power up
		CHECK_ABORT( swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ) );

        for (i = 0; i < timeout; i++) {
            CHECK_ABORT_BREAK( swd_read_dp(DP_CTRL_STAT, &tmp));
            if ((tmp & (CDBGPWRUPACK | CSYSPWRUPACK)) == (CDBGPWRUPACK | CSYSPWRUPACK)) {
                // Break from loop if powerup is complete
                break;
            }
        }
        CHECK_ABORT( i != timeout  &&  do_abort == 0 );

        CHECK_ABORT( swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ | TRNNORMAL | MASKLANE) );

        CHECK_ABORT( swd_write_ap(AP_CSW, 1) );                                // force dap_state.csw to "0"
        CHECK_ABORT( swd_write_ap(AP_CSW, 0) );

		CHECK_ABORT( swd_read_ap(0xfc, &tmp) );
		CHECK_ABORT( swd_write_dp(DP_SELECT, 0) );

        return 1;

    } while (--retries > 0);

    return 0;
}   // rp2040_swd_init_debug



static uint8_t rp2040_swd_set_target_state(target_state_t state)
{
    uint32_t val;
    int8_t ap_retries = 2;

    cdc_debug_printf("+++++++++++++++ rp2040_swd_set_target_state(%d)\n", state);

    /* Calling swd_init prior to entering RUN state causes operations to fail. */
    if (state != RUN) {
        swd_init();
    }

    switch (state) {
        case RESET_HOLD:
            swd_set_target_reset(1);
            break;

        case RESET_RUN:
            swd_set_target_reset(1);
            osDelay(2);
            swd_set_target_reset(0);
            osDelay(2);

            if (!rp2040_swd_init_debug()) {
                return 0;
            }

            // Power down
            // Per ADIv6 spec. Clear first CSYSPWRUPREQ followed by CDBGPWRUPREQ
            if (!swd_read_dp(DP_CTRL_STAT, &val)) {
                return 0;
            }

            if (!swd_write_dp(DP_CTRL_STAT, val & ~CSYSPWRUPREQ)) {
                return 0;
            }

            // Wait until ACK is deasserted
            do {
                if (!swd_read_dp(DP_CTRL_STAT, &val)) {
                    return 0;
                }
            } while ((val & (CSYSPWRUPACK)) != 0);

            if (!swd_write_dp(DP_CTRL_STAT, val & ~CDBGPWRUPREQ)) {
                return 0;
            }

            // Wait until ACK is deasserted
            do {
                if (!swd_read_dp(DP_CTRL_STAT, &val)) {
                    return 0;
                }
            } while ((val & (CDBGPWRUPACK)) != 0);

            swd_off();
            break;

        case RESET_PROGRAM:
            if (!rp2040_swd_init_debug()) {
                return 0;
            }

            // Enable debug and halt the core (DHCSR <- 0xA05F0003)
            while (swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT) == 0) {
                if ( --ap_retries <=0 ) {
                    return 0;
                }
                // Target is in invalid state?
                swd_set_target_reset(1);
                osDelay(2);
                swd_set_target_reset(0);
                osDelay(2);
            }

            // Wait until core is halted
            do {
                if (!swd_read_word(DBG_HCSR, &val)) {
                    return 0;
                }
            } while ((val & S_HALT) == 0);

            // Enable halt on reset
            if (!swd_write_word(DBG_EMCR, VC_CORERESET)) {
                return 0;
            }

            // Perform a soft reset
            if (!swd_read_word(NVIC_AIRCR, &val)) {
                return 0;
            }

            if (!swd_write_word(NVIC_AIRCR, VECTKEY | (val & SCB_AIRCR_PRIGROUP_Msk) | soft_reset)) {
                return 0;
            }

            osDelay(2);

            do {
                if (!swd_read_word(DBG_HCSR, &val)) {
                    return 0;
                }
            } while ((val & S_HALT) == 0);

            // Disable halt on reset
            if (!swd_write_word(DBG_EMCR, 0)) {
                return 0;
            }

            break;

        case NO_DEBUG:
            if (!swd_write_word(DBG_HCSR, DBGKEY)) {
                return 0;
            }

            break;

        case DEBUG:
            if (!swd_clear_errors()) {
                return 0;
            }

            // Ensure CTRL/STAT register selected in DPBANKSEL
            if (!swd_write_dp(DP_SELECT, 0)) {
                return 0;
            }

            // Power up
            if (!swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ)) {
                return 0;
            }

            // Enable debug
            if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN)) {
                return 0;
            }

            break;

        case HALT:
            if (!rp2040_swd_init_debug()) {
                return 0;
            }

            // Enable debug and halt the core (DHCSR <- 0xA05F0003)
            if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT)) {
                return 0;
            }

            // Wait until core is halted
            do {
                if (!swd_read_word(DBG_HCSR, &val)) {
                    return 0;
                }
            } while ((val & S_HALT) == 0);
            break;

        case RUN:
            if (!swd_write_word(DBG_HCSR, DBGKEY)) {
                return 0;
            }
            swd_off();
            break;

        case POST_FLASH_RESET:
            // This state should be handled in target_reset.c, nothing needs to be done here.
            break;

        default:
            return 0;
    }

    return 1;
}   // rp2040_swd_set_target_state


/*************************************************************************************************/


static void rp2040_swd_set_target_reset(uint8_t asserted)
{
	extern void probe_assert_reset(bool);

    // set HW signal accordingly, asserted means "active"
    cdc_debug_printf("----- rp2040_swd_set_target_reset(%d)\n", asserted);
    probe_assert_reset(asserted);
}   // rp2040_swd_set_target_reset



static uint8_t rp2040_target_set_state(target_state_t state)
{
    cdc_debug_printf("----- rp2040_target_set_state(%d)\n", state);
    return rp2040_swd_set_target_state(state);
}   // rp2040_target_set_state



static void rp2040_target_before_init_debug(void)
{
	int r;

    cdc_debug_printf("----- rp2040_target_before_init_debug()                               BEGIN\n");
    r = dp_initialize();
#if 1
    {
    	uint32_t tmp;
    	swd_read_ap(0xfc, &tmp);
    	swd_write_dp(DP_SELECT, 0);
    }
#endif
    cdc_debug_printf("----- rp2040_target_before_init_debug()                               dp_initialize: %d\n", r);
}   // rp2040_target_before_init_debug



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

static target_cfg_t rp2040_target_device = {
    .version                    = kTargetConfigVersion,
    .sectors_info               = sectors_info,
    .sector_info_length         = (sizeof(sectors_info))/(sizeof(sector_info_t)),
    .flash_regions[0].start     = DAPLINK_ROM_IF_START,
    .flash_regions[0].end       = DAPLINK_ROM_IF_START + DAPLINK_ROM_IF_SIZE,
    .flash_regions[0].flags     = kRegionIsDefault,
    .ram_regions[0].start       = DAPLINK_RAM_APP_START,
    .ram_regions[0].end         = DAPLINK_RAM_APP_START + DAPLINK_RAM_APP_SIZE,
};

const board_info_t g_board_info = {
    .info_version       = kBoardInfoVersion,
    .board_id           = "0000",                // see e.g. https://github.com/pyocd/pyOCD/blob/main/pyocd/board/board_ids.py and https://os.mbed.com/request-board-id
    .daplink_url_name   = "-unknown-",
    .daplink_drive_name = "-unknown-",
    .daplink_target_url = "https://daplink.io",
    .target_cfg         = &rp2040_target_device,
};

static const target_family_descriptor_t g_rp2040_family = {
    .swd_set_target_reset     = &rp2040_swd_set_target_reset,
    .target_set_state         = &rp2040_target_set_state,
	.target_before_init_debug = &rp2040_target_before_init_debug,
};

const target_family_descriptor_t *g_target_family = &g_rp2040_family;

