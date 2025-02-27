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

// notes:
// - DBG_HCSR -> DHCSR - Debug Halting Control and Status Register
// - DBG_EMCR -> DEMCR - Debug Exception and Monitor Control Register

// Use the CMSIS-Core definition if available.
#if !defined(SCB_AIRCR_PRIGROUP_Pos)
    #define SCB_AIRCR_PRIGROUP_Pos              8U                                            /*!< SCB AIRCR: PRIGROUP Position */
    #define SCB_AIRCR_PRIGROUP_Msk             (7UL << SCB_AIRCR_PRIGROUP_Pos)                /*!< SCB AIRCR: PRIGROUP Mask */
#endif

#define BIT(nr)                    (1UL << (nr))

// Flash Patch Control Register (breakpoints)
#define FP_CTRL                    0xE0002000
#define FP_CTRL_KEY                BIT(1)
#define FP_CTRL_ENABLE             BIT(0)

// Debug Security Control and Status Register
#define DCB_DSCSR                  0xE000EE08
#define DSCSR_CDSKEY               BIT(17)
#define DSCSR_CDS                  BIT(16)

#define ACCESSCTRL_LOCK_OFFSET     0x40060000u
#define ACCESSCTRL_LOCK_DEBUG_BITS 0x00000008u
#define ACCESSCTRL_CFGRESET_OFFSET 0x40060008u
#define ACCESSCTRL_WRITE_PASSWORD  0xacce0000u

extern target_family_descriptor_t g_raspberry_rp2350_family;
static const uint32_t soft_reset = SYSRESETREQ;



//
// Control/Status Register Defines
//
#define SWDERRORS           (STICKYORUN|STICKYCMP|STICKYERR)

#define CHECK_OK_BOOL(func) { bool ok = func; if ( !ok) return false; }


// Core will point at whichever one is current...
static uint8_t core;


/*************************************************************************************************/


static void swd_from_dormant(void)
/**
 * Wake up SWD.
 * Taken from RP2350 datasheet, "3.5.1 Connecting to the SW-DP"
 */
{
    const uint8_t ones_seq[]            = {0xff};
    const uint8_t selection_alert_seq[] = {0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19};
    const uint8_t zero_seq[]            = {0x00};
    const uint8_t act_seq[]             = {0x1a};
    const uint8_t reset_seq[]           = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03};
    uint32_t rv;

//    printf("---swd_from_dormant()\n");

    SWJ_Sequence(  8, ones_seq);
    SWJ_Sequence(128, selection_alert_seq);
    SWJ_Sequence(  4, zero_seq);
    SWJ_Sequence(  8, act_seq);
    SWJ_Sequence( 52, reset_seq);

    swd_read_dp(DP_IDCODE, &rv);
//    printf("---  id(%u)=0x%08lx\n", core, rv);   // 0x4c013477 is the RP2350
}   // swd_from_dormant


/**
 * @brief Does the basic core select and then reads      as required
 *
 * See also ADIv6.0 specification, "B4.3.4 Target selection protocol, SWD protocol version 2"
 * @param _core
 * @return true -> ok
 */
static bool dp_core_select(uint8_t _core)
{
//    printf("---dp_core_select(%u)\n", _core);

    if (core == _core) {
        return true;
    }

    g_raspberry_rp2350_family.apsel = 0x2d00;       // TODO where from is this xd00 ?  taken from openocd
    if (_core == 1) {
        g_raspberry_rp2350_family.apsel = 0x4d00;
    }

#if 0
    CHECK_OK_BOOL(swd_read_dp(DP_IDCODE, &rv));
    printf("---  id(%u)=0x%08lx\n", _core, rv);   // 0x4c013477 is the RP2350
    CHECK_OK_BOOL(swd_read_dp(DP_IDCODE, &rv));
    printf("---  id(%u)=0x%08lx\n", _core, rv);   // 0x4c013477 is the RP2350
#endif

    core = _core;
    return true;
}   // dp_core_select


// TODO desperate tries to read ROM table
#define xDUMP_ROM_TABLES
#ifdef DUMP_ROM_TABLES
static uint32_t cnt;
static void dump_rom_tables_ap(uint32_t apsel, uint32_t offs, uint32_t len)
{
    uint32_t apsel_save = g_raspberry_rp2350_family.apsel;

    if (apsel == 0  &&  offs == 0)
        ++cnt;
    if (cnt != 5)
        return;

    osDelay(100);
    printf("----------------------------------- 0x%08lx\n", apsel);
    g_raspberry_rp2350_family.apsel = apsel;

    for (uint32_t n = 0;  n < len;  n += 4) {
        uint32_t mem;
        uint8_t r;

        osDelay(1);

        r = swd_read_ap(offs + n, &mem);
//        r = swd_read_word(offs + n, &mem);
//        r = swd_read_dp(n, &mem);

        printf("     0x%04lx:0x%08lx (%d)", offs + n, mem, r);
        if ((n & 0x0f) == 0x0c)
            printf("\n");
    }
    printf("\n");

    g_raspberry_rp2350_family.apsel = apsel_save;
}   // dump_rom_tables_ap


static void dump_rom_tables(uint32_t apsel, uint32_t offs, uint32_t len)
{
    uint32_t apsel_save = g_raspberry_rp2350_family.apsel;

    if (cnt != 5)
        return;

    osDelay(100);
    printf("----------------------------------- 0x%08lx\n", apsel);
    g_raspberry_rp2350_family.apsel = apsel;

#if 1
    for (uint32_t n = 0;  n < len;  n += 4) {
        uint32_t mem;
        uint8_t r;

        r = swd_read_word(offs + n, &mem);

        printf("     0x%04lx:0x%08lx (%d)", offs + n, mem, r);
        if ((n & 0x0f) == 0x0c) {
            osDelay(10);
            printf("\n");
        }
    }
    printf("\n");
#else
    uint8_t buf[256];

    swd_read_memory(offs, buf, sizeof(buf));
    for (uint32_t n = 0;  n < sizeof(buf);  ++n) {
        if ((n & 0x0f) == 0) {
            printf("\n    0x%08lx:", offs + n);
        }
        printf(" %02x", buf[n]);
    }
    printf("\n");
#endif

    g_raspberry_rp2350_family.apsel = apsel_save;
}   // dump_rom_tables
#endif


/**
 * Disable HW breakpoints.
 * \pre
 *    DP must be powered on
 */
static bool dp_disable_breakpoints()
{
    return swd_write_word(FP_CTRL, FP_CTRL_KEY);
}   // dp_disable_breakpoints


static bool rp2350_init_accessctrl(void)
/**
 * Attempt to reset ACCESSCTRL, in case Secure access to SRAM has been
 * blocked, which will stop us from loading/running algorithms such as RCP
 * init. (Also ROM, QMI regs are needed later)
 *
 * More or less taken from https://github.com/raspberrypi/openocd/blob/sdk-2.0.0/src/flash/nor/rp2040.c
 */
{
    uint32_t accessctrl_lock_reg;

    if ( !swd_read_word(ACCESSCTRL_LOCK_OFFSET, &accessctrl_lock_reg)) {
        picoprobe_error("Failed to read ACCESSCTRL lock register");
        // Failed to read an APB register which should always be readable from
        // any security/privilege level. Something fundamental is wrong. E.g.:
        //
        // - The debugger is attempting to perform Secure bus accesses on a
        //   system where Secure debug has been disabled
        // - clk_sys or busfabric clock are stopped (try doing a rescue reset)
        return false;
    }

    picoprobe_debug("ACCESSCTRL_LOCK:  %08lx\n", accessctrl_lock_reg);

    if (accessctrl_lock_reg & ACCESSCTRL_LOCK_DEBUG_BITS) {
        picoprobe_error("ACCESSCTRL is locked, so can't reset permissions. Following steps might fail.\n");
    }
    else {
        picoprobe_debug("Reset ACCESSCTRL permissions via CFGRESET\n");
        return swd_write_word(ACCESSCTRL_CFGRESET_OFFSET, ACCESSCTRL_WRITE_PASSWORD | 1u);
    }
    return true;
}   // rp2350_init_accessctrl


static void rp2350_init_arm_core0(void)
/**
 * Flash algorithms (and the RCP init stub called by this function) must
 * run in the Secure state, so flip the state now before attempting to
 * execute any code on the core.
 *
 * \note
 *    Currently no init code is executed...
 *
 * Parts taken from https://github.com/raspberrypi/openocd/blob/sdk-2.0.0/src/flash/nor/rp2040.c
 */
{
    uint32_t dscsr;

    (void)swd_read_word(DCB_DSCSR, &dscsr);
    picoprobe_debug("DSCSR:  %08lx\n", dscsr);
    if ( !(dscsr & DSCSR_CDS)) {
        picoprobe_info("Setting Current Domain Secure in DSCSR\n");
        (void)swd_write_word(DCB_DSCSR, (dscsr & ~DSCSR_CDSKEY) | DSCSR_CDS);
        (void)swd_read_word(DCB_DSCSR, &dscsr);
        picoprobe_info("DSCSR*: %08lx\n", dscsr);
    }
}


/*************************************************************************************************/


#define CHECK_ABORT(COND)          if ( !(COND)) { do_abort = true; continue; }
#define CHECK_ABORT_BREAK(COND)    if ( !(COND)) { do_abort = true; break; }

static bool rp2350_swd_init_debug(uint8_t core)
/**
 * Try very hard to initialize the target processor.
 * Code is very similar to the one in swd_host.c except that the JTAG2SWD() sequence is not used.
 *
 * \note
 *    swd_host has to be tricked in it's caching of DP_SELECT and AP_CSW
 */
{
    uint32_t tmp = 0;
    int i = 0;
    const int timeout = 100;
    int8_t retries = 4;
    bool do_abort = false;

//    printf("rp2350_swd_init_debug(%d)\n", core);

    swd_init();
    swd_from_dormant();

    do {
//        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        if (do_abort) {
            // do an abort on stale target, then reset the device
            swd_write_dp(DP_ABORT, DAPABORT);
            swd_set_target_reset(1);
            osDelay(2);
            swd_set_target_reset(0);
            osDelay(2);
            do_abort = false;
        }

#if 0
// required or not!?
        if (core == 0) {
            CHECK_ABORT( rp2350_init_accessctrl() );
            rp2350_init_arm_core0();
        }
#endif

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

        CHECK_ABORT( swd_read_ap(AP_IDR, &tmp) );                                // AP IDR: must it be 0x34770008?
//        printf("##########1 0x%08lx\n", tmp);
        CHECK_ABORT( swd_read_ap(AP_ROM, &tmp) );                                // AP ROM: must it be 0xe00ff003?
//        printf("##########2 0x%08lx\n", tmp);
        CHECK_ABORT( swd_write_dp(DP_SELECT, 0) );

#ifdef DUMP_ROM_TABLES
        // obtain some info about the ROM table
        // openocd -f interface/cmsis-dap.cfg -f ./rp2350.cfg -c "init; dap info"
        dump_rom_tables_ap(0x00000000, 0x00000000, 256);
        dump_rom_tables_ap(0x00000000, 0xe0002003, 256);
        dump_rom_tables(0x00002d00, 0x00002fd0, 256);
#endif

        return true;

    } while (--retries > 0);

    return false;
}   // rp2350_swd_init_debug



/**
 * Set state of a single core, the core will be selected as well.
 * \return true -> ok
 *
 * \note
 *    - the current (hardware) reset operation does a reset of both cores
 *    -
 */
static bool rp2350_swd_set_target_state(uint8_t core, target_state_t state)
{
    uint32_t val;
    int8_t ap_retries = 2;

//    printf("+++++++++++++++ rp2350_swd_set_target_state(%d, %d)\n", core, state);

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

            if (!rp2350_swd_init_debug(core)) {
                return false;
            }

            // reset C_HALT (required for RP2350)
            if (!swd_write_word(DBG_HCSR, DBGKEY)) {
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
            if (!rp2350_swd_init_debug(core)) {
                return false;
            }

            // Enable debug and halt the core (DHCSR <- 0xA05F0003)
            while (swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT) == 0) {
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

            if ( !dp_disable_breakpoints()) {
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
            if (!rp2350_swd_init_debug(core)) {
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
            if (!rp2350_swd_init_debug(core)) {
                return false;
            }
            break;

        default:
            return false;
    }

    return true;
}   // rp2350_swd_set_target_state


/*************************************************************************************************/


static void rp2350_swd_set_target_reset(uint8_t asserted)
{
    extern void probe_reset_pin_set(uint32_t);

    // set HW signal accordingly, asserted means "active"
//    printf("----- rp2350_swd_set_target_reset(%d)\n", asserted);
    probe_reset_pin_set(asserted ? 0 : 1);
}   // rp2350_swd_set_target_reset



/**
 * Set state of the RP2350.
 * Currently core1 is held most of the time in HALT, so that it does not disturb operation.
 *
 * \note
 *    Take care, that core0 is the selected core at end of function
 */
static uint8_t rp2350_target_set_state(target_state_t state)
{
    uint8_t r = false;

//    printf("---------------------------------------------- rp2350_target_set_state(%d)\n", state);

    switch (state) {
        case RESET_HOLD:
            // Hold target in reset
            // pre: -
            r = rp2350_swd_set_target_state(0, RESET_HOLD);
            // post: both cores are in HW reset
            break;

        case RESET_PROGRAM:
            // Reset target and setup for flash programming
            // pre: -
            rp2350_swd_set_target_state(1, HALT);
            r = rp2350_swd_set_target_state(0, RESET_PROGRAM);
            // post: core1 in HALT, core0 ready for programming
            break;

        case RESET_RUN:
            // Reset target and run normally
            // pre: -
            r = rp2350_swd_set_target_state(1, RESET_RUN)  &&  rp2350_swd_set_target_state(0, RESET_RUN);
            swd_off();
            // post: both cores are running
            break;

        case NO_DEBUG:
            // Disable debug on running target
            // pre: !swd_off()  &&  core0 selected
            r = rp2350_swd_set_target_state(0, NO_DEBUG);
            // post: core0 in NO_DEBUG
            break;

        case DEBUG:
            // Enable debug on running target
            // pre: !swd_off()  &&  core0 selected
            r = rp2350_swd_set_target_state(0, DEBUG);
            // post: core0 in DEBUG
            break;

        case HALT:
            // Halt the target without resetting it
            // pre: -
            r = rp2350_swd_set_target_state(1, HALT)  &&  rp2350_swd_set_target_state(0, HALT);
            // post: both cores in HALT
            break;

        case RUN:
            // Resume the target without resetting it
            // pre: -
            r = rp2350_swd_set_target_state(1, RUN)  &&  rp2350_swd_set_target_state(0, RUN);
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
            r = rp2350_swd_set_target_state(1, ATTACH)  &&  rp2350_swd_set_target_state(0, ATTACH);
            break;

        default:
            r = false;
            break;
    }

    return r;
}   // rp2350_target_set_state


//----------------------------------------------------------------------------------------------------------------------

target_family_descriptor_t g_raspberry_rp2350_family = {
    .family_id                = TARGET_RP2350_FAMILY_ID,
    .swd_set_target_reset     = &rp2350_swd_set_target_reset,
    .target_set_state         = &rp2350_target_set_state,
    .apsel = 0x2d00
};
