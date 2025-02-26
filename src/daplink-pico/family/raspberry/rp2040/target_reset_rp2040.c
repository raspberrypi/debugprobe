/* CMSIS-DAP Interface Firmware
 * Copyright (c) 2024 Hardy Griech
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdio.h>
#include "DAP_config.h"
#include "target_family.h"
#include "swd_host.h"
#include "cmsis_os2.h"

#include "raspberry/target_utils_raspberry.h"


#define NVIC_Addr    (0xe000e000)
#define DBG_Addr     (0xe000edf0)
#include "debug_cm.h"

// Use the CMSIS-Core definition if available.
#if !defined(SCB_AIRCR_PRIGROUP_Pos)
    #define SCB_AIRCR_PRIGROUP_Pos              8U                                            /*!< SCB AIRCR: PRIGROUP Position */
    #define SCB_AIRCR_PRIGROUP_Msk             (7UL << SCB_AIRCR_PRIGROUP_Pos)                /*!< SCB AIRCR: PRIGROUP Mask */
#endif


static const uint32_t soft_reset = SYSRESETREQ;



//
// Control/Status Register Defines
//
#define SWDERRORS           (STICKYORUN|STICKYCMP|STICKYERR)

#define CHECK_OK_BOOL(func) { bool ok = func; if ( !ok) return false; }


// Core will point at whichever one is current...
static uint8_t core;


/*************************************************************************************************/


/// taken from pico_debug and output of pyODC
static void swd_from_dormant(void)
{
    const uint8_t ones_seq[] = {0xff};
    const uint8_t selection_alert_seq[] = {0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19};
    const uint8_t zero_seq[] = {0x00};
    const uint8_t act_seq[] = { 0x1a };

//    printf("---swd_from_dormant()\n");

    SWJ_Sequence(  8, ones_seq);
    SWJ_Sequence(128, selection_alert_seq);
    SWJ_Sequence(  4, zero_seq);
    SWJ_Sequence(  8, act_seq);
}   // swd_from_dormant


/// taken from pico_debug and output of pyODC
static void swd_line_reset(void)
{
    const uint8_t reset_seq[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03};

//    printf("---swd_line_reset()\n");

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

//    printf("---swd_targetsel(%u)\n", core);

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
 * @brief Does the basic core select and then reads DP_IDCODE as required
 *
 * See also ADIv5.2 specification, "B4.3.4 Target selection protocol, SWD protocol version 2"
 * @param _core
 * @return true -> ok
 */
static bool dp_core_select(uint8_t _core)
{
    uint32_t rv;

//    printf("---dp_core_select(%u)\n", _core);

    if (core == _core) {
        return true;
    }

    swd_line_reset();
    swd_targetsel(_core);

    CHECK_OK_BOOL(swd_read_dp(DP_IDCODE, &rv));
//    printf("---  id(%u)=0x%08lx\n", _core, rv);   // 0x0bc12477 is the RP2040

    core = _core;
    return true;
}   // dp_core_select


/**
 * Clear all HW breakpoints.
 * \pre
 *    DP must be powered on
 */
static bool dp_disable_breakpoint()
{
    static const uint32_t bp_reg[4] = { 0xE0002008, 0xE000200C, 0xE0002010, 0xE0002014 };

//    printf("---dp_disable_breakpoint()\n");

    // Clear each of the breakpoints...
    for (int i = 0;  i < 4;  ++i) {
        CHECK_OK_BOOL(swd_write_word(bp_reg[i], 0));
    }
    return true;
}   // dp_disable_breakpoint


/*************************************************************************************************/


#define CHECK_ABORT(COND)          if ( !(COND)) { do_abort = true; continue; }
#define CHECK_ABORT_BREAK(COND)    if ( !(COND)) { do_abort = true; break; }

/**
 * Try very hard to initialize the target processor.
 * Code is very similar to the one in swd_host.c except that the JTAG2SWD() sequence is not used.
 *
 * \note
 *    swd_host has to be tricked in it's caching of DP_SELECT and AP_CSW
 */
static bool rp2040_swd_init_debug(uint8_t core)
{
    uint32_t tmp = 0;
    int i = 0;
    const int timeout = 100;
    int8_t retries = 4;
    bool do_abort = false;

//    printf("rp2040_swd_init_debug(%d)\n", core);

    swd_init();
    swd_from_dormant();

    do {
        if (do_abort) {
            // do an abort on stale target, then reset the device
            swd_write_dp(DP_ABORT, DAPABORT);
            swd_set_target_reset(1);
            osDelay(2);
            swd_set_target_reset(0);
            osDelay(2);
            do_abort = false;
        }

        CHECK_ABORT( dp_core_select(core) );

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

        CHECK_ABORT( swd_read_ap(AP_IDR, &tmp) );                                // AP IDR: must it be 0x4770031?
        CHECK_ABORT( swd_write_dp(DP_SELECT, 0) );

        return true;

    } while (--retries > 0);

    return false;
}   // rp2040_swd_init_debug



/**
 * Set state of a single core, the core will be selected as well.
 * \return true -> ok
 *
 * \note
 *    - the current (hardware) reset operation does a reset of both cores
 *    -
 */
static bool rp2040_swd_set_target_state(uint8_t core, target_state_t state)
{
    uint32_t val;
    int8_t ap_retries = 2;

//    printf("+++++++++++++++ rp2040_swd_set_target_state(%d, %d)\n", core, state);

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

            if (!rp2040_swd_init_debug(core)) {
                return false;
            }

            // Power down
            // Per ADIv6 spec. Clear first CSYSPWRUPREQ followed by CDBGPWRUPREQ
            if (!swd_read_dp(DP_CTRL_STAT, &val)) {
                return false;
            }

            if (!swd_write_dp(DP_CTRL_STAT, val & ~CSYSPWRUPREQ)) {
                return false;
            }

            // Wait until ACK is deasserted
            do {
                if (!swd_read_dp(DP_CTRL_STAT, &val)) {
                    return false;
                }
            } while ((val & (CSYSPWRUPACK)) != 0);

            if (!swd_write_dp(DP_CTRL_STAT, val & ~CDBGPWRUPREQ)) {
                return false;
            }

            // Wait until ACK is deasserted
            do {
                if (!swd_read_dp(DP_CTRL_STAT, &val)) {
                    return false;
                }
            } while ((val & (CDBGPWRUPACK)) != 0);
            break;

        case RESET_PROGRAM:
            if (!rp2040_swd_init_debug(core)) {
                return false;
            }

            // Enable debug and halt the core (DHCSR <- 0xA05F0003)
            while ( !swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT)) {
                if ( --ap_retries <=0 ) {
                    return false;
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
                    return false;
                }
            } while ((val & S_HALT) == 0);

            if ( !dp_disable_breakpoint()) {
                return false;
            }

            // Enable halt on reset
            if (!swd_write_word(DBG_EMCR, VC_CORERESET)) {
                return false;
            }

            // Perform a soft reset
            if (!swd_read_word(NVIC_AIRCR, &val)) {
                return false;
            }

            if (!swd_write_word(NVIC_AIRCR, VECTKEY | (val & SCB_AIRCR_PRIGROUP_Msk) | soft_reset)) {
                return false;
            }

            osDelay(2);

            do {
                if (!swd_read_word(DBG_HCSR, &val)) {
                    return false;
                }
            } while ((val & S_HALT) == 0);

            // Disable halt on reset
            if (!swd_write_word(DBG_EMCR, 0)) {
                return false;
            }
            break;

        case NO_DEBUG:
            if (!swd_write_word(DBG_HCSR, DBGKEY)) {
                return false;
            }
            break;

        case DEBUG:
            if (!swd_clear_errors()) {
                return false;
            }

            // Ensure CTRL/STAT register selected in DPBANKSEL
            if (!swd_write_dp(DP_SELECT, 0)) {
                return false;
            }

            // Power up
            if (!swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ)) {
                return false;
            }

            // Enable debug
            if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN)) {
                return false;
            }
            break;

        case HALT:
            if (!rp2040_swd_init_debug(core)) {
                return false;
            }

            // Enable debug and halt the core (DHCSR <- 0xA05F0003)
            if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT)) {
                return false;
            }

            // Wait until core is halted
            do {
                if (!swd_read_word(DBG_HCSR, &val)) {
                    return false;
                }
            } while ((val & S_HALT) == 0);
            break;

        case RUN:
            if (!swd_write_word(DBG_HCSR, DBGKEY)) {
                return false;
            }
            break;

        case POST_FLASH_RESET:
            // This state should be handled in target_reset.c, nothing needs to be done here.
            break;

        case ATTACH:
            // attach without doing anything else
            if (!rp2040_swd_init_debug(core)) {
                return false;
            }
            break;

        default:
            return false;
    }

    return true;
}   // rp2040_swd_set_target_state


/*************************************************************************************************/


static void rp2040_swd_set_target_reset(uint8_t asserted)
{
    extern void probe_reset_pin_set(uint32_t);

    // set HW signal accordingly, asserted means "active"
//    printf("----- rp2040_swd_set_target_reset(%d)\n", asserted);
    probe_reset_pin_set(asserted ? 0 : 1);
}   // rp2040_swd_set_target_reset



/**
 * Set state of the RP2040.
 * Currently core1 is held most of the time in HALT, so that it does not disturb operation.
 *
 * \note
 *    Take care, that core0 is the selected core at end of function
 */
static uint8_t rp2040_target_set_state(target_state_t state)
{
    uint8_t r = false;

//    printf("----- rp2040_target_set_state(%d)\n", state);

    switch (state) {
        case RESET_HOLD:
            // Hold target in reset
            // pre: -
            r = rp2040_swd_set_target_state(0, RESET_HOLD);
            // post: both cores are in HW reset
            break;

        case RESET_PROGRAM:
            // Reset target and setup for flash programming
            // pre: -
            rp2040_swd_set_target_state(1, HALT);
            r = rp2040_swd_set_target_state(0, RESET_PROGRAM);
            // post: core1 in HALT, core0 ready for programming
            break;

        case RESET_RUN:
            // Reset target and run normally
            // pre: -
            r = rp2040_swd_set_target_state(1, RESET_RUN)  &&  rp2040_swd_set_target_state(0, RESET_RUN);
            swd_off();
            // post: both cores are running
            break;

        case NO_DEBUG:
            // Disable debug on running target
            // pre: !swd_off()  &&  core0 selected
            r = rp2040_swd_set_target_state(0, NO_DEBUG);
            // post: core0 in NO_DEBUG
            break;

        case DEBUG:
            // Enable debug on running target
            // pre: !swd_off()  &&  core0 selected
            r = rp2040_swd_set_target_state(0, DEBUG);
            // post: core0 in DEBUG
            break;

        case HALT:
            // Halt the target without resetting it
            // pre: -
            r = rp2040_swd_set_target_state(1, HALT)  &&  rp2040_swd_set_target_state(0, HALT);
            // post: both cores in HALT
            break;

        case RUN:
            // Resume the target without resetting it
            // pre: -
            r = rp2040_swd_set_target_state(1, RUN)  &&  rp2040_swd_set_target_state(0, RUN);
            swd_off();
            // post: both cores are running
            break;

        case POST_FLASH_RESET:
            // Reset target after flash programming
            break;

        case POWER_ON:
            // Poweron the target
            break;

        case SHUTDOWN:
            // Poweroff the target
            break;

        case ATTACH:
            r = rp2040_swd_set_target_state(1, ATTACH)  &&  rp2040_swd_set_target_state(0, ATTACH);
            break;

        default:
            r = false;
            break;
    }

    return r;
}   // rp2040_target_set_state


//----------------------------------------------------------------------------------------------------------------------

const target_family_descriptor_t g_raspberry_rp2040_family = {
    .family_id                = TARGET_RP2040_FAMILY_ID,
    .swd_set_target_reset     = &rp2040_swd_set_target_reset,
    .target_set_state         = &rp2040_target_set_state,
};
