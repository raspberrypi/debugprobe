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
#define INCLUDE_SIGROK


#if !defined(NDEBUG)
    #define picoprobe_out(format,args...) printf(format, ## args)
#else
    #define picoprobe_out(format,...) ((void)0)
#endif

#if 1  &&  !defined(NDEBUG)
    #define picoprobe_info(format,args...) printf("(II) " format, ## args)
#else
    #define picoprobe_info(format,...) ((void)0)
#endif

#if 0  &&  !defined(NDEBUG)
    #define picoprobe_debug(format,args...) printf("(DD) " format, ## args)
#else
    #define picoprobe_debug(format,...) ((void)0)
#endif

#if 0  &&  !defined(NDEBUG)
    #define picoprobe_dump(format,args...) printf("(..) " format, ## args)
#else
    #define picoprobe_dump(format,...) ((void)0)
#endif

#if 1  &&  !defined(NDEBUG)
    #define picoprobe_error(format,args...) printf("(EE) " format, ## args)
#else
    #define picoprobe_error(format,...) ((void)0)
#endif


// Base value of sys_clk in khz.  Must be <=125Mhz per RP2040 spec and a multiple of 24Mhz
// to support integer divisors of the PIO clock and ADC clock (for sigrok)
#define PROBE_CPU_CLOCK_KHZ      ((120 + 2*24) * 1000)             // overclocked, even 264MHz seems to be no problem


// PIO config
#define PROBE_PIO                pio0
#define PROBE_SM                 0
#define PROBE_PIN_OFFSET         1
#define PROBE_PIN_SWDIR          (PROBE_PIN_OFFSET + 0) // 1
#define PROBE_PIN_SWCLK          (PROBE_PIN_OFFSET + 1) // 2
#define PROBE_PIN_SWDIO          (PROBE_PIN_OFFSET + 2) // 3
#define PROBE_PIN_RESET          6                      // Target reset config
#define PROBE_MAX_KHZ            25000U                 // overclocked: according to RP2040 datasheet 24MHz
#define PROBE_DEFAULT_KHZ        15000

// UART config (UART target -> probe)
#define PICOPROBE_UART_TX        4
#define PICOPROBE_UART_RX        5
#define PICOPROBE_UART_INTERFACE uart1
#define PICOPROBE_UART_BAUDRATE  115200

//
// Other pin definitions
// - LED     actual handling is done in led.c, pin definition is PICOPROBE_LED / PICO_DEFAULT_LED_PIN
// - sigrok  defines are in pico-sigrok/sigrok-int.h
// - Debug   used in probe.c
//


// sigrok config
#define SIGROK_PIO               pio1
#define SIGROK_SM                0                      // often hard coded


// optimize a single function
#if 0
    // actually no positive effect
    #define __TIME_CRITICAL_FUNCTION(func)   __attribute__((optimize("O3")))   __time_critical_func(func)
#else
    #define __TIME_CRITICAL_FUNCTION(func)   func
#endif

#endif
