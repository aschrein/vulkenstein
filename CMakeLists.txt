cmake_minimum_required(VERSION 3.4.3)
project(vulkenstein)


set (BASE_LLVM_VERSION 11.0.0)
find_package(LLVM ${BASE_LLVM_VERSION} REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM_LIBRARY_DIR: ${LLVM_LIBRARY_DIR}")
execute_process(
        COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libfiles x86 core analysis bitreader bitwriter codegen irreader linker mc passes support target transformutils
        OUTPUT_VARIABLE LLVM_LIBRARY_LIST
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
function(str_to_list inStr outStr)
string(REPLACE " " ";" tmpOutput "${inStr}")
set(${outStr} ${tmpOutput} PARENT_SCOPE)
endfunction()

str_to_list("${LLVM_LIBRARY_LIST}" LLVM_LIBRARY_LIST)
message(STATUS "LLVM_LIBRARY_LIST:" ${LLVM_LIBRARY_LIST})

add_custom_command(
OUTPUT llvm_stdlib.h
DEPENDS ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll
COMMAND python3 ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll
                  > llvm_stdlib.ll &&
                  ${LLVM_TOOLS_BINARY_DIR}/llvm-as llvm_stdlib.ll -o llvm_stdlib.bc &&
                  xxd -i llvm_stdlib.bc > llvm_stdlib.h
) 
add_executable(s2l
spirv_to_llvm.cpp
llvm_stdlib.h
)
target_include_directories(s2l
  PRIVATE
  3rdparty/SPIRV
  3rdparty
  /usr/include/SPIRV
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(s2l PRIVATE "-DS2L_EXE -fmax-errors=1" ${LLVM_COMPILE_FLAGS})
target_link_libraries(s2l ${LLVM_LIBRARY_LIST} pthread ncurses z)