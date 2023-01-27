cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/freertos)
include(FreeRTOS_Kernel_import.cmake)

project(picoprobe)

pico_sdk_init()


if (PICOPROBE_VERSION_MAJOR)
    add_definitions(-DPICOPROBE_VERSION_MAJOR=${PICOPROBE_VERSION_MAJOR})
else()
    set(PICOPROBE_VERSION_MAJOR 0)
endif()
if (PICOPROBE_VERSION_MINOR)
    add_definitions(-DPICOPROBE_VERSION_MINOR=${PICOPROBE_VERSION_MINOR})
else()
    set(PICOPROBE_VERSION_MINOR 0)
endif()

# there are perhaps smarter ways...
if (PICOPROBE_VERSION_MINOR LESS "10")
    add_definitions(-DPICOPROBE_VERSION_STRING="${PICOPROBE_VERSION_MAJOR}.0${PICOPROBE_VERSION_MINOR}")
else()
    add_definitions(-DPICOPROBE_VERSION_STRING="${PICOPROBE_VERSION_MAJOR}.${PICOPROBE_VERSION_MINOR}")
endif()

if (OPTIMIZE_FOR_OPENOCD)
    add_definitions(-DOPTIMIZE_FOR_OPENOCD=${OPTIMIZE_FOR_OPENOCD})
endif()
if (GIT_HASH)
    add_definitions(-DGIT_HASH="${GIT_HASH}")
endif()


add_executable(picoprobe
        src/cdc_uart.c
        src/dap_util.c
        src/get_serial.c
        src/led.c
        src/main.c
        src/misc_utils.c
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
        )

target_include_directories(picoprobe PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Include/
        CMSIS_5/CMSIS/Core/Include/
        include/
        )

#--------------------------------------------------------------------------------------------------
#
# this is for MSC/DAPLink operation
#
target_sources(picoprobe PRIVATE
        src/msc/msc_drive.c
        src/msc/msc_utils.c
        src/daplink/daplink/util.c
        src/daplink/daplink/drag-n-drop/flash_manager.c
        src/daplink/daplink/interface/swd_host.c
        src/daplink/daplink/interface/target_flash.c
        src/daplink/daplink/settings/settings_rom_stub.c
        src/daplink/family/nordic/target_reset_nrf52.c
        src/daplink/family/nordic/nrf52/target.c
        src/daplink/target/target_board.c
        src/daplink/target/target_family.c
        src/daplink-pico/board/rp2040/pico.c
        src/daplink-pico/board/rp2040/pico_target_utils.c
        src/daplink-pico/board/rp2040/program_flash_generic.c
        src/daplink-pico/family/raspberry/rp2040/target_reset_rp2040.c
        )

target_include_directories(picoprobe PRIVATE
        src/daplink/daplink
        src/daplink/daplink/cmsis-dap
        src/daplink/daplink/drag-n-drop
        src/daplink/daplink/interface
        src/daplink/daplink/settings
        src/daplink/hic_hal
        src/daplink/target
        src/daplink-pico/board/rp2040
        src/daplink-pico/family/raspberry
        src/daplink-pico/hic_hal/raspberry/rp2040
        $ENV{PICO_SDK_PATH}/src/common/boot_uf2/include
        )

# Both are actually dummies to prevent the DAPLink files from throwing errors
# Actually not required because picoprobes structure is different
add_compile_definitions(INTERFACE_RP2040=1
                        DAPLINK_IF=1)

# not sure if this is still required, but this equals to DAPLINK_HIC_ID_RP2040 in daplink.h
add_compile_definitions(DAPLINK_HIC_ID=0x08154711)

# required to get required DAPLink functions
add_compile_definitions(DRAG_N_DROP_SUPPORT=1)

#--------------------------------------------------------------------------------------------------
#
# this is for SIGROK operation (taken from https://github.com/pico-coder/sigrok-pico)
#
target_sources(picoprobe PRIVATE
        src/pico-sigrok/cdc_sigrok.c
        src/pico-sigrok/sigrok.c
        src/pico-sigrok/sigrok_int.c
        )

target_link_libraries(picoprobe PRIVATE
        hardware_adc
        hardware_dma
    )

pico_generate_pio_header(picoprobe ${CMAKE_CURRENT_LIST_DIR}/src/pico-sigrok/sigrok.pio)

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
# TODO this is for newer SDK!?
#	PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
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
