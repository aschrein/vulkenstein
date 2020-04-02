export SCRIPT=`realpath $0`
export SCRIPTPATH=`dirname $SCRIPT`
export PATH=$SCRIPTPATH/build:$PATH
WD=`pwd`
if [ -z "$1" ]
  then
    echo "No arguments supplied. Please provide the path to a valid file"
    exit
fi
export INPUT=`realpath $1`
mkdir -p build/tests && cd build/tests && \
python3 $INPUT && \
s2l shader.spv > shader.ll && \
llvm-as shader.ll -o shader.bc && \
g++ -g stdlib.cpp -fPIC -c -o shader_stdlib.o
llc --mtriple=x86_64-unknown-linux-gnu -filetype=obj shader.bc -o shader.o && \
gcc shader.o shader_stdlib.o -shared -fPIC -o shader.so && \
g++ -g $SCRIPTPATH/test_driver.cpp -o test_driver -ldl && \
./test_driver shader.so && \
exit 0
exit 1

-Wl,--unresolved-symbols=ignore-all
-fPIE -pie
