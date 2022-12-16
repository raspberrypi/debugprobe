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

#include "daplink_addr.h"
#include "swd_host.h"
#include "target_board.h"
#include "target_family.h"

#include "cmsis_os2.h"

#define DBG_Addr     (0xe000edf0)
#include "debug_cm.h"

#include "FreeRTOS.h"
#include "task.h"



//
// Debug Port Register Addresses
//
#define DP_DPIDR                        0x00U   // IDCODE Register (RD)
#define DP_ABORT                        0x00U   // Abort Register (WR)
#define DP_CTRL_STAT                    0x04U   // Control & Status
#define DP_RESEND                       0x08U   // Resend (RD)
#define DP_SELECT                       0x08U   // Select Register (WR)
#define DP_RDBUFF                       0x0CU   // Read Buffer (RD)
#define DP_TARGETSEL                    0x0CU   // Read Buffer (WR)

#define DP_DLCR                         0x14    // (RW)
#define DP_TARGETID                     0x24    // Target ID (RD)
#define DP_DLPIDR                       0x34    // (RD)
#define DP_EVENTSTAT                    0x44    // (RO)

//
// Control/Status Register Defines
//
//#define STICKYORUN          (1<<1)
//#define STICKYCMP           (1<<4)
//#define STICKYERR           (1<<5)
#define SWDERRORS           (STICKYORUN|STICKYCMP|STICKYERR)

#define DCB_DHCSR           0xE000EDF0


// DBGSWENABLE, AHB_MASTER_DEBUG, HPROT1, no-auto-inc, need to add size...
#define AP_MEM_CSW_SINGLE     (1 << 31) \
                            | (1 << 29) \
                            | (1 << 25) \
                            | (0 << 4)

#define AP_MEM_CSW_32       0b010

#define AP_MEM_CSW          0x00
#define AP_MEM_TAR          0x04
#define AP_MEM_DRW          0x0C


//
// Abort Register Defines
//
#define DAP_ABORT           (1<<0)
//#define STKCMPCLR           (1<<1)
//#define STKERRCLR           (1<<2)
//#define WDERRCLR            (1<<3)
//#define ORUNERRCLR          (1<<4)
#define ALLERRCLR           (STKCMPCLR|STKERRCLR|WDERRCLR|ORUNERRCLR)

#define SWD_OK              0
#define SWD_WAIT            1
#define SWD_ERROR           3
#define CHECK_OK(func)      { int rc = func; if (rc != SWD_OK) return rc; }


struct core {
    int                 state;
    int                 reason;

    uint32_t            dp_select_cache;
    uint32_t            ap_mem_csw_cache;

    uint32_t            breakpoints[4];
//    struct reg          reg_cache[24];
};


struct core cores[2];

// Core will point at whichever one is current...
struct core *core = &cores[0];


static void swd_targetsel(uint8_t core);
static int core_enable_debug(void);



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
#if 0
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


///// taken from output of pyODC
//static uint8_t swd_read_idcode(uint32_t *id)
//{
//    return swd_read_dp(DP_IDCODE, id);
//}


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


static int swd_read(int APnDP, int addr, uint32_t *result)
{
	uint8_t sts = 0;

	cdc_debug_printf("---swd_read(%d, %d, .)\n", APnDP, addr);

	for (int i = 0;  sts == 0  &&  i < 10;  ++i) {
		if (APnDP != 0) {
			sts = swd_read_ap(addr, result);
		}
		else {
			sts = swd_read_dp(addr, result);
		}
	}
	return (sts != 0) ? SWD_OK : SWD_ERROR;
}   // swd_read


static int swd_write(int APnDP, int addr, uint32_t value)
{
	uint8_t sts = 0;

	cdc_debug_printf("---swd_write(%d, %d, 0x%lx)\n", APnDP, addr, value);

	for (int i = 0;  sts == 0  &&  i < 10;  ++i) {
		if (APnDP != 0) {
			sts = swd_write_ap(addr, value);
		}
		else {
			sts = swd_write_dp(addr, value);
		}
	}
	return (sts != 0) ? SWD_OK : SWD_ERROR;
}   // swd_write


static inline int dp_select_bank(int bank)
{
    int rc = SWD_OK;


	assert(bank <= 0xf);

    if ((core->dp_select_cache & 0xf) != bank) {
		cdc_debug_printf("---dp_select_bank(%d)\n", bank);
        core->dp_select_cache = (core->dp_select_cache & 0xfffffff0) | bank;
        rc = swd_write(0, DP_SELECT, core->dp_select_cache);
    }
    return rc;
}   // dp_select_bank


static int dp_read(uint32_t addr, uint32_t *res)
{
    int rc;

	cdc_debug_printf("---dp_read(%lu, .)\n", addr);

	// First check to see if we are reading something where we might
    // care about the dp_banksel
    if ((addr & 0x0f) == 4) {
        rc = dp_select_bank((addr & 0xf0) >> 4);
        if (rc != SWD_OK)
        	return rc;
    }
    return swd_read(0, addr & 0xf, res);
}   // dp_read


static int dp_write(uint32_t addr, uint32_t value)
{
    int rc;

	cdc_debug_printf("---dp_write(%lu, 0x%lx)\n", addr, value);

	// First check to see if we are writing something where we might
    // care about the dp_banksel
    if ((addr & 0x0f) == 4) {
        rc = dp_select_bank((addr & 0xf0) >> 4);
        if (rc != SWD_OK)
        	return rc;
    }
    return swd_write(0, addr & 0xf, value);
}   // dp_write


/**
 * @brief Select the AP and bank if we need to (note bank is bits 4-7)
 *
 * @param ap
 * @param bank
 * @return int
 */
static inline int ap_select_with_bank(uint ap, uint bank)
{
    int rc = SWD_OK;


	assert((bank & 0x0f) == 0);
    assert(bank <= 255);
    assert(ap <= 255);

    if ((ap != (core->dp_select_cache >> 24)) || (bank != (core->dp_select_cache & 0xf0))) {
		cdc_debug_printf("---ap_select_with_bank(%u, %u)\n", ap, bank);
        core->dp_select_cache = (ap << 24) | bank | (core->dp_select_cache & 0xf);
        rc = swd_write(0, DP_SELECT, core->dp_select_cache);
    }
    return rc;
}   // ap_select_with_bank


/**
 * @brief Write a value to a given AP
 *
 * @param apnum
 * @param addr
 * @param value
 * @return int
 */
int ap_write(int apnum, uint32_t addr, uint32_t value)
{
    int rc;

	cdc_debug_printf("---ap_write(%d, %lu, 0x%lx)\n", apnum, addr, value);

	// Select the AP and bank (if needed)
    rc = ap_select_with_bank(apnum, addr & 0xf0);
    if (rc != SWD_OK)
    	return rc;

    // Now kick off the write (addr[3:2])...
    rc = swd_write(1, addr & 0xc, value);
    return rc;
}   // ap_write


/**
 * @brief Update the memory csw if we need to
 *
 * @param value
 */
static inline int ap_mem_set_csw(uint32_t value) {
//    static uint32_t ap_mem_csw_cache = 0xffffffff;
    int rc = SWD_OK;


	if (core->ap_mem_csw_cache != value) {
		cdc_debug_printf("---ap_mem_set_csw(0x%lx)\n", value);
        core->ap_mem_csw_cache = value;
        rc = ap_write(0, AP_MEM_CSW, value);
    }
    return rc;
}   // ap_mem_set_csw


int mem_write32(uint32_t addr, uint32_t value)
{
	cdc_debug_printf("---mem_write32(0x%lx, 0x%lx)\n", addr, value);

	CHECK_OK(ap_mem_set_csw(AP_MEM_CSW_SINGLE | AP_MEM_CSW_32));
    CHECK_OK(ap_write(0, AP_MEM_TAR, addr));
    return ap_write(0, AP_MEM_DRW, value);
}   // mem_write32


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
    rc = swd_read(0, DP_DPIDR, &rv);
    if (rc != SWD_OK) {
        cdc_debug_printf("---rescue failed (DP_IDR read rc=%d)\n", rc);
        return rc;
    }

    // Now toggle the power request which will cause the reset...
    rc = swd_write(0, DP_CTRL_STAT, CDBGPWRUPREQ);
    cdc_debug_printf("---RESET rc=%d\n", rc);
    rc = swd_write(0, DP_CTRL_STAT, 0);
    cdc_debug_printf("---RESET rc=%d\n", rc);

    // Make sure the write completes...
//    swd_send_bits((uint32_t *)zero, 8);
    SWD_Sequence(8, zero, NULL);

    // And delay a bit... no idea how long we need, but we need something.
    for (int i=0; i < 2; i++) {
//        swd_send_bits((uint32_t *)zero, 32);
        SWD_Sequence(32, zero, NULL);
    }
    return SWD_OK;
}   // dp_rescue_reset


/**
 * @brief Does the basic core select and then reads DP_DPIDR as required
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

    CHECK_OK(swd_read(0, DP_DPIDR, &rv));
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
    CHECK_OK(swd_write(0, DP_ABORT, ALLERRCLR));
    CHECK_OK(swd_write(0, DP_SELECT, 0));
    CHECK_OK(swd_read(0, DP_CTRL_STAT, &rv));

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
        if (dp_write(DP_CTRL_STAT, CDBGPWRUPREQ|CSYSPWRUPREQ) != SWD_OK) continue;
        if (dp_read(DP_CTRL_STAT, &rv) != SWD_OK) continue;
        if (rv & SWDERRORS) { dp_write(DP_ABORT, ALLERRCLR); continue; }
        if (!(rv & CDBGPWRUPACK)) continue;
        if (!(rv & CSYSPWRUPACK)) continue;
        return SWD_OK;
    }
    return SWD_ERROR;
}   // dp_power_on


static const uint32_t bp_reg[4] = { 0xE0002008, 0xE000200C, 0xE0002010, 0xE0002014 };

static int core_enable_debug()
{
	cdc_debug_printf("---core_enable_debug()\n");

	// Enable debug
    CHECK_OK(mem_write32(DBG_HCSR, DBGKEY | C_DEBUGEN));

    // Clear each of the breakpoints...
    for (int i=0; i < 4; i++) {
        CHECK_OK(mem_write32(bp_reg[i], 0));
    }
    return SWD_OK;
}   // core_enable_debug


static int core_select(uint8_t num)
{
    uint32_t dlpidr = 0;

	cdc_debug_printf("---core_select(%u)\n", num);

	// See if we are already selected...
    if (core == &cores[num])
    	return SWD_OK;

    CHECK_OK(dp_core_select(num));

    // Need to switch the core here for dp_read to work...
    core = &cores[num];

    // The core_select above will have set some of the SELECT bits to zero
    core->dp_select_cache &= 0xfffffff0;

    // If that was ok we can validate the switch by checking the TINSTANCE part of
    // DLPIDR
    CHECK_OK(dp_read(DP_DLPIDR, &dlpidr));

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

	core = NULL;

    swd_from_dormant();

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
            core = &cores[c];
            core->dp_select_cache = 0;
            if (dp_power_on() != SWD_OK) 
                continue;

            // Now we can enable debugging... (and remove breakpoints)
            if (core_enable_debug() != SWD_OK)
                continue;

            // If we get here, then this core is fine...
            break;
        }
    }
    // And lets make sure we end on core 0
    if (dp_core_select(0) != SWD_OK) {
        return SWD_ERROR;
    }
    core = &cores[0];

    return SWD_OK;
}   // dp_initialize


/*************************************************************************************************/



extern void probe_assert_reset(bool);

void rp2040_swd_set_target_reset(uint8_t asserted)
{
    // TODO set HW signal accordingly, asserted means "active"
    cdc_debug_printf("----- rp2040_swd_set_target_reset(%d)\n", asserted);
    probe_assert_reset(asserted);
}

void rp2040_prerun_board_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_board_config()\n");
}

void board_bootloader_init(void)
{
    cdc_debug_printf("----- board_bootloader_init()\n");
}

uint8_t rp2040_target_set_state(target_state_t state)
{
    cdc_debug_printf("----- rp2040_target_set_state(%d)\n", state);
    return swd_set_target_state_hw(state);
}

void rp2040_target_before_init_debug(void)
{
	int r;

    cdc_debug_printf("----- rp2040_target_before_init_debug()\n");
    r = dp_initialize();
    cdc_debug_printf("----- rp2040_target_before_init_debug() - dp_initialize: %d\n", r);
    r = core_select(0);
    cdc_debug_printf("----- rp2040_target_before_init_debug() - core_select: %d\n", r);
}

void rp2040_prerun_target_config(void)
{
    cdc_debug_printf("----- rp2040_prerun_target_config()\n");
}

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
};

const target_family_descriptor_t *g_target_family = &g_rp2040_family;

