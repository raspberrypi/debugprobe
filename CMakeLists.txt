cmake_minimum_required(VERSION 3.14)

include(pico_sdk_import.cmake)

project(picoprobe)

pico_sdk_init()

# On the next line, retain PICO for Raspberry Pi Pico,
# or set 'SELECT_HARDWARE' to:
#   QTPY    for Adafruit QTPy with WS2812
#   FEATHER for Adafruit Feather RP2040 with WS2812
#   SPARKPM for SparkFun Pro Micro RP2040 with WS2812
set(SELECT_HARDWARE "PICO")

# PICO  | PIN |
# ----  | --- |
# SWDIO | 3   |
# SWCLK | 2   |

# PIN            | QTPY | FEATHER | SPARKPM |
# -------------- | ---- | ------- | ------- |
# STEMMA SWDIO Y | 23   | 3       | 17      |
# STEMMA SWCLK B | 22   | 2       | 16      |
# NEOPIXEL PWR   | 11   | N/A     | N/A     |
# NEOPIXEL IN    | 12   | 16      | 25      |

add_executable(picoprobe
               src/led.c
               src/main.c
               src/usb_descriptors.c
               src/probe.c
               src/cdc_uart.c
               src/get_serial.c
)

if (SELECT_HARDWARE STREQUAL "QTPY")
    message("Building for Adafruit QTPy RP2040 with WS2812")
    add_definitions(-DUSE_QTPY_SWD -DUSE_WS2812)
    add_compile_definitions(NEO_PIN_PWR=11;NEO_PIN_DAT=12)
elseif (SELECT_HARDWARE STREQUAL "FEATHER")
    message("Building for Adafruit Feather RP2040 with WS2812")
    add_definitions(-DUSE_WS2812)
    add_compile_definitions(NEO_PIN_DAT=16)
elseif (SELECT_HARDWARE STREQUAL "SPARKPM")
    message("Building for SparkFun Pro Micro RP2040 with WS2812")
    add_definitions(-DUSE_SFPM_SWD -DUSE_WS2812)
    add_compile_definitions(NEO_PIN_DAT=25)
else()
    message("Building for Raspberry Pi Pico")
endif()

if (NOT SELECT_HARDWARE STREQUAL "PICO")
    target_sources(picoprobe PRIVATE src/ws2812.c)
    pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
    add_compile_definitions(RGB_COLOUR=0x001010) # CYAN -- Dilute to taste!
endif()

if (DEFINED ENV{PICOPROBE_LED})
    message("PICOPROBE_LED is defined as " $ENV{PICOPROBE_LED})
    target_compile_definitions(picoprobe PRIVATE PICOPROBE_LED=$ENV{PICOPROBE_LED})
endif()

set(DBG_PIN_COUNT=4)

pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)

target_include_directories(picoprobe PRIVATE src)

target_link_libraries(picoprobe
                      PRIVATE
                      pico_stdlib
                      pico_unique_id
                      tinyusb_device
                      tinyusb_board
                      hardware_pio)

pico_add_extra_outputs(picoprobe)
