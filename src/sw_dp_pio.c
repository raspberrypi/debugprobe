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



/* Slight hack - we're not bitbashing so we need to set baudrate off the DAP's delay cycles.
 * Ideally we don't want calls to udiv everywhere... */
#define MAKE_KHZ(fast, delay) (fast ? 100000 : (CPU_CLOCK / 2000) / (delay * DELAY_SLOW_CYCLES + IO_PORT_WRITE_CYCLES))
volatile uint32_t cached_delay = 0;



// Generate SWJ Sequence
//   count:  sequence bit count
//   data:   pointer to sequence bit data
//   return: none
#if ((DAP_SWD != 0) || (DAP_JTAG != 0))
void SWJ_Sequence(uint32_t count, const uint8_t *data)
{
    uint32_t bits;
    uint32_t n;

    if (DAP_Data.clock_delay != cached_delay) {
        probe_set_swclk_freq(MAKE_KHZ(DAP_Data.fast_clock, DAP_Data.clock_delay));
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
#endif



// Generate SWD Sequence
//   info:   sequence information
//   swdo:   pointer to SWDIO generated data
//   swdi:   pointer to SWDIO captured data
//   return: none
#if (DAP_SWD != 0)
void SWD_Sequence (uint32_t info, const uint8_t *swdo, uint8_t *swdi)
{
    uint32_t bits;
    uint32_t n;

    if (DAP_Data.clock_delay != cached_delay) {
        probe_set_swclk_freq(MAKE_KHZ(DAP_Data.fast_clock, DAP_Data.clock_delay));
        cached_delay = DAP_Data.clock_delay;
    }
    n = info & SWD_SEQUENCE_CLK;
    if (n == 0U) {
        n = 64U;
    }
    bits = n;
    if (info & SWD_SEQUENCE_DIN) {
        //    picoprobe_debug("SWD sequence in, %lu\n", bits);
        probe_read_mode();
        while (n > 0) {
            if (n > 8)
                bits = 8;
            else
                bits = n;
            *swdi++ = probe_read_bits(bits);
            n -= bits;
        }
        probe_write_mode();
    }
    else {
        //    picoprobe_debug("SWD sequence out, %lu\n", bits);
        probe_write_mode();
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
#endif



#if (DAP_SWD != 0)
// SWD Transfer I/O
//   request: A[3:2] RnW APnDP
//   data:    DATA[31:0]
//   return:  ACK[2:0]
uint8_t SWD_Transfer (uint32_t request, uint32_t *data)
{
	uint8_t prq = 0;
	uint8_t ack;
	uint8_t bit;
	uint32_t val = 0;
	uint32_t parity = 0;
	uint32_t n;

	if (DAP_Data.clock_delay != cached_delay) {
		probe_set_swclk_freq(MAKE_KHZ(DAP_Data.fast_clock, DAP_Data.clock_delay));
		cached_delay = DAP_Data.clock_delay;
	}
	//  picoprobe_debug("SWD_transfer(0x%02lx)\n", request);
	/* Generate the request packet */
	prq |= (1 << 0); /* Start Bit */
	for (n = 1; n < 5; n++) {
		bit = (request >> (n - 1)) & 0x1;
		prq |= bit << n;
		parity += bit;
	}
	prq |= (parity & 0x1) << 5; /* Parity Bit */
	prq |= (0 << 6); /* Stop Bit */
	prq |= (1 << 7); /* Park bit */
	probe_write_bits(8, prq);
	//  picoprobe_debug("SWD_transfer(0x%02lx)\n", prq);

	/* Turnaround (ignore read bits) */
	probe_read_mode();

	ack = probe_read_bits(DAP_Data.swd_conf.turnaround + 3);
	ack >>= DAP_Data.swd_conf.turnaround;

	if (ack == DAP_TRANSFER_OK) {
		/* Data transfer phase */
		if (request & DAP_TRANSFER_RnW) {
			/* Read RDATA[0:31] - note probe_read shifts to LSBs */
			val = probe_read_bits(32);
			bit = probe_read_bits(1);
			parity = __builtin_popcount(val);
			if ((parity ^ bit) & 1U) {
				/* Parity error */
				ack = DAP_TRANSFER_ERROR;
			}
			if (data)
				*data = val;
			//      picoprobe_debug("Read %02x ack %02x 0x%08lx parity %01x\n", prq, ack, val, bit);
			/* Turnaround for line idle */
			probe_read_bits(DAP_Data.swd_conf.turnaround);
			probe_write_mode();
		} else {
			/* Turnaround for write */
			probe_read_bits(DAP_Data.swd_conf.turnaround);
			probe_write_mode();

			/* Write WDATA[0:31] */
			val = *data;
			probe_write_bits(32, val);
			parity = __builtin_popcount(val);
			/* Write Parity Bit */
			probe_write_bits(1, parity & 0x1);
			//      picoprobe_debug("write %02x ack %02x 0x%08lx parity %01lx\n", prq, ack, val, parity);
		}
		/* Capture Timestamp */
		if (request & DAP_TRANSFER_TIMESTAMP) {
			DAP_Data.timestamp = time_us_32();
		}

		/* Idle cycles - drive 0 for N clocks */
		if (DAP_Data.transfer.idle_cycles) {
			for (n = DAP_Data.transfer.idle_cycles; n; ) {
				if (n > 32) {
					probe_write_bits(32, 0);
					n -= 32;
				} else {
					probe_write_bits(n, 0);
					n -= n;
				}
			}
		}

#if 0
		// debugging, note that bits are reversed
		if (prq == 0x81) {
			picoprobe_debug("SWD_transfer - DP write ABORT 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0x87) {
			picoprobe_debug("SWD_transfer - AP read CSW = 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0x8b) {
			picoprobe_debug("SWD_transfer - AP write TAR 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0x8d) {
			picoprobe_debug("SWD_transfer - DP read CTRL/STAT = 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0x95) {
			picoprobe_debug("SWD_transfer - DP read RESEND = 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0x9f) {
			picoprobe_debug("SWD_transfer - AP read DRW = 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xa3) {
			picoprobe_debug("SWD_transfer - AP write CSW 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xa5) {
			picoprobe_debug("SWD_transfer - DP read DPIDR = 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xa9) {
			picoprobe_debug("SWD_transfer - DP write CTRL/STAT 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xaf) {
			picoprobe_debug("SWD_transfer - AP read TAR = 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xb1) {
			picoprobe_debug("SWD_transfer - DP write SELECT 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xbb) {
			picoprobe_debug("SWD_transfer - AP write DRW 0x%lx (%d)\n", *data, ack);
		}
		else if (prq == 0xbd) {
			picoprobe_debug("SWD_transfer - DP read buffer = 0x%lx (%d)\n", *data, ack);
		}
		else {
			if (request & DAP_TRANSFER_RnW) {
				picoprobe_debug("SWD_transfer - unknown write: 0x%02x 0x%lx (%d)\n", prq, *data, ack);
			}
			else {
				picoprobe_debug("SWD_transfer - unknown read: 0x%02x 0x%lx (%d)\n", prq, *data, ack);
			}
		}
#endif

		return ((uint8_t)ack);
	}

#if 1
	if (ack != DAP_TRANSFER_WAIT) {
	    if (request & DAP_TRANSFER_RnW) {
			picoprobe_debug("SWD_transfer - unknown FAILED read: 0x%02x (%d)\n", prq, ack);
		}
	    else {
	        picoprobe_debug("SWD_transfer - unknown FAILED write: 0x%02x 0x%lx (%d)\n", prq, *data, ack);
	    }
	}
#endif

	if ((ack == DAP_TRANSFER_WAIT) || (ack == DAP_TRANSFER_FAULT)) {
		if (DAP_Data.swd_conf.data_phase && ((request & DAP_TRANSFER_RnW) != 0U)) {
			/* Dummy Read RDATA[0:31] + Parity */
			probe_read_bits(33);
		}
		probe_read_bits(DAP_Data.swd_conf.turnaround);
		probe_write_mode();
		if (DAP_Data.swd_conf.data_phase && ((request & DAP_TRANSFER_RnW) == 0U)) {
			/* Dummy Write WDATA[0:31] + Parity */
			probe_write_bits(32, 0);
			probe_write_bits(1, 0);
		}
		return ((uint8_t)ack);
	}

	/* Protocol error */
	n = DAP_Data.swd_conf.turnaround + 32U + 1U;
	/* Back off data phase */
	probe_read_bits(n);
	probe_write_mode();
	return ((uint8_t)ack);
}   // SWD_Transfer
#endif  /* (DAP_SWD != 0) */



void SWx_Configure(void)
{
	DAP_Data.transfer.idle_cycles = 2;
	DAP_Data.transfer.retry_count = 80;
	DAP_Data.transfer.match_retry = 0;

	DAP_Data.swd_conf.turnaround  = 0;
	DAP_Data.swd_conf.data_phase  = 0;
}   // SWx_Configure
