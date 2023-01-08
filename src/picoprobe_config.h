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


#if !defined(PICOPROBE_VERSION)
    #define PICOPROBE_VERSION   0x0103
#endif

/// which means: pyocd will not work.
#ifndef OPTIMIZE_FOR_OPENOCD
    #define OPTIMIZE_FOR_OPENOCD     0
#endif

#define INCLUDE_RTT_CONSOLE
#define INCLUDE_SIGROK



#if !defined(NDEBUG)
    int cdc_debug_printf(const char* format, ...) __attribute__ ((format (printf, 1, 2)));
#endif

#if !defined(NDEBUG)
    #define picoprobe_out(format,args...) cdc_debug_printf(format, ## args)
#else
    #define picoprobe_out(format,...) ((void)0)
#endif

#if 1  &&  !defined(NDEBUG)
    #define picoprobe_info(format,args...) cdc_debug_printf("(II) " format, ## args)
#else
    #define picoprobe_info(format,...) ((void)0)
#endif

#if 1  &&  !defined(NDEBUG)
    #define picoprobe_debug(format,args...) cdc_debug_printf("(DD) " format, ## args)
#else
    #define picoprobe_debug(format,...) ((void)0)
#endif

#if 0  &&  !defined(NDEBUG)
    #define picoprobe_dump(format,args...) cdc_debug_printf("(..) " format, ## args)
#else
    #define picoprobe_dump(format,...) ((void)0)
#endif

#if 1  &&  !defined(NDEBUG)
    #define picoprobe_error(format,args...) cdc_debug_printf("(EE) " format, ## args)
#else
    #define picoprobe_error(format,...) ((void)0)
#endif

#define PROBE_CPU_CLOCK_KHZ      (150*1000)             // overclocked to 150MHz, even 200MHz seems to be no problem

// PIO config
#define PROBE_PIO                pio0
#define PROBE_SM                 0
#define PROBE_PIN_OFFSET         2
#define PROBE_PIN_SWCLK          (PROBE_PIN_OFFSET + 0) // 2
#define PROBE_PIN_SWDIO          (PROBE_PIN_OFFSET + 1) // 3
#define PROBE_PIN_RESET          6                      // Target reset config
#define PROBE_MAX_KHZ            25000U                 // overclocked: according to RP2040 datasheet 24MHz
#define PROBE_DEFAULT_KHZ        15000

// UART config (UART target -> probe)
#define PICOPROBE_UART_TX        4
#define PICOPROBE_UART_RX        5
#define PICOPROBE_UART_INTERFACE uart1
#define PICOPROBE_UART_BAUDRATE  115200

// LED config
#ifndef PICOPROBE_LED
    #ifndef PICO_DEFAULT_LED_PIN
        #error PICO_DEFAULT_LED_PIN is not defined, run PICOPROBE_LED=<led_pin> cmake
    #elif PICO_DEFAULT_LED_PIN == -1
        #error PICO_DEFAULT_LED_PIN is defined as -1, run PICOPROBE_LED=<led_pin> cmake
    #else
        #define PICOPROBE_LED PICO_DEFAULT_LED_PIN
    #endif
#endif

// sigrok config
#define SIGROK_PIO               pio1
#define SIGROK_SM                0                      // often hard coded


extern uint32_t probe_freq_khz;


#endif
