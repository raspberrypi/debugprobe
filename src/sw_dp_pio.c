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
#define MAKE_KHZ(x) (CPU_CLOCK / (2000 * ((x) + 1)))

// Generate SWJ Sequence
//   count:  sequence bit count
//   data:   pointer to sequence bit data
//   return: none
#if ((DAP_SWD != 0) || (DAP_JTAG != 0))
void SWJ_Sequence (uint32_t count, const uint8_t *data) {
  uint32_t bits;
  uint32_t n;

  probe_set_swclk_freq(MAKE_KHZ(DAP_Data.clock_delay));
  picoprobe_info("SWJ sequence count = %d FDB=0x%2x\n", count, data[0]);
  n = count;
  while (n > 0) {
    if (n > 8)
      bits = 8;
    else
      bits = n;
    probe_write_bits(bits, *data++);
    n -= bits;
  }
}
#endif

// Generate SWD Sequence
//   info:   sequence information
//   swdo:   pointer to SWDIO generated data
//   swdi:   pointer to SWDIO captured data
//   return: none
#if (DAP_SWD != 0)
void SWD_Sequence (uint32_t info, const uint8_t *swdo, uint8_t *swdi) {
  uint32_t bits;
  uint32_t n;

  probe_set_swclk_freq(MAKE_KHZ(DAP_Data.clock_delay));
  picoprobe_info("SWD sequence\n");
  n = info & SWD_SEQUENCE_CLK;
  if (n == 0U) {
    n = 64U;
  }
  bits = n;
  if (info & SWD_SEQUENCE_DIN) {
    while (n > 0) {
      if (n > 8)
        bits = 8;
      else
        bits = n;
      *swdi++ = probe_read_bits(bits);
      n -= bits;
    }
  } else {
    while (n > 0) {
      if (n > 8)
        bits = 8;
      else
        bits = n;
      probe_write_bits(bits, *swdo++);
      n -= bits;
    }
  }
}
#endif

#if (DAP_SWD != 0)
// SWD Transfer I/O
//   request: A[3:2] RnW APnDP
//   data:    DATA[31:0]
//   return:  ACK[2:0]
uint8_t SWD_Transfer (uint32_t request, uint32_t *data) {
  uint8_t prq = 0;
  uint8_t ack;
  uint8_t bit;
  uint32_t val = 0;
  uint32_t parity = 0;
  uint32_t n;

  probe_set_swclk_freq(MAKE_KHZ(DAP_Data.clock_delay));
  picoprobe_info("SWD_transfer\n");
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

  /* Turnaround (ignore read bits) */
  probe_read_mode();
  probe_read_bits(DAP_Data.swd_conf.turnaround);

  ack = probe_read_bits(3);

  if (ack == DAP_TRANSFER_OK) {
    /* Data transfer phase */
    if (request & DAP_TRANSFER_RnW) {
      parity = 0;
      /* Read RDATA[0:31] - note probe_read shifts to LSBs */
      for (n = 0; n < 32; n += 8) {
        bit = probe_read_bits(8);
        parity += __builtin_popcount(bit);
        val |= (bit & 0xff) << n;
      }
      bit = probe_read_bits(1);
      if ((parity ^ bit) & 1U) {
        /* Parity error */
        ack = DAP_TRANSFER_ERROR;
      }
      if (data)
        *data = val;

      /* Turnaround for line idle */
      probe_read_bits(DAP_Data.swd_conf.turnaround);
      probe_write_mode();
    } else {
      /* Turnaround for write */
      probe_read_bits(DAP_Data.swd_conf.turnaround);
      probe_write_mode();

      /* Write WDATA[0:31] */
      val = *data;
      parity = 0;
      for (n = 0; n < 32; n += 8) {
        bit = (val >> n) & 0xff;
        probe_write_bits(8, bit);
        parity += __builtin_popcount(bit);
      }
      /* Write Parity Bit */
      probe_write_bits(1, parity & 0x1);
    }
    /* Capture Timestamp */
    if (request & DAP_TRANSFER_TIMESTAMP) {
      DAP_Data.timestamp = time_us_32();
    }

    /* Idle cycles - drive 0 for N clocks */
    if (DAP_Data.transfer.idle_cycles) {
      for (n = DAP_Data.transfer.idle_cycles; n; ) {
        if (n > 8) {
          probe_write_bits(8, 0);
          n -= 8;
        } else {
          probe_write_bits(n, 0);
          n -= n;
        }
      }
    }
    return ((uint8_t)ack);
  }

  if ((ack == DAP_TRANSFER_WAIT) || (ack == DAP_TRANSFER_FAULT)) {
    probe_read_bits(DAP_Data.swd_conf.turnaround);
    probe_write_mode();
    return ((uint8_t)ack);
  }

  /* Protocol error */
  n = DAP_Data.swd_conf.turnaround + 32U + 1U;
  /* Back off data phase */
  probe_read_bits(n);
  probe_write_mode();
  return ((uint8_t)ack);
}

#endif  /* (DAP_SWD != 0) */