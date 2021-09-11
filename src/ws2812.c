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

#define LED_COUNT_SHIFT 14
#define LED_COUNT_MAX   5 * (1 << LED_COUNT_SHIFT)

static uint32_t led_count;
static uint     pio_offset;
static int      sm;
PIO             pio;

static inline void put_pixel(uint32_t colour) {
    // GRB is irrational, so convert 'colour' from RGB
    uint32_t grb_colour = ((colour & 0xFF0000) >> 8) | ((colour & 0xFF00) << 8) | (colour & 0xFF);
    pio_sm_put_blocking(pio, sm, grb_colour << 8u);
}

void ws2812_init(void) {
    led_count = 0;
    pio = pio1;
    pio_offset = 0;
    sm = 0;

    #ifdef NEO_PIN_PWR
    // Power up WS2812
    gpio_init(NEO_PIN_PWR);
    gpio_set_dir(NEO_PIN_PWR, GPIO_OUT);
    gpio_put(NEO_PIN_PWR, 1);
    #endif

    // Set PIO output to feed the WS2182 via pin NEO_PIN_DAT
    pio_offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, pio_offset, NEO_PIN_DAT, 800000, true);

    // Set pixel on
    put_pixel(RGB_COLOUR);
}

void ws2812_task(void) {
    if (led_count != 0) {
        --led_count;
        bool is_set = !((led_count >> LED_COUNT_SHIFT) & 1);
        put_pixel(is_set ? RGB_COLOUR : 0x00);
    }
}

void ws2812_signal_activity(uint total_bits) {
    if (led_count == 0) put_pixel(0x00);
    if (led_count < LED_COUNT_MAX) led_count += total_bits;
}
