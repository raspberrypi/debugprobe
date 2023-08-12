/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Federico Zuccardi Merli
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

#ifndef GET_CONFIG_H_
#define GET_CONFIG_H_

#include <stdint.h>


#define _xxCoNfSTR(S)  #S
#define xxCoNfSTR(S)   _xxCoNfSTR(S)

#if OPT_CMSIS_DAPV1
    #define __OPT_CMSIS_DAPV1         " [CMSIS: DAPv1]"
#else
    #define __OPT_CMSIS_DAPV1
#endif
#if OPT_CMSIS_DAPV2
    #define __OPT_CMSIS_DAPV2         " [CMSIS: DAPv2]"
#else
    #define __OPT_CMSIS_DAPV2
#endif
#if OPT_MSC
    #define __OPT_MSC                 " [MSC: DAPLink]"
#else
    #define __OPT_MSC
#endif
#if OPT_TARGET_UART
    #define __OPT_TARGET_UART         " [CDC: target]"
#else
    #define __OPT_TARGET_UART
#endif
#if OPT_SIGROK
    #define __OPT_SIGROK              " [CDC: sigrok]"
#else
    #define __OPT_SIGROK
#endif
#if OPT_CDC_SYSVIEW
    #define __OPT_CDC_SYSVIEW         " [CDC: SysView]"
#else
    #define __OPT_CDC_SYSVIEW
#endif
#if OPT_PROBE_DEBUG_OUT
    #if OPT_PROBE_DEBUG_OUT_CDC
        #define __OPT_PROBE_DEBUG_OUT " [CDC: probe debug]"
    #elif OPT_PROBE_DEBUG_OUT_RTT
        #define __OPT_PROBE_DEBUG_OUT " [RTT: probe debug]"
    #elif OPT_PROBE_DEBUG_OUT_UART
        #define __OPT_PROBE_DEBUG_OUT " [UART: probe debug]"
    #else
        #error "Illegal OPT_PROBE_DEBUG_OUT definition"
    #endif
#else
    #define __OPT_PROBE_DEBUG_OUT
#endif
#if OPT_NET
    #define __OPT_NET_IP              " [Net-"
    #if OPT_NET_PROTO_ECM
        #define __OPT_NET_CONF        __OPT_NET_IP "ECM:"
    #elif OPT_NET_PROTO_NCM
        #define __OPT_NET_CONF        __OPT_NET_IP "NCM:"
    #elif OPT_NET_PROTO_RNDIS
        #define __OPT_NET_CONF        __OPT_NET_IP "RNDIS:"
    #else
        #error "Illegal OPT_NET_PROTO definition"
    #endif
    #define __OPT_NET_CONF_END        "]"
#else
    #define __OPT_NET
    #define __OPT_NET_CONF            ""
    #define __OPT_NET_CONF_END        ""
#endif
#if OPT_NET_SYSVIEW_SERVER
    #define __OPT_NET_SYSVIEW_SERVER  " SysView"
#else
    #define __OPT_NET_SYSVIEW_SERVER
#endif
#if OPT_NET_ECHO_SERVER
    #define __OPT_NET_ECHO_SERVER     " Echo"
#else
    #define __OPT_NET_ECHO_SERVER
#endif
#if OPT_NET_IPERF_SERVER
    #define __OPT_NET_IPERF_SERVER    " IPerf"
#else
    #define __OPT_NET_IPERF_SERVER
#endif


/**
 * CONFIG_FEATURES
 */
#define CONFIG_FEATURES()  __OPT_CMSIS_DAPV1 __OPT_CMSIS_DAPV2 __OPT_MSC __OPT_TARGET_UART __OPT_SIGROK           \
                           __OPT_PROBE_DEBUG_OUT __OPT_CDC_SYSVIEW                                                \
                           __OPT_NET_CONF __OPT_NET_SYSVIEW_SERVER __OPT_NET_ECHO_SERVER __OPT_NET_IPERF_SERVER __OPT_NET_CONF_END

/**
 * CONFIG_BOARD
 */
#if defined(TARGET_BOARD_PICO)
    #define CONFIG_BOARD() "Pico"
#elif defined(TARGET_BOARD_PICO_W)
    #define CONFIG_BOARD() "Pico_W"
#elif defined(TARGET_BOARD_PICO_DEBUG_PROBE)
    #define CONFIG_BOARD() "Pico Debug Probe"
#else
    #define CONFIG_BOARD() "UNKNOWN board"
#endif


/* Contains unique serial number string (NUL terminated) after call to init_usb_serial */
extern char usb_serial[];

void get_config_init(void);

#endif
