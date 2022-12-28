.PHONY: clean
clean:
	cd build && ninja clean


.PHONY: all
all:
	cd build && ninja -v all


.PHONY: cmake-create-debug
cmake-create-debug:
	mkdir -p build
	cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=True ..
	# don't know if this is required
	cd build && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: cmake-create-release
cmake-create-release:
	mkdir -p build
	cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=True ..
	# don't know if this is required
	cd build && sed -i 's/arm-none-eabi-gcc/gcc/' compile_commands.json


.PHONY: clean-build
clean-build:
	rm -rfv build
	mkdir -p build


.PHONY: flash
flash: all
	@echo "Waiting for RPi bootloader..."
	@until [ -f /media/pico/INDEX.HTM ]; do sleep 1; done; echo "ready!"
	cp build/picoprobe.uf2 /media/pico
	@echo "ok."
