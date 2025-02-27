/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// pico_cmake_set PICO_PLATFORM=rp2040

// This header may be included by other board headers as "boards/pico_debug_probe.h".
// But normally this is included via "#include <pico/stdlib.h>" if PICO_BOARD is set accordingly.
// Schematic: https://datasheets.raspberrypi.com/debug/raspberry-pi-debug-probe-schematics.pdf

#ifndef _BOARDS_PICO_DEBUG_PROBE_H
#define _BOARDS_PICO_DEBUG_PROBE_H

// For board detection
#define RASPBERRYPI_PICO_DEBUG_PROBE

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
    #define PICO_DEFAULT_LED_PIN 2
#endif


// --- FLASH ---

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 0
#endif



// --- Definitions for YAPicoprobe

#define PICOPROBE_LED            PICO_DEFAULT_LED_PIN
#define PICOPROBE_LED_CONNECTED  15
#define PICOPROBE_LED_RUNNING    16
#define PICOPROBE_LED_TARGET_RX  7                      // host -> probe -> target UART / RTT data, i.e. target is receiving
#define PICOPROBE_LED_TARGET_TX  8                      // target -> probe -> host UART / RTT data, i.e. target is transmitting

// PIO config
#define PROBE_PIO                pio0
#define PROBE_SM                 0
#define PROBE_PIN_OFFSET         11
#define PROBE_PIN_COUNT          4
#define PROBE_PIN_SWDIR          11                     // not used
#define PROBE_PIN_SWCLK          12
#define PROBE_PIN_SWDIN          13                     // dedicated input
#define PROBE_PIN_SWDIO          14                     // used as output
//#define PROBE_PIN_RESET                              // Target reset config (not connected)
//#define PROBE_MAX_KHZ         now in g_board_info.target_cfg->rt_max_swd_kHz, setup in pico::pico_prerun_board_config()

// UART config (UART target -> probe)
#define PICOPROBE_UART_TX        4
#define PICOPROBE_UART_RX        5                      // or 6
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

// Pin usage, this is from pico/pico_w
// Currently sigrok cannot be used on the debug probe, so move sigrok on unused pins
// GP0 and 1 are reserved for debug uart
// GP2-GP22 are digital inputs
// GP23 controls power supply modes and is not a board input
// GP24-25 are not on the board and not used
// GP26-28 are ADC.

// number of analog channels
#define SR_NUM_A_CHAN            2
// first digital channel port
#define SR_BASE_D_CHAN           18
// number of digital channels
#define SR_NUM_D_CHAN            8
// Storage size of the DMA buffer.  The buffer is split into two halves so that when the first
// buffer fills we can send the trace data serially while the other buffer is DMA'd into.
// 102000 buffer size allows 200000 of D4 samples.
#define SR_DMA_BUF_SIZE          10000


#endif
