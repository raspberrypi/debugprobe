cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(picoprobe)

pico_sdk_init()

add_executable(picoprobe
        src/led.c
        src/main.c
        src/usb_descriptors.c
        src/probe.c
        src/cdc_uart.c
        src/cdc_sump.c
        src/get_serial.c
)

if (DEFINED ENV{PICOPROBE_LED})
        message("PICOPROBE_LED is defined as " $ENV{PICOPROBE_LED})
        target_compile_definitions(picoprobe PRIVATE PICOPROBE_LED=$ENV{PICOPROBE_LED})
endif()

if (DEFINED ENV{TURBO_200MHZ})
        message("TURBO_200MHZ is defined as " $ENV{TURBO_200MHZ})
        target_compile_definitions(picoprobe PRIVATE TURBO_200MHZ=$ENV{TURBO_200MHZ})
endif()

set(DBG_PIN_COUNT=4)

pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)

target_include_directories(picoprobe PRIVATE src)

target_link_libraries(picoprobe PRIVATE pico_stdlib pico_unique_id
		      tinyusb_device tinyusb_board
		      hardware_pio hardware_dma hardware_pwm)

pico_add_extra_outputs(picoprobe)
