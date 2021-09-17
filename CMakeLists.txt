cmake_minimum_required(VERSION 3.14)

include(pico_sdk_import.cmake)

project(picoprobe)

# If the env var 'PICO_BOARD' is set in the calling shell
# eg. export PICO_BOARD=adafruit_qtpy_rp2040
# then use it here
if (NOT DEFINED ENV{PICO_BOARD})
    # Otherwise use PICO_BOARD as defined here, which is handy when
    # you're running CMake from an IDE (eg. VSCode) rather than the
    # commmand line.
    # Set 'PICO_BOARD' to a board name as defined in
    # https://github.com/raspberrypi/pico-sdk/tree/master/src/boards/include/boards
    #
    # For example,
    #    "adafruit_qtpy_rp2040"    for the Adafruit QTPy
    #    "adafruit_feather_rp2040" for the Adafruit Feather RP2040
    #    "sparkfun_promicro"       for the SparkFun Pro Micro RP2040
    #
    # The default is "pico"

    # Set PICO_BORD here so that the SDK correctly applies it
    set(PICO_BOARD "pico")
    message("Environment variable PICO_BOARD undefined -- using \"pico\"")
else()
    # Set PICO_BORD here, from the env var, so that the SDK correctly applies it
    set(PICO_BOARD "$ENV{PICO_BOARD}")
    message("Environment variable PICO_BOARD defined -- using $ENV{PICO_BOARD}")
endif()

set(EXECUTABLE_NAME "picoprobe_${PICO_BOARD}")

pico_sdk_init()

add_executable(${EXECUTABLE_NAME}
               src/led.c
               src/main.c
               src/usb_descriptors.c
               src/probe.c
               src/cdc_uart.c
               src/get_serial.c
)

if (${PICO_BOARD} STREQUAL "adafruit_qtpy_rp2040")
    message("Building for Adafruit QTPy RP2040 with WS2812")
elseif (${PICO_BOARD} STREQUAL "adafruit_feather_rp2040")
    message("Building for Adafruit Feather RP2040 with WS2812")
elseif (${PICO_BOARD} STREQUAL "sparkfun_promicro")
    message("Building for SparkFun Pro Micro RP2040 with WS2812")
else()
    message("Building for Raspberry Pi Pico")
endif()

if (NOT ${PICO_BOARD} STREQUAL "pico")
    target_sources(${EXECUTABLE_NAME} PRIVATE src/ws2812.c)
    pico_generate_pio_header(${EXECUTABLE_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
    add_compile_definitions(RGB_COLOUR=0x001010) # CYAN -- Dilute to taste!
endif()

if (DEFINED ENV{PICOPROBE_LED})
    message("PICOPROBE_LED is defined as " $ENV{PICOPROBE_LED})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE PICOPROBE_LED=$ENV{PICOPROBE_LED})
endif()

set(DBG_PIN_COUNT=4)

pico_generate_pio_header(${EXECUTABLE_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)

target_include_directories(${EXECUTABLE_NAME} PRIVATE src)

target_link_libraries(${EXECUTABLE_NAME}
                      PRIVATE
                      pico_stdlib
                      pico_unique_id
                      tinyusb_device
                      tinyusb_board
                      hardware_pio)

pico_add_extra_outputs(${EXECUTABLE_NAME})
