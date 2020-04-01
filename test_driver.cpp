#ifdef SHADER_STDLIB
#include <stdlib.h>
#include <stdio.h>

extern "C"{
//  declare i8 addrspace(1) *@get_input_ptr(i32 %id)
//  declare i8 addrspace(3) *@get_output_ptr(i32 %id)

float g_input[2] = {1.0f, 2.0f};

struct mat4 {float m[16];};
struct vec4 {float m[4];};

#pragma pack(1)
struct UBO {
  mat4 transform;
  int albedo_id;
  int normal_id;
  int arm_id;
  float metal_factor;
  float roughness_factor;
  char pad[12];
  vec4 vec_4;
} uniforms;
#pragma pop

void *get_uniform_ptr(int set, int binding) {
  uniforms.vec_4.m[0] = 1.0f;
  uniforms.vec_4.m[1] = 2.0f;
  uniforms.vec_4.m[2] = 3.0f;
  uniforms.vec_4.m[3] = 4.0f;
  return &uniforms;
}

void *get_input_ptr(int id) {
  return &g_input[id];
}

vec4 g_output;

void *get_output_ptr(int id) {
  return &g_output;
}

void spv_on_exit() {
  fprintf(stdout, "[%f, %f, %f, %f]\n",
  g_output.m[0],
  g_output.m[1],
  g_output.m[2],
  g_output.m[3]
  );
}

}
#else
#include <stdlib.h>
#include <stdio.h>
#define UTILS_IMPL
#include "utils.hpp"
#include <dlfcn.h>



int main(int argc, char **argv) {
  ASSERT_ALWAYS(argc == 2);
  typedef void (*main_t)();

  void *myso = dlopen(argv[1], RTLD_NOW);
  if(myso == NULL) {
    fprintf(stderr, "[ERROR] dlopen: %s\n", dlerror());
    exit(1);
  }
  main_t func = (main_t)dlsym(myso, "main");
  func();
  dlclose(myso);
  return 0;
}
#endif
