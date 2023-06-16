# Picoprobe
Picoprobe allows a Pico / RP2040 to be used as USB -> SWD and UART bridge. This means it can be used as a debugger and serial console for another Pico.

# Documentation
Picoprobe documentation can be found in the [Pico Getting Started Guide](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). See "Appendix A: Using Picoprobe".

# Hacking

For the purpose of making changes or studying of the code, you may want to compile the code yourself. 

To compile the "picoprobe" version of this project just initialize the submodules and update them: 

 git submodule init
 git submodule update

then create and switch to the build directory: 
 mkdir build
 cd build

then run cmake and build the code:

 cmake ..
 make

Easy! 

If you want to create the version that runs on the raspberry pi debugprobe, then you need to change the configuration of the software a bit. In the file src/picoprobe_config.h near the bottom, you'll find three includes, two of which are commented out. Uncomment the one for debugprobe and recompile. Please note that the project still builds as "picoprobe". You might want to rename the resulting binary (.uf2) to "debugprobe.uf2". The same goes for the ".elf" file. 


# TODO
- TinyUSB's vendor interface is FIFO-based and not packet-based. Using raw tx/rx callbacks is preferable as this stops DAP command batches from being concatenated, which confused openOCD.
- Instead of polling, move the DAP thread to an asynchronously started/stopped one-shot operation to reduce CPU wakeups
- AutoBaud selection, as PIO is a capable frequency counter
- Possibly include RTT support
- Allow switching between building picoprobe/debugprobe versions with a cmake definition. 
- generate the debugprobe version under the right name.
