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

#if false
#define picoprobe_info(format,args...) printf(format, ## args)
#else
#define picoprobe_info(format,...) ((void)0)
#endif

#if false
#define picoprobe_debug(format,args...) printf(format, ## args)
#else
#define picoprobe_debug(format,...) ((void)0)
#endif

#if false
#define picoprobe_dump(format,args...) printf(format, ## args)
#else
#define picoprobe_dump(format,...) ((void)0)
#endif

// PIO config
#define PROBE_SM 0
#define PROBE_PIN_OFFSET 2
#define PROBE_PIN_SWCLK PROBE_PIN_OFFSET + 0 // 2
#define PROBE_PIN_SWDIO PROBE_PIN_OFFSET + 1 // 3

// Target reset config
#define PROBE_PIN_RESET 6

// UART config
#define PICOPROBE_UART_TX 4
#define PICOPROBE_UART_RX 5
#define PICOPROBE_UART_INTERFACE uart1
#define PICOPROBE_UART_BAUDRATE 115200

// LED config
#ifdef PICOPROBE_LED
  #define HAS_FLASHER 1
#else
  #if PICO_DEFAULT_LED_PIN == -1
    #undef PICO_DEFAULT_LED_PIN
  #endif
  #ifdef PICO_DEFAULT_LED_PIN
    #define PICOPROBE_LED PICO_DEFAULT_LED_PIN
    #define HAS_FLASHER 1
  #endif
#endif

// WS2812 confid
#ifdef PICOPROBE_WS2812
  #define HAS_FLASHER 1
#else
  #if PICO_DEFAULT_WS2812_PIN == -1
    #undef PICO_DEFAULT_WS2812_PIN
  #endif
  #ifdef PICO_DEFAULT_WS2812_PIN
    #define PICOPROBE_WS2812 PICO_DEFAULT_WS2812_PIN
    #define HAS_FLASHER 1
  #endif
#endif

#ifndef HAS_FLASHER
  #warning PICO_DEFAULT_LED_PIN is undefined or defined as -1, run PICOPROBE_LED=<led_pin> cmake
  #warning PICO_DEFAULT_WS2812_PIN is undefined or defined as -1, run PICOPROBE_WS2812=<ws2812_pin> cmake
  #error Either or both of PICOPROBE_LED or PICOPROBE_WS2812 must be defined, or have sensible defaults from the board definition.
#endif

// Keep the colours no brighter than necessary to avoid overloading the grain-
// of-rice voltage regulator since this is also supplying the target board.

#define COLOUR_RED     0x040000
#define COLOUR_GREEN   0x000300
#define COLOUR_BLUE    0x000004
#define COLOUR_CYAN    0x000102
#define COLOUR_MAGENTA 0x020002
#define COLOUR_YELLOW  0x020100
#define COLOUR_WHITE   0x010102
#define COLOUR_BLACK   0x000000

#define COLOUR_SWD_W   COLOUR_GREEN
#define COLOUR_SWD_R   COLOUR_MAGENTA
#define COLOUR_UART_W  COLOUR_YELLOW
#define COLOUR_UART_R  COLOUR_BLUE

#endif

