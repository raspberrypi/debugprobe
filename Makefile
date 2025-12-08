# Makefile to build YAPicoprobe
#
# ATTENTION: to get the version number & git hash into the image, cmake-create-* has to be invoked.
#
VERSION_MAJOR        := 1
VERSION_MINOR        := 25

BUILD_DIR            := _build
PROJECT              := picoprobe


GIT_HASH := $(shell git rev-parse --short HEAD)

CMAKE_FLAGS  = -DPICOPROBE_VERSION_MAJOR=$(VERSION_MAJOR)
CMAKE_FLAGS += -DPICOPROBE_VERSION_MINOR=$(VERSION_MINOR)
CMAKE_FLAGS += -DPROJECT=$(PROJECT)
CMAKE_FLAGS += -DGIT_HASH=$(GIT_HASH)
CMAKE_FLAGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=1

# speeds up a lot if often switching configurations, see benchmarking of create-images below
#CMAKE_FLAGS += -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

ifeq ($(PICO_BOARD),)
    # pico|pico_w|pico_debug_probe|pico2
    PICO_BOARD := pico2
endif



.PHONY: clean
clean:
	ninja -C $(BUILD_DIR) clean


.PHONY: clean-build
clean-build:
	-rm -rf $(BUILD_DIR)


.PHONY: all
all:
	ninja -C $(BUILD_DIR) all
	@echo "--------------------------"
	@arm-none-eabi-size -Ax $(BUILD_DIR)/$(PROJECT).elf | awk '{size=strtonum($$2); addr=strtonum($$3); if (addr>=0x20000000 && addr<0x20040000) ram += size; if (addr>=0x10000000 && addr<0x20000000) rom += size; } END {print "Flash: " rom "  RAM: " ram}'
	@echo "--------------------------"


.PHONY: details
details: all
	@arm-none-eabi-size -Ax $(BUILD_DIR)/$(PROJECT).elf


# cmake parameter: -DPICO_CLIB=llvm_libc;picolibc;newlib
#     gcc                 14.2.1
#         develop-8fcd
#             llvm_libc    FAIL      (mixture of newlib/llvm_libc)
#             picolibc     FAIL      (FDEV_SETUP_STREAM is unknown, mixture if newlib/picolibc)
#             newlib        OK       (same as -DPICO_CLIB=)
#         new
#             llvm_libc    FAIL      (mixture of newlib/llvm_libc)
#             picolibc     FAIL      (FDEV_SETUP_STREAM is unknown, mixture if newlib/picolibc)
#             newlib        OK       (same as -DPICO_CLIB=)
.PHONY: cmake-create-debug
cmake-create-debug: clean-build
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPICO_BOARD=$(PICO_BOARD)                                 \
	      $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK)) $(CMAKE_FLAGS)                                                \
	      -DPICO_CLIB=newlib                                                                                           \
	      $(CMAKE_FLAGS)



.PHONY: cmake-create-release
cmake-create-release: clean-build
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=$(PICO_BOARD)                               \
	      $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK)) $(CMAKE_FLAGS)                                                \
	      -DPICO_CLIB=newlib                                                                                           \
	      $(CMAKE_FLAGS)


#
# cmake parameter: -DPICO_CLIB=llvm_libc;picolibc;newlib
#     clang                19.1.5   21.1.1
#         develop-8fcd
#              llvm_libc    FAIL     FAIL      (19.1.5: wrong header, 21.1.1: unknown _BEGIN_STD_C)
#              picolibc      OK      FAIL      (21.1.1: cmake fails, same as -DPICO_CLIB=)
#              newlib        OK      FAIL      (19.1.5: program claims picolibc, 21.1.1: cmake fails)
#         new
#              llvm_libc -  FAIL      OK       (19.1.5: llvm_libc has some incompatibilities)
#              picolibc  -   OK       OK
#              newlib    -   OK       OK       (same as -DPICO_CLIB=)
#
#     links:
#     - https://github.com/arm/arm-toolchain/tree/arm-software
#     - https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm
#
.PHONY: cmake-create-debug-clang
cmake-create-debug-clang: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin;                                                           \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPICO_BOARD=$(PICO_BOARD)                                 \
	         $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK))                                                            \
 	         -DPICO_CLIB=newlib                                                                                        \
	         -DPICO_COMPILER=pico_arm_clang                                                                            \
	         $(CMAKE_FLAGS)


.PHONY: cmake-create-release-clang
cmake-create-release-clang: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin;                                                           \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=$(PICO_BOARD)                               \
	         $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK))                                                            \
	         -DPICO_CLIB=newlib                                                                                        \
	         -DPICO_COMPILER=pico_arm_clang                                                                            \
	         $(CMAKE_FLAGS)


.PHONY: cmake-create-minsizerel-clang
cmake-create-minsizerel-clang: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin;                                                           \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_BOARD=$(PICO_BOARD)                            \
	         $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK))                                                            \
	         -DPICO_CLIB=newlib                                                                                        \
	         -DPICO_COMPILER=pico_arm_clang                                                                            \
	         $(CMAKE_FLAGS) 


.PHONY: flash
flash: all
	@echo "Waiting for RPi bootloader..."
	@until [ -f /media/pico/INDEX.HTM ]; do sleep 0.1; done; echo "ready!"
	cp $(BUILD_DIR)/$(PROJECT).uf2 /media/pico
	@echo "ok."


#
# benchmarking                      first  second
# clang 21.1.1 / newlib / ccache :   4:50    1:10     (NTFS)
# clang 21.1.1 / newlib          :   2:42             (NTFS)
# gcc 15.2.0   / newlib / ccache :  12:06    0:53     (NTFS)
# gcc 15.2.0   / newlib          :   6:45             (NTFS)
#
# clang 21.1.1 / newlib          :   0:26             (ext4)  !!!!
# gcc 15.2.0   / newlib          :   0:27             (ext4)  !!!!
#
.PHONY: create-images
create-images:
	# with SDK2 clang no longer works.  This is a TODO
	mkdir -p images
	#
	$(MAKE) cmake-create-release-clang PICO_BOARD=pico
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-pico-$(GIT_HASH).uf2
	#
	# does not compile with clang because of missing __heap_start/end
	$(MAKE) cmake-create-release-clang PICO_BOARD=pico_w
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-picow-$(GIT_HASH).uf2
	#
	$(MAKE) cmake-create-release-clang PICO_BOARD=pico_debug_probe
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-picodebugprobe-$(GIT_HASH).uf2
	#
	$(MAKE) cmake-create-release-clang PICO_BOARD=pico2
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-pico2-$(GIT_HASH).uf2
	#
	# put development environment in a clean state
	$(MAKE) cmake-create-debug-clang


.PHONY: check-clang
check-clang:
	find src -type f -iname "*.c*" | xargs run-clang-tidy -p _build -header-filter=.*   \
	-checks='-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-analyzer-security.insecureAPI.strcpy'


.PHONY: show-options
show-options:
	@cd $(BUILD_DIR) && cmake -LH . | sed -n -e '/OPT_/{x;1!p;g;$!N;p;D;}' -e h


#
# The following targets are for debugging the probe itself.
# Therefor a debugger and a debuggEE probe needs to be configured.
# - debugger probe has all features except net/sysview
# - debuggEE probe has just net and debug output goes to RTT
# Additionally there is a special target for flashing and resetting the debuggEE probe.
# Notes
# - debugger probe is untouched after initial setup
# - debugger probe is flashed with the standard procedure
# - most work is done in the debuggEE
# 
DEBUGGER_SERNO ?= 2739E00F30FE67E7
OPENOCD_R := /home/hardy/.pico-sdk/openocd/0.12.0+dev
OPENOCD   := $(OPENOCD_R)/openocd
OPENOCD_S := $(OPENOCD_R)/scripts
DEBUGGEE_CLIB := picolibc
#DEBUGGEE_CLIB := newlib
#DEBUGGEE_CLIB := llvm_libc

.PHONY: debuggEE-flash
debuggEE-flash:
	$(MAKE) all
	$(OPENOCD) -s $(OPENOCD_S) -f interface/cmsis-dap.cfg -f target/rp2350.cfg                                         \
	           -c "adapter speed 10000; adapter serial $(DEBUGGER_SERNO)"                                              \
	           -c "program {$(BUILD_DIR)/$(PROJECT).hex}  verify reset; exit;"
	# attention: pyocd hijacks the previous connection (or insert a delay of 1s)
	sleep 1s
	pyocd reset -f 1M --probe $(DEBUGGER_SERNO)
	@echo "ok."


.PHONY: debuggEE-reset
debuggEE-reset:
	pyocd reset -f 1M --probe $(DEBUGGER_SERNO)


.PHONY: cmake-create-debuggEE
cmake-create-debuggEE: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin;                                                           \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPICO_BOARD=$(PICO_BOARD)                                 \
	         $(CMAKE_FLAGS) -DPICO_COMPILER=pico_arm_clang                                                             \
	         -DPICO_CLIB=$(DEBUGGEE_CLIB)                                                                              \
	         -DOPT_NET=NCM -DOPT_PROBE_DEBUG_OUT=RTT                                                                   \
	         -DOPT_SIGROK=0 -DOPT_MSC=0 -DOPT_CMSIS_DAPV1=0 -DOPT_CMSIS_DAPV2=0 -DOPT_TARGET_UART=0


.PHONY: cmake-create-debugger
cmake-create-debugger: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin;                                                           \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=$(PICO_BOARD)                               \
	         $(CMAKE_FLAGS)                                                                                            \
	         -DPICO_COMPILER=pico_arm_clang                                                                            \
	         -DPICO_CLIB=newlib                                                                                        \
	         -DOPT_NET= -DOPT_SIGROK=0 -DOPT_MSC=0
