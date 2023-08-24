cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/freertos)
include(FreeRTOS_Kernel_import.cmake)

project(picoprobe)

pico_sdk_init()

add_executable(picoprobe
        src/led.c
        src/main.c
        src/usb_descriptors.c
        src/probe.c
        src/cdc_uart.c
        src/get_serial.c
        src/sw_dp_pio.c
        src/tusb_edpt_handler.c
)

target_sources(picoprobe PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Source/DAP.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/JTAG_DP.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/DAP_vendor.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/SWO.c
        #CMSIS_5/CMSIS/DAP/Firmware/Source/SW_DP.c
        )

target_include_directories(picoprobe PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Include/
        CMSIS_5/CMSIS/Core/Include/
        include/
        )

target_compile_options(picoprobe PRIVATE -Wall)

pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)
pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe_oen.pio)

target_include_directories(picoprobe PRIVATE src)

target_compile_definitions (picoprobe PRIVATE
	PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

option (DEBUGPROBE "compile for the debugprobe" OFF)
if (DEBUGPROBE)
    target_compile_definitions (picoprobe PRIVATE 
	DEBUGPROBE=1
    )
    set_target_properties(picoprobe PROPERTIES 
        OUTPUT_NAME "debugprobe"
    )
endif ()


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

pico_set_binary_type(picoprobe copy_to_ram)

pico_add_extra_outputs(picoprobe)
