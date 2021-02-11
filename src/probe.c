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

#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#include <hardware/clocks.h>
#include <hardware/gpio.h>

#include "led.h"
#include "picoprobe_config.h"
#include "probe.pio.h"
#include "tusb.h"

#define DIV_ROUND_UP(m, n)	(((m) + (n) - 1) / (n))

// Only want to set / clear one gpio per event so go up in powers of 2
enum _dbg_pins {
    DBG_PIN_WRITE = 1,
    DBG_PIN_WRITE_WAIT = 2,
    DBG_PIN_READ = 4,
    DBG_PIN_PKT = 8,
};

CU_REGISTER_DEBUG_PINS(probe_timing)

// Uncomment to enable debug
//CU_SELECT_DEBUG_PINS(probe_timing)

#define PROBE_BUF_SIZE 8192
struct _probe {
    // Total length
    uint tx_len;
    // Data back to host
    uint8_t tx_buf[PROBE_BUF_SIZE];

    // CMD / Data RX'd from
    uint rx_len;
    uint8_t rx_buf[PROBE_BUF_SIZE];

    // PIO offset
    uint offset;
};

static struct _probe probe;

enum PROBE_CMDS {
    PROBE_INVALID      = 0, // Invalid command
    PROBE_WRITE_BITS   = 1, // Host wants us to write bits
    PROBE_READ_BITS    = 2, // Host wants us to read bits
    PROBE_SET_FREQ     = 3, // Set TCK
    PROBE_RESET        = 4, // Reset all state
    PROBE_TARGET_RESET = 5, // Reset target
};

struct __attribute__((__packed__)) probe_cmd_hdr {
	uint8_t id;
    uint8_t cmd;
    uint32_t bits;
};

struct __attribute__((__packed__)) probe_pkt_hdr {
    uint32_t total_packet_length;
};

void probe_set_swclk_freq(uint freq_khz) {
    picoprobe_info("Set swclk freq %dKHz\n", freq_khz);
    uint clk_sys_freq_khz = clock_get_hz(clk_sys) / 1000;
    // Worked out with saleae
    uint32_t divider = clk_sys_freq_khz / freq_khz / 2;
    pio_sm_set_clkdiv_int_frac(pio0, PROBE_SM, divider, 0);
}

static inline void probe_assert_reset(bool state)
{
    /* Change the direction to out to drive pin to 0 or to in to emulate open drain */
    gpio_set_dir(PROBE_PIN_RESET, state);
}

static inline void probe_write_bits(uint bit_count, uint8_t data_byte) {
    DEBUG_PINS_SET(probe_timing, DBG_PIN_WRITE);
    pio_sm_put_blocking(pio0, PROBE_SM, bit_count - 1);
    pio_sm_put_blocking(pio0, PROBE_SM, data_byte);
    DEBUG_PINS_SET(probe_timing, DBG_PIN_WRITE_WAIT);
    picoprobe_dump("Write %d bits 0x%x\n", bit_count, data_byte);
    // Wait for pio to push garbage to rx fifo so we know it has finished sending
    pio_sm_get_blocking(pio0, PROBE_SM);
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_WRITE_WAIT);
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_WRITE);
}

static inline uint8_t probe_read_bits(uint bit_count) {
    DEBUG_PINS_SET(probe_timing, DBG_PIN_READ);
    pio_sm_put_blocking(pio0, PROBE_SM, bit_count - 1);
    uint32_t data = pio_sm_get_blocking(pio0, PROBE_SM);
    uint8_t data_shifted = data >> 24;

    if (bit_count < 8) {
        data_shifted = data_shifted >> 8-bit_count;
    }

    picoprobe_dump("Read %d bits 0x%x (shifted 0x%x)\n", bit_count, data, data_shifted);
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_READ);
    return data_shifted;
}

static void probe_read_mode(void) {
    pio_sm_exec(pio0, PROBE_SM, pio_encode_jmp(probe.offset + probe_offset_in_posedge));
    while(pio0->dbg_padoe & (1 << PROBE_PIN_SWDIO));
}

static void probe_write_mode(void) {
    pio_sm_exec(pio0, PROBE_SM, pio_encode_jmp(probe.offset + probe_offset_out_negedge));
    while(!(pio0->dbg_padoe & (1 << PROBE_PIN_SWDIO)));
}

void probe_init() {
    // Funcsel pins
    pio_gpio_init(pio0, PROBE_PIN_SWCLK);
    pio_gpio_init(pio0, PROBE_PIN_SWDIO);
    // Make sure SWDIO has a pullup on it. Idle state is high
    gpio_pull_up(PROBE_PIN_SWDIO);

    // Target reset pin: pull up, input to emulate open drain pin
    gpio_pull_up(PROBE_PIN_RESET);
    // gpio_init will leave the pin cleared and set as input
    gpio_init(PROBE_PIN_RESET);

    uint offset = pio_add_program(pio0, &probe_program);
    probe.offset = offset;

    pio_sm_config sm_config = probe_program_get_default_config(offset);

    // Set SWCLK as a sideset pin
    sm_config_set_sideset_pins(&sm_config, PROBE_PIN_SWCLK);

    // Set SWDIO offset
    sm_config_set_out_pins(&sm_config, PROBE_PIN_SWDIO, 1);
    sm_config_set_set_pins(&sm_config, PROBE_PIN_SWDIO, 1);
    sm_config_set_in_pins(&sm_config, PROBE_PIN_SWDIO);

    // Set SWD and SWDIO pins as output to start. This will be set in the sm
    pio_sm_set_consecutive_pindirs(pio0, PROBE_SM, PROBE_PIN_OFFSET, 2, true);

    // shift output right, autopull off, autopull threshold
    sm_config_set_out_shift(&sm_config, true, false, 0);
    // shift input right as swd data is lsb first, autopush off
    sm_config_set_in_shift(&sm_config, true, false, 0);

    // Init SM with config
    pio_sm_init(pio0, PROBE_SM, offset, &sm_config);

    // Set up divisor
    probe_set_swclk_freq(1000);

    // Enable SM
    pio_sm_set_enabled(pio0, PROBE_SM, 1);

    // Jump to write program
    probe_write_mode();
}

void probe_handle_read(uint total_bits) {
    picoprobe_debug("Read %d bits\n", total_bits);
    probe_read_mode();

    uint chunk;
    uint bits = total_bits;
    while (bits > 0) {
        if (bits > 8) {
            chunk = 8;
        } else {
            chunk = bits;
        }
        probe.tx_buf[probe.tx_len] = probe_read_bits(chunk);
        probe.tx_len++;
        // Decrement remaining bits
        bits -= chunk;
    }
}

void probe_handle_write(uint8_t *data, uint total_bits) {
    picoprobe_debug("Write %d bits\n", total_bits);

    led_signal_activity(total_bits);

    probe_write_mode();

    uint chunk;
    uint bits = total_bits;
    while (bits > 0) {
        if (bits > 8) {
            chunk = 8;
        } else {
            chunk = bits;
        }

        probe_write_bits(chunk, *data++);
        bits -= chunk;
    }
}

void probe_prepare_read_header(struct probe_cmd_hdr *hdr) {
    // We have a read so need to prefix the data with the cmd header
    if (probe.tx_len == 0) {
        // Reserve some space for probe_pkt_hdr
        probe.tx_len += sizeof(struct probe_pkt_hdr);
    }

    memcpy((void*)&probe.tx_buf[probe.tx_len], hdr, sizeof(struct probe_cmd_hdr));
    probe.tx_len += sizeof(struct probe_cmd_hdr);
}

void probe_handle_pkt(void) {
    uint8_t *pkt = &probe.rx_buf[0] + sizeof(struct probe_pkt_hdr);
    uint remaining = probe.rx_len - sizeof(struct probe_pkt_hdr);

    DEBUG_PINS_SET(probe_timing, DBG_PIN_PKT);

    picoprobe_debug("Processing packet of length %d\n", probe.rx_len);

    probe.tx_len = 0;
    while (remaining) {
        struct probe_cmd_hdr *hdr = (struct probe_cmd_hdr*)pkt;
        uint data_bytes = DIV_ROUND_UP(hdr->bits, 8);
        pkt += sizeof(struct probe_cmd_hdr);
        remaining -= sizeof(struct probe_cmd_hdr);

        if (hdr->cmd == PROBE_WRITE_BITS) {
            uint8_t *data = pkt;
            probe_handle_write(data, hdr->bits);
            pkt += data_bytes;
            remaining -= data_bytes;
        } else if (hdr->cmd == PROBE_READ_BITS) {
            probe_prepare_read_header(hdr);
            probe_handle_read(hdr->bits);
        } else if (hdr->cmd == PROBE_SET_FREQ) {
            probe_set_swclk_freq(hdr->bits);
        } else if (hdr->cmd == PROBE_RESET) {
            // TODO: Is there anything to do after a reset?
            // tx len and rx len should already be 0
            ;
        } else if (hdr->cmd == PROBE_TARGET_RESET) {
            probe_assert_reset(hdr->bits);
        }
    }
    probe.rx_len = 0;

    if (probe.tx_len) {
        // Fill in total packet length before sending
        struct probe_pkt_hdr *tx_hdr = (struct probe_pkt_hdr*)&probe.tx_buf[0];
        tx_hdr->total_packet_length = probe.tx_len;
        tud_vendor_write(&probe.tx_buf[0], probe.tx_len);
        picoprobe_debug("Picoprobe wrote %d response bytes\n", probe.tx_len);
    }
    probe.tx_len = 0;

    DEBUG_PINS_CLR(probe_timing, DBG_PIN_PKT);
}

// USB bits
void probe_task(void) {
    if ( tud_vendor_available() ) {
        uint count = tud_vendor_read(&probe.rx_buf[probe.rx_len], 64);
        if (count == 0) {
            return;
        }
        probe.rx_len += count;
    }

    if (probe.rx_len >= sizeof(struct probe_pkt_hdr)) {
        struct probe_pkt_hdr *pkt_hdr = (struct probe_pkt_hdr*)&probe.rx_buf[0];
        if (pkt_hdr->total_packet_length == probe.rx_len) {
            probe_handle_pkt();
        }
    }
}