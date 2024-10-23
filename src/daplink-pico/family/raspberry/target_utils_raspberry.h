/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Hardy Griech
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

/**
 * This module holds some additional SWD functions.
 */

#ifndef _TARGET_RP2040_H
#define _TARGET_RP2040_H


#include <stdbool.h>


#ifdef __cplusplus
    extern "C" {
#endif


// required for linking of \a g_board_info.target_cfg and \a g_raspberry_rp2040_family
#define TARGET_RP2040_FAMILY_ID       CREATE_FAMILY_ID(127, 1)
#define TARGET_RP2350_FAMILY_ID       CREATE_FAMILY_ID(127, 2)


bool target_core_is_halted(void);
bool target_core_halt(void);
bool target_core_unhalt(void);
bool target_core_unhalt_with_masked_ints(void);


#ifdef __cplusplus
    }
#endif

#endif
