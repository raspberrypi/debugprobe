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
#if TURBO_200MHZ
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#endif
#include "probe.h"
#include "cdc_uart.h"
#include "cdc_sump.h"
#include "get_serial.h"
#include "led.h"

// UART0 for Picoprobe debug
// UART1 for picoprobe to target device

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    if (itf == 0)
        cdc_uart_line_coding(line_coding);
    else if (itf == 1)
        cdc_sump_line_coding(line_coding);
}

int main(void) {

#if TURBO_200MHZ
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    set_sys_clock_khz(200000, true);
#endif

    board_init();
    usb_serial_init();
    cdc_uart_init();
    cdc_sump_init();
    tusb_init();
    probe_init();
    led_init();

    picoprobe_info("Welcome to Picoprobe!\n");

    while (1) {
        tud_task(); // tinyusb device task
        cdc_uart_task();
        cdc_sump_task();
        probe_task();
        led_task();
    }

    return 0;
}