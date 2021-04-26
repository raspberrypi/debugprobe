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

#include "tusb.h"

#include "picoprobe_config.h"
#include "cdc_uart.h"

void cdc_uart_init(void) {
    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    uart_init(PICOPROBE_UART_INTERFACE, PICOPROBE_UART_BAUDRATE);
}

#define MAX_UART_PKT 64
void cdc_uart_task(void) {
    uint8_t rx_buf[MAX_UART_PKT];
    uint8_t tx_buf[MAX_UART_PKT];
    const char itf = 0;

    // Consume uart fifo regardless even if not connected
    uint rx_len = 0;
    while(uart_is_readable(PICOPROBE_UART_INTERFACE) && (rx_len < MAX_UART_PKT)) {
        rx_buf[rx_len++] = uart_getc(PICOPROBE_UART_INTERFACE);
    }

    if (tud_cdc_n_connected(itf)) {
        // Do we have anything to display on the host's terminal?
        if (rx_len) {
            tud_cdc_n_write(itf, rx_buf, rx_len);
            tud_cdc_n_write_flush(itf);
        }

        if (tud_cdc_n_available(itf)) {
            // Is there any data from the host for us to tx
            uint tx_len = tud_cdc_n_read(itf, tx_buf, sizeof(tx_buf));
            uart_write_blocking(PICOPROBE_UART_INTERFACE, tx_buf, tx_len);
        }
    }
}

void cdc_uart_line_coding(cdc_line_coding_t const* line_coding) {
    picoprobe_info("New baud rate %d\n", line_coding->bit_rate);
    uart_init(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);
}
