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
    // PIO offset
    uint offset;
    uint initted;
};

static struct _probe probe;

void probe_set_swclk_freq(uint freq_khz) {
        uint clk_sys_freq_khz = clock_get_hz(clk_sys) / 1000;
        picoprobe_info("Set swclk freq %dKHz sysclk %dkHz\n", freq_khz, clk_sys_freq_khz);
        uint32_t divider = clk_sys_freq_khz / freq_khz / 4;
        if (divider == 0)
            divider = 1;
        pio_sm_set_clkdiv_int_frac(pio0, PROBE_SM, divider, 0);
}

void probe_assert_reset(bool state)
{
#if defined(PROBE_PIN_RESET)
    /* Change the direction to out to drive pin to 0 or to in to emulate open drain */
    gpio_set_dir(PROBE_PIN_RESET, state);
#endif
}

int probe_reset_level(void)
{
#if defined(PROBE_PIN_RESET)
    return gpio_get(PROBE_PIN_RESET);
#else
    return 0;
#endif
}

typedef enum probe_pio_command {
    CMD_WRITE = 0,
    CMD_SKIP,
    CMD_TURNAROUND,
    CMD_READ
} probe_pio_command_t;

static inline uint32_t fmt_probe_command(uint bit_count, bool out_en, probe_pio_command_t cmd) {
    uint cmd_addr =
        cmd == CMD_WRITE      ? probe.offset + probe_offset_write_cmd :
        cmd == CMD_SKIP       ? probe.offset + probe_offset_get_next_cmd :
        cmd == CMD_TURNAROUND ? probe.offset + probe_offset_turnaround_cmd :
                                probe.offset + probe_offset_read_cmd;
    return ((bit_count - 1) & 0xff) | ((uint)out_en << 8) | (cmd_addr << 9);
}

void probe_write_bits(uint bit_count, uint32_t data_byte) {
    DEBUG_PINS_SET(probe_timing, DBG_PIN_WRITE);
    pio_sm_put_blocking(pio0, PROBE_SM, fmt_probe_command(bit_count, true, CMD_WRITE));
    pio_sm_put_blocking(pio0, PROBE_SM, data_byte);
    picoprobe_dump("Write %d bits 0x%x\n", bit_count, data_byte);
    // Return immediately so we can cue up the next command whilst this one runs
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_WRITE);
}

void probe_hiz_clocks(uint bit_count) {
    pio_sm_put_blocking(pio0, PROBE_SM, fmt_probe_command(bit_count, false, CMD_TURNAROUND));
    pio_sm_put_blocking(pio0, PROBE_SM, 0);
}

uint32_t probe_read_bits(uint bit_count) {
    DEBUG_PINS_SET(probe_timing, DBG_PIN_READ);
    pio_sm_put_blocking(pio0, PROBE_SM, fmt_probe_command(bit_count, false, CMD_READ));
    uint32_t data = pio_sm_get_blocking(pio0, PROBE_SM);
    uint32_t data_shifted = data;
    if (bit_count < 32) {
        data_shifted = data >> (32 - bit_count);
    }

    picoprobe_dump("Read %d bits 0x%x (shifted 0x%x)\n", bit_count, data, data_shifted);
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_READ);
    return data_shifted;
}

static void probe_wait_idle() {
    pio0->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + PROBE_SM);
    while (!(pio0->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + PROBE_SM))))
        ;
}

void probe_read_mode(void) {
    pio_sm_put_blocking(pio0, PROBE_SM, fmt_probe_command(0, false, CMD_SKIP));
    probe_wait_idle();
}

void probe_write_mode(void) {
    pio_sm_put_blocking(pio0, PROBE_SM, fmt_probe_command(0, true, CMD_SKIP));
    probe_wait_idle();
}

void probe_init() {
    if (!probe.initted) {
        uint offset = pio_add_program(pio0, &probe_program);
        probe.offset = offset;

        pio_sm_config sm_config = probe_program_get_default_config(offset);
        probe_sm_init(&sm_config);
        pio_sm_init(pio0, PROBE_SM, offset, &sm_config);

        // Set up divisor
        probe_set_swclk_freq(1000);

        // Jump SM to command dispatch routine, and enable it
        pio_sm_exec(pio0, PROBE_SM, offset + probe_offset_get_next_cmd);
        pio_sm_set_enabled(pio0, PROBE_SM, 1);
        probe.initted = 1;
    }
}

void probe_deinit(void)
{
  if (probe.initted) {
    probe_read_mode();
    pio_sm_set_enabled(pio0, PROBE_SM, 0);
    pio_remove_program(pio0, &probe_program, probe.offset);
    probe.initted = 0;
  }
}
