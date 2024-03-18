/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
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

#ifndef BOARD_EXAMPLE_H_
#define BOARD_EXAMPLE_H_
#error "Example board configuration requested - specify PICO_BOARD and re-run CMake."

/* Select one of these. */
/* Direct connection - SWCLK/SWDIO on two GPIOs */
#define PROBE_IO_RAW
/* SWCLK connected to a GPIO, SWDO driven from a GPIO, SWDI sampled via a level shifter */
#define PROBE_IO_SWDI
/* Level-shifted SWCLK, SWDIO with separate SWDO, SWDI and OE_N pin */
#define PROBE_IO_OEN

/* Include CDC interface to bridge to target UART. Omit if not used. */
#define PROBE_CDC_UART

/* Board implements hardware flow control for UART RTS/CTS instead of ACM control */
#define PROBE_UART_HWFC

/* Target reset GPIO (active-low). Omit if not used.*/
#define PROBE_PIN_RESET 1

#define PROBE_SM 0
#define PROBE_PIN_OFFSET 12
/* PIO config for PROBE_IO_RAW */
#if defined(PROBE_IO_RAW)
#define PROBE_PIN_SWCLK (PROBE_PIN_OFFSET + 0)
#define PROBE_PIN_SWDIO (PROBE_PIN_OFFSET + 1)
#endif

/* PIO config for PROBE_IO_SWDI */
#if defined(PROBE_IO_SWDI)
#define PROBE_PIN_SWCLK (PROBE_PIN_OFFSET + 0)
#define PROBE_PIN_SWDIO (PROBE_PIN_OFFSET + 1)
#define PROBE_PIN_SWDI  (PROBE_PIN_OFFSET + 2)
#endif

/* PIO config for PROBE_IO_OEN - note that SWDIOEN and SWCLK are both side_set signals, so must be consecutive. */
#if defined(PROBE_IO_SWDIOEN)
#define PROBE_PIN_SWDIOEN (PROBE_PIN_OFFSET + 0)
#define PROBE_PIN_SWCLK (PROBE_PIN_OFFSET + 1)
#define PROBE_PIN_SWDIO (PROBE_PIN_OFFSET + 2)
#define PROBE_PIN_SWDI (PROBE_PIN_OFFSET + 3)
#endif

#if defined(PROBE_CDC_UART)
#define PROBE_UART_TX 4
#define PROBE_UART_RX 5
#define PROBE_UART_INTERFACE uart1
#define PROBE_UART_BAUDRATE 115200

#if defined(PROBE_UART_HWFC)
/* Hardware flow control - see 1.4.3 in the RP2040 datasheet for valid pin settings */
#define PROBE_UART_CTS 6
#define PROBE_UART_RTS 7
#else
/* Software flow control - RTS and DTR can be omitted if not used */
#define PROBE_UART_RTS 9
#endif
#define PROBE_UART_DTR 10

#endif

/* LED config - some or all of these can be omitted if not used */
#define PROBE_USB_CONNECTED_LED 2
#define PROBE_DAP_CONNECTED_LED 15
#define PROBE_DAP_RUNNING_LED 16
#define PROBE_UART_RX_LED 7
#define PROBE_UART_TX_LED 8

#define PROBE_PRODUCT_STRING "Example Debug Probe"

#endif
