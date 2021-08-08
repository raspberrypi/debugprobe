cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(picoprobe)

pico_sdk_init()

# Uncomment the following line to use QTPy RP2040 SWD and WS2812 RGB LED
#add_definitions(-DUSE_QPTY_SWD -DUSE_QPTY_WS2812)

add_executable(picoprobe
        src/led.c
        src/main.c
        src/usb_descriptors.c
        src/probe.c
        src/cdc_uart.c
        src/get_serial.c
)

if (DEFINED USE_QPTY_WS2812)
    pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

    add_executable(picoprobe
                   src/ws2812.c
    )
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
