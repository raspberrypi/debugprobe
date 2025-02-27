/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// This header may be included by other board headers as "boards/pico.h".
// But normally this is included via "#include <pico/stdlib.h>" if PICO_BOARD is set accordingly.

// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_PICO2_H
#define _BOARDS_PICO2_H

// For board detection
#define RASPBERRYPI_PICO2

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif
// no PICO_DEFAULT_WS2812_PIN

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (4 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif
// Drive high to force power supply into PWM mode (lower ripple on 3V3 at light loads)
#define PICO_SMPS_MODE_PIN 23

// The GPIO Pin used to read VBUS to determine if the device is battery powered.
#ifndef PICO_VBUS_PIN
#define PICO_VBUS_PIN 24
#endif

// The GPIO Pin used to monitor VSYS. Typically you would use this with ADC.
// There is an example in adc/read_vsys in pico-examples.
#ifndef PICO_VSYS_PIN
#define PICO_VSYS_PIN 29
#endif

#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

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
