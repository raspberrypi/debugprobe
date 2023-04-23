/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
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

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_RHPORT0_MODE         OPT_MODE_DEVICE

// force usage of FreeRTOS, can't be overwritten in CMakeLists.txt / Makefile
#undef CFG_TUSB_OS
#define CFG_TUSB_OS                   OPT_OS_FREERTOS


#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN            __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
    #define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//

//**********************************************
// Functionality can be enabled/disabled here.
// Note that still all modules are compiled.
// This will change in the future.
//**********************************************

#define CFG_TUD_CDC_UART              1            // CDC for target UART IO
#if !defined(TARGET_BOARD_PICO_DEBUG_PROBE)
    #define CFG_TUD_CDC_SIGROK        1            // CDC for sigrok IO
#else
    #define CFG_TUD_CDC_SIGROK        0            // no sigrok for debug probe
#endif
#if !defined(NDEBUG)
    #define CFG_TUD_CDC_DEBUG         1            // CDC for debug output of the probe
#else
    #define CFG_TUD_CDC_DEBUG         0
#endif

#define CFG_TUD_HID                   1            // CMSIS-DAPv1
#define CFG_TUD_VENDOR                1            // CMSIS-DAPv2
#define CFG_TUD_MSC                   1            // DAPLink drive
#define CFG_TUD_CDC                   (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK + CFG_TUD_CDC_DEBUG)


// CDC numbering (must go 0.. consecutive)
#define CDC_UART_N                    (CFG_TUD_CDC_UART - 1)
#define CDC_SIGROK_N                  (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK - 1)
#define CDC_DEBUG_N                   (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK + CFG_TUD_CDC_DEBUG - 1)

//------------- BUFFER SIZES -------------//

#define CFG_TUD_CDC_RX_BUFSIZE        64
#define CFG_TUD_CDC_TX_BUFSIZE        256

// these numbers must be in the range 64..1024 (and the same)
#define CFG_TUD_VENDOR_RX_BUFSIZE     1024
#define CFG_TUD_VENDOR_TX_BUFSIZE     CFG_TUD_VENDOR_RX_BUFSIZE

 // note: this is optimized for DAPLink write speed
#define CFG_TUD_MSC_EP_BUFSIZE        512


#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
