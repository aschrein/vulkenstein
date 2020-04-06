export SCRIPT=`realpath $0`
export SCRIPTPATH=`dirname $SCRIPT`
export PATH=$SCRIPTPATH/build:$PATH
export PATH=/home/aschrein/dev/llvm/build/Release/bin:$PATH
echo "using $(which clang)"
export WAVE_WIDTH=64
WD=`pwd`
if [ -z "$1" ]
  then
    echo "No arguments supplied. Please provide the path to a valid file"
    exit
fi
export INPUT=`realpath $1`
rm -rf $SCRIPTPATH/build/tests
mkdir -p $SCRIPTPATH/build/tests && cd $SCRIPTPATH/build/tests && \
python3 $INPUT && \
spirv-dis --raw-id shader.spv -o shader.spv.S && \
s2l shader.spv $WAVE_WIDTH > shader.ll && \
llvm-as shader.ll -o shader.bc && \
clang -DWAVE_WIDTH="$WAVE_WIDTH" -g -I$SCRIPTPATH stdlib.cpp -fPIC -c -o shader_stdlib.o && \
opt -strip -O3  shader.bc -o shader.bc && \
llvm-dis shader.bc -o shader.opt.ll && \
llc -mattr=+avx2 -O3 --relocation-model=pic --mtriple=x86_64-unknown-linux-gnu -filetype=obj shader.bc -o shader.o && \
objdump -D -M intel shader.o > shader.S && \
clang shader.o shader_stdlib.o -shared -fPIC -o shader.so && \
clang++ -g $SCRIPTPATH/test_driver.cpp -o test_driver -ldl && \
./test_driver shader.so && \
exit 0
exit 1

opt -early-cse --amdgpu-load-store-vectorizer --load-store-vectorizer \
    --interleaved-load-combine --vector-combine --instnamer shader.bc -o shader.bc && \

-Wl,--unresolved-symbols=ignore-all
-fPIE -pie

llc:
-mattr=+avx2

optimal options:
-early-cse --amdgpu-load-store-vectorizer --load-store-vectorizer  --interleaved-load-combine --vector-combine --instnamer

clang -emit-llvm -g -I$SCRIPTPATH stdlib.cpp -S -o shader_stdlib.bc && \
llvm-link shader.bc shader_stdlib.bc -o shader.bc && \
