## Toy software Vulkan driver
![Vulkan scene](readme/preview.gif)
![Gears](readme/preview_2.gif)
## Run tests
```console
cd vulkenstein
python3 tests/run_all_tests.py
```

## Build
### LLVM Version: 11.* commit 0d3149f43173967d6f4e4c5c904a05e1022071d4
* Used for JIT code generation
### Vulkan SDK: 1.2.135.0
* Used for headers, spirv disassembly
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
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make
```
