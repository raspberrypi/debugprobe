/*
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// pico_cmake_set PICO_PLATFORM        = rp2040
// pico_cmake_set PICO_CYW43_SUPPORTED = 1

// This header may be included by other board headers as "boards/pico.h".
// But normally this is included via "#include <pico/stdlib.h>" if PICO_BOARD is set accordingly.

#ifndef _BOARDS_PICO_W_H
#define _BOARDS_PICO_W_H

// For board detection
#define RASPBERRYPI_PICO_W

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
// no PICO_DEFAULT_LED_PIN - LED is on Wireless chip
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

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// note the SMSP mode pin is on WL_GPIO1
// #define PICO_SMPS_MODE_PIN

#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 0
#endif

#ifndef PICO_RP2040_B1_SUPPORTED
#define PICO_RP2040_B1_SUPPORTED 0
#endif

#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 24
#endif

#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 23
#endif

// gpio pin for spi data out to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 24u
#endif

// gpio pin for spi data in from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 24u
#endif

// gpio pin for the spi clock line to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 29u
#endif

// gpio pin for the spi chip select to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 25u
#endif

#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0
#endif

// Pin used to monitor VSYS using ADC
#ifndef PICO_VSYS_PIN
#define PICO_VSYS_PIN 29
#endif


// --- Definitions for YAPicoprobe

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
#define SR_DMA_BUF_SIZE          62000


#endif
