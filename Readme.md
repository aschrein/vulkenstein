## Toy SPIRV to LLVM IR translator

## Build
### LibPFC
```console
cd 3rdparty/libpfc
make
sudo echo 0 > /proc/sys/kernel/nmi_watchdog
sudo echo 2 > /sys/bus/event_source/devices/cpu/rdpmc
sudo insmod pfc.ko
```

## TODO
* Scalar(SIMD1) support
* Vectorized(SIMD4, SIMD32, SIMD64) support
