/*
 * Copyright (c) 2013-2022 ARM Limited. All rights reserved.
 * Copyright (c) 2022 Raspberry Pi Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This is a shim between the SW_DP functions and the PIO
 * implementation used for Picoprobe. Instead of calling bitbash functions,
 * hand off the bit sequences to a SM for asynchronous completion.
 */

#include <stdio.h>

#include "DAP_config.h"
#include "DAP.h"
#include "probe.h"



#define MAKE_KHZ(fast, delay) ((fast) ? 100000 : (CPU_CLOCK / 2000) / ((delay) * DELAY_SLOW_CYCLES + IO_PORT_WRITE_CYCLES))
volatile uint32_t cached_delay = 0;



// Generate SWJ Sequence
//   count:  sequence bit count
//   data:   pointer to sequence bit data
//   return: none
void __TIME_CRITICAL_FUNCTION(SWJ_Sequence)(uint32_t count, const uint8_t *data)
{
    uint32_t bits;
    uint32_t n;

    if (DAP_Data.clock_delay != cached_delay) {
        probe_set_swclk_freq_khz(MAKE_KHZ(DAP_Data.fast_clock, DAP_Data.clock_delay), true);
        cached_delay = DAP_Data.clock_delay;
    }
    //  picoprobe_debug("SWJ sequence count = %lu FDB=0x%2x\n", count, data[0]);
    n = count;
    while (n > 0) {
        if (n > 8)
            bits = 8;
        else
            bits = n;
        probe_write_bits(bits, *data++);
        n -= bits;
    }
}   // SWJ_Sequence



// Generate SWD Sequence
//   info:   sequence information
//   swdo:   pointer to SWDIO generated data
//   swdi:   pointer to SWDIO captured data
//   return: none
void __TIME_CRITICAL_FUNCTION(SWD_Sequence)(uint32_t info, const uint8_t *swdo, uint8_t *swdi)
{
    uint32_t bits;
    uint32_t n;

    if (DAP_Data.clock_delay != cached_delay) {
        probe_set_swclk_freq_khz(MAKE_KHZ(DAP_Data.fast_clock, DAP_Data.clock_delay), true);
        cached_delay = DAP_Data.clock_delay;
    }
    n = info & SWD_SEQUENCE_CLK;
    if (n == 0U) {
        n = 64U;
    }
    bits = n;
    if (info & SWD_SEQUENCE_DIN) {
        //    picoprobe_debug("SWD sequence in, %lu\n", bits);
        while (n > 0) {
            if (n > 8)
                bits = 8;
            else
                bits = n;
            *swdi++ = probe_read_bits(bits, true, true);
            n -= bits;
        }
    }
    else {
        //    picoprobe_debug("SWD sequence out, %lu\n", bits);
        while (n > 0) {
            if (n > 8)
                bits = 8;
            else
                bits = n;
            probe_write_bits(bits, *swdo++);
            n -= bits;
        }
    }
}   // SWD_Sequence



/**
 * SWD Transfer I/O.
 *
 * \param request  A[3:2] RnW APnDP
 * \param data     DATA[31:0]
 * \return         ACK[2:0]
 *
 * Sequences are described in "ARM Debug Interface v5 Architecture Specification", "5.3 Serial Wire
 * Debug protocol operation".
 *
 * \pre  SWD in write mode
 * \post SWD in write mode
 *
 * \note
 *    - \a turnaround:  see also Wire Control Register (WCR), legal values 1..4
 *    - \a data_phase:  do a data phase on \a DAP_TRANSFER_WAIT and \a DAP_TRANSFER_FAULT
 *    - \a idle_cycles: number of extra idle cycles after each transfer
 */
uint8_t __TIME_CRITICAL_FUNCTION(SWD_Transfer)(uint32_t request, uint32_t *data)
{
    static const uint8_t prqs[16] = { 0x81, 0xa3, 0xa5, 0x87,
                                      0xa9, 0x8b, 0x8d, 0xaf,
                                      0xb1, 0x93, 0x95, 0xb7,
                                      0x99, 0xbb, 0xbd, 0x9f
                                    };
	uint8_t prq = 0;
	uint8_t ack;

	if (DAP_Data.clock_delay != cached_delay) {
		probe_set_swclk_freq_khz(MAKE_KHZ(DAP_Data.fast_clock, DAP_Data.clock_delay), true);
		cached_delay = DAP_Data.clock_delay;
	}
	//  picoprobe_debug("SWD_transfer(0x%02lx)\n", request);

	prq = prqs[request & 0x0f];

	if (request & DAP_TRANSFER_RnW) {
	    //
	    // read data
	    //
	    probe_write_bits(8, prq);
	    ack = probe_read_bits(3 + DAP_Data.swd_conf.turnaround, true, true) >> DAP_Data.swd_conf.turnaround;
	    if (ack == DAP_TRANSFER_OK) {
	        uint32_t bit;
	        uint32_t parity;
	        uint32_t val;

	        probe_read_bits(32, true, false);
	        probe_read_bits(DAP_Data.swd_conf.turnaround + 1, true, false);

            val = probe_read_bits(32, false, true);
            if (data) {
                *data = val;
            }
            // read parity and turn around
            bit = probe_read_bits(DAP_Data.swd_conf.turnaround + 1, false, true);

            parity = __builtin_popcount(val);
            if ((parity ^ bit) & 1U) {
                /* Parity error */
                ack = DAP_TRANSFER_ERROR;
            }
//            picoprobe_debug("Read %02x ack %02x 0x%08lx parity %01lx\n", prq, ack, val, bit);

            /* Idle cycles - drive 0 for N clocks */
            if (DAP_Data.transfer.idle_cycles != 0) {
                probe_write_bits(DAP_Data.transfer.idle_cycles, 0);
            }
	    }
	}
	else {
	    //
	    // write data
	    //
        probe_write_bits(8, prq);
        ack = probe_read_bits(3 + DAP_Data.swd_conf.turnaround, true, true) >> DAP_Data.swd_conf.turnaround;
	    if (ack == DAP_TRANSFER_OK) {
	        uint32_t parity;

	        probe_read_bits(DAP_Data.swd_conf.turnaround, true, true);

            /* Write WDATA[0:31] */
            probe_write_bits(32, *data);
            parity = __builtin_popcount(*data);
            /* Write Parity Bit */
            probe_write_bits(1, parity & 0x1);
            //      picoprobe_debug("write %02x ack %02x 0x%08lx parity %01lx\n", prq, ack, val, parity);

            /* Idle cycles - drive 0 for N clocks */
            if (DAP_Data.transfer.idle_cycles != 0) {
                probe_write_bits(DAP_Data.transfer.idle_cycles, 0);
            }
	    }
	}
    // post: ((ack == DAP_TRANSFER_OK)  &&  "SWD in write mode")  ||  "SWD in read mode"

    if (ack == DAP_TRANSFER_OK) {
        /* Capture Timestamp */
        if (request & DAP_TRANSFER_TIMESTAMP) {
            DAP_Data.timestamp = time_us_32();
        }
    }
    else if (ack == DAP_TRANSFER_WAIT  ||  ack == DAP_TRANSFER_FAULT) {
        // pre: "SWD in read mode"
		if (DAP_Data.swd_conf.data_phase) {
		    // -> there is always a data phase
		    if ((request & DAP_TRANSFER_RnW) != 0U) {
                /* Dummy Read RDATA[0:31] + Parity */
                probe_read_bits(33, true, true);
                probe_read_bits(DAP_Data.swd_conf.turnaround, true, true);
            }
		    else {
                /* Dummy Write WDATA[0:31] + Parity */
	            probe_read_bits(DAP_Data.swd_conf.turnaround, true, true);

                probe_write_bits(32, 0);
                probe_write_bits(1, 0);
		    }
		}
		else {
            probe_read_bits(DAP_Data.swd_conf.turnaround, true, true);
		}
		// post: cleaned up and "SWD in write mode"
	}
	else /* Protocol error */ {
	    // pre: "SWD in read mode"
	    uint32_t n;

        n = DAP_Data.swd_conf.turnaround + 32U + 1U;
        /* Back off data phase */
        probe_read_bits(n, true, true);
        // post: cleaned up and "SWD in write mode"
	}

    //
    // debugging output
    //
#define DEBUG_LEVEL   0

    if (ack == DAP_TRANSFER_OK) {
        // debugging, note that bits are reversed
#if (DEBUG_LEVEL == 2)
        if (prq == 0x81) {
            printf("SWD_transfer - DP write ABORT 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0x87) {
            printf("SWD_transfer - AP read CSW = 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0x8b) {
            printf("SWD_transfer - AP write TAR 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0x8d) {
            printf("SWD_transfer - DP read CTRL/STAT = 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0x95) {
            printf("SWD_transfer - DP read RESEND = 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0x9f) {
            printf("SWD_transfer - AP read DRW = 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xa3) {
            printf("SWD_transfer - AP write CSW 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xa5) {
            printf("SWD_transfer - DP read DPIDR = 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xa9) {
            printf("SWD_transfer - DP write CTRL/STAT 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xaf) {
            printf("SWD_transfer - AP read TAR = 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xb1) {
            printf("SWD_transfer - DP write SELECT 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xbb) {
            printf("SWD_transfer - AP write DRW 0x%lx (%d)\n", *data, ack);
        }
        else if (prq == 0xbd) {
            printf("SWD_transfer - DP read buffer = 0x%lx (%d)\n", *data, ack);
        }
        else {
            if (request & DAP_TRANSFER_RnW) {
                printf("SWD_transfer - unknown write: 0x%02x 0x%lx (%d)\n", prq, *data, ack);
            }
            else {
                printf("SWD_transfer - unknown read: 0x%02x 0x%lx (%d)\n", prq, *data, ack);
            }
        }
#endif
    }
    else if (ack != DAP_TRANSFER_WAIT) {
#if (DEBUG_LEVEL >= 1)
        if (request & DAP_TRANSFER_RnW) {
            picoprobe_error("SWD_transfer - unknown FAILED read: 0x%02x (%d)\n", prq, ack);
        }
        else {
            picoprobe_error("SWD_transfer - unknown FAILED write: 0x%02x 0x%lx (%d)\n", prq, *data, ack);
        }
#endif
    }

	return (uint8_t)ack;
}   // SWD_Transfer



void SWx_Configure(void)
{
    // idle_cycles is always zero
    // TODO check the several initializations
	DAP_Data.transfer.idle_cycles = 0;
	DAP_Data.transfer.retry_count = 80;
	DAP_Data.transfer.match_retry = 0;

	DAP_Data.swd_conf.turnaround  = 1;
	DAP_Data.swd_conf.data_phase  = 0;
}   // SWx_Configure
