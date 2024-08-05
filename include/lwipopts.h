/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt
 *
 */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#define NO_SYS                          0
#define MEM_ALIGNMENT                   4
#define LWIP_IPV6                       0                         // default: 0
#define LWIP_RAW                        0                         // default: 0
#define LWIP_NETCONN                    0                         // default: 0
#define LWIP_SOCKET                     0                         // default: 0, provide socket API
#define LWIP_UDP                        1
#define LWIP_TCP                        1

// ARP
#define LWIP_ARP                        1                         // default: 1
//#define ETH_PAD_SIZE                    0
//#define ETHARP_SUPPORT_STATIC_ENTRIES   1
#define ARP_TABLE_SIZE                  4

// ICMP
#define LWIP_ICMP                       1                         // default: 1
//#define LWIP_MULTICAST_PING             1
//#define LWIP_BROADCAST_PING             1

// DHCP, required to give the host an IP
#define LWIP_DHCP                       1
//#define LWIP_IP_ACCEPT_UDP_PORT(p)      ((p) == PP_NTOHS(67))

// AUTOIP
//#define LWIP_AUTOIP                     1                         // default: 0
//#define LWIP_DHCP_AUTOIP_COOP           (LWIP_DHCP && LWIP_AUTOIP)

// netif
#define LWIP_SINGLE_NETIF               1


//--------------------------------------
// performance tuning (do not change without extensive testing, optimized for ECM/NCM)
#define TCP_MSS                                (1500 - 20 - 20)    // MTU minus header sizes (best value til now)
#define TCP_SND_BUF                            (8 * TCP_MSS)       //   good tuning

#define TCP_WND                                (4 * TCP_MSS)       // til now no good value found
#define TCP_SND_QUEUELEN                       16
#define TCP_SNDQUEUELOWAT                      (TCP_SND_QUEUELEN / 2)
#define MEMP_NUM_TCP_SEG                       32

#define PBUF_POOL_SIZE                         4

//--------------------------------------
// memory
#define MEM_SIZE                               20000
//#define MEMP_OVERFLOW_CHECK                    1
//#define LWIP_ALLOW_MEM_FREE_FROM_OTHER_CONTEXT 1


//--------------------------------------
// for freertos mode
#define TCPIP_MBOX_SIZE                        64
#define TCPIP_THREAD_STACKSIZE                 8192
#define TCPIP_THREAD_PRIO                      27


//--------------------------------------
// trying...
#define LWIP_PROVIDE_ERRNO              1
#if LWIP_SOCKET
    #define LWIP_TIMEVAL_PRIVATE        0                          // required for LWIP_SOCKET
#endif


//--------------------------------------
// statistics
// use stats_display() for display
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0
#define ETHARP_STATS                    0                          // do not display the topics below
#define ICMP_STATS                      0
#define IPFRAG_STATS                    0
#define LINK_STATS                      0
#define MEMP_STATS                      0
#define SYS_STATS                       0
#define UDP_STATS                       0


//--------------------------------------
// debugging
//#define LWIP_DEBUG
#define API_LIB_DEBUG                   LWIP_DBG_OFF
#define API_MSG_DEBUG                   LWIP_DBG_OFF
#define AUTOIP_DEBUG                    LWIP_DBG_OFF
#define DHCP_DEBUG                      LWIP_DBG_OFF
#define DNS_DEBUG                       LWIP_DBG_OFF
#define ETHARP_DEBUG                    LWIP_DBG_OFF
#define ICMP_DEBUG                      LWIP_DBG_OFF
#define IGMP_DEBUG                      LWIP_DBG_OFF
#define INET_DEBUG                      LWIP_DBG_OFF
#define IP_DEBUG                        LWIP_DBG_OFF
#define NETIF_DEBUG                     LWIP_DBG_OFF
#define PBUF_DEBUG                      LWIP_DBG_OFF
#define RAW_DEBUG                       LWIP_DBG_OFF
#define SLIP_DEBUG                      LWIP_DBG_OFF
#define SOCKETS_DEBUG                   LWIP_DBG_OFF
#define SYS_DEBUG                       LWIP_DBG_OFF
#define TCP_DEBUG                       LWIP_DBG_OFF
#define TCP_INPUT_DEBUG                 LWIP_DBG_OFF
#define TCP_FR_DEBUG                    LWIP_DBG_OFF
#define TCP_RTO_DEBUG                   LWIP_DBG_OFF
#define TCP_CWND_DEBUG                  LWIP_DBG_ON
#define TCP_WND_DEBUG                   LWIP_DBG_OFF
#define TCP_RST_DEBUG                   LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG                  LWIP_DBG_OFF
#define TCPIP_DEBUG                     LWIP_DBG_OFF
#define TIMERS_DEBUG                    LWIP_DBG_OFF
#define UDP_DEBUG                       LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
