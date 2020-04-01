glslangValidator -V test.frag.glsl -o test.frag.spv && \
./s2l test.frag.spv > tmp.ll && \
llvm-as tmp.ll -o tmp.bc && \
g++ -g ../test_driver.cpp -DSHADER_STDLIB -fPIC -c -o shader_stdlib.o
llc --mtriple=x86_64-unknown-linux-gnu -filetype=obj tmp.bc -o test.frag.o && \
gcc test.frag.o shader_stdlib.o -shared -fPIC -o test.frag.so && \
g++ -g ../test_driver.cpp -o test_driver -ldl && \
exit 0
exit 1

-Wl,--unresolved-symbols=ignore-all
-fPIE -pie
