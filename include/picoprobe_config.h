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

#if defined(OPT_SERIAL_CRLF)
    #define PROBE_DEBUG_OPT_CR "\r"
#else
    #define PROBE_DEBUG_OPT_CR ""
#endif


#if OPT_PROBE_DEBUG_OUT
    #define picoprobe_info_out(format,args...) printf(format PROBE_DEBUG_OPT_CR, ## args)
#else
    #define picoprobe_info_out(format,...) ((void)0)
#endif

#if OPT_PROBE_DEBUG_OUT
    #define picoprobe_info(format,args...) printf("(II) " format PROBE_DEBUG_OPT_CR, ## args)
#else
    #define picoprobe_info(format,...) ((void)0)
#endif

#if 0  &&  OPT_PROBE_DEBUG_OUT
    #define picoprobe_debug(format,args...) printf("(DD) " format PROBE_DEBUG_OPT_CR, ## args)
#else
    #define picoprobe_debug(format,...) ((void)0)
#endif

#if 0  &&  OPT_PROBE_DEBUG_OUT
    #define picoprobe_dump(format,args...) printf("(..) " format PROBE_DEBUG_OPT_CR, ## args)
#else
    #define picoprobe_dump(format,...) ((void)0)
#endif

#if 1  &&  OPT_PROBE_DEBUG_OUT
    #define picoprobe_error(format,args...) printf("(EE) " format PROBE_DEBUG_OPT_CR, ## args)
#else
    #define picoprobe_error(format,...) ((void)0)
#endif


// Base value of sys_clk in MHz.  Must be <=125MHz per RP2040 spec and a multiple of 24MHz
// to support integer divisors of the PIO clock and ADC clock (for sigrok).
// Can be overridden via configuration.
#ifdef OPT_MCU_OVERCLOCK_MHZ
    #define PROBE_CPU_CLOCK_MHZ      OPT_MCU_OVERCLOCK_MHZ     // overclocked, even 264MHz seems to be no problem
#else
    #define PROBE_CPU_CLOCK_MHZ      120
#endif
#define PROBE_CPU_CLOCK_MIN_MHZ      (3 * 24)
#define PROBE_CPU_CLOCK_MAX_MHZ      (12 * 24)


// pin configurations can be found in include/boards/*.h


// optimize a single function
#if 0
    // actually no positive effect
    #define __TIME_CRITICAL_FUNCTION(func)   __attribute__((optimize("O3")))   __time_critical_func(func)
#else
    #define __TIME_CRITICAL_FUNCTION(func)   func
#endif

//
// minIni definitions
//
#define MININI_VAR_NET       "net"
#define MININI_VAR_NICK      "nick"
#define MININI_VAR_FCPU      "f_cpu"
#define MININI_VAR_FSWD      "f_swd"
#define MININI_VAR_RSTART    "ram_start"
#define MININI_VAR_REND      "ram_end"
#define MININI_VAR_PWD       "pwd"
#define MININI_VAR_RTT       "rtt"
#define MININI_VAR_DAP_PSIZE "dap_psize"
#define MININI_VAR_DAP_PCNT  "dap_pcnt"

#define MININI_VAR_NAMES    MININI_VAR_NET, MININI_VAR_NICK, MININI_VAR_FCPU, MININI_VAR_FSWD, \
                            MININI_VAR_RSTART, MININI_VAR_REND, MININI_VAR_PWD, MININI_VAR_RTT, \
                            MININI_VAR_DAP_PSIZE, MININI_VAR_DAP_PCNT

#endif
