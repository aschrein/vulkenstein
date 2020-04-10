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

# ATTENTION: COPY PASTE
# SIMD1 | sed 's/llvm_stdlib/llvm_stdlib_1/g'
add_custom_command(
OUTPUT llvm_stdlib_1.h
DEPENDS ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll
COMMAND python3 ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll 1
                  > llvm_stdlib_1.ll &&
                  ${LLVM_TOOLS_BINARY_DIR}/llvm-as llvm_stdlib_1.ll -o llvm_stdlib_1.bc &&
                  xxd -i llvm_stdlib_1.bc  > llvm_stdlib_1.h
)
# SIMD4
add_custom_command(
OUTPUT llvm_stdlib_4.h
DEPENDS ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll
COMMAND python3 ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll 4
                  > llvm_stdlib_4.ll &&
                  ${LLVM_TOOLS_BINARY_DIR}/llvm-as llvm_stdlib_4.ll -o llvm_stdlib_4.bc &&
                  xxd -i llvm_stdlib_4.bc > llvm_stdlib_4.h
)
# SIMD64
add_custom_command(
OUTPUT llvm_stdlib_64.h
DEPENDS ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll
COMMAND python3 ${CMAKE_SOURCE_DIR}/stdlibgen.py ${CMAKE_SOURCE_DIR}/default_stdlib.ll 64
                  > llvm_stdlib_64.ll &&
                  ${LLVM_TOOLS_BINARY_DIR}/llvm-as llvm_stdlib_64.ll -o llvm_stdlib_64.bc &&
                  xxd -i llvm_stdlib_64.bc > llvm_stdlib_64.h
)
### END OF COPY PASTE
configure_file(icd_template.json icd.json)
#add_custom_command(
#OUTPUT icd.h
#DEPENDS ${CMAKE_SOURCE_DIR}/icd_template.json
#COMMAND cp ${CMAKE_SOURCE_DIR}/icd_template.json ${CMAKE_BINARY_DIR}/icd.json &&
#                  cat ${CMAKE_BINARY_DIR}/icd.json | sed -e "s/BUILD_DIR/'${CMAKE_BINARY_DIR}'/g" > ${CMAKE_BINARY_DIR}/icd.json
#)

add_library(vk SHARED
vk.cpp
)
target_include_directories(vk
  PRIVATE
  3rdparty/SPIRV
  3rdparty
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
)
#ADD_DEPENDENCIES(vk icd.json)

add_executable(s2l
spirv_to_llvm.cpp
llvm_stdlib_1.h
llvm_stdlib_4.h
llvm_stdlib_64.h
)
target_include_directories(s2l
  PRIVATE
  3rdparty/SPIRV
  3rdparty
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${LLVM_INCLUDE_DIRS}
)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -fmax-errors=1 -fno-exceptions -fno-rtti -fvisibility=hidden")
target_compile_definitions(s2l PRIVATE "-DS2L_EXE " ${LLVM_COMPILE_FLAGS})
target_link_libraries(s2l ${LLVM_LIBRARY_LIST} pthread ncurses z)
