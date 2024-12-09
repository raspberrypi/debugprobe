# Makefile to build YAPicoprobe
#
# ATTENTION: to get the version number & git hash into the image, cmake-create-* has to be invoked.
#
VERSION_MAJOR        := 1
VERSION_MINOR        := 23

BUILD_DIR            := _build
PROJECT              := picoprobe


GIT_HASH := $(shell git rev-parse --short HEAD)

CMAKE_FLAGS  = -DPICOPROBE_VERSION_MAJOR=$(VERSION_MAJOR)
CMAKE_FLAGS += -DPICOPROBE_VERSION_MINOR=$(VERSION_MINOR)
CMAKE_FLAGS += -DPROJECT=$(PROJECT)
CMAKE_FLAGS += -DGIT_HASH=$(GIT_HASH)
CMAKE_FLAGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=True

ifeq ($(PICO_BOARD),)
    # pico|pico_w|pico_debug_probe|pico2
    PICO_BOARD := pico2
endif



.PHONY: clean
clean:
	ninja -C $(BUILD_DIR) -v clean


.PHONY: clean-build
clean-build:
	rm -rf $(BUILD_DIR)


.PHONY: all
all:
	ninja -C $(BUILD_DIR) -v all
	@echo "--------------------------"
	@arm-none-eabi-size -Ax $(BUILD_DIR)/$(PROJECT).elf | awk '{size=strtonum($$2); addr=strtonum($$3); if (addr>=0x20000000 && addr<0x20040000) ram += size; if (addr>=0x10000000 && addr<0x20000000) rom += size; } END {print "Flash: " rom "  RAM: " ram}'
	@echo "--------------------------"


.PHONY: details
details: all
	@arm-none-eabi-size -Ax $(BUILD_DIR)/$(PROJECT).elf


.PHONY: cmake-create-debug
cmake-create-debug: clean-build
	export PICO_COMPILER=pico_arm_gcc;                     \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK)) $(CMAKE_FLAGS)


.PHONY: cmake-create-release
cmake-create-release: clean-build
	export PICO_COMPILER=pico_arm_gcc;                     \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) $(CMAKE_FLAGS)


.PHONY: cmake-create-debug-clang
cmake-create-debug-clang: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi-18.1.3/bin; \
	export PICO_COMPILER=pico_arm_clang;                     \
	export xxPICO_CLIB=llvm-libc;                                 \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) \
	         $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK)) \
	         $(CMAKE_FLAGS) -DPICO_COMPILER=pico_arm_clang


.PHONY: cmake-create-release-clang
cmake-create-release-clang: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin; \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) \
	         $(CMAKE_FLAGS) -DPICO_COMPILER=pico_arm_clang


.PHONY: cmake-create-minsizerel-clang
cmake-create-minsizerel-clang: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin; \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) \
	         $(CMAKE_FLAGS) -DPICO_COMPILER=pico_arm_clang


.PHONY: flash
flash: all
	@echo "Waiting for RPi bootloader..."
	@until [ -f /media/pico/INDEX.HTM ]; do sleep 0.1; done; echo "ready!"
	cp $(BUILD_DIR)/$(PROJECT).uf2 /media/pico
	@echo "ok."


.PHONY: create-images
create-images:
	# with SDK2 clang no longer works.  This is a TODO
	mkdir -p images
	#
	$(MAKE) cmake-create-release PICO_BOARD=pico
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-pico-$(GIT_HASH).uf2
	#
	# does not compile with clang because of missing __heap_start/end
	#$(MAKE) cmake-create-release PICO_BOARD=pico_w
	#$(MAKE) all
	#cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-picow-$(GIT_HASH).uf2
	#
	$(MAKE) cmake-create-release PICO_BOARD=pico_debug_probe
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-picodebugprobe-$(GIT_HASH).uf2
	#
	$(MAKE) cmake-create-release PICO_BOARD=pico2
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-pico2-$(GIT_HASH).uf2
	#
	# put development environment in a clean state
	$(MAKE) cmake-create-debug


.PHONY: check-clang
check-clang:
	# clang-tidy has its limit if another target is used...
	@cd $(BUILD_DIR) && run-clang-tidy -checks='-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling'

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
DEBUGGER_SERNO ?= E6614C775B333D35
OPENOCD := /mnt/d/u/pico/arduino-pico/system/openocd/bin/openocd   #/home/hardy/.platformio/packages/tool-openocd-rp2040-earlephilhower/bin/openocd

.PHONY: debuggEE-flash
debuggEE-flash:
	ninja -C $(BUILD_DIR) -v all  &&  $(OPENOCD) -f /mnt/d/u/pico/arduino-pico/lib/picoprobe_cmsis_dap.tcl -c "adapter speed 12000; adapter serial $(DEBUGGER_SERNO)" \
	        -c "program {$(BUILD_DIR)/$(PROJECT).hex}  verify reset; shutdown;"
	#$(MAKE) debuggEE-reset
	pyocd reset -t rp2040_core0 -f 10M --probe $(DEBUGGER_SERNO)
	@echo "ok."


.PHONY: debuggEE-reset
debuggEE-reset:
	pyocd reset -t rp2040_core1 -f 10M --probe $(DEBUGGER_SERNO)
	pyocd reset -t rp2040_core0 -f 10M --probe $(DEBUGGER_SERNO)


.PHONY: cmake-create-debuggEE
cmake-create-debuggEE: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin; \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) \
	         $(CMAKE_FLAGS) -DPICO_COMPILER=pico_arm_clang                                                               \
	         -DOPT_NET=NCM -DOPT_PROBE_DEBUG_OUT=RTT                                                                     \
	         -DOPT_SIGROK=0 -DOPT_MSC=0 -DOPT_CMSIS_DAPV1=0 -DOPT_CMSIS_DAPV2=0 -DOPT_TARGET_UART=0


.PHONY: cmake-create-debugger
cmake-create-debugger: clean-build
	export PICO_TOOLCHAIN_PATH=~/bin/llvm-arm-none-eabi/bin; \
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) \
	         $(CMAKE_FLAGS) -DPICO_COMPILER=pico_arm_clang                                                                 \
	         -DOPT_NET= -DOPT_SIGROK=0 -DOPT_MSC=0
