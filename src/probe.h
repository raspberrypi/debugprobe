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

#ifndef PROBE_H_
#define PROBE_H_

#if defined(PROBE_IO_RAW) || defined(PROBE_IO_SWDI)
#include "probe.pio.h"
#endif

#if defined(PROBE_IO_OEN)
#include "probe_oen.pio.h"
#endif

void probe_set_swclk_freq(uint freq_khz);

// Bit counts in the range 1..256
void probe_write_bits(uint bit_count, uint32_t data_byte);
uint32_t probe_read_bits(uint bit_count);
void probe_hiz_clocks(uint bit_count);

void probe_read_mode(void);
void probe_write_mode(void);

void probe_init(void);
void probe_deinit(void);
void probe_assert_reset(bool state);
int probe_reset_level(void);

#endif
