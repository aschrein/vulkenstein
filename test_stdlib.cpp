#include <cmath>
#include <stdint.h>
//#include <glm/glm.hpp>
//#include <glm/gtx/vec_swizzle.hpp>
//#include <iostream>
//#include <stdio.h>
//#include <stdlib.h>

// using namespace glm;

extern "C" {
typedef int (*printf_t)(const char *__restrict __format, ...);

typedef float float4 __attribute__((ext_vector_type(4)))
__attribute__((aligned(16)));
typedef float float3 __attribute__((ext_vector_type(3)))
__attribute__((aligned(4)));
typedef float float2 __attribute__((ext_vector_type(2)))
__attribute__((aligned(4)));
typedef int32_t int4 __attribute__((ext_vector_type(4)))
__attribute__((aligned(16)));
typedef int32_t int3 __attribute__((ext_vector_type(3)))
__attribute__((aligned(4)));
typedef int32_t int2 __attribute__((ext_vector_type(2)))
__attribute__((aligned(4)));

// stack allocated struct passed per each entry invocation
struct Invocation_Info {
  //  ivec3 gid;
  //  ivec3 lid;
  //  ivec3 work_group_size;
  int subgroup_size; // eventually we'd want to emulate subgroup extension and
                     // dFdx etc
  //  GPU_State *gpu_state;
};

float spv_sqrt(float a) { return sqrtf(a); }

float spv_dot_f4(float4 a, float4 b) {
  return
    a.x * b.x +
    a.y * b.y +
    a.z * b.z +
    a.w * b.w;
}

float spv_dot_f3(float3 a, float3 b) {
  return
    a.x * b.x +
    a.y * b.y +
    a.z * b.z;
}

float spv_dot_f2(float2 a, float2 b) {
  return
    a.x * b.x +
    a.y * b.y;
}

float spv_length_f4(float4 a) { return spv_sqrt(spv_dot_f4(a, a)); }
float spv_length_f3(float3 a) { return spv_sqrt(spv_dot_f3(a, a)); }
float spv_length_f2(float2 a) { return spv_sqrt(spv_dot_f2(a, a)); }

void deinterleave(float4 const *in, float *out) {
  for (int i = 0; i < WAVE_WIDTH; i++) {
    out[i + WAVE_WIDTH * 0] = in[i].x;
    out[i + WAVE_WIDTH * 1] = in[i].y;
    out[i + WAVE_WIDTH * 2] = in[i].z;
    out[i + WAVE_WIDTH * 3] = in[i].w;
  }
}

#define TEST(x)                                                                \
  if (!(x)) {                                                                  \
    ((printf_t)_printf)("%s:%i [FAIL]\n", __FILE__, __LINE__);                 \
    exit(1);                                                                   \
  }
#define TEST_EQ(a, b)                                                          \
  if ((a) != (b)) {                                                            \
    ((printf_t)_printf)("%s:%i [FAIL] %s != %s \n", __FILE__, __LINE__, #a,    \
                        #b);                                                   \
    exit(1);                                                                   \
  }
//#define TEST_EQ(a, b)                                                          \
//  if ((a) != (b)) {                                                            \
//    std::cerr << __FILE__ << ":" << __LINE__ << " FAILED: " << #a              \
//              << " != " << #b << " -> " << (a) << " != " << (b) << "\n";       \
//    exit(1);                                                                   \
//  }
}
