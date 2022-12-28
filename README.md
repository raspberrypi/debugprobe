# Yet another Picoprobe
Picoprobe allows a Pico / RP2040 to be used as USB -> SWD and UART bridge. This means it can be used as a debugger and serial console for another Pico.

Yet another Picoprobe is a fork of the original [Picoprobe](https://github.com/raspberrypi/picoprobe) and was created due to my lazyness to follow the PR discussions and delays.

Another reason for this fork is that I wanted to play around with SWD, RTT etc pp, so the established development process was a little bit hindering.

So there is unfortunately one more Picoprobe around.



# Features
## Plus
* CMSIS-DAPv2 WinUSB (driver-less vendor-specific bulk) - CMSIS compliant debug channel
* CMSIS-DAPv1 HID - CMSIS compliant debug channel as a fallback
* MSC - drag-n-drop support a la [DAPLink](https://github.com/ARMmbed/DAPLink) for the RP2040 Pico / PicoW
* CDC - virtual com port for logging of the target
  * UART connection between target and probe is redirected
  * RTT terminal channel is automatically redirected into this CDC (if there is no DAPv2/MSC connection)
* CDC - virtual com port for logging of the probe
* LED for state indication

## Other Benefits
* no more Zadig fiddling
* easy upload of a firmware image to the target via the probe
* no more reset push button required for the target to put into BOOTSEL mode

## Minus
* custom Picoprobe protocol has been dropped



# Documentation
## Overview and Wiring
Picoprobe documentation can be found in the [Pico Getting Started Guide](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). See "Appendix A: Using Picoprobe".

Wires between probe and target board are the same as before, but the UART wires can be omitted if RTT is used for logging on the target device.

I recommend to use a simple Pico board as the probe.  The PicoW does not add any features, instead the LED indicator does not work there.

## Tool Compatibility

| Tool | Linux | Windows (10) | Command line |
| -- | :--: | :--: | -- |
| openocd | yes | yes | `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 25000"    -c "program {firmware.elf}  verify reset; shutdown;"` |
| pyocd | yes | no | `pyocd flash -f 2500000 -firmware.elf` |
| cp / copy | yes | yes | `cp firmware.uf2 /media/picoprobe` |



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
ACTION=="add", SUBSYSTEMS=="usb", KERNEL=="ttyACM[0-9]*", ATTRS{interface}=="YAPicoprobe CDC-ACM UART", MODE="0666", SYMLINK+="ttyPicoTarget"
ACTION=="add", SUBSYSTEMS=="usb", KERNEL=="ttyACM[0-9]*", ATTRS{interface}=="YAPicoprobe CDC-DEBUG",    MODE="0666", SYMLINK+="ttyPicoProbe"

# mount Picoprobe to /media/picoprobe
ACTION=="add", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", RUN+="/usr/bin/logger --tag picoprobe-mount Mounting what seems to be a Raspberry Pi Picoprobe", RUN+="/usr/bin/systemd-mount --no-block --collect --fsck=0 -o uid=hardy,gid=hardy,flush $devnode /media/picoprobe"
ACTION=="remove", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", RUN+="/usr/bin/logger --tag picoprobe-mount Unmounting what seems to be a Raspberry Pi Picoprobe", RUN+="/usr/bin/systemd-umount /media/picoprobe"

# mount RPi bootloader to /media/pico
ACTION=="add", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", RUN+="/usr/bin/logger --tag rpi-pico-mount Mounting what seems to be a Raspberry Pi Pico", RUN+="/usr/bin/systemd-mount --no-block --collect --fsck=0 -o uid=hardy,gid=hardy,flush $devnode /media/pico"
ACTION=="remove", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", RUN+="/usr/bin/logger --tag rpi-pico-mount Unmounting what seems to be a Raspberry Pi Pico", RUN+="/usr/bin/systemd-umount /media/pico"
```

### PlatformIO



# Benchmarking
Benchmarking is done with an image with a size around 400KByte.  Command lines are as follows:

* **cp**: `time cp firmware.uf2 /media/picoprobe/`
* **openocd 0.12.0-rc2** (CMSIS-DAP)v2: `time openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 25000" -c "program {firmware.elf}  verify reset; shutdown;"`
* **openocd 0.12.0-rc2** (CMSIS-DAP)v1: `time openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "cmsis_dap_backend hid; adapter speed 25000" -c "program {firmware.elf}  verify reset; shutdown;"`
* **pyocd 0.34.3**: `time pyocd flash -f 25000000 firmware.elf`

Note that benchmarking takes place under Linux.  Surprisingly `openocd` and `pyocd` behave differently under Windows.
DAPv2 is always used, because DAPv1 does not run under Linux(?).

| command / version  | cp    | openocd v1 | openocd v2 | pyocd | comment |
| --------           | -----:| ----------:| ----------:| -----:| ------- |
| very early version |   -   |         -  |     10.4s  |     - |         |
| v1.00              |  6.4s |         -  |      8.1s  | 16.5s |         |
| git-3120a90        |  5.7s |         -  |      7.8s  | 15.4s |         |
| - same but NDEBUG -|  7.3s |         -  |      9.5s  | 16.6s | a bad miracle... to make things worse, pyocd is very instable |
| git-bd8c41f        |  5.7s |     28.6s  |      7.7s  | 19.9s | there was a python update :-/ |


# TODO / Known Bugs
* Bugs
  * check the benchmark "miracle" with the NDEBUG version 
  * if `configTICK_RATE_HZ` is around 100, the SWD IF no longer works
* TODO
  * fast search for RTT control block
  * TX host->target via RTT
  * pyocd auto-detect?
  * support of Nordic nRF52xxx (my other target)
  * DAP_PACKET_SIZE: how to increase?
* tests
  * Reset line between probe and target have to be reviewed
  * Win10 (tools) compatibility
  * compatibility with [PlatformIO](https://platformio.org/)
* Misc:
  * tool compatibility list
