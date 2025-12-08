/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PROBE_BOARDS_PICO_H
#define _PROBE_BOARDS_PICO_H

// required to set some paths
pico_board_cmake_set(PICO_PLATFORM, rp2040)

#include "boards/pico.h"

// --- Definitions for YAPicoprobe

// Base value of sys_clk in MHz.  Must be <=125MHz per RP2040 spec and a multiple of 24MHz
// to support integer divisors of the PIO clock and ADC clock (for sigrok).
// Can be overridden via configuration.
#ifdef OPT_MCU_OVERCLOCK_MHZ
    #define PROBE_CPU_CLOCK_MHZ  OPT_MCU_OVERCLOCK_MHZ
#else
    #define PROBE_CPU_CLOCK_MHZ  120
#endif
#define PROBE_CPU_CLOCK_MIN_MHZ  (5 * 24)
#define PROBE_CPU_CLOCK_MAX_MHZ  (12 * 24)

// LED config
#define PROBE_LED                PICO_DEFAULT_LED_PIN

// PIO config
#define PROBE_PIO                pio0
#define PROBE_SM                 0
#define PROBE_PIN_OFFSET         1
#define PROBE_PIN_COUNT          3
#define PROBE_PIN_SWDIR          (PROBE_PIN_OFFSET + 0) // 1
#define PROBE_PIN_SWCLK          (PROBE_PIN_OFFSET + 1) // 2
#define PROBE_PIN_SWDIO          (PROBE_PIN_OFFSET + 2) // 3
#define PROBE_PIN_RESET          6                      // Target reset config
//#define PROBE_MAX_KHZ         now in g_board_info.target_cfg->rt_max_swd_kHz, setup in pico::pico_prerun_board_config()

// UART config (UART target -> probe)
#define PROBE_UART_TX            4
#define PROBE_UART_RX            5
#define PROBE_UART_INTERFACE     uart1
#define PROBE_UART_BAUDRATE      115200

// sigrok config
#define SIGROK_PIO               pio1
#define SIGROK_SM                0                      // often hard coded

//Pin usage
//GP0 and 1 are reserved for debug uart
//GP2-GP22 are digital inputs
//GP23 controls power supply modes and is not a board input
//GP24-25 are not on the board and not used
//GP26-28 are ADC.

// number of analog channels
#define SR_NUM_A_CHAN            3
// first digital channel port
#define SR_BASE_D_CHAN           10
// number of digital channels
#define SR_NUM_D_CHAN            8
// Storage size of the DMA buffer.  The buffer is split into two halves so that when the first
// buffer fills we can send the trace data serially while the other buffer is DMA'd into.
// 102000 buffer size allows 200000 of D4 samples.
#define SR_DMA_BUF_SIZE          102000


#endif
