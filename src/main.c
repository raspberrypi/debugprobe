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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "picoprobe_config.h"
#include "probe.h"
#include "cdc_uart.h"
#include "get_serial.h"
#include "led.h"

#ifdef USE_QPTY_WS2812
#include "ws2812.h"
#endif

// UART0 for Picoprobe debug
// UART1 for picoprobe to target device

int main(void) {

    board_init();
    usb_serial_init();
    cdc_uart_init();
    tusb_init();
    probe_init();

#ifndef USE_QPTY_WS2812
    led_init();
#else
    ws2812_init();
#endif

    picoprobe_info("Welcome to Picoprobe!\n");

    while (1) {
        tud_task(); // tinyusb device task
        cdc_task();
        probe_task();

#ifndef USE_QPTY_WS2812
        led_task();
#else
        ws2812_task();
#endif

    }

    return 0;
}