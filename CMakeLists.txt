cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/freertos)
include(FreeRTOS_Kernel_import.cmake)

project(picoprobe)

pico_sdk_init()

add_executable(picoprobe
        src/cdc_uart.c
        src/get_serial.c
        src/led.c
        src/main.c
        src/probe.c
        src/rtt_console.c
        src/sw_dp_pio.c
        src/sw_lock.c
        src/usb_descriptors.c
)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        target_sources(picoprobe PRIVATE
                src/cdc_debug.c
                )
endif()

target_sources(picoprobe PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Source/DAP.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/JTAG_DP.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/DAP_vendor.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/SWO.c
        # CMSIS_5/CMSIS/DAP/Firmware/Source/SW_DP.c
        )

target_include_directories(picoprobe PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Include/
        CMSIS_5/CMSIS/Core/Include/
        include/
        )

#--------------------------------------------------------------------------------------------------
#
# this is all for MSC operation
#
target_sources(picoprobe PRIVATE
        src/msc/msc_drive.c
        src/msc/msc_utils.c
        src/daplink/hic_hal/rp2040/rp2040.c
        src/daplink/interface/swd_host.c
        src/daplink/target/target_family.c
        )

target_include_directories(picoprobe PRIVATE
        src/daplink
        src/daplink/cmsis-dap
        src/daplink/drag-n-drop
        src/daplink/hic_hal
        src/daplink/hic_hal/rp2040
        src/daplink/interface
        src/daplink/target
        $ENV{PICO_SDK_PATH}/src/common/boot_uf2/include
        )

add_compile_definitions(INTERFACE_RP2040=1
                        DAPLINK_IF=1)

#--------------------------------------------------------------------------------------------------

target_compile_options(picoprobe PRIVATE -Wall)

if (DEFINED ENV{PICOPROBE_LED})
        message("PICOPROBE_LED is defined as " $ENV{PICOPROBE_LED})
        target_compile_definitions(picoprobe PRIVATE PICOPROBE_LED=$ENV{PICOPROBE_LED})
endif()

set(DBG_PIN_COUNT=4)

pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)

target_include_directories(picoprobe PRIVATE src)

target_compile_definitions (picoprobe PRIVATE
	PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

target_link_libraries(picoprobe PRIVATE
        pico_multicore
        pico_stdlib
        pico_unique_id
        tinyusb_device
        tinyusb_board
        hardware_pio
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap1
)

#
# code resides in flash (in former versions it was in RAM)
# if you want to force single functions into RAM, use "__no_inline_not_in_flash_func()" from platform.h
# 
pico_set_binary_type(picoprobe default)

pico_add_extra_outputs(picoprobe)
