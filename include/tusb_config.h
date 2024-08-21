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

#include <stdint.h>

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

#define CFG_TUD_TASK_QUEUE_SZ         64

//------------- CLASS -------------//

//**********************************************
// Functionality can be enabled/disabled here.
// Note that still all modules are compiled.
// This will change in the future.
//**********************************************

#if OPT_TARGET_UART                                // CDC for target UART IO
    #define CFG_TUD_CDC_UART          1
#else
    #define CFG_TUD_CDC_UART          0
#endif
#if OPT_SIGROK                                     // CDC for sigrok IO
    #define CFG_TUD_CDC_SIGROK        1
#else
    #define CFG_TUD_CDC_SIGROK        0
#endif
#if OPT_PROBE_DEBUG_OUT_CDC
    #define CFG_TUD_CDC_DEBUG         1            // CDC for debug output of the probe
#else
    #define CFG_TUD_CDC_DEBUG         0
#endif
#if OPT_CDC_SYSVIEW                                // CDC for SysView
    #define CFG_TUD_CDC_SYSVIEW       1
#else
    #define CFG_TUD_CDC_SYSVIEW       0
#endif

#if OPT_CMSIS_DAPV1                                // CMSIS-DAPv1
    #define CFG_TUD_HID               1
#else
    #define CFG_TUD_HID               0
#endif
#if OPT_CMSIS_DAPV2                                // CMSIS-DAPv2
    #define CFG_TUD_VENDOR            1
#else
    #define CFG_TUD_VENDOR            0
#endif
#if OPT_MSC                                        // DAPLink drive
    #define CFG_TUD_MSC               1
#else
    #define CFG_TUD_MSC               0
#endif
#define CFG_TUD_CDC                   (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK + CFG_TUD_CDC_DEBUG + CFG_TUD_CDC_SYSVIEW)
#if OPT_NET
    #if OPT_NET_PROTO_ECM  ||  OPT_NET_PROTO_RNDIS
        #define CFG_TUD_ECM_RNDIS     1            // RNDIS under Windows works only if it's the only class, so we try NCM for Linux
        #define CFG_TUD_NCM           0
    #elif OPT_NET_PROTO_NCM
        #define CFG_TUD_ECM_RNDIS     0
        #define CFG_TUD_NCM           1
    #else
        #error "Illegal OPT_NET_PROTO definition"
    #endif
#else
    #define CFG_TUD_ECM_RNDIS         0
    #define CFG_TUD_NCM               0
#endif

// CDC numbering (must go 0.. consecutive)
#define CDC_UART_N                    (CFG_TUD_CDC_UART - 1)
#if OPT_SIGROK
    #define CDC_SIGROK_N              (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK - 1)
#endif
#define CDC_DEBUG_N                   (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK + CFG_TUD_CDC_DEBUG - 1)
#if OPT_CDC_SYSVIEW
    #define CDC_SYSVIEW_N             (CFG_TUD_CDC_UART + CFG_TUD_CDC_SIGROK + CFG_TUD_CDC_DEBUG + CFG_TUD_CDC_SYSVIEW - 1)
#endif

//------------- BUFFER SIZES -------------//

#define CFG_TUD_CDC_RX_BUFSIZE        64
#define CFG_TUD_CDC_TX_BUFSIZE        512

// these numbers must be in the range 64..1024 (and the same)
#define CFG_TUD_VENDOR_RX_BUFSIZE     1024
#define CFG_TUD_VENDOR_TX_BUFSIZE     CFG_TUD_VENDOR_RX_BUFSIZE

#if OPT_MSC
    // note: this is optimized for DAPLink write speed
    #define CFG_TUD_MSC_EP_BUFSIZE    512
#endif

#if OPT_NET
    #define CFG_TUD_NET_MTU           1514
#endif

#define CFG_TUD_NCM_OUT_NTB_N         2
#define CFG_TUD_NCM_IN_NTB_N          3

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
