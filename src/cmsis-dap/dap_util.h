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

#ifndef DAP_UTIL_H
#define DAP_UTIL_H

#include <stdint.h>


typedef enum {
    E_DAPTOOL_UNKNOWN,
    E_DAPTOOL_OPENOCD,
    E_DAPTOOL_PYOCD,
    E_DAPTOOL_PROBERS,
    E_DAPTOOL_USER
} daptool_t;


static const uint32_t DAP_CHECK_ABORT = 99999999;

uint32_t DAP_GetCommandLength(const uint8_t *request_data, uint32_t request_len);
daptool_t DAP_FingerprintTool(const uint8_t *request, uint32_t request_len);
bool DAP_OfflineCommand(const uint8_t *request_data);

#endif
