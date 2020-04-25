#ifndef SPV_STDLIB
#define SPV_STDLIB
#include <cmath>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <x86intrin.h>

extern "C" {

typedef float    float4 __attribute__((ext_vector_type(4))) __attribute__((aligned(16)));
typedef float    float3 __attribute__((ext_vector_type(3))) __attribute__((aligned(4)));
typedef float    float2 __attribute__((ext_vector_type(2))) __attribute__((aligned(4)));
typedef int32_t  int4 __attribute__((ext_vector_type(4))) __attribute__((aligned(16)));
typedef int32_t  int3 __attribute__((ext_vector_type(3))) __attribute__((aligned(4)));
typedef int32_t  int2 __attribute__((ext_vector_type(2))) __attribute__((aligned(4)));
typedef uint32_t uint4 __attribute__((ext_vector_type(4))) __attribute__((aligned(16)));
typedef uint32_t uint3 __attribute__((ext_vector_type(3))) __attribute__((aligned(4)));
typedef uint32_t uint2 __attribute__((ext_vector_type(2))) __attribute__((aligned(4)));

struct Invocation_Info {
  uint3 work_group_size;
  uint3 invocation_id;
  uint3 invocation_count;
  uint3 subgroup_size;
  // For compute we might want to have different lane mappings
  uint32_t subgroup_x_bits, subgroup_x_offset;
  uint32_t subgroup_y_bits, subgroup_y_offset;
  uint32_t subgroup_z_bits, subgroup_z_offset;
  uint32_t wave_width;
  // array of (void *[])
  void *private_data;
  void *input;
  void *builtin_output;
  // for gl_FragCoord stuff
  void *   pixel_positions;
  void *   output;
  uint8_t *push_constants;
  void *   print_fn;
  void *   trap_fn;
  float *  barycentrics;
  void **  descriptor_sets[0x10];
  uint8_t *is_front_face;
  // if we take paths with lowest popcnt we don't need
  // more than logN depth
  uint64_t mask_stack[6];
  uint32_t mask_top;
};

typedef int (*printf_t)(const char *__restrict __format, ...);
typedef void (*trap_t)();

struct Sampler {
  enum class Address_Mode {
    ADDRESS_MODE_REPEAT = 0,
  };
  enum class Mipmap_Mode {
    MIPMAP_MODE_LINEAR = 0,
  };
  enum class Filter { NEAREST = 0 };
  Address_Mode address_mode;
  Mipmap_Mode  mipmap_mode;
  Filter       filter;
};

struct Image {
  uint8_t *data;
  uint32_t bpp, pitch, width, height, depth, mip_levels, array_layers;
  enum class Format {
    UNKNOWN = 0,
    R8G8B8A8_UNORM,
    R32G32B32A32_FLOAT,
  };
  Format format;
  // textures are up to (1 << 16) in size
  // max bpp is 16 for rgbaf32
  // max size is (1 << 16)*(1 << 16)*(1 << 4) == (1 << 36) for 2d image
  size_t mip_offsets[0x10];
  size_t array_offsets[0x10];
};

struct Combined_Image {
  uint64_t image_handle;
  uint64_t sampler_handle;
};

#ifdef SPV_STDLIB_JUST_TYPES
#define FNATTR inline
#else
#define FNATTR
#endif
#define PURE __attribute__ ((pure))
FNATTR uint32_t morton(uint32_t x, uint32_t y) {
  uint32_t x_dep = _pdep_u32(x, 0b01'01'01'01'01'01'01'01'01'01'01'01'01'01'01'01);
  uint32_t y_dep = _pdep_u32(y, 0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10);
  return x_dep | y_dep;
}

FNATTR void unmorton(uint32_t address, uint32_t *x, uint32_t *y) {
  *x = _pext_u32(address, 0b01'01'01'01'01'01'01'01'01'01'01'01'01'01'01'01);
  *y = _pext_u32(address, 0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10);
}

FNATTR bool spv_is_front_face(Invocation_Info *state, uint32_t lane_id) {
  return state->is_front_face[lane_id] == 1;
}

FNATTR float4 get_pixel_position(Invocation_Info *state, uint32_t lane_id, float b0, float b1,
                                 float b2) {
  float4 v0 = ((float4 *)state->pixel_positions)[lane_id * 3 + 0];
  float4 v1 = ((float4 *)state->pixel_positions)[lane_id * 3 + 1];
  float4 v2 = ((float4 *)state->pixel_positions)[lane_id * 3 + 2];
  return v0 * b0 + v1 * b1 + v2 * b2;
}

FNATTR void spv_push_mask(Invocation_Info *state, uint64_t mask) {
  state->mask_stack[state->mask_top++] = mask;
}

FNATTR uint64_t spv_pop_mask(Invocation_Info *state) {
  return state->mask_stack[--state->mask_top];
}

FNATTR bool PURE spv_get_lane_mask(uint64_t mask, uint32_t lane_id) {
  return ((mask >> lane_id) & 1) == 1;
}

FNATTR void pixel_store_depth(Invocation_Info *state, uint32_t lane_id, float d) {
  ((float *)state->builtin_output)[lane_id] = d;
}
// For pixel shaders we follow morton curve order
// There are a number of configurations employed in this implementation
// 1: 2x2
//    0  1
//    2  3
// _________________________
// 2: 2x4
//    0  1  4  5
//    2  3  6  7
// _________________________
// 2: 8x8
//    0  1  4  5 16 17 20 21
//    2  3  6  7 18 19 22 23
//    8  9 12 13 24 25 28 29
//   10 11 14 15 26 27 30 31
//   32 33 36 37 48 49 52 53
//   34 35 38 39 50 51 54 55
//   40 41 44 45 56 57 60 61
//   42 43 46 47 58 59 62 63
// _________________________
// So that any consecutive 4 aligned 4 lane gang makes up a derivative group
FNATTR void get_derivatives(Invocation_Info *state, float *in_values, float2 *out_derivs) {
  uint32_t num_of_quads = state->wave_width / 4;
  if (state->wave_width == 4) {         // 2x2
  } else if (state->wave_width == 8) {  // 2x4
  } else if (state->wave_width == 64) { // 8x8
  } else {
    ((trap_t)state->trap_fn)();
  }
  for (uint32_t quad_id = 0; quad_id < num_of_quads; quad_id++) {
    float  v_00  = in_values[quad_id * 4 + 0];
    float  v_01  = in_values[quad_id * 4 + 1];
    float  v_10  = in_values[quad_id * 4 + 2];
    float  v_11  = in_values[quad_id * 4 + 3];
    float  dfdx  = (v_01 - v_00);
    float  dfdy  = (v_10 - v_00);
    float2 deriv = (float2){dfdx, dfdy};
    // Broadcast the derivative to all lanes within this quad
    out_derivs[quad_id * 4 + 0] = deriv;
    out_derivs[quad_id * 4 + 1] = deriv;
    out_derivs[quad_id * 4 + 2] = deriv;
    out_derivs[quad_id * 4 + 3] = deriv;
  }
}

FNATTR float clamp(float x, float min, float max) { return x > max ? max : x < min ? min : x; }

FNATTR uint32_t rgba32f_to_rgba8unorm(float4 in) {
  uint8_t r = (uint8_t)clamp(255.0f * in.x, 0.0f, 255.0f);
  uint8_t g = (uint8_t)clamp(255.0f * in.y, 0.0f, 255.0f);
  uint8_t b = (uint8_t)clamp(255.0f * in.z, 0.0f, 255.0f);
  uint8_t a = (uint8_t)clamp(255.0f * in.w, 0.0f, 255.0f);
  return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

FNATTR float4 rgba8unorm_to_rgba32f(uint32_t in) {
  float4 out;
  out.x = ((in >> 0) & 0xff) / 255.0f;
  out.y = ((in >> 8) & 0xff) / 255.0f;
  out.z = ((in >> 16) & 0xff) / 255.0f;
  out.w = ((in >> 24) & 0xff) / 255.0f;
  return out;
}
FNATTR float spv_clamp_f32(float x, float min, float max) {
  return x > max ? max : x < min ? min : x;
}
FNATTR float spv_clamp_u32(uint32_t x, uint32_t min, uint32_t max) {
  return x > max ? max : x < min ? min : x;
}
FNATTR uint64_t get_combined_image(uint64_t combined_handle) {
  Combined_Image *image = (Combined_Image *)(void *)(size_t)combined_handle;
  return image->image_handle;
}
FNATTR uint64_t get_combined_sampler(uint64_t combined_handle) {
  Combined_Image *image = (Combined_Image *)(void *)(size_t)combined_handle;
  return image->sampler_handle;
}
FNATTR float3 get_barycentrics(Invocation_Info *state, uint32_t lane_id) {
  return (float3){state->barycentrics[lane_id * 3], state->barycentrics[lane_id * 3 + 1],
                  state->barycentrics[lane_id * 3 + 2]};
}
FNATTR void *get_input_ptr(Invocation_Info *state) { return state->input; }
FNATTR void *get_private_ptr(Invocation_Info *state) { return state->private_data; }
FNATTR void *get_push_constant_ptr(Invocation_Info *state) {
  return (void *)&state->push_constants[0];
}
FNATTR void * get_output_ptr(Invocation_Info *state) { return state->output; }
FNATTR void * get_builtin_output_ptr(Invocation_Info *state) { return state->builtin_output; }
FNATTR float4 dummy_sample() { return (float4){0.0f, 0.0f, 0.0f, 0.0f}; }
using mask_t = uint64_t;
FNATTR void    kill(Invocation_Info *state, mask_t mask) {}
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

FNATTR uint3 spv_get_work_group_size(Invocation_Info *state) { return state->work_group_size; }

FNATTR void *get_uniform_const_ptr(Invocation_Info *state, uint32_t set, uint32_t binding) {
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
  Image *ptr = (Image *)(void *)(size_t)handle;
  return *(uint32_t *)&ptr->data[coord * ptr->bpp];
}

FNATTR void spv_image_write_1d_i32(uint64_t handle, uint32_t coord, uint32_t val, bool write_mask) {
  if (!write_mask)
    return;
  Image *ptr                                  = (Image *)(void *)(size_t)handle;
  *(uint32_t *)(&ptr->data[coord * ptr->bpp]) = val;
}

FNATTR float4 spv_image_read_2d_float4(uint64_t handle, int2 coord) {
  Image *ptr = (Image *)(void *)(size_t)handle;
  return *(float4 *)&ptr->data[ptr->bpp * coord.x + coord.y * ptr->pitch];
}

FNATTR void spv_image_write_f4(int *handle, int2 coord, float4 val) {
  Image *ptr = (Image *)(void *)(size_t)handle;
  *(float4 *)&ptr->data[ptr->bpp * coord.x + coord.y * ptr->pitch] = val;
}

FNATTR float spv_sqrt(float a) { return sqrtf(a); }

FNATTR float spv_dot_f4(float4 a, float4 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

FNATTR float spv_dot_f3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

FNATTR float spv_dot_f2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }

FNATTR float spv_length_f4(float4 a) { return spv_sqrt(spv_dot_f4(a, a)); }
FNATTR float spv_length_f3(float3 a) { return spv_sqrt(spv_dot_f3(a, a)); }
FNATTR float spv_length_f2(float2 a) { return spv_sqrt(spv_dot_f2(a, a)); }

FNATTR float4 spv_fabs_f4(float4 a) {
  return (float4){std::abs(a.x), std::abs(a.y), std::abs(a.z), std::abs(a.z)};
}
FNATTR float3 spv_fabs_f3(float3 a) {
  return (float3){std::abs(a.x), std::abs(a.y), std::abs(a.z)};
}
FNATTR float2 spv_fabs_f2(float2 a) { return (float2){std::abs(a.x), std::abs(a.y)}; }
FNATTR float  spv_fabs_f1(float a) { return std::abs(a); }

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
  return (float3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

FNATTR float spv_pow(float a, float b) { return std::pow(a, b); }

FNATTR float3 spv_reflect(float3 I, float3 N) { return I - 2.0f * spv_dot_f3(I, N) * N; }

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
  static uint32_t arr[64]  = {0,  1,  2,  7,  3,  13, 8,  19, 4,  25, 14, 28, 9,  34, 20, 40,
                             5,  17, 26, 38, 15, 46, 29, 48, 10, 31, 35, 54, 21, 50, 41, 57,
                             63, 6,  12, 18, 24, 27, 33, 39, 16, 37, 45, 47, 30, 53, 49, 56,
                             62, 11, 23, 32, 36, 44, 52, 55, 61, 22, 43, 51, 60, 42, 59, 58};
  const uint64_t  debruijn = 0x0218A392CD3D5DBFULL;
  return arr[((num & (~num + 1)) * debruijn) >> 58];
}

FNATTR float4 spv_image_read_2d_rgba32f_lod(uint64_t handle, uint2 coord, uint32_t mip_level) {
  Image *  image      = (Image *)(void *)(size_t)handle;
  uint32_t mip_width  = spv_clamp_u32(image->width >> mip_level, 1, image->width);
  uint32_t mip_height = spv_clamp_u32(image->height >> mip_level, 1, image->height);
  float4 * mip_data   = (float4 *)(image->data + image->mip_offsets[mip_level]);
  uint32_t pitch      = spv_clamp_u32(image->pitch >> mip_level, 1, image->pitch);
  float4 * row        = (float4 *)(image->data + image->mip_offsets[mip_level] + pitch * coord.y);
  return row[coord.x];
}
FNATTR float4 spv_image_read_2d_rgba8unorm_lod(uint64_t handle, uint2 coord, uint32_t mip_level) {
  Image *   image      = (Image *)(void *)(size_t)handle;
  uint32_t  mip_width  = spv_clamp_u32(image->width >> mip_level, 1, image->width);
  uint32_t  mip_height = spv_clamp_u32(image->height >> mip_level, 1, image->height);
  uint32_t *mip_data   = (uint32_t *)(image->data + image->mip_offsets[mip_level]);
  uint32_t  pitch      = spv_clamp_u32(image->pitch >> mip_level, 1, image->pitch);
  uint32_t *row = (uint32_t *)(image->data + image->mip_offsets[mip_level] + pitch * coord.y);
  return rgba8unorm_to_rgba32f(row[coord.x]);
}
FNATTR float4 spv_image_read_2d_lod(uint64_t handle, uint2 coord, uint32_t mip_level) {
  Image *image = (Image *)(void *)(size_t)handle;
  switch (image->format) {
  case Image::Format::R8G8B8A8_UNORM: {
    return spv_image_read_2d_rgba8unorm_lod(handle, coord, mip_level);
  }
  case Image::Format::R32G32B32A32_FLOAT: {
    return spv_image_read_2d_rgba32f_lod(handle, coord, mip_level);
  }
  default:
    // TODO: proper traping
    __builtin_unreachable();
  }
}

FNATTR float spv_fract(float x) {
  // TODO: more robust version
  return x - (int)x;
}

FNATTR float spv_lerp(float a, float b, float t) {
  // TODO: a better version?
  return a * (1.0f - t) + b * t;
}

FNATTR float4 spv_lerp_f4(float4 a, float4 b, float t) {
  // TODO: a better version?
  return a * (1.0f - t) + b * t;
}

FNATTR float spv_wraparound(float a) { return a - floor(a); }

FNATTR float4 spv_image_sample_2d_lod(uint64_t handle, float coordx, float coordy,
                                      uint32_t mip_level) {
  Image *  image = (Image *)(void *)(size_t)handle;
  float    u     = image->width * spv_wraparound(spv_fract(coordx));
  float    v     = image->height * spv_wraparound(spv_fract(coordy));
  uint32_t x0    = (uint32_t)(u);
  uint32_t y0    = (uint32_t)(v);
  uint32_t x1    = (uint32_t)(u + 1.0f);
  uint32_t y1    = (uint32_t)(v + 1.0f);
  float4   s00   = spv_image_read_2d_lod(handle, (uint2){x0, y0}, mip_level);
  float4   s01   = spv_image_read_2d_lod(handle, (uint2){x1, y0}, mip_level);
  float4   s10   = spv_image_read_2d_lod(handle, (uint2){x0, y1}, mip_level);
  float4   s11   = spv_image_read_2d_lod(handle, (uint2){x1, y1}, mip_level);
  float    l0    = spv_fract(u);
  float    l1    = spv_fract(v);
  return spv_lerp_f4(spv_lerp_f4(s00, s01, l0), spv_lerp_f4(s10, s11, l0), l1);
}
FNATTR float4 spv_image_sample_2d_float4(uint64_t image_handle, uint64_t sampler_handle,
                                         float coordx, float coordy, float dudx, float dudy,
                                         float dvdx, float dvdy) {
  //      return (float4){coordx, coordy, 0.0f, 1.0f};
  Image *  image   = (Image *)(void *)(size_t)image_handle;
  Sampler *sampler = (Sampler *)(void *)(size_t)sampler_handle;
  //  return spv_image_sample_2d_lod(
  //      image_handle,
  //      coordx, coordy,
  //      0);
  float    u = image->width * spv_wraparound(spv_fract(coordx));
  float    v = image->height * spv_wraparound(spv_fract(coordy));
  uint32_t x = (uint32_t)(u);
  uint32_t y = (uint32_t)(v);
  return spv_image_read_2d_lod(image_handle, (uint2){x, y}, 0);
  //_______________
  //|    |    |   |
  //|  i | j  | k |
  //|____|____|___|
  //| du | du | 0 |
  //| dx | dy |   |
  //|____|____|___|
  //| dv | dv | 0 |
  //| dx | dy |   |
  //|____|____|___|
  float area             = spv_clamp_f32(fabsf(dudx * dvdy - dudy * dvdx), 0.0f, 1.0f);
  float texels_per_unit  = (float)(image->width * image->height);
  float texels_per_pixel = area * texels_per_unit;
  // Magnification
  if (texels_per_pixel <= 1.0f) {
  }
  // texels_per_row = sqrt(texels_per_pixel)
  // mip_level = log2(sqrt(texels_per_pixel)) == 1/2 * log2(texels_per_pixel)
  float pow2 = log2f(texels_per_pixel) * 0.5;
  // log2(x)          = 0, 1, 2, 3, ...  N
  // x                = 1, 2, 4, 8, ... (1 << N)
  // pow2 has to be > 0.0 here
  //  float max_size = fmaxf((float)image->width, (float)image->height);
  float mip_level = spv_clamp_f32((float)image->mip_levels - pow2, 0.0f, (float)image->mip_levels);
  if (sampler->mipmap_mode == Sampler::Mipmap_Mode::MIPMAP_MODE_LINEAR) {
    uint32_t mip_level_0 = (uint32_t)floorf(mip_level);
    uint32_t mip_level_1 = (uint32_t)floorf(mip_level + 1.0f);
    if (sampler->filter == Sampler::Filter::NEAREST) {
    }
  } else {
    // unreachable
    return (float4){777.0f, 777.0f, 777.0f, 777.0f};
  }
}
FNATTR void dump_float4x4(Invocation_Info *state, float *m) {
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[0], m[1], m[2], m[3]);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[4], m[5], m[6], m[7]);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[8], m[9], m[10], m[11]);
  ((printf_t)state->print_fn)("[%f %f %f %f]\n", m[12], m[13], m[14], m[15]);
  ((printf_t)state->print_fn)("________________\n");
}
FNATTR void dump_float4(Invocation_Info *state, float *m) {
  ((printf_t)state->print_fn)("<%f %f %f %f>\n", m[0], m[1], m[2], m[3]);
}
FNATTR void dump_float3(Invocation_Info *state, float *m) {
  ((printf_t)state->print_fn)("<%f %f %f>\n", m[0], m[1], m[2]);
}
FNATTR void dump_float2(Invocation_Info *state, float *m) {
  ((printf_t)state->print_fn)("<%f %f>\n", m[0], m[1]);
}
FNATTR void dump_float(Invocation_Info *state, float m) {
  ((printf_t)state->print_fn)("<%f>\n", m);
}
FNATTR void dump_string(Invocation_Info *state, char const *str) {
  ((printf_t)state->print_fn)("%s\n", str);
}
FNATTR void dump_mask(Invocation_Info *state, uint64_t mask) {
  for (int i = 0; i < state->wave_width; i++) ((printf_t)state->print_fn)("%i", ((mask >> i) & 1));
  ((printf_t)state->print_fn)("\n");
}
}
#endif
