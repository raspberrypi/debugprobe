/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jaroslav Kysela <perex@perex.cz>
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
#include "cdc_sump.h"

void cdc_sump_init(void)
{
}

#define MAX_UART_PKT 64
void cdc_sump_task(void) {
    uint8_t tx_buf[MAX_UART_PKT];
    const char itf = 1;

    if (tud_cdc_n_connected(itf)) {
        if (tud_cdc_n_available(itf)) {
            tud_cdc_n_read(itf, tx_buf, sizeof(tx_buf));
        }
    }
}

void cdc_sump_line_coding(cdc_line_coding_t const* line_coding) {
    picoprobe_info("Sump new baud rate %d\n", line_coding->bit_rate);
}
