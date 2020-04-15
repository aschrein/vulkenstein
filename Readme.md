## Toy SPIRV to LLVM IR translator

## Build
### LibPFC
```console
cd 3rdparty/libpfc
make
su
echo 0 > /proc/sys/kernel/nmi_watchdog
echo 2 > /sys/bus/event_source/devices/cpu/rdpmc
insmod pfc.ko
```
### Vulkenstein
```console
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make
```
