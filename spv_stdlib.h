#ifndef SPV_STDLIB
#define SPV_STDLIB
#include <cmath>
#include <stdint.h>

extern "C" {

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
typedef uint32_t uint4 __attribute__((ext_vector_type(4)))
__attribute__((aligned(16)));
typedef uint32_t uint3 __attribute__((ext_vector_type(3)))
__attribute__((aligned(4)));
typedef uint32_t uint2 __attribute__((ext_vector_type(2)))
__attribute__((aligned(4)));

struct Invocation_Info {
  uint3 work_group_size;
  uint3 invocation_id;
  uint3 invocation_count;
  uint3 subgroup_size;
  uint32_t subgroup_x_bits, subgroup_x_offset;
  uint32_t subgroup_y_bits, subgroup_y_offset;
  uint32_t subgroup_z_bits, subgroup_z_offset;
  uint32_t wave_width;
  // void *[N]
  void **descriptor_sets[0x10];
};

struct Image1D {
  uint8_t *data;
  uint32_t format, width;
};

struct Image2D {
  uint8_t *data;
  uint32_t format, pitch, width, height;
};

struct Image3D {
  uint8_t *data;
  uint32_t format, width_pitch, height_pitch, width, height, depth;
};

uint3 spv_get_global_invocation_id(Invocation_Info *state, uint32_t lane_id) {

  uint3 subgroup_offset = (uint3){
      state->invocation_id.x * state->subgroup_size.x,
      state->invocation_id.y * state->subgroup_size.y,
      state->invocation_id.z * state->subgroup_size.z,
  };

  uint3 lane_offset = (uint3){
      (lane_id & state->subgroup_x_bits) >> state->subgroup_x_offset,
      (lane_id & state->subgroup_y_bits) >> state->subgroup_y_offset,
      (lane_id & state->subgroup_z_bits) >> state->subgroup_z_offset,
  };

  return subgroup_offset + lane_offset;
}

uint3 spv_get_work_group_size(Invocation_Info *state) {
  return state->work_group_size;
}

void *get_uniform_const_ptr(Invocation_Info *state, uint32_t set,
                            uint32_t binding) {
  return &state->descriptor_sets[set][binding];
}

void *get_uniform_ptr(Invocation_Info *state, int set, int binding) {
  return &state->descriptor_sets[set][binding];
}

uint32_t spv_image_read_1d_i32(uint64_t handle, uint32_t coord) {
  Image1D *ptr = (Image1D*)(void*)(size_t)handle;
  return *(uint32_t*)&ptr->data[coord];
}

void spv_image_write_1d_i32(uint64_t handle, uint32_t coord, uint32_t val) {
  Image1D *ptr = (Image1D*)(void*)(size_t)handle;
  ptr->data[coord] = val;
}

float4 spv_image_read_2d_float4(uint64_t handle, int2 coord) {
  Image2D *ptr = (Image2D*)(void*)(size_t)handle;
  return *(float4*)&ptr->data[16 *(coord.x + coord.y * ptr->pitch)];
}

void spv_image_write_f4(int *handle, int2 coord, float4 val) {
  Image2D *ptr = (Image2D*)(void*)(size_t)handle;
  *(float4*)&ptr->data[16 *(coord.x + coord.y * ptr->pitch)] = val;
}


float spv_sqrt(float a) { return sqrtf(a); }

float spv_dot_f4(float4 a, float4 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

float spv_dot_f3(float3 a, float3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float spv_dot_f2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }

float spv_length_f4(float4 a) { return spv_sqrt(spv_dot_f4(a, a)); }
float spv_length_f3(float3 a) { return spv_sqrt(spv_dot_f3(a, a)); }
float spv_length_f2(float2 a) { return spv_sqrt(spv_dot_f2(a, a)); }

void deinterleave(float4 const *in, float *out, uint32_t subgroup_size) {
  for (int i = 0; i < subgroup_size; i++) {
    out[i + subgroup_size * 0] = in[i].x;
    out[i + subgroup_size * 1] = in[i].y;
    out[i + subgroup_size * 2] = in[i].z;
    out[i + subgroup_size * 3] = in[i].w;
  }
}

uint64_t spv_lsb_64(uint64_t num) {
  static uint32_t arr[64] = {0,  1,  2,  7,  3,  13, 8,  19, 4,  25, 14, 28, 9,
                             34, 20, 40, 5,  17, 26, 38, 15, 46, 29, 48, 10, 31,
                             35, 54, 21, 50, 41, 57, 63, 6,  12, 18, 24, 27, 33,
                             39, 16, 37, 45, 47, 30, 53, 49, 56, 62, 11, 23, 32,
                             36, 44, 52, 55, 61, 22, 43, 51, 60, 42, 59, 58};
  const uint64_t debruijn = 0x0218A392CD3D5DBFULL;
  return arr[((num & (~num + 1)) * debruijn) >> 58];
}
}
#endif
