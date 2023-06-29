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

#ifndef PICOPROBE_H_
#define PICOPROBE_H_


#define INCLUDE_RTT_CONSOLE


#if OPT_PROBE_DEBUG_OUT
    #define picoprobe_info_out(format,args...) printf(format, ## args)
#else
    #define picoprobe_info_out(format,...) ((void)0)
#endif

#if OPT_PROBE_DEBUG_OUT
    #define picoprobe_info(format,args...) printf("(II) " format, ## args)
#else
    #define picoprobe_info(format,...) ((void)0)
#endif

#if 0  &&  OPT_PROBE_DEBUG_OUT
    #define picoprobe_debug(format,args...) printf("(DD) " format, ## args)
#else
    #define picoprobe_debug(format,...) ((void)0)
#endif

#if 0  &&  OPT_PROBE_DEBUG_OUT
    #define picoprobe_dump(format,args...) printf("(..) " format, ## args)
#else
    #define picoprobe_dump(format,...) ((void)0)
#endif

#if 1  &&  OPT_PROBE_DEBUG_OUT
    #define picoprobe_error(format,args...) printf("(EE) " format, ## args)
#else
    #define picoprobe_error(format,...) ((void)0)
#endif


// Base value of sys_clk in khz.  Must be <=125Mhz per RP2040 spec and a multiple of 24Mhz
// to support integer divisors of the PIO clock and ADC clock (for sigrok)
#if 0
#define PROBE_CPU_CLOCK_KHZ      ((120 + 2*24) * 1000)             // overclocked, even 264MHz seems to be no problem
#else
#define PROBE_CPU_CLOCK_KHZ      ((120 + 5*24) * 1000)             // overclocked, even 264MHz seems to be no problem
#endif


// pin configurations can be found in include/boards/*.h


// optimize a single function
#if 0
    // actually no positive effect
    #define __TIME_CRITICAL_FUNCTION(func)   __attribute__((optimize("O3")))   __time_critical_func(func)
#else
    #define __TIME_CRITICAL_FUNCTION(func)   func
#endif

#endif
