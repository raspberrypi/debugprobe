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
#include <stdbool.h>
#include <string.h>

#include <hardware/clocks.h>
#include <hardware/gpio.h>

#include "led.h"
#include "picoprobe_config.h"
#include "probe.pio.h"
#include "tusb.h"

#include "DAP_config.h"
#include "DAP.h"

#include "target_board.h"    // DAPLink



#define CTRL_WORD_WRITE(CNT, DATA)    (((DATA) << 13) + ((CNT) << 8) + (probe.offset + probe_offset_short_output))
#define CTRL_WORD_READ(CNT)           (                 ((CNT) << 8) + (probe.offset + probe_offset_input))



// Only want to set / clear one gpio per event so go up in powers of 2
enum _dbg_pins {
    DBG_PIN_READ  = 1,
    DBG_PIN_WRITE = 2,
    DBG_PIN_WAIT  = 4,
};


CU_REGISTER_DEBUG_PINS(probe_timing)

// Uncomment to enable debug
//CU_SELECT_DEBUG_PINS(probe_timing)


uint32_t probe_freq_khz = 0;


struct _probe {
    uint      offset;
    bool      initted;
};

static struct _probe probe;



/**
 * Set SWD frequency.
 * Frequency is checked against maximum values and stored as a future default.
 *
 * \param freq_khz  new frequency setting
 */
void probe_set_swclk_freq(uint32_t freq_khz)
{
    uint32_t clk_sys_freq_khz = clock_get_hz(clk_sys) / 1000;
    uint32_t div_256;
    uint32_t div_int;
    uint32_t div_frac;

#if OPT_SPECIAL_CLK_FOR_PIO
    // This very defensive frequency setting was introduced by either Max or Earle.  We prefer higher clock rates.
    // Clock rate can be set via tool, e.g. "pyocd reset -f 50000000" to get maximum target SWD frequency.
    if (freq_khz == 1000)
    {
        freq_khz = probe_freq_khz;
        if (freq_khz >= g_board_info.target_cfg->rt_max_swd_khz  ||  freq_khz == 0)
        {
            freq_khz = g_board_info.target_cfg->rt_swd_khz;                  // take a fair frequency
        }
    }
#endif

    if (freq_khz > g_board_info.target_cfg->rt_max_swd_khz)
    {
        freq_khz = g_board_info.target_cfg->rt_max_swd_khz;
    }
    else if (freq_khz < 100)
    {
        freq_khz = g_board_info.target_cfg->rt_swd_khz;
    }
    probe_freq_khz = freq_khz;

    div_256 = (256 * clk_sys_freq_khz + 3 * freq_khz) / (6 * freq_khz);      // SWDCLK goes with PIOCLK / 6
    div_int  = div_256 >> 8;
    div_frac = div_256 & 0xff;

    {
        static uint32_t prev_div_256;

        if (div_256 != prev_div_256) {
            prev_div_256 = div_256;
            picoprobe_info("SWD clk req   : %lukHz = %lukHz / (6 * (%lu + %lu/256)), eff : %lukHz\n",
                           freq_khz, clk_sys_freq_khz, div_int, div_frac,
                           (256 * clk_sys_freq_khz) / (6 * div_256));
        }
    }

    if (div_int == 0) {
        picoprobe_error("probe_set_swclk_freq: underflow of clock setup, setting clock to maximum.\n");
        div_int  = 1;
        div_frac = 0;
    }
    else if (div_int > 0xffff) {
        div_int = 0xffff;
        div_frac = 0xff;
    }

    // Worked out with pulseview
    pio_sm_set_clkdiv_int_frac(PROBE_PIO, PROBE_SM, div_int, div_frac);
}   // probe_set_swclk_freq



void probe_assert_reset(bool state)
{
    /* Change the direction to out to drive pin to 0 or to in to emulate open drain */
    gpio_set_dir(PROBE_PIN_RESET, state);
}   // probe_assert_reset



/**
 * Write data stream via SWD.
 * Actually only 32bit can be set, more data is sent as "zero".  Especially useful for
 * idle cycles (although never seen),
 */
void __TIME_CRITICAL_FUNCTION(probe_write_bits)(uint bit_count, uint32_t data)
{
    DEBUG_PINS_SET(probe_timing, DBG_PIN_WRITE);
    for (;;) {
        if (bit_count <= 16) {
            pio_sm_put_blocking(PROBE_PIO, PROBE_SM, CTRL_WORD_WRITE(bit_count - 1, data));
            break;
        }

        pio_sm_put_blocking(PROBE_PIO, PROBE_SM, CTRL_WORD_WRITE(16 - 1, data & 0xffff));
        data >>= 16;
        bit_count -= 16;
    }
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_WRITE);
    picoprobe_dump("Write %u bits 0x%lx\n", bit_count, data);
}   // probe_write_bits



uint32_t __TIME_CRITICAL_FUNCTION(probe_read_bits)(uint bit_count, bool push, bool pull)
{
    uint32_t data = 0xffffffff;
    uint32_t data_shifted;

    DEBUG_PINS_SET(probe_timing, DBG_PIN_READ);
    if (push) {
        pio_sm_put_blocking(PROBE_PIO, PROBE_SM, CTRL_WORD_READ(bit_count - 1));
    }
    if (pull) {
        data = pio_sm_get_blocking(PROBE_PIO, PROBE_SM);
    }
    DEBUG_PINS_CLR(probe_timing, DBG_PIN_READ);
    data_shifted = data;
    if (bit_count < 32) {
        data_shifted = data >> (32 - bit_count);
    }
    picoprobe_dump("Read %u bits 0x%lx (shifted 0x%lx)\n", bit_count, data, data_shifted);
    return data_shifted;
}   // probe_read_bits



void probe_gpio_init()
{
	static bool initialized;

	if ( !initialized) {
		initialized = true;
		picoprobe_debug("probe_gpio_init()\n");

		// Funcsel pins
        pio_gpio_init(PROBE_PIO, PROBE_PIN_SWDIR);
		pio_gpio_init(PROBE_PIO, PROBE_PIN_SWCLK);
		pio_gpio_init(PROBE_PIO, PROBE_PIN_SWDIO);
		// Make sure SWDIO has a pullup on it. Idle state is high
		gpio_pull_up(PROBE_PIN_SWDIO);

    // Adjusting the GPIO slew and drive strength seems to break connectivity
    // with certain targets, namely the STM32H7xx line of microcontrollers.
    // For the time being, both parameters remain unset to ensure compatibility
#if 0
        gpio_set_slew_rate(PROBE_PIN_SWCLK, GPIO_SLEW_RATE_FAST);
		gpio_set_drive_strength(PROBE_PIN_SWCLK, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(PROBE_PIN_SWDIO, GPIO_SLEW_RATE_FAST);
        gpio_set_drive_strength(PROBE_PIN_SWDIO, GPIO_DRIVE_STRENGTH_12MA);
#endif

		gpio_debug_pins_init();
#ifdef PICOPROBE_LED_CONNECTED
        gpio_init(PICOPROBE_LED_CONNECTED);
        gpio_set_dir(PICOPROBE_LED_CONNECTED, GPIO_OUT);
        gpio_put(PICOPROBE_LED_CONNECTED, 0);
#endif
#ifdef PICOPROBE_LED_RUNNING
        gpio_init(PICOPROBE_LED_RUNNING);
        gpio_set_dir(PICOPROBE_LED_RUNNING, GPIO_OUT);
        gpio_put(PICOPROBE_LED_RUNNING, 0);
#endif
	}
}   // probe_gpio_init



void probe_init()
{
//    picoprobe_info("probe_init()\n");

    // Target reset pin: pull up, input to emulate open drain pin
    gpio_pull_up(PROBE_PIN_RESET);
    // gpio_init will leave the pin cleared and set as input
    gpio_init(PROBE_PIN_RESET);
    if ( !probe.initted) {
//        picoprobe_info("     2. probe_init()\n");
        uint offset = pio_add_program(PROBE_PIO, &probe_program);
        probe.offset = offset;

        pio_sm_config sm_config = probe_program_get_default_config(offset);

        // SWDIR and SWCLK are sideset pins
        sm_config_set_sideset_pins(&sm_config, PROBE_PIN_SWDIR);

        // Set SWDIO offset
        sm_config_set_out_pins(&sm_config, PROBE_PIN_SWDIO, 1);
        sm_config_set_set_pins(&sm_config, PROBE_PIN_SWDIO, 1);
#ifdef PROBE_PIN_SWDIN
        sm_config_set_in_pins(&sm_config, PROBE_PIN_SWDIN);
#else
        sm_config_set_in_pins(&sm_config, PROBE_PIN_SWDIO);
#endif

        // Set SWDIR, SWCLK and SWDIO pins as output to start. This will be set in the sm
        pio_sm_set_consecutive_pindirs(PROBE_PIO, PROBE_SM, PROBE_PIN_OFFSET, PROBE_PIN_COUNT, true);

        // shift output right, autopull on, autopull threshold
        sm_config_set_out_shift(&sm_config, true, true, 32);
        // shift input right as swd data is lsb first, autopush off
        sm_config_set_in_shift(&sm_config, true, false, 0);

        // Init SM with config
        pio_sm_init(PROBE_PIO, PROBE_SM, offset, &sm_config);

        // Set up divisor
        probe_set_swclk_freq(probe_freq_khz);

        // Enable SM
        pio_sm_set_enabled(PROBE_PIO, PROBE_SM, true);
        probe.initted = true;
    }
}   // probe_init



void probe_deinit(void)
{
    if (probe.initted) {
        pio_sm_set_enabled(PROBE_PIO, PROBE_SM, 0);
        pio_remove_program(PROBE_PIO, &probe_program, probe.offset);
        probe.initted = false;
    }
}   // prebe_deinit
