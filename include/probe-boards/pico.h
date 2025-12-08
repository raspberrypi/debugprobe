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

#define PICOPROBE_LED            PICO_DEFAULT_LED_PIN

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
