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

#include "picoprobe_config.h"

#ifdef PICOPROBE_WS2812
#include "ws2812.h"
#endif

static uint32_t led_count;


void led_init(void) {
    led_count = 0xffff; // We can watch this being counted down towards zero with a 'scope

#ifdef PICOPROBE_LED
    gpio_init(PICOPROBE_LED);
    gpio_set_dir(PICOPROBE_LED, GPIO_OUT);
#endif
#ifdef PICOPROBE_WS2812
    ws2812_init();
    put_pixel(COLOUR_WHITE);
#endif
#ifdef PICOPROBE_LED
    gpio_put(PICOPROBE_LED, 1);
    gpio_put(PICOPROBE_LED, 0); // Recognisable timing datum
    gpio_put(PICOPROBE_LED, 1); // Start of 64K countdown
#endif
}

/*
 * Timing on a Waveshare RP2040-Zero is 65535 counting down to zero = approx
 * 150 mSec. So if a signal (SWD write etc.) triggers the start, a preload of
 * 64K would cap repetition to roughly that rate, and turning off the LED at
 * 32K would give an "on" time of roughly 75 mSec which is compatible with
 * industry usage. 
*/


void led_task(void) {
    if (led_count != 0) {
        led_count -= 1;
        if (led_count == 32767) {
#ifdef PICOPROBE_LED
            gpio_put(PICOPROBE_LED, 0);
#endif
#ifdef PICOPROBE_WS2812
            // Set pixel off
            put_pixel(COLOUR_BLACK);
#endif
        }
    }
}

void led_signal_activity(uint total_bits, uint32_t colour) {
    if (led_count == 0) {
        led_count = 65535;
#ifdef PICOPROBE_LED
        gpio_put(PICOPROBE_LED, 1);
#endif
#ifdef PICOPROBE_WS2812
        put_pixel(colour);
#endif
    }
}


void led_signal_write_swd(uint total_bits) {
    led_signal_activity(total_bits, COLOUR_SWD_W);
}

void led_signal_read_swd(uint total_bits) {
    led_signal_activity(total_bits, COLOUR_SWD_R);
}

void led_signal_write_uart(uint total_bytes) {
    led_signal_activity(total_bytes << 3, COLOUR_UART_W);
}

void led_signal_read_uart(uint total_bytes) {
    led_signal_activity(total_bytes << 3, COLOUR_UART_R);
}

