cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(picoprobe)

pico_sdk_init()

add_executable(picoprobe
        src/main.c
        src/usb_descriptors.c
        src/probe.c
        src/cdc_uart.c
        src/get_serial.c
)

set(DBG_PIN_COUNT=4)

pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)

target_include_directories(picoprobe PRIVATE src)

target_link_libraries(picoprobe PRIVATE pico_stdlib pico_unique_id tinyusb_device tinyusb_board hardware_pio)

pico_add_extra_outputs(picoprobe)
