# Makefile to build YAPicoprobe
#
# ATTENTION: to get the version number & git hash into the image, cmake-create-* has to be invoked.
#

VERSION_MAJOR        := 1
VERSION_MINOR        := 16

BUILD_DIR            := _build
PROJECT              := picoprobe


GIT_HASH := $(shell git rev-parse --short HEAD)

CMAKE_FLAGS  = -DPICOPROBE_VERSION_MAJOR=$(VERSION_MAJOR)
CMAKE_FLAGS += -DPICOPROBE_VERSION_MINOR=$(VERSION_MINOR)
CMAKE_FLAGS += -DPROJECT=$(PROJECT)
CMAKE_FLAGS += -DGIT_HASH=$(GIT_HASH)
CMAKE_FLAGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=True

ifeq ($(PICO_BOARD),)
    # pico|pico_w|pico_debug_probe
    PICO_BOARD := pico
endif



.PHONY: clean
clean:
	ninja -C $(BUILD_DIR) -v clean


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
cmake-create-debug:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) $(if $(OPT_SIGROK),-DOPT_SIGROK=$(OPT_SIGROK)) $(CMAKE_FLAGS)
    # don't know if this is required
	@cd $(BUILD_DIR) && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: cmake-create-release
cmake-create-release:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DPICO_BOARD=$(PICO_BOARD) $(CMAKE_FLAGS)
    # don't know if this is required
	@cd $(BUILD_DIR) && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: clean-build
clean-build:
	rm -rf $(BUILD_DIR)


.PHONY: flash
flash: all
	@echo "Waiting for RPi bootloader..."
	@until [ -f /media/pico/INDEX.HTM ]; do sleep 0.1; done; echo "ready!"
	cp $(BUILD_DIR)/$(PROJECT).uf2 /media/pico
	@echo "ok."


.PHONY: create-images
create-images:
	$(MAKE) clean-build
	$(MAKE) cmake-create-release PICO_BOARD=pico OPT_SIGROK=0
	$(MAKE) all
	mkdir -p images
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-pico-$(GIT_HASH).uf2
	#
	$(MAKE) cmake-create-release PICO_BOARD=pico_w OPT_SIGROK=0
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-picow-$(GIT_HASH).uf2
	#
	$(MAKE) cmake-create-release PICO_BOARD=pico_debug_probe OPT_SIGROK=0
	$(MAKE) all
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02d%02d" $(VERSION_MAJOR) $(VERSION_MINOR))-picodebugprobe-$(GIT_HASH).uf2


.PHONY: check-clang
check-clang:
	# clang-tidy has its limit if another target is used...
	@cd $(BUILD_DIR) && run-clang-tidy -checks='-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling'

.PHONY: show-options
show-options:
	@cd $(BUILD_DIR) && cmake -LH . | sed -n -e '/OPT_/{x;1!p;g;$!N;p;D;}' -e h

