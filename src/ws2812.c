/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 a-pushkin on GitHub
 * Copyright (c) 2021 a-smittytone on GitHub
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
#include <stdint.h>

#include "hardware/pio.h"
#include "picoprobe_config.h"
#include "ws2812.pio.h"

static uint     pio_offset;
static int      sm;
PIO             pio;

void put_pixel(uint32_t colour) {
    // GRB is irrational, so convert 'colour' from RGB
    uint32_t grb_colour = ((colour & 0xFF0000) >> 8) | ((colour & 0xFF00) << 8) | (colour & 0xFF);
    pio_sm_put_blocking(pio, sm, grb_colour << 8u);
}

void ws2812_init(void) {
    pio = pio1;
    pio_offset = 0;
    sm = 0;

    #ifdef PICO_DEFAULT_WS2812_POWER_PIN
    // Power up WS2812 on QTPY RP2040
    gpio_init(PICO_DEFAULT_WS2812_POWER_PIN);
    gpio_set_dir(PICO_DEFAULT_WS2812_POWER_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_WS2812_POWER_PIN, 1);
    #endif

    // Set PIO output to feed the WS2182 via pin PICOPROBE_WS2812
    pio_offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, pio_offset, PICOPROBE_WS2812, 800000, true);
}
