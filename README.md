# Yet another Picoprobe
Picoprobe allows a Pico / RP2040 to be used as USB -> SWD and UART bridge. This means it can be used as a debugger and serial console for another Pico.

Yet another Picoprobe is a fork of the original [Picoprobe](https://github.com/raspberrypi/picoprobe) and was created due to my lazyness to follow the PR discussions and delays.

Another reason for this fork is that I wanted to play around with SWD, RTT etc pp, so the established development process was a little bit hindering.

So there is unfortunately one more Picoprobe around, the YAPicoprobe.



# Features
## Plus
* **FAST**, at least faster than most other picoprobes
* standard debug tool connectivity
  * CMSIS-DAPv2 WinUSB (driver-less vendor-specific bulk) - CMSIS compliant debug channel
  * CMSIS-DAPv1 HID - CMSIS compliant debug channel as a fallback
* MSC - drag-n-drop support of UF2 firmware image à la [DAPLink](https://github.com/ARMmbed/DAPLink) for the RP2040 Pico / PicoW
* CDC - virtual com port for logging of the target
  * UART connection between target and probe is redirected
  * RTT terminal channel is automatically redirected into this CDC (if there is no CMSIS-DAPv2/MSC connection)
* CDC - virtual com port for logging of the probe
* LED for state indication

## Other Benefits
* no more Zadig fiddling because the underlying protocols of CMSIS-DAPv1 and v2 are driver-less
* easy drag-n-drop (or copy) upload of a firmware image to the target via the probe
* no more reset push button (or disconnect/connect cycle)  to put the target into BOOTSEL mode

## Minus
* custom Picoprobe protocol has been dropped



# Documentation
## Overview and Wiring
Picoprobe documentation can be found in the [Pico Getting Started Guide](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). See "Appendix A: Using Picoprobe".

Wires between probe and target board are the same as before, but the UART wires can be omitted if RTT is used for logging on the target device.

I recommend to use a simple Raspberry Pi Pico board as the probe.  The Pico W board does not add any features, instead the LED indicator does not work there.


## Tool Compatibility

| Tool | Linux | Windows (10) | Command line |
| -- | :--: | :--: | -- |
| openocd | yes | yes | `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 25000"    -c "program {firmware.elf}  verify reset; shutdown;"` |
| pyocd | yes | no | `pyocd flash -f 2500000 -firmware.elf` |
| cp / copy | yes | yes | `cp firmware.uf2 /media/picoprobe` |


## Adapter Speed
The tools above allow specification of the adapter speed.  This is the clock frequency between probe and target device.
Unfortunately DAP converts internally the frequency into delays which are always even multiples of clock cycles.
That means that actual clock speeds are `125MHz / (2*n)`, `n>=3` -> 20833kHz, 12500kHz, 10417kHz, ...

Normally the requested frequency is rounded down according to the possible values from above.  But if the specified frequency 
is completely out of range, the allowed maximum SWD frequency of the RP2040 is used, which is 24MHz.

Actually usable frequency depends on cabling and the DAP speed.  If the DAP cannot access memory with speed determined by the host, it responds
with WAIT and the host needs to retry.

Effects of cabling should be clear: the longer the cables plus some more effects, the worse the signals.  Which effectively means
slowing down clock frequency is required to get the data transported.

**Note:** SWCLK speed for MSC and RTT (below) is set according to the latest used tool setup.  E.g. `pyocd reset -f 5000000` sets
SWCLK to 5MHz.


## MSC - Mass Storage Device Class
Via MSC the so called "drag-n-drop" supported is implemented.  Actually this also helps in copying a UF2 image directly into the target via command line.

Note, that flash erase takes place on a 64KByte base:  on the first write to a 64 KByte page, the corresponding page is erased.  That means, that multiple UF2 images can be flashed into the target as long as there is no overlapping within 64 KByte boundaries.

Because writing/erasing of the flash is target depending, the current implementation is limited to the RP2040 and its "win w25q16jv".
CMSIS-DAP should be generic, which means that its tools dependant (openocd/pyocd).


## RTT - Real Time Transfer
[RTT](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/) allows transfer from the target to the host in "realtime".  YAPicoprobe currently reads channel 0 of the targets RTT and sends it into the CDC of the target.  Effectively this allows RTT debug output into a terminal.

Notes:
* only the RP2040 RAM is scanned for an RTT control block which means 0x20000000-0x2003ffff
* don't be too overwhelmed about Seggers numbers in the above mentioned document.  The data must still be transferred which to my opinion is not taken into account (of course the target processor has finished after writing the data)
* only one of CMSIS-DAP / MSC / RTT can access the target at the same time.  RTT is disconnected in case CMSIS-DAP
  or MSC are claiming access


## LED Indication

| state | indication |
| -- | -- |
| no target found | 5Hz blinking |
| DAPv1 connected | LED on, off for 100ms once per second |
| DAPv2 connected | LED on, off for 100ms twice per second |
| MSC active | LED on, off for 100ms thrice per second |
| UART data from target | slow flashing: 300ms on, 700ms off |
| target found | LED off, flashes once per second for 20ms |
| RTT control block found | LED off, flashes twice per second for 20ms |
| RTT data received | LED off, flashes thrice per second for 20ms |


## Configuration

### udev rules for MSC and CMSIS-DAP
/etc/udev/rules.d/90-picoprobes.rules:
```
# set mode to allow access for regular user
SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="000c", MODE:="0666"

# create COM port for target CDC
ACTION=="add", SUBSYSTEMS=="usb", KERNEL=="ttyACM[0-9]*", ATTRS{interface}=="YAPicoprobe CDC-UART",  MODE="0666", SYMLINK+="ttyPicoTarget"
ACTION=="add", SUBSYSTEMS=="usb", KERNEL=="ttyACM[0-9]*", ATTRS{interface}=="YAPicoprobe CDC-DEBUG", MODE="0666", SYMLINK+="ttyPicoProbe"

# mount Picoprobe to /media/picoprobe
ACTION=="add", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", RUN+="/usr/bin/logger --tag picoprobe-mount Mounting what seems to be a Raspberry Pi Picoprobe", RUN+="/usr/bin/systemd-mount --no-block --collect --fsck=0 -o uid=hardy,gid=hardy,flush $devnode /media/picoprobe"
ACTION=="remove", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", RUN+="/usr/bin/logger --tag picoprobe-mount Unmounting what seems to be a Raspberry Pi Picoprobe", RUN+="/usr/bin/systemd-umount /media/picoprobe"

# mount RPi bootloader to /media/pico
ACTION=="add", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", RUN+="/usr/bin/logger --tag rpi-pico-mount Mounting what seems to be a Raspberry Pi Pico", RUN+="/usr/bin/systemd-mount --no-block --collect --fsck=0 -o uid=hardy,gid=hardy,flush $devnode /media/pico"
ACTION=="remove", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", RUN+="/usr/bin/logger --tag rpi-pico-mount Unmounting what seems to be a Raspberry Pi Pico", RUN+="/usr/bin/systemd-umount /media/pico"
```

### PlatformIO
[PlatformIO](https://platformio.org/) configuration in `platformio.ini` is pretty straight forward:

```
[env:pico]
framework = arduino
platform = https://github.com/maxgerhardt/platform-raspberrypi
board = rpipicow
board_build.core = earlephilhower
upload_protocol = cmsis-dap
debug_tool = cmsis-dap
monitor_speed = 115200
monitor_port  = /dev/ttyPicoTarget
```

The firmware image can alternativly copied directly (and faster) via MSC with custom upload:

```
[env:pico_cp]
...
upload_protocol = custom
upload_command = cp .pio/build/pico_cp/firmware.uf2 /media/picoprobe
...
```

I'm sure there are smarter ways to specify the image path directly.

There is also a special PlatformIO handling in the probe: it ignores the defensive 1MHz clock setting which is used by
the above contained openocd.  Standard clock is thus 15MHz.  If this is too fast, set the frequency with
`pyocd reset -f 1100000` or similar.  If this is too slow, use `pyocd reset -f 50000000`.



# Optimizations

## Benchmarking
Benchmarking is done with an image with a size around 400KByte.  Command lines are as follows:

* **cp**: `time cp firmware.uf2 /media/picoprobe/`
* **openocd 0.12.0-rc2** (CMSIS-DAP)v2: `time openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 25000" -c "program {firmware.elf}  verify reset; shutdown;"`
* **openocd 0.12.0-rc2** (CMSIS-DAP)v1: `time openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "cmsis_dap_backend hid; adapter speed 25000" -c "program {firmware.elf}  verify reset; shutdown;"`
* **pyocd 0.34.3**: `time pyocd flash -f 25000000 firmware.elf`, pyocd ignores silently "-O cmsis_dap.prefer_v1=true", except for the "list" option

Note that benchmarking takes place under Linux.  Surprisingly `openocd` and `pyocd` behave differently under Windows.
DAPv2 is always used, because DAPv1 does not run under Linux(?).

| command / version  | cp    | openocd v1 | openocd v2 | pyocd | comment |
| --------           | -----:| ----------:| ----------:| -----:| ------- |
| very early version |   -   |         -  |     10.4s  |     - |         |
| v1.00              |  6.4s |         -  |      8.1s  | 16.5s |         |
| git-3120a90        |  5.7s |         -  |      7.8s  | 15.4s |         |
| - same but NDEBUG -|  7.3s |         -  |      9.5s  | 16.6s | a bad miracle... to make things worse, pyocd is very instable |
| git-bd8c41f        |  5.7s |     28.6s  |      7.7s  | 19.9s | there was a python update :-/ |
| git-0d6c6a8        |  5.7s |     28.5s  |      6.8s  | 20.2s |         |
| - same but optimized for openocd | 5.7s | 28.5s | 6.1s | - | pyocd crashes |
| git-0eba8bf        |  4.9s |     28.6s  |      6.5s  | 13.8s | cp shows sometimes 5.4s |
| - same but optimized for openocd | 4.9s | 28.6s | 5.8s | - | pyocd crashes |
| git-e38fa52        |  4.8s |     28.6s  |      6.6s  | 14.0s | cp shows sometimes 5.4s |
| - same but optimized for openocd | 4.8s | 28.6s | 5.9s | - | pyocd crashes |
| git-28fd8db        |  4.1s |     28.6s  |      6.2s  | 13.9s | cp shows sometimes 4.6s, SWCLK tuned to 25MHz |
| - same but optimized for openocd | 4.1s | 28.6s | 5.7s | - | pyocd crashes |


## PIO
Several PIO optimizations has been implemented.  Main idea of PIO control has been taken from [pico_debug](https://github.com/essele/pico_debug/blob/main/swd.pio).

To monitor the progress between the several versions, [PulseView](https://sigrok.org/wiki/PulseView) has been used. LA probe was [sigrok-pico](https://github.com/pico-coder/sigrok-pico).

### First Version (03.01.2023 - e2b4a67)
![First Version](doc/png/Screenshot_20230103_074404.png "First Version (03.01.2023)")

### (Currently) Final Version (06.01.2023 - 28fd8db)
![Final Version (06.01.2023)](doc/png/Screenshot_20230106_153629.png "Final Version (06.01.2023)")

### Explanation / Conclusion
The plots above are taken with SWCLK = 15MHz.  Absolute time of the four commands shrunk from ~25us to 18us.  Not bad.

But there are still gaps which may give more optimization opportunities.  Switching times between read / write and the
gap between two commands are candidates.  Note that moving code into RAM did not really help (and optimization is still
a non/slow-working mystery).



# TODO / Known Bugs
* Bugs
  * check the benchmark "miracle" with the NDEBUG version 
  * if `configTICK_RATE_HZ` is around 100, the SWD IF no longer works
* TODO
  * pyocd auto-detect?
  * support of Nordic nRF52xxx (my other target)
  * DAP_PACKET_SIZE: how to increase?
* tests
  * Reset line between probe and target have to be reviewed
  * Win10 (tools) compatibility
* Misc:
  * tool compatibility list
