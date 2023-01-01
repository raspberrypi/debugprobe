# Makefile to build YAPicoprobe
#
# ATTENTION: to get the version number & git hash into the image, cmake-create-* has to be invoked.
#

VERSION              := 0104
OPTIMIZE_FOR_OPENOCD ?= 0


GIT_HASH := $(shell git rev-parse --short HEAD)

CMAKE_FLAGS  = -DPICOPROBE_VERSION=$(VERSION)
CMAKE_FLAGS += -DOPTIMIZE_FOR_OPENOCD=$(OPTIMIZE_FOR_OPENOCD)
CMAKE_FLAGS += -DGIT_HASH=$(GIT_HASH)
CMAKE_FLAGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=True


.PHONY: clean
clean:
	ninja -C build -v clean


.PHONY: all
all:
	ninja -C build -v all


.PHONY: cmake-create-debug
cmake-create-debug:
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
    # don't know if this is required
	@cd build && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: cmake-create-release
cmake-create-release:
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)
    # don't know if this is required
	@cd build && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: clean-build
clean-build:
	rm -rfv build


.PHONY: flash
flash: all
	@echo "Waiting for RPi bootloader..."
	@until [ -f /media/pico/INDEX.HTM ]; do sleep 0.1; done; echo "ready!"
	cp build/picoprobe.uf2 /media/pico
	@echo "ok."


.PHONY: create-images
create-images:
	$(MAKE) clean-build
	$(MAKE) cmake-create-debug OPTIMIZE_FOR_OPENOCD=1
	$(MAKE) all
	mkdir -p images
	rm images/*.uf2
	cp build/picoprobe.uf2 images/yapicoprobe-$(VERSION)-$(GIT_HASH)-openocd.uf2
	@
	$(MAKE) cmake-create-debug OPTIMIZE_FOR_OPENOCD=0
	$(MAKE) all
	cp build/picoprobe.uf2 images/yapicoprobe-$(VERSION)-$(GIT_HASH).uf2
