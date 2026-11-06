/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 a-pushkin on GitHub
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

#include "probe_config.h"

#ifdef DEBUG_ON_ZERO
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include <stdio.h>

static uint8_t red_s, green_s, blue_s;
PIO pio;
uint sm;
uint offset;

void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio1, 0, pixel_grb << 8u);
}

void led_red_set(uint8_t set){
    if(set != red_s){
        red_s = set;
        put_pixel((green_s << 16) | (red_s << 8) | (blue_s << 0));
    }
}

void led_green_set(uint8_t set){
    if(set != green_s){
        green_s = set;
        put_pixel((green_s << 16) | (red_s << 8) | (blue_s << 0));
    }
}

void led_blue_set(uint8_t set){
    if(set != blue_s){
        blue_s = set;
        put_pixel((green_s << 16) | (red_s << 8) | (blue_s << 0));
    }
}

void led_init(void) {
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, 16, 1, true); 
    hard_assert(success);
    ws2812_program_init(pio, sm, offset, 16, 800000, false);
}
#else

void led_init(void) {
#ifdef PROBE_USB_CONNECTED_LED
    gpio_init(PROBE_USB_CONNECTED_LED);
    gpio_set_dir(PROBE_USB_CONNECTED_LED, GPIO_OUT);
#endif
#ifdef PROBE_DAP_CONNECTED_LED
    gpio_init(PROBE_DAP_CONNECTED_LED);
    gpio_set_dir(PROBE_DAP_CONNECTED_LED, GPIO_OUT);
#endif
#ifdef PROBE_DAP_RUNNING_LED
    gpio_init(PROBE_DAP_RUNNING_LED);
    gpio_set_dir(PROBE_DAP_RUNNING_LED, GPIO_OUT);
#endif
#ifdef PROBE_UART_RX_LED
    gpio_init(PROBE_UART_RX_LED);
    gpio_set_dir(PROBE_UART_RX_LED, GPIO_OUT);
#endif
#ifdef PROBE_UART_TX_LED
    gpio_init(PROBE_UART_TX_LED);
    gpio_set_dir(PROBE_UART_TX_LED, GPIO_OUT);
#endif
}
#endif