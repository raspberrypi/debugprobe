# Picoprobe
Picoprobe allows a Pico / RP2040 to be used as USB -> SWD and UART bridge. This means it can be used as a debugger and serial console for another Pico.

# Documentation
Picoprobe documentation can be found in the [Pico Getting Started Guide](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). See "Appendix A: Using Picoprobe".

# Hacking

For the purpose of making changes or studying of the code, you may want to compile the code yourself. 

To compile this project firstly initialize and update the submodules: 
```
 git submodule update --init
```
then create and switch to the build directory: 
```
 mkdir build
 cd build
```
then run cmake and build the code:
```
 cmake ..
 make
```
Done! You should now have a `picoprobe.uf2` that you can upload to your Pico in the normal way. 

If you want to create the version that runs on the Raspberry Pi Debug Probe, then you need to invoke `cmake` in the sequence above with the `DEBUGPROBE=ON` option: 
```
cmake -DDEBUGPROBE=ON ..
```

This will build with the configuration for the Debug Probe and call the output program `debugprobe.uf2`, as opposed to `picoprobe.uf2` for the vanilla version. 

Note that if you first ran through the whole sequence to compile for the Pico, then you don't need to start back at the top. You can just go back to the `cmake` step and start from there.


# TODO
- TinyUSB's vendor interface is FIFO-based and not packet-based. Using raw tx/rx callbacks is preferable as this stops DAP command batches from being concatenated, which confused openOCD.
- Instead of polling, move the DAP thread to an asynchronously started/stopped one-shot operation to reduce CPU wakeups
- AutoBaud selection, as PIO is a capable frequency counter
- Possibly include RTT support
