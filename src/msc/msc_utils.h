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

#ifndef _MSC_UTILS_H
#define _MSC_UTILS_H


#include <stdint.h>
#include <stdbool.h>

#include "boot/uf2.h"                // this is the Pico variant of the UF2 header


#ifdef __cplusplus
    extern "C" {
#endif


bool target_connect(bool write_mode);
bool target_write_memory(const struct uf2_block *uf2);
bool target_read_memory(struct uf2_block *uf2, uint32_t target_addr, uint32_t block_no, uint32_t num_blocks);

bool is_uf2_record(const void *sector, uint32_t sector_size);

void msc_init(uint32_t task_prio);

#ifdef __cplusplus
    }
#endif

#endif
