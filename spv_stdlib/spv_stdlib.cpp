#ifndef SPV_STDLIB
#define SPV_STDLIB
#include <cmath>
#include <pthread.h>
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
  // array of (void *[])
  void **descriptor_sets[0x10];
  void *private_data;
  void *input;
  void *builtin_output;
  void *output;
  uint8_t push_constants[0x100];
  void *print_fn;
};

struct Image1D {
  uint8_t *data;
  uint32_t bpp, width;
};

struct Image2D {
  uint8_t *data;
  uint32_t bpp, pitch, width, height;
};

struct Image3D {
  uint8_t *data;
  uint32_t bpp, width_pitch, height_pitch, width, height, depth;
};

#ifdef SPV_STDLIB_JUST_TYPES
#define FNATTR inline
#else
#define FNATTR
#endif

FNATTR void *get_input_ptr(Invocation_Info *state) { return state->input; }
FNATTR void *get_private_ptr(Invocation_Info *state) { return state->private_data; }
FNATTR void *get_push_constant_ptr(Invocation_Info *state) {
  return (void *)&state->push_constants[0];
}
FNATTR void *get_output_ptr(Invocation_Info *state) { return state->output; }
FNATTR void *get_builtin_output_ptr(Invocation_Info *state) {
  return state->builtin_output;
}
FNATTR float4 dummy_sample() { return (float4){0.0f, 0.0f, 0.0f, 0.0f}; }
using mask_t = uint64_t;
FNATTR void kill(Invocation_Info *state, mask_t mask) {}
FNATTR int32_t spv_atomic_add_i32(int32_t *ptr, int32_t val) {
  return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}
FNATTR int32_t spv_atomic_sub_i32(int32_t *ptr, int32_t val) {
  return __atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST);
}
FNATTR int32_t spv_atomic_or_i32(int32_t *ptr, int32_t val) {
  return __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST);
}

FNATTR uint3 spv_get_global_invocation_id(Invocation_Info *state, uint32_t lane_id) {

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

FNATTR uint3 spv_get_work_group_size(Invocation_Info *state) {
  return state->work_group_size;
}

FNATTR void *get_uniform_const_ptr(Invocation_Info *state, uint32_t set,
                            uint32_t binding) {
  return state->descriptor_sets[set][binding];
}

FNATTR void *get_uniform_ptr(Invocation_Info *state, int set, int binding) {
  return state->descriptor_sets[set][binding];
}

FNATTR void *get_storage_ptr(Invocation_Info *state, int set, int binding) {
  abort();
  return NULL;
}

FNATTR uint32_t spv_image_read_1d_i32(uint64_t handle, uint32_t coord) {
  Image1D *ptr = (Image1D *)(void *)(size_t)handle;
  return *(uint32_t *)&ptr->data[coord * ptr->bpp];
}

FNATTR void spv_image_write_1d_i32(uint64_t handle, uint32_t coord, uint32_t val) {
  Image1D *ptr = (Image1D *)(void *)(size_t)handle;
  *(uint32_t *)(&ptr->data[coord * ptr->bpp]) = val;
}

FNATTR float4 spv_image_read_2d_float4(uint64_t handle, int2 coord) {
  Image2D *ptr = (Image2D *)(void *)(size_t)handle;
  return *(float4 *)&ptr->data[ptr->bpp * coord.x + coord.y * ptr->pitch];
}

FNATTR void spv_image_write_f4(int *handle, int2 coord, float4 val) {
  Image2D *ptr = (Image2D *)(void *)(size_t)handle;
  *(float4 *)&ptr->data[ptr->bpp * coord.x + coord.y * ptr->pitch] = val;
}

FNATTR float spv_sqrt(float a) { return sqrtf(a); }

FNATTR float spv_dot_f4(float4 a, float4 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

FNATTR float spv_dot_f3(float3 a, float3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

FNATTR float spv_dot_f2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }

FNATTR float spv_length_f4(float4 a) { return spv_sqrt(spv_dot_f4(a, a)); }
FNATTR float spv_length_f3(float3 a) { return spv_sqrt(spv_dot_f3(a, a)); }
FNATTR float spv_length_f2(float2 a) { return spv_sqrt(spv_dot_f2(a, a)); }

FNATTR float2 normalize_f2(float2 in) {
  float len = spv_length_f2(in);
  return (float2){in.x / len, in.y / len};
}
FNATTR float3 normalize_f3(float3 in) {
  float len = spv_length_f3(in);
  return (float3){in.x / len, in.y / len, in.z / len};
}
FNATTR float4 normalize_f4(float4 in) {
  float len = spv_length_f4(in);
  return (float4){in.x / len, in.y / len, in.z / len, in.w / len};
}

FNATTR float3 spv_cross(float3 a, float3 b) {
  return (float3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                  a.x * b.y - a.y * b.x};
}

FNATTR float4 spv_matrix_times_float_4x4(float4 *matrix, float4 vector) {
  float4 out;
  out.x = spv_dot_f4(matrix[0], vector);
  out.y = spv_dot_f4(matrix[1], vector);
  out.z = spv_dot_f4(matrix[2], vector);
  out.w = spv_dot_f4(matrix[3], vector);
  return out;
}

FNATTR void deinterleave(float4 const *in, float *out, uint32_t subgroup_size) {
  for (int i = 0; i < subgroup_size; i++) {
    out[i + subgroup_size * 0] = in[i].x;
    out[i + subgroup_size * 1] = in[i].y;
    out[i + subgroup_size * 2] = in[i].z;
    out[i + subgroup_size * 3] = in[i].w;
  }
}

FNATTR uint64_t spv_lsb_i64(uint64_t num) {
  static uint32_t arr[64] = {0,  1,  2,  7,  3,  13, 8,  19, 4,  25, 14, 28, 9,
                             34, 20, 40, 5,  17, 26, 38, 15, 46, 29, 48, 10, 31,
                             35, 54, 21, 50, 41, 57, 63, 6,  12, 18, 24, 27, 33,
                             39, 16, 37, 45, 47, 30, 53, 49, 56, 62, 11, 23, 32,
                             36, 44, 52, 55, 61, 22, 43, 51, 60, 42, 59, 58};
  const uint64_t debruijn = 0x0218A392CD3D5DBFULL;
  return arr[((num & (~num + 1)) * debruijn) >> 58];
}

FNATTR void dump_float4x4(Invocation_Info *state, float *m) {
  typedef int (*printf_t)(const char *__restrict __format, ...);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[0], m[1], m[2], m[3]);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[4], m[5], m[6], m[7]);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[8], m[9], m[10], m[11]);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[12], m[13], m[14], m[15]);
  ((printf_t)state->print_fn)("________________\n");
}
FNATTR void dump_float4(Invocation_Info *state, float *m) {
  typedef int (*printf_t)(const char *__restrict __format, ...);
  ((printf_t)state->print_fn)("<%f %f %f %f>\n", m[0], m[1], m[2], m[3]);
  ((printf_t)state->print_fn)("________________\n");
}
FNATTR void dump_string(Invocation_Info *state, char const *str) {
  typedef int (*printf_t)(const char *__restrict __format, ...);
  ((printf_t)state->print_fn)("%s\n", str);
  ((printf_t)state->print_fn)("________________\n");
}

}
#endif
