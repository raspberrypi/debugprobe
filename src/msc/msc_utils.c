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

#include <pico/stdlib.h>

#include "msc_utils.h"

#include "target_family.h"
#include "swd_host.h"
#include "DAP_config.h"
#include "DAP.h"
#include "daplink_addr.h"

#define DBG_Addr     (0xe000edf0)
#include "debug_cm.h"


#define DEBUG_MODULE    0

#if 0
	/*
	 * For lowlevel debugging (or better recording), DAP.c can be modified as follows:
	 * (but take care that the output FIFO of cdc_debug_printf() is big enough to hold all output)
	 */
            uint32_t DAP_ProcessCommand(const uint8_t *request, uint8_t *response)
            {
            uint32_t num;

            num = _DAP_ProcessCommand(request, response);

            {
                uint32_t req_len = (num >> 16);
                uint32_t resp_len = (num & 0xffff);
                const char *s;

                switch (*request) {
                case ID_DAP_Info              : s = "ID_DAP_Info              "; break;
                case ID_DAP_HostStatus        : s = "ID_DAP_HostStatus        "; break;
                case ID_DAP_Connect           : s = "ID_DAP_Connect           "; break;    
                case ID_DAP_Disconnect        : s = "ID_DAP_Disconnect        "; break;
                case ID_DAP_TransferConfigure : s = "ID_DAP_TransferConfigure "; break;
                case ID_DAP_Transfer          : s = "ID_DAP_Transfer          "; break;
                case ID_DAP_TransferBlock     : s = "ID_DAP_TransferBlock     "; break;
                case ID_DAP_TransferAbort     : s = "ID_DAP_TransferAbort     "; break;
                case ID_DAP_WriteABORT        : s = "ID_DAP_WriteABORT        "; break;
                case ID_DAP_Delay             : s = "ID_DAP_Delay             "; break;
                case ID_DAP_ResetTarget       : s = "ID_DAP_ResetTarget       "; break;
                case ID_DAP_SWJ_Pins          : s = "ID_DAP_SWJ_Pins          "; break;
                case ID_DAP_SWJ_Clock         : s = "ID_DAP_SWJ_Clock         "; break;
                case ID_DAP_SWJ_Sequence      : s = "ID_DAP_SWJ_Sequence      "; break;
                case ID_DAP_SWD_Configure     : s = "ID_DAP_SWD_Configure     "; break;
                case ID_DAP_SWD_Sequence      : s = "ID_DAP_SWD_Sequence      "; break;
                case ID_DAP_JTAG_Sequence     : s = "ID_DAP_JTAG_Sequence     "; break;
                case ID_DAP_JTAG_Configure    : s = "ID_DAP_JTAG_Configure    "; break;
                case ID_DAP_JTAG_IDCODE       : s = "ID_DAP_JTAG_IDCODE       "; break;
                case ID_DAP_SWO_Transport     : s = "ID_DAP_SWO_Transport     "; break;
                case ID_DAP_SWO_Mode          : s = "ID_DAP_SWO_Mode          "; break;
                case ID_DAP_SWO_Baudrate      : s = "ID_DAP_SWO_Baudrate      "; break;
                case ID_DAP_SWO_Control       : s = "ID_DAP_SWO_Control       "; break;
                case ID_DAP_SWO_Status        : s = "ID_DAP_SWO_Status        "; break;
                case ID_DAP_SWO_ExtendedStatus: s = "ID_DAP_SWO_ExtendedStatus"; break;
                case ID_DAP_SWO_Data          : s = "ID_DAP_SWO_Data          "; break;       
                default                       : s = "unknown                  "; break;
                }

            #if 1
                cdc_debug_printf("    /* len */ %2ld, /* %s */ 0x%02x, ", req_len, s, *request);
                for (uint32_t u = 1;  u < req_len;  ++u) {
                cdc_debug_printf("0x%02x, ", request[u]);
                }
                cdc_debug_printf("\n");
            #endif
            #if 0
                cdc_debug_printf("                                                                                               /* --> len=0x%02lx: ", resp_len);
                for (uint32_t u = 0;  u < MIN(resp_len, 32);  ++u) {
                cdc_debug_printf("0x%02x, ", response[u]);
                }
                cdc_debug_printf(" */\n");
            #endif
            }

            return num;
            }
#endif



// -----------------------------------------------------------------------------------
// THIS CODE IS DESIGNED TO RUN ON THE TARGET AND WILL BE COPIED OVER
// (hence it has it's own section)
// -----------------------------------------------------------------------------------
//
// Memory Map on target for programming:
//
// 0x2000 0000      64K incoming data buffer
// 0x2001 0000      start of code
// 0x2002 0000      stage2 bootloader copy
// 0x2004 0800      top of stack
//


extern char __start_for_target[];
extern char __stop_for_target[];

#define FOR_TARGET_CODE     __attribute__((noinline, section("for_target")))
#define TARGET_CODE         0x20010000
#define TARGET_STACK        0x20030800
#define TARGET_FLASH_BLOCK  ((uint32_t)flash_block - (uint32_t)__start_for_target + TARGET_CODE)
#define TARGET_BOOT2        0x20020000
#define TARGET_BOOT2_SIZE   256
#define TARGET_DATA         0x20000000

#define FLASH_BASE          0x10000000

static bool flash_code_copied = false;

#define rom_hword_as_ptr(rom_address) (void *)(uintptr_t)(*(uint16_t *)rom_address)
#define fn(a, b)        (uint32_t)((b << 8) | a)
typedef void *(*rom_table_lookup_fn)(uint16_t *table, uint32_t code);

typedef void *(*rom_void_fn)(void);
typedef void *(*rom_flash_erase_fn)(uint32_t addr, size_t count, uint32_t block_size, uint8_t block_cmd);
typedef void *(*rom_flash_prog_fn)(uint32_t addr, const uint8_t *data, size_t count);


// pre: flash connected, post: generic XIP active
#define FLASH_RANGE_ERASE(OFFS, CNT, BLKSIZE, CMD)             \
    do {                                                       \
        _flash_exit_xip();                                     \
        _flash_range_erase((OFFS), (CNT), (BLKSIZE), (CMD));   \
        _flash_flush_cache();                                  \
        _flash_enter_cmd_xip();                                \
    } while (0)

// pre: flash connected, post: generic XIP active
#define FLASH_RANGE_PROGRAM(ADDR, DATA, LEN)                   \
    do {                                                       \
        _flash_exit_xip();                                     \
        _flash_range_program((ADDR), (DATA), (LEN));           \
        _flash_flush_cache();                                  \
        _flash_enter_cmd_xip();                                \
    } while (0)

// post: flash connected && fast or generic XIP active
#define FLASH_ENTER_CMD_XIP()                                  \
    do {                                                       \
        _connect_internal_flash();                             \
        _flash_flush_cache();                                  \
        if (*((uint32_t *)TARGET_BOOT2) == 0xffffffff) {       \
            _flash_enter_cmd_xip();                            \
        }                                                      \
        else {                                                 \
            ((void (*)(void))TARGET_BOOT2+1)();                \
        }                                                      \
    } while (0)


///
/// Code should be checked via "arm-none-eabi-objdump -S build/picoprobe.elf"
/// \param addr     \a DAPLINK_ROM_START....  A 64KByte block will be erased if \a addr is on a 64K boundary
/// \param src      pointer to source data
/// \param length   length of data block (256, 512, 1024, 2048 are legal (but unchecked)), packet may not overflow
///                 into next 64K block
/// \return         bit0=1 -> page erased, bit1=1->data flashed, bit31=1->data verify failed
///
/// \note
///    This version is not optimized and depends on order of incoming sectors
///
FOR_TARGET_CODE uint32_t flash_block(uint32_t addr, uint32_t *src, int length)
{
    // Fill in the rom functions...
    rom_table_lookup_fn rom_table_lookup = (rom_table_lookup_fn)rom_hword_as_ptr(0x18);
    uint16_t            *function_table = (uint16_t *)rom_hword_as_ptr(0x14);

    rom_void_fn         _connect_internal_flash = rom_table_lookup(function_table, fn('I', 'F'));
    rom_void_fn         _flash_exit_xip = rom_table_lookup(function_table, fn('E', 'X'));
    rom_flash_erase_fn  _flash_range_erase = rom_table_lookup(function_table, fn('R', 'E'));
    rom_flash_prog_fn   _flash_range_program = rom_table_lookup(function_table, fn('R', 'P'));
    rom_void_fn         _flash_flush_cache = rom_table_lookup(function_table, fn('F', 'C'));
    rom_void_fn         _flash_enter_cmd_xip = rom_table_lookup(function_table, fn('C', 'X'));

    const uint32_t erase_block_size = 0x10000;
    uint32_t offset = addr - DAPLINK_ROM_START;

    uint32_t res = 0;

    // We want to make sure the flash is connected so that we can check its current content
    FLASH_ENTER_CMD_XIP();

    if ((offset & (erase_block_size - 1)) == 0) {
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
            FLASH_RANGE_ERASE(offset, erase_block_size, erase_block_size, 0xD8);     // 64K erase
            res |= 0x0001;
        }
    }

    if (src != NULL  &&  length != 0) {
        FLASH_RANGE_PROGRAM(offset, (uint8_t *)src, length);
        res |= 0x0002;
    }

	FLASH_ENTER_CMD_XIP();

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
}   // flash_block


// -----------------------------------------------------------------------------------



// Read 16-bit word from target memory.
uint8_t swd_read_word16(uint32_t addr, uint16_t *val)
{
	uint8_t v1, v2;

	if ( !swd_read_byte(addr+0, &v1))
		return 0;
	if ( !swd_read_byte(addr+1, &v2))
		return 0;

	*val = ((uint16_t)v2 << 8) + v1;
	return 1;
}   // swd_read_word16



// this is 'M' 'u', 1 (version)
#define BOOTROM_MAGIC 0x01754d
#define BOOTROM_MAGIC_ADDR 0x00000010


uint32_t rp2040_find_rom_func(char ch1, char ch2)
{
	uint16_t tag = (ch2 << 8) | ch1;

	// First read the bootrom magic value...
	uint32_t magic;

	if ( !swd_read_word(BOOTROM_MAGIC_ADDR, &magic))
		return 0;
	if ((magic & 0x00ffffff) != BOOTROM_MAGIC)
		return 0;

	// Now find the start of the table...
	uint16_t v;
	uint32_t tabaddr;
	if ( !swd_read_word16(BOOTROM_MAGIC_ADDR+4, &v))
		return 0;
	tabaddr = v;

	// Now try to find our function...
	uint16_t value;
	do {
		if ( !swd_read_word16(tabaddr, &value))
			return 0;
		if (value == tag) {
			if ( !swd_read_word16(tabaddr+2, &value))
				return 0;
			return (uint32_t)value;
		}
		tabaddr += 4;
	} while (value != 0);
	return 0;
}   // rp2040_find_rom_func



bool rp2040_copy_code(void)
{
    bool rc;

    if ( !flash_code_copied)
    {
        int code_len = (__stop_for_target - __start_for_target);

        picoprobe_info("FLASH: Copying custom flash code to 0x%08x (%d bytes)\r\n", TARGET_CODE, code_len);
        rc = swd_write_memory(TARGET_CODE, (uint8_t *)__start_for_target, code_len);
        if ( !rc)
        	return false;

#if 1
        // this works only if target and probe have the same BOOT2 code
        picoprobe_info("FLASH: Copying BOOT2 code to 0x%08x (%d bytes)\r\n", TARGET_BOOT2, TARGET_BOOT2_SIZE);
        rc = swd_write_memory(TARGET_BOOT2, (uint8_t *)DAPLINK_ROM_START, TARGET_BOOT2_SIZE);
#else
        // TODO this means, that the target function fetches the code from the image (actually this could be done here...)
        rc = swd_write_word(TARGET_BOOT2, 0xffffffff);
#endif
        if ( !rc)
        	return false;

        flash_code_copied = true;
	}
    return true;
}   // rp2040_copy_code



static bool core_is_halted(void)
{
	uint32_t value;

	if ( !swd_read_word(DBG_HCSR, &value))
		return false;
	if (value & S_HALT)
		return true;
	return false;
}   // core_is_halted



static bool core_halt(void)
{
	if ( !swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_MASKINTS | C_HALT)) {
        return false;
    }

    while ( !core_is_halted())
    	;
    return true;
}   // core_halt



static bool core_unhalt(void)
{
    if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN)) {
        return false;
    }
    return true;
}   // core_unhalt



static bool core_unhalt_with_masked_ints(void)
{
    if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_MASKINTS)) {
        return false;
    }
    return true;
}   // core_unhalt_with_masked_ints



#if DEBUG_MODULE
static bool display_reg(uint8_t num)
{
	uint32_t val;
	bool rc;

    rc = swd_read_core_register(num, &val);
    if ( !rc)
    	return rc;
    cdc_debug_printf("xx %d r%d=0x%lx\n", __LINE__, num, val);
    return true;
}   // display_reg
#endif



///
/// Call function on the target device at address \a addr.
/// Arguments are in \a args[] / \a argc, result of the called function (from r0) will be
/// put to \a *result (if != NULL).
///
/// \pre
///    - target MCU must be connected
///    - code must be already uploaded to target
//
bool rp2040_call_function(uint32_t addr, uint32_t args[], int argc, uint32_t *result)
{
    static uint32_t trampoline_addr = 0;  // trampoline is fine to get the return value of the callee
    static uint32_t trampoline_end;

    if ( !core_halt())
    	return false;

    assert(argc <= 4);

    // First get the trampoline address...  (actually not required, because the functions reside in RAM...)
    if (trampoline_addr == 0) {
        trampoline_addr = rp2040_find_rom_func('D', 'T');
        trampoline_end = rp2040_find_rom_func('D', 'E');
        if (trampoline_addr == 0  ||  trampoline_end == 0)
        	return false;
    }

    // Set the registers for the trampoline call...
    // function in r7, args in r0, r1, r2, and r3, end in lr?
    for (int i = 0;  i < argc;  ++i) {
        if ( !swd_write_core_register(i, args[i]))
        	return false;
    }
    if ( !swd_write_core_register(7, addr))
    	return false;

    // Set the stack pointer to something sensible... (MSP)
    if ( !swd_write_core_register(13, TARGET_STACK))
    	return false;

    // Now set the PC to go to our address
    if ( !swd_write_core_register(15, trampoline_addr))
    	return false;

    // Set xPSR for the thumb thingy...
    if ( !swd_write_core_register(16, (1 << 24)))
    	return false;

    if ( !core_halt())
    	return false;

#if DEBUG_MODULE
    for (int i = 0;  i < 18;  ++i)
        display_reg(i);
#endif

    // start execution
    picoprobe_info(".................... execute\n");
    if ( !core_unhalt_with_masked_ints())
    	return false;

    // check status
    {
    	uint32_t status;

		if (!swd_read_dp(DP_CTRL_STAT, &status)) {
			return false;
		}
		if (status & (STICKYERR | WDATAERR)) {
			return false;
		}
    }

    // Wait until core is halted (again)
    {
    	const uint32_t timeout_us = 5000000;
    	bool interrupted = false;
    	uint32_t start_us = time_us_32();

    	while ( !core_is_halted()) {
    		uint32_t dt_us = time_us_32() - start_us;

    		if (dt_us > timeout_us) {
    			core_halt();
    			picoprobe_error("TARGET: ???????????????????? execution timed out after %lu ms\n", dt_us / 1000);
    			interrupted = true;
    		}
    	}
    	if ( !interrupted)
			picoprobe_info("!!!!!!!!!!!!!!!!!!!! execution finished after %lu ms\n", (time_us_32() - start_us) / 1000);
    }

#if DEBUG_MODULE
    for (int i = 0;  i < 18;  ++i)
        display_reg(i);
#endif

    if (result != NULL) {
    	// fetch result of function (r0)
    	if ( !swd_read_core_register(0, result))
    		return false;
    }

    {
    	uint32_t r15;

    	if ( !swd_read_core_register(15, &r15)) {
    		return false;
    	}

		if (r15 != (trampoline_end & 0xfffffffe)) {
			picoprobe_error("rp2040_call_function: invoked target function did not run til end: 0x%0lx != 0x%0lx\n", r15, trampoline_end);
			return false;
		}
    }
    return true;
}   // rp2040_call_function



#define DO_MSC  1

static volatile bool in_function;

bool swd_connect_target(bool write_mode)
{
	if (in_function) {
		picoprobe_info("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! reentered swd_connect_target()\n");
		return false;
	}
	in_function = true;

    static uint64_t last_trigger_us;
    uint64_t now_us = time_us_64();
    bool ok = true;

    if (now_us - last_trigger_us > 1000*1000) {
        last_trigger_us = now_us;
    	picoprobe_info("========================================================================\n");

#if DO_MSC
        ok = target_set_state(RESET_PROGRAM);
        picoprobe_info("---------------------------------- %d\n", ok);

        //
        // simple test to read some memory
        //
        if (0) {
        	uint8_t buff[32];
        	uint8_t r;

        	picoprobe_info("---------------------------------- buff:\n");
        	memset(buff, 0xaa, sizeof(buff));
        	r = swd_read_memory(0x10000000, buff,  sizeof(buff));
        	picoprobe_info("   %u -", r);
        	for (int i = 0;  i < sizeof(buff);  ++i)
        		picoprobe_info(" %02x", buff[i]);
        	picoprobe_info("\n");
        	memset(buff, 0xaa, sizeof(buff));
        	r = swd_read_memory(0x10000000, buff,  sizeof(buff));
        	picoprobe_info("   %u -", r);
        	for (int i = 0;  i < sizeof(buff);  ++i)
        		picoprobe_info(" %02x", buff[i]);
        	picoprobe_info("\n");
            picoprobe_info("----------------------------------\n");
        }

        {
            const uint32_t data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

//        	uint32_t arg[] = {DAPLINK_ROM_START, (uint32_t)NULL, 0};
        	uint32_t arg[] = {DAPLINK_ROM_START, TARGET_DATA, sizeof(data)};
        	uint32_t res;
        	bool rc;

            rp2040_copy_code();
            rc = swd_write_memory(TARGET_DATA, (uint8_t *)data, sizeof(data));
            picoprobe_info("   rc is %d\n", rc);
        	rc = rp2040_call_function(TARGET_FLASH_BLOCK, arg, sizeof(arg) / sizeof(arg[0]), &res);
        	picoprobe_info("   result2 is 0x%lx (%d)\n", res, rc);
        }
#endif

//        ok = target_set_state(RESET_RUN);
    }
    last_trigger_us = now_us;

    in_function = false;

    return ok;
}   // swd_connect_target



void setup_uf2_record(struct uf2_block *uf2, uint32_t target_addr, uint32_t payload_size, uint32_t block_no, uint32_t num_blocks)
{
    uf2->magic_start0 = UF2_MAGIC_START0;
    uf2->magic_start1 = UF2_MAGIC_START1;
    uf2->flags        = UF2_FLAG_FAMILY_ID_PRESENT;
    uf2->target_addr  = target_addr;
    uf2->payload_size = payload_size;
    uf2->block_no     = block_no;
    uf2->num_blocks   = num_blocks;
    uf2->file_size    = RP2040_FAMILY_ID;
    uf2->magic_end    = UF2_MAGIC_END;
}   // setup_uf2_record
