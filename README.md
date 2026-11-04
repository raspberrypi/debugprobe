# Debugprobe rp2040-zero

It's port of debugprobe for cheap ~2$ board with extra support of on-board rgb led.

### Done:
- Fix problem with not working firmware after upload and reset. PICO_XOSC_STARTUP_DELAY_MULTIPLIER is solution.
- Change ports for easy access
- Add built-in led support

### Pinout:
```
GP8     TX
GP9     RX
GP10    SWCLK
GP11    SWDIO
GP12    GND
GP13    GND
GP14    3V3
```

![probe_image](img/img1.jpg)

![probe_image](img/img2.jpg)

### Led legend:
- Dimm red: power on, bright red: usb comm.
- Green: Flashing/Debugging
- Blue: RX Serial

### How to make it
If you wanna compile it, follow the guidelines in chapter 'Hacking' below and compile with "cmake -DDEBUG_ON_ZERO=ON ..".
Binary uf2 is for download in relese tab. Connecting pins GP13/GP14 to gnd/3v3 is optional if you want use board as power supply.

### Links
- [Information about the RP2040-Zero board with pinouts](https://www.waveshare.com/wiki/RP2040-Zero)
- [3D model of case](https://www.thingiverse.com/thing:6832908)

# Debugprobe

Firmware source for the Raspberry Pi Debug Probe SWD/UART accessory. Can also be run on a Raspberry Pi Pico or Pico 2.

[Raspberry Pi Debug Probe product page](https://www.raspberrypi.com/products/debug-probe/)

[Raspberry Pi Pico product page](https://www.raspberrypi.com/products/raspberry-pi-pico/)

[Raspberry Pi Pico 2 product page](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)

# Documentation

Debug Probe documentation can be found at the [Raspberry Pi Microcontroller Documentation portal](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html#about-the-debug-probe).

# Hacking

For the purpose of making changes or studying of the code, you may want to compile the code yourself.

First, clone the repository:
```
git clone https://github.com/tchv71/debugprobe_rp2040-zero.git
cd debugprobe_rp2040-zero
```
Initialize and update the submodules:
```
 git submodule update --init --recursive
```
Then create and switch to the build directory:
```
 mkdir build
 cd build
```
If your environment doesn't contain `PICO_SDK_PATH`, then either add it to your environment variables with `export PICO_SDK_PATH=/path/to/sdk` or add `-DPICO_SDK_PATH=/path/to/sdk` to the arguments to CMake below.

Run cmake and build the code:
```
 cmake ..
 make
```
Done! You should now have a `debugprobe.uf2` that you can upload to your Debug Probe via the UF2 bootloader.

## Building for the Pico 1

If you want to create the version that runs on the Pico, then you need to invoke `cmake` in the sequence above with the `DEBUG_ON_PICO=ON` option:
```
cmake -DDEBUG_ON_PICO=ON ..
```
This will build with the configuration for the Pico and call the output program `debugprobe_on_pico.uf2`, as opposed to `debugprobe.uf2` for the accessory hardware.

Note that if you first ran through the whole sequence to compile for the Debug Probe, then you don't need to start back at the top. You can just go back to the `cmake` step and start from there.

## Building for the Pico 2

If using an existing debugprobe clone:
- You must completely regenerate your build directory, or use a different one.
- You must also sync and update submodules.
- `PICO_SDK_PATH` must point to a version 2.0.0 or greater install.

```
git submodule sync
git submodule update --init --recursive
mkdir build-pico2
cd build-pico2
cmake -DDEBUG_ON_PICO=1 -DPICO_BOARD=pico2 ../
```
This will build with the configuration for the Pico 2 and call the output program `debugprobe_on_pico2.uf2`.

# AutoBaud

Mode which automatically detects and sets the UART baud rate as data arrives.

To enable AutoBaud, configure the USB CDC port to the following custom baud rate:
```
9728 (0x2600)
```
> **Note:** Some Linux serial tools cannot set custom baud values. PuTTY on Windows and any terminal that supports arbitrary baud rates works.

Changing the baud rate to any other value disables AutoBaud.

