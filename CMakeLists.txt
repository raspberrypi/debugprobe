cmake_minimum_required(VERSION 3.14)

include(pico_sdk_import.cmake)

project(picoprobe)

pico_sdk_init()

# Keep at PICO for Raspberry Pi Pico
# Set this to QTPY_WS2812 for QTPy with WS2812
# Set this to QTPY_NO_WS2812 for QTPy without WS2812
set(SELECT_HARDWARE "PICO")

add_executable(picoprobe
               src/led.c
               src/main.c
               src/usb_descriptors.c
               src/probe.c
               src/cdc_uart.c
               src/get_serial.c
)

if (SELECT_HARDWARE STREQUAL "QTPY_WS2812")
    message("Building for Adafruit QTPy RP2040 with WS2812")
    add_definitions(-DUSE_QPTY_SWD -DUSE_QPTY_WS2812)
    target_sources(picoprobe PRIVATE src/ws2812.c)
    pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
elseif (SELECT_HARDWARE STREQUAL "QTPY_NO_WS2812")
    message("Building for Adafruit QTPy RP2040 without WS2812 (SWD only)")
    add_definitions(-DUSE_QPTY_SWD)
else()
    message("Building for Raspberry Pi Pico")
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
