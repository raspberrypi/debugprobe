# Makefile to build YAPicoprobe
#
# ATTENTION: to get the version number & git hash into the image, cmake-create-* has to be invoked.
#

VERSION_MAJOR        := 1
VERSION_MINOR        := 9

BUILD_DIR            := build
PROJECT              := picoprobe


GIT_HASH := $(shell git rev-parse --short HEAD)

CMAKE_FLAGS  = -DPICOPROBE_VERSION_MAJOR=$(VERSION_MAJOR)
CMAKE_FLAGS += -DPICOPROBE_VERSION_MINOR=$(VERSION_MINOR)
CMAKE_FLAGS += -DPROJECT=$(PROJECT)
CMAKE_FLAGS += -DGIT_HASH=$(GIT_HASH)
CMAKE_FLAGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=True


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
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
    # don't know if this is required
	@cd $(BUILD_DIR) && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: cmake-create-release
cmake-create-release:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)
    # don't know if this is required
	@cd $(BUILD_DIR) && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: clean-build
clean-build:
	rm -rfv $(BUILD_DIR)


.PHONY: flash
flash: all
	@echo "Waiting for RPi bootloader..."
	@until [ -f /media/pico/INDEX.HTM ]; do sleep 0.1; done; echo "ready!"
	cp $(BUILD_DIR)/$(PROJECT).uf2 /media/pico
	@echo "ok."


.PHONY: create-image
create-image:
	$(MAKE) clean-build
	$(MAKE) cmake-create-debug
	$(MAKE) all
	mkdir -p images
	cp $(BUILD_DIR)/$(PROJECT).uf2 images/yapicoprobe-$(shell printf "%02x%02x" $(VERSION_MAJOR) $(VERSION_MINOR))-$(GIT_HASH).uf2
