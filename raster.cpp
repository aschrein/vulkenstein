#define UTILS_IMPL

#include "utils.hpp"
#include <x86intrin.h>

#ifdef RASTER_EXE

#include "3rdparty/libpfc/include/libpfc.h"

#define call_pfc(call)                                                         \
  {                                                                            \
    int err = call;                                                            \
    if (err < 0) {                                                             \
      const char *msg = pfcErrorString(err);                                   \
      fprintf(stderr, "failed %s\n", msg);                                     \
      abort();                                                                 \
    }                                                                          \
  }

template <typename T> uint64_t measure_fn(T t, uint64_t N = 1) {
  uint64_t sum = 0u;
  uint64_t counter = 0u;
  PFC_CFG cfg[7] = {7, 7, 7, 0, 0, 0, 0};
  PFC_CNT cnt[7] = {0, 0, 0, 0, 0, 0, 0};
  cfg[3] = pfcParseCfg("cpu_clk_unhalted.ref_xclk:auk");
  cfg[4] = pfcParseCfg("cpu_clk_unhalted.core_clk");
  cfg[5] = pfcParseCfg("*cpl_cycles.ring0>=1:uk");
  call_pfc(pfcWrCfgs(0, 7, cfg));
  call_pfc(pfcWrCnts(0, 7, cnt));
  for (uint64_t i = 0; i < N; i++) {
    memset(cnt, 0, sizeof(cnt));
    _mm_lfence();
    PFCSTART(cnt);
    _mm_lfence();
    t();
    _mm_lfence();
    PFCEND(cnt);
    _mm_lfence();
    uint64_t diff = 0u;
    diff = (uint64_t)cnt[4];
    sum += diff;
    counter++;
  }
  return sum;
}

#define PRINT_CLOCKS(fun) fprintf(stdout, "%lu\n", measure_fn([&] { fun; }));

void write_image_2d_i32_ppm(const char *file_name, void *data, uint32_t pitch,
                            uint32_t width, uint32_t height) {
  FILE *file = fopen(file_name, "wb");
  ASSERT_ALWAYS(file);
  fprintf(file, "P6\n");
  fprintf(file, "%d %d\n", width, height);
  fprintf(file, "255\n");
  ito(height) {
    jto(width) {
      uint32_t pixel =
          *(uint32_t *)(void *)(((uint8_t *)data) + i * pitch + j * 4);
      uint8_t r = ((pixel >> 0) & 0xff);
      uint8_t g = ((pixel >> 8) & 0xff);
      uint8_t b = ((pixel >> 16) & 0xff);
      fputc(r, file);
      fputc(g, file);
      fputc(b, file);
    }
  }
  fclose(file);
}

void write_image_2d_i8_ppm(const char *file_name, void *data, uint32_t pitch,
                           uint32_t width, uint32_t height) {
  FILE *file = fopen(file_name, "wb");
  ASSERT_ALWAYS(file);
  fprintf(file, "P6\n");
  fprintf(file, "%d %d\n", width, height);
  fprintf(file, "255\n");
  ito(height) {
    jto(width) {
      uint8_t r = *(uint8_t *)(void *)(((uint8_t *)data) + i * pitch + j);
      fputc(r, file);
      fputc(r, file);
      fputc(r, file);
    }
  }
  fclose(file);
}

#endif

void clear_image_2d_i32(void *image, uint32_t pitch, uint32_t width,
                        uint32_t height, uint32_t value) {
  ito(height) {
    jto(width) {
      *(uint32_t *)(void *)(((uint8_t *)image) + i * pitch + j * 4) = value;
    }
  }
}

void clear_image_2d_i8(void *image, uint32_t pitch, uint32_t width,
                       uint32_t height, uint8_t value) {
  ito(height) {
    jto(width) {
      *(uint8_t *)(void *)(((uint8_t *)image) + i * pitch + j) = value;
    }
  }
}

void rasterize_triangles_tiled(
    // clang-format off
    uint16_t *x0, uint16_t *y0,
    uint16_t *x1, uint16_t *y1,
    uint16_t *x2, uint16_t *y2,
    uint8_t *id_tile,
    uint32_t tile_size,
    uint16_t pixel_bit_mask, uint16_t pixel_bit_offset,
    uint16_t subpixel_bit_mask, uint16_t subpixel_bit_offset,
    uint16_t triangle_count
    // clang-format on
) {
  ASSERT_ALWAYS((tile_size & (~tile_size + 1)) == tile_size); // power of two
  uint16_t x_min_pixel = 0;
  uint16_t x_max_pixel = (uint16_t)(tile_size - 1);
  uint16_t y_min_pixel = 0;
  uint16_t y_max_pixel = (uint16_t)(tile_size - 1);
  ito(triangle_count) {
    uint16_t x0_pixel = (x0[i] & pixel_bit_mask) >> pixel_bit_offset;
    uint16_t y0_pixel = (y0[i] & pixel_bit_mask) >> pixel_bit_offset;
    uint16_t x1_pixel = (x0[i] & pixel_bit_mask) >> pixel_bit_offset;
    uint16_t y1_pixel = (y0[i] & pixel_bit_mask) >> pixel_bit_offset;
    uint16_t x2_pixel = (x0[i] & pixel_bit_mask) >> pixel_bit_offset;
    uint16_t y2_pixel = (y0[i] & pixel_bit_mask) >> pixel_bit_offset;
  }
}

// Just do nested loops over each pixel on the screen
void rasterize_triangle_naive_0(float x0, float y0, float x1, float y1,
                                float x2, float y2, uint8_t *image,
                                uint32_t width, uint32_t height,
                                uint8_t value) {
  uint32_t subpixel_precision = 4;                   // bits
  ASSERT_ALWAYS((width & (~width + 1)) == width);    // power of two
  ASSERT_ALWAYS((height & (~height + 1)) == height); // power of two
  float n0_x = -(y1 - y0);
  float n0_y = (x1 - x0);
  float n1_x = -(y2 - y1);
  float n1_y = (x2 - x1);
  float n2_x = -(y0 - y2);
  float n2_y = (x0 - x2);
  ito(height) {
    jto(width) {
      float x = (((float)j) + 0.5f) / (float)width;
      float y = (((float)i) + 0.5f) / (float)height;
      float e0 = n0_x * (x - x0) + n0_y * (y - y0);
      float e1 = n1_x * (x - x1) + n1_y * (y - y1);
      float e2 = n2_x * (x - x2) + n2_y * (y - y2);
      if (e0 > 0.0f && e1 > 0.0f && e2 > 0.0f) {
        image[i * width + j] = value;
      }
    }
  }
}

int32_t clamp(int32_t v, int32_t min, int32_t max) {
  return v < min ? min : v > max ? max : v;
}

int16_t clamp(int16_t v, int16_t min, int16_t max) {
  return v < min ? min : v > max ? max : v;
}

// Nested scalar loops over each pixel in a bounding box
void rasterize_triangle_naive_1(float _x0, float _y0, float _x1, float _y1,
                                float _x2, float _y2, uint8_t *image,
                                uint32_t width_pow, uint32_t height_pow,
                                uint8_t value) {
  uint32_t pixel_precision = 15; // bits
  float k = (float)(1 << pixel_precision);
  int32_t upper_bound = (1 << pixel_precision) - 1;
  int32_t lower_bound = 0;
  int32_t x0 = clamp((int32_t)(_x0 * k), lower_bound, upper_bound);
  int32_t y0 = clamp((int32_t)(_y0 * k), lower_bound, upper_bound);
  int32_t x1 = clamp((int32_t)(_x1 * k), lower_bound, upper_bound);
  int32_t y1 = clamp((int32_t)(_y1 * k), lower_bound, upper_bound);
  int32_t x2 = clamp((int32_t)(_x2 * k), lower_bound, upper_bound);
  int32_t y2 = clamp((int32_t)(_y2 * k), lower_bound, upper_bound);

  int32_t n0_x = -(y1 - y0);
  int32_t n0_y = (x1 - x0);
  int32_t n1_x = -(y2 - y1);
  int32_t n1_y = (x2 - x1);
  int32_t n2_x = -(y0 - y2);
  int32_t n2_y = (x0 - x2);

  int32_t min_x = MIN(x0, MIN(x1, x2));
  int32_t max_x = MAX(x0, MAX(x1, x2));
  int32_t min_y = MIN(y0, MIN(y1, y2));
  int32_t max_y = MAX(y0, MAX(y1, y2));

  // how many bits there are between pixels
  ASSERT_ALWAYS(width_pow < 16);
  ASSERT_ALWAYS(height_pow < 16);
  uint32_t width_subpixel_precision = pixel_precision - width_pow;
  uint32_t height_subpixel_precision = pixel_precision - height_pow;
  int32_t x_delta = 1 << width_subpixel_precision;
  int32_t y_delta = 1 << height_subpixel_precision;
  for (int32_t y = min_y; y <= max_y; y += y_delta) {
    uint8_t *row = image + ((y >> height_subpixel_precision) << width_pow);
    int32_t e0 = n0_x * (min_x - x0) + n0_y * (y - y0);
    int32_t e1 = n1_x * (min_x - x1) + n1_y * (y - y1);
    int32_t e2 = n2_x * (min_x - x2) + n2_y * (y - y2);
    for (int32_t x = min_x; x <= max_x; x += x_delta) {
      if (e0 > 0 && e1 > 0 && e2 > 0) {
        *(row + (x >> height_subpixel_precision)) = value;
      }
      e0 += x_delta * n0_x;
      e1 += x_delta * n1_x;
      e2 += x_delta * n2_x;
    }
  }
}

using i32x8 = __m256i;
using i64x4 = __m256i;
using i16x16 = __m256i;
using i8x32 = __m256i;
using i8x16 = __m128i;
inline i32x8 add_si32x8(i32x8 a, i32x8 b) { return _mm256_add_epi32(a, b); }
inline i8x32 add_si8x32(i8x32 a, i8x32 b) { return _mm256_add_epi8(a, b); }
inline i16x16 add_si16x16(i16x16 a, i16x16 b) { return _mm256_add_epi16(a, b); }
inline i8x32 cmpeq_i8x32(i8x32 a, i8x32 b) { return _mm256_cmpeq_epi8(a, b); }
#define init_i32x8 _mm256_setr_epi32
#define init_i8x32 _mm256_setr_epi8
#define init_i16x16 _mm256_setr_epi16
#define init_i64x4 _mm256_setr_epi64x
#define shuffle_i8x32 _mm256_shuffle_epi8
#define ymm_or(a, b) _mm256_or_si256(a, b)
inline int32_t extract_sign_i32x8(i32x8 v) {
  return _mm256_movemask_ps(reinterpret_cast<__m256>((v)));
}
// two bits for each lane with total of 32 bits per 16 lanes
inline uint32_t extract_sign_i16x16(i16x16 v) {
  uint32_t mask_i8 = (uint32_t)_mm256_movemask_epi8(v);
  mask_i8 &= (uint32_t)0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10u;
  return mask_i8 | (mask_i8 >> 1);
}
// maybe use _mm256_set1_epi32/16?
inline i16x16 broadcast_i16x16(int16_t a) {
  uint32_t v = ((uint32_t)a & 0xffff) | (((uint32_t)a & 0xffff) << 16u);
  return reinterpret_cast<__m256i>(
      _mm256_broadcast_ss(reinterpret_cast<float const *>(&(v))));
}
inline i8x16 broadcast_i8x16(uint8_t a) {
  uint32_t v = (((uint32_t)a & 0xff) << 0u) | (((uint32_t)a & 0xff) << 8u) |
               (((uint32_t)a & 0xff) << 16u) | (((uint32_t)a & 0xff) << 24u);
  return reinterpret_cast<i8x16>(
      _mm_broadcast_ss(reinterpret_cast<float const *>(&(v))));
}
inline i32x8 broadcast_i32x8(int32_t v) {
  return reinterpret_cast<__m256i>(
      _mm256_broadcast_ss(reinterpret_cast<float const *>(&(v))));
}
inline i64x4 broadcast_i64x4(int64_t v) {
  return reinterpret_cast<__m256i>(
      _mm256_broadcast_sd(reinterpret_cast<double const *>(&(v))));
}
inline uint64_t unpack_mask_i1x4(int32_t mask) {
  return ((0xff * (((uint64_t)mask >> 0) & 1ull)) << 0) |
         ((0xff * (((uint64_t)mask >> 1) & 1ull)) << 8) |
         ((0xff * (((uint64_t)mask >> 2) & 1ull)) << 16) |
         ((0xff * (((uint64_t)mask >> 3) & 1ull)) << 24) |
         ((0xff * (((uint64_t)mask >> 4) & 1ull)) << 32) |
         ((0xff * (((uint64_t)mask >> 5) & 1ull)) << 40) |
         ((0xff * (((uint64_t)mask >> 6) & 1ull)) << 48) |
         ((0xff * (((uint64_t)mask >> 7) & 1ull)) << 56) | 0ull;
}
inline void unpack_mask_i2x16(uint32_t mask, uint64_t *low, uint64_t *high) {
  *low = ((0xff * (((uint64_t)mask >> 1) & 1ull)) << 0) |
         ((0xff * (((uint64_t)mask >> 3) & 1ull)) << 8) |
         ((0xff * (((uint64_t)mask >> 5) & 1ull)) << 16) |
         ((0xff * (((uint64_t)mask >> 7) & 1ull)) << 24) |
         ((0xff * (((uint64_t)mask >> 9) & 1ull)) << 32) |
         ((0xff * (((uint64_t)mask >> 11) & 1ull)) << 40) |
         ((0xff * (((uint64_t)mask >> 13) & 1ull)) << 48) |
         ((0xff * (((uint64_t)mask >> 15) & 1ull)) << 56) | 0ull;
  mask >>= 16;
  *high = ((0xff * (((uint64_t)mask >> 1) & 1ull)) << 0) |
          ((0xff * (((uint64_t)mask >> 3) & 1ull)) << 8) |
          ((0xff * (((uint64_t)mask >> 5) & 1ull)) << 16) |
          ((0xff * (((uint64_t)mask >> 7) & 1ull)) << 24) |
          ((0xff * (((uint64_t)mask >> 9) & 1ull)) << 32) |
          ((0xff * (((uint64_t)mask >> 11) & 1ull)) << 40) |
          ((0xff * (((uint64_t)mask >> 13) & 1ull)) << 48) |
          ((0xff * (((uint64_t)mask >> 15) & 1ull)) << 56) | 0ull;
}
__m256i get_mask3(const uint32_t mask) {
  i32x8 vmask = broadcast_i32x8((int32_t)mask);
  i64x4 shuffle = init_i64x4(0x0000000000000000, 0x0101010101010101,
                             0x0202020202020202, 0x0303030303030303);
  vmask = shuffle_i8x32(vmask, shuffle);
  i64x4 bit_mask = broadcast_i64x4(0x7fbfdfeff7fbfdfe);
  vmask = ymm_or(vmask, bit_mask);
  return cmpeq_i8x32(vmask, broadcast_i64x4(~0));
}

inline __m256i full_shuffle_i8x32(__m256i value, __m256i shuffle) {
  // the problem with just avx2 shuffle is that you can't move bites
  // across 128 bit (16 bytes) boundary.
  // a shuffle mask is a set of 32 bytes each representing an address
  // for the destination register to grab from.
  // the 8th bit says to put zero. [3:0] bits in each byte is an address
  // in the corresponding 128bit lane.
  // in full shuffle we allow crossing the 128bit boundary by extending
  // the address to [4:0] bits.
  // 4th bit set for the lower 128bit lane says the src needs to cross the
  // boundary. 4th bit unset in the upper 128bit lane says we need to grab a
  // byte from the lower 128 bits.
  // One of the solutions (here copied from stackoverflow) is following:
  //////////////////////////////////////////////
  // First pass: move within 128 bit lanes
  // 0x70 == 0111'0000 is meant to have 8th bit set whenever an address is
  // bigger than 4 bits(crosses 128 bit for lowest lane) so the destination is
  // zeroed. 0xF0 == 1111'0000 is meant to nullify the destination whenever an
  // address is lesser than 4 bits(crosses 128 bits for highest lane)
  //////////////////////////////////////////////
  // Second pass: move across 128 bit lanes
  // _mm256_permute4x64_epi64 is used to make different 128 bit lanes accessible
  // for the shuffle instruction. 0x4E == 01'00'11'10 == [1, 0, 3, 2]
  // effectively swaps the low and hight 128 bit lanes.
  // Finally we just or those passes together to get combined local+far moves
  __m256i K0 = init_i8x32(0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
                          0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0xF0, 0xF0,
                          0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
                          0xF0, 0xF0, 0xF0, 0xF0, 0xF0);

  __m256i K1 = init_i8x32(0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
                          0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0x70, 0x70,
                          0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
                          0x70, 0x70, 0x70, 0x70, 0x70);
  // move within 128 bit lanes
  __m256i local_mask = add_si8x32(shuffle, K0);
  __m256i local_shuffle = shuffle_i8x32(value, local_mask);
  // swap low and high 128 bit lanes
  __m256i lowhigh = _mm256_permute4x64_epi64(value, 0x4E);
  __m256i far_mask = add_si8x32(shuffle, K1);
  __m256i far_pass = shuffle_i8x32(lowhigh, far_mask);
  return ymm_or(local_shuffle, far_pass);
}

// i32x8 vectorized loop over each pixel in a bounding box
void rasterize_triangle_naive_2(float _x0, float _y0, float _x1, float _y1,
                                float _x2, float _y2, void *image,
                                uint32_t width_pow, uint32_t height_pow,
                                uint8_t value) {
  uint32_t pixel_precision = 15; // bits
  float k = (float)(1 << pixel_precision);
  int32_t upper_bound = (1 << pixel_precision) - 1;
  int32_t lower_bound = 0;
  int32_t x0 = clamp((int32_t)(_x0 * k), lower_bound, upper_bound);
  int32_t y0 = clamp((int32_t)(_y0 * k), lower_bound, upper_bound);
  int32_t x1 = clamp((int32_t)(_x1 * k), lower_bound, upper_bound);
  int32_t y1 = clamp((int32_t)(_y1 * k), lower_bound, upper_bound);
  int32_t x2 = clamp((int32_t)(_x2 * k), lower_bound, upper_bound);
  int32_t y2 = clamp((int32_t)(_y2 * k), lower_bound, upper_bound);

  int32_t n0_x = -(y1 - y0);
  int32_t n0_y = (x1 - x0);
  int32_t n1_x = -(y2 - y1);
  int32_t n1_y = (x2 - x1);
  int32_t n2_x = -(y0 - y2);
  int32_t n2_y = (x0 - x2);

  int32_t min_x = MIN(x0, MIN(x1, x2));
  int32_t max_x = MAX(x0, MAX(x1, x2));
  int32_t min_y = MIN(y0, MIN(y1, y2));
  int32_t max_y = MAX(y0, MAX(y1, y2));

  min_x &= (~0x1f);

  // how many bits there are between pixels
  ASSERT_ALWAYS(width_pow < 16);
  ASSERT_ALWAYS(height_pow < 16);
  uint32_t width_subpixel_precision = pixel_precision - width_pow;
  uint32_t height_subpixel_precision = pixel_precision - height_pow;
  int32_t x_delta = 1 << width_subpixel_precision;
  int32_t y_delta = 1 << height_subpixel_precision;
  // clang-format off
  i32x8 v_e0_init = init_i32x8(
    x_delta * n0_x * 0,
    x_delta * n0_x * 1,
    x_delta * n0_x * 2,
    x_delta * n0_x * 3,
    x_delta * n0_x * 4,
    x_delta * n0_x * 5,
    x_delta * n0_x * 6,
    x_delta * n0_x * 7
  );
  i32x8 v_e1_init = init_i32x8(
    x_delta * n1_x * 0,
    x_delta * n1_x * 1,
    x_delta * n1_x * 2,
    x_delta * n1_x * 3,
    x_delta * n1_x * 4,
    x_delta * n1_x * 5,
    x_delta * n1_x * 6,
    x_delta * n1_x * 7
  );
  i32x8 v_e2_init = init_i32x8(
    x_delta * n2_x * 0,
    x_delta * n2_x * 1,
    x_delta * n2_x * 2,
    x_delta * n2_x * 3,
    x_delta * n2_x * 4,
    x_delta * n2_x * 5,
    x_delta * n2_x * 6,
    x_delta * n2_x * 7
  );
  i32x8 v_e0_delta = broadcast_i32x8(
    x_delta * n0_x * 8
  );
  i32x8 v_e1_delta = broadcast_i32x8(
    x_delta * n1_x * 8
  );
  i32x8 v_e2_delta = broadcast_i32x8(
    x_delta * n2_x * 8
  );
  uint64_t v_value_full =
    ((uint64_t)value << 0u)   |
    ((uint64_t)value << 8u)   |
    ((uint64_t)value << 16u)  |
    ((uint64_t)value << 24u)  |
    ((uint64_t)value << 32u)  |
    ((uint64_t)value << 40u)  |
    ((uint64_t)value << 48u)  |
    ((uint64_t)value << 56u)  |
  0
  ;
  value = 0x40;
  uint64_t v_value_partial =
    ((uint64_t)value << 0u)   |
    ((uint64_t)value << 8u)   |
    ((uint64_t)value << 16u)  |
    ((uint64_t)value << 24u)  |
    ((uint64_t)value << 32u)  |
    ((uint64_t)value << 40u)  |
    ((uint64_t)value << 48u)  |
    ((uint64_t)value << 56u)  |
  0
  ;
  // clang-format on
  for (int32_t y = min_y; y <= max_y; y += y_delta) {
    uint64_t *row = (uint64_t *)(size_t)(
        (uint8_t *)image + ((y >> height_subpixel_precision) << width_pow));
    int32_t e0 = n0_x * (min_x - x0) + n0_y * (y - y0);
    int32_t e1 = n1_x * (min_x - x1) + n1_y * (y - y1);
    int32_t e2 = n2_x * (min_x - x2) + n2_y * (y - y2);
    i32x8 v_e0 = broadcast_i32x8(e0);
    i32x8 v_e1 = broadcast_i32x8(e1);
    i32x8 v_e2 = broadcast_i32x8(e2);
    v_e0 = add_si32x8(v_e0, v_e0_init);
    v_e1 = add_si32x8(v_e1, v_e1_init);
    v_e2 = add_si32x8(v_e2, v_e2_init);
    for (int32_t x = min_x; x <= max_x; x += x_delta * 8) {
      // clang-format off
      int32_t e0_sign = extract_sign_i32x8(v_e0);
      int32_t e1_sign = extract_sign_i32x8(v_e1);
      int32_t e2_sign = extract_sign_i32x8(v_e2);
      int32_t mask =
      (e0_sign | e1_sign | e2_sign);
      if (mask == 0) {
        *(row + ((x >> height_subpixel_precision) >> 3)) = v_value_full;
      } else if (mask != 0xffu) {
        *(row + ((x >> height_subpixel_precision) >> 3)) = v_value_partial;
      }
      v_e0 = add_si32x8(v_e0, v_e0_delta);
      v_e1 = add_si32x8(v_e1, v_e1_delta);
      v_e2 = add_si32x8(v_e2, v_e2_delta);
      // clang-format on
    }
  }
}

// try using i16 for computation
void rasterize_triangle_naive_3(float _x0, float _y0, float _x1, float _y1,
                                float _x2, float _y2, uint8_t *image,
                                uint32_t width_pow, uint32_t height_pow,
                                uint8_t value) {
  int16_t pixel_precision = 8; // bits
  float k = (float)(1 << pixel_precision);
  int16_t upper_bound = (int16_t)((int16_t)1 << pixel_precision) - (int16_t)1;
  int16_t lower_bound = 0;
  int16_t x0 = clamp((int16_t)(_x0 * k), lower_bound, upper_bound);
  int16_t y0 = clamp((int16_t)(_y0 * k), lower_bound, upper_bound);
  int16_t x1 = clamp((int16_t)(_x1 * k), lower_bound, upper_bound);
  int16_t y1 = clamp((int16_t)(_y1 * k), lower_bound, upper_bound);
  int16_t x2 = clamp((int16_t)(_x2 * k), lower_bound, upper_bound);
  int16_t y2 = clamp((int16_t)(_y2 * k), lower_bound, upper_bound);

  int16_t n0_x = -(y1 - y0);
  int16_t n0_y = (x1 - x0);
  int16_t n1_x = -(y2 - y1);
  int16_t n1_y = (x2 - x1);
  int16_t n2_x = -(y0 - y2);
  int16_t n2_y = (x0 - x2);

  int16_t min_x = MIN(x0, MIN(x1, x2));
  int16_t max_x = MAX(x0, MAX(x1, x2));
  int16_t min_y = MIN(y0, MIN(y1, y2));
  int16_t max_y = MAX(y0, MAX(y1, y2));

  // how many bits there are between pixels
  ASSERT_ALWAYS(width_pow <= 8);
  ASSERT_ALWAYS(height_pow <= 8);
  int16_t width_subpixel_precision = pixel_precision - (int16_t)width_pow;
  int16_t height_subpixel_precision = pixel_precision - (int16_t)height_pow;
  int16_t x_delta = (int16_t)(1 << width_subpixel_precision);
  int16_t y_delta = (int16_t)(1 << height_subpixel_precision);
  for (int16_t y = min_y; y <= max_y; y += y_delta) {
    uint8_t *row = image + ((y >> height_subpixel_precision) << width_pow);
    int16_t e0 = n0_x * (min_x - x0) + n0_y * (y - y0);
    int16_t e1 = n1_x * (min_x - x1) + n1_y * (y - y1);
    int16_t e2 = n2_x * (min_x - x2) + n2_y * (y - y2);
    for (int16_t x = min_x; x <= max_x; x += x_delta) {
      if (e0 > 0 && e1 > 0 && e2 > 0) {
        *(row + (x >> height_subpixel_precision)) = value;
      }
      e0 += x_delta * n0_x;
      e1 += x_delta * n1_x;
      e2 += x_delta * n2_x;
    }
  }
}

void print_i2x16(uint32_t mask) {
  ito(16) {
    fputc((mask & 2) == 0 ? '0' : '1', stdout);
    mask >>= 2;
  }
  fputc('\n', stdout);
}

void print_i16x16(i16x16 v) {
#define print_lane_N(N)                                                        \
  int16_t a_##N = (int16_t)_mm256_extract_epi16(v, N);                         \
  fprintf(stdout, "%i,", (int32_t)a_##N);
  print_lane_N(0);
  print_lane_N(1);
  print_lane_N(2);
  print_lane_N(3);
  print_lane_N(4);
  print_lane_N(5);
  print_lane_N(6);
  print_lane_N(7);
  print_lane_N(8);
  print_lane_N(9);
  print_lane_N(10);
  print_lane_N(11);
  print_lane_N(12);
  print_lane_N(13);
  print_lane_N(14);
  print_lane_N(15);
#undef print_lane_N
  fputc('\n', stdout);
}

// 64 bytes a cache line == 8x8 so probably we'd  want to write in chunks of 8x8
// bytes/8x2 pixels R8G8B8A8? again we don't know how many small triangles there
// are and if it'll destroy any packetization on pixel level

// i16x16 vectorized loop over pixels in a bounding box
void rasterize_triangle_naive_4(float _x0, float _y0, float _x1, float _y1,
                                float _x2, float _y2, uint8_t *image,
                                uint32_t width_pow, uint32_t height_pow,
                                uint8_t value) {
  int16_t pixel_precision = 8; // bits
  float k = (float)(1 << pixel_precision);
  int16_t upper_bound = (int16_t)((int16_t)1 << pixel_precision) - (int16_t)1;
  int16_t lower_bound = 0;
  int16_t x0 = clamp((int16_t)(_x0 * k), lower_bound, upper_bound);
  int16_t y0 = clamp((int16_t)(_y0 * k), lower_bound, upper_bound);
  int16_t x1 = clamp((int16_t)(_x1 * k), lower_bound, upper_bound);
  int16_t y1 = clamp((int16_t)(_y1 * k), lower_bound, upper_bound);
  int16_t x2 = clamp((int16_t)(_x2 * k), lower_bound, upper_bound);
  int16_t y2 = clamp((int16_t)(_y2 * k), lower_bound, upper_bound);

  int16_t n0_x = -(y1 - y0);
  int16_t n0_y = (x1 - x0);
  int16_t n1_x = -(y2 - y1);
  int16_t n1_y = (x2 - x1);
  int16_t n2_x = -(y0 - y2);
  int16_t n2_y = (x0 - x2);

  int16_t min_x = MIN(x0, MIN(x1, x2));
  int16_t max_x = MAX(x0, MAX(x1, x2));
  int16_t min_y = MIN(y0, MIN(y1, y2));
  int16_t max_y = MAX(y0, MAX(y1, y2));

  min_x &= (~0x1f);

  // how many bits there are between pixels
  ASSERT_ALWAYS(width_pow <= 8);
  ASSERT_ALWAYS(height_pow <= 8);
  // Aligned to 32 bytes
  ASSERT_ALWAYS(((size_t)image & 0x1fu) == 0);
  int16_t width_subpixel_precision = pixel_precision - (int16_t)width_pow;
  int16_t height_subpixel_precision = pixel_precision - (int16_t)height_pow;
  int16_t x_delta = (int16_t)(1 << width_subpixel_precision);
  int16_t y_delta = (int16_t)(1 << height_subpixel_precision);
  // clang-format off
  i16x16 v_e0_init = init_i16x16(
    x_delta * n0_x * 0,
    x_delta * n0_x * 1,
    x_delta * n0_x * 2,
    x_delta * n0_x * 3,
    x_delta * n0_x * 4,
    x_delta * n0_x * 5,
    x_delta * n0_x * 6,
    x_delta * n0_x * 7,
    x_delta * n0_x * 8,
    x_delta * n0_x * 9,
    x_delta * n0_x * 10,
    x_delta * n0_x * 11,
    x_delta * n0_x * 12,
    x_delta * n0_x * 13,
    x_delta * n0_x * 14,
    x_delta * n0_x * 15
  );
  i16x16 v_e1_init = init_i16x16(
    x_delta * n1_x * 0,
    x_delta * n1_x * 1,
    x_delta * n1_x * 2,
    x_delta * n1_x * 3,
    x_delta * n1_x * 4,
    x_delta * n1_x * 5,
    x_delta * n1_x * 6,
    x_delta * n1_x * 7,
    x_delta * n1_x * 8,
    x_delta * n1_x * 9,
    x_delta * n1_x * 10,
    x_delta * n1_x * 11,
    x_delta * n1_x * 12,
    x_delta * n1_x * 13,
    x_delta * n1_x * 14,
    x_delta * n1_x * 15
  );
  i16x16 v_e2_init = init_i16x16(
    x_delta * n2_x * 0,
    x_delta * n2_x * 1,
    x_delta * n2_x * 2,
    x_delta * n2_x * 3,
    x_delta * n2_x * 4,
    x_delta * n2_x * 5,
    x_delta * n2_x * 6,
    x_delta * n2_x * 7,
    x_delta * n2_x * 8,
    x_delta * n2_x * 9,
    x_delta * n2_x * 10,
    x_delta * n2_x * 11,
    x_delta * n2_x * 12,
    x_delta * n2_x * 13,
    x_delta * n2_x * 14,
    x_delta * n2_x * 15
  );
  i16x16 v_e0_delta = broadcast_i16x16(
    x_delta * n0_x * 16
  );
  i16x16 v_e1_delta = broadcast_i16x16(
    x_delta * n1_x * 16
  );
  i16x16 v_e2_delta = broadcast_i16x16(
    x_delta * n2_x * 16
  );
//  print_i16x16(v_e0_delta);
//  print_i16x16(v_e1_delta);
//  print_i16x16(v_e2_delta);
  i8x16 v_value_full = broadcast_i8x16(value);
  i8x16 v_value_partial = broadcast_i8x16(0x40);
  // clang-format on
  for (int16_t y = min_y; y <= max_y; y += y_delta) {
    i8x16 *row = (i8x16 *)(size_t)(
        (uint8_t *)image +
        (((uint32_t)y >> height_subpixel_precision) << width_pow) +
        ((uint32_t)min_x >> width_subpixel_precision));
    int16_t e0 = n0_x * (min_x - x0) + n0_y * (y - y0);
    int16_t e1 = n1_x * (min_x - x1) + n1_y * (y - y1);
    int16_t e2 = n2_x * (min_x - x2) + n2_y * (y - y2);
    i16x16 v_e0 = broadcast_i16x16(e0);
    i16x16 v_e1 = broadcast_i16x16(e1);
    i16x16 v_e2 = broadcast_i16x16(e2);
    v_e0 = add_si16x16(v_e0, v_e0_init);
    v_e1 = add_si16x16(v_e1, v_e1_init);
    v_e2 = add_si16x16(v_e2, v_e2_init);
    for (int16_t x = min_x; x <= max_x; x += x_delta * 16) {

      uint32_t e0_sign = extract_sign_i16x16(v_e0);
      uint32_t e1_sign = extract_sign_i16x16(v_e1);
      uint32_t e2_sign = extract_sign_i16x16(v_e2);
      uint32_t mask = (e0_sign | e1_sign | e2_sign);
      if (mask == 0) {
        *row = v_value_full;
      } else if (mask != 0xffffffffu) {
        *row = v_value_partial;
      }
      v_e0 = add_si16x16(v_e0, v_e0_delta);
      v_e1 = add_si16x16(v_e1, v_e1_delta);
      v_e2 = add_si16x16(v_e2, v_e2_delta);
      row += 1;
    }
  }
}

void rasterize_triangle_naive_256x256(float _x0, float _y0, float _x1,
                                      float _y1, float _x2, float _y2,
                                      uint8_t *image, uint8_t value) {
  int16_t pixel_precision = 8;
  float k = (float)(1 << pixel_precision);
  int16_t upper_bound = (int16_t)((int16_t)1 << 8) - (int16_t)1;
  int16_t lower_bound = 0;
  int16_t x0 = clamp((int16_t)(_x0 * k), lower_bound, upper_bound);
  int16_t y0 = clamp((int16_t)(_y0 * k), lower_bound, upper_bound);
  int16_t x1 = clamp((int16_t)(_x1 * k), lower_bound, upper_bound);
  int16_t y1 = clamp((int16_t)(_y1 * k), lower_bound, upper_bound);
  int16_t x2 = clamp((int16_t)(_x2 * k), lower_bound, upper_bound);
  int16_t y2 = clamp((int16_t)(_y2 * k), lower_bound, upper_bound);

  int16_t n0_x = -(y1 - y0);
  int16_t n0_y = (x1 - x0);
  int16_t n1_x = -(y2 - y1);
  int16_t n1_y = (x2 - x1);
  int16_t n2_x = -(y0 - y2);
  int16_t n2_y = (x0 - x2);

  int16_t min_x = MIN(x0, MIN(x1, x2));
  int16_t max_x = MAX(x0, MAX(x1, x2));
  int16_t min_y = MIN(y0, MIN(y1, y2));
  int16_t max_y = MAX(y0, MAX(y1, y2));

  min_x &= (~0x1f);
  // Aligned to 32 bytes
  ASSERT_ALWAYS(((size_t)image & 0x1fu) == 0);
  // clang-format off
  i16x16 v_e0_init = init_i16x16(
    n0_x * 0,
    n0_x * 1,
    n0_x * 2,
    n0_x * 3,
    n0_x * 4,
    n0_x * 5,
    n0_x * 6,
    n0_x * 7,
    n0_x * 8,
    n0_x * 9,
    n0_x * 10,
    n0_x * 11,
    n0_x * 12,
    n0_x * 13,
    n0_x * 14,
    n0_x * 15
  );
  i16x16 v_e1_init = init_i16x16(
    n1_x * 0,
    n1_x * 1,
    n1_x * 2,
    n1_x * 3,
    n1_x * 4,
    n1_x * 5,
    n1_x * 6,
    n1_x * 7,
    n1_x * 8,
    n1_x * 9,
    n1_x * 10,
    n1_x * 11,
    n1_x * 12,
    n1_x * 13,
    n1_x * 14,
    n1_x * 15
  );
  i16x16 v_e2_init = init_i16x16(
    n2_x * 0,
    n2_x * 1,
    n2_x * 2,
    n2_x * 3,
    n2_x * 4,
    n2_x * 5,
    n2_x * 6,
    n2_x * 7,
    n2_x * 8,
    n2_x * 9,
    n2_x * 10,
    n2_x * 11,
    n2_x * 12,
    n2_x * 13,
    n2_x * 14,
    n2_x * 15
  );
  i16x16 v_e0_delta = broadcast_i16x16(
    n0_x * 16
  );
  i16x16 v_e1_delta = broadcast_i16x16(
    n1_x * 16
  );
  i16x16 v_e2_delta = broadcast_i16x16(
    n2_x * 16
  );
//  print_i16x16(v_e0_delta);
//  print_i16x16(v_e1_delta);
//  print_i16x16(v_e2_delta);
  i8x16 v_value_full = broadcast_i8x16(value);
  i8x16 v_value_partial = broadcast_i8x16(0x40);
  // clang-format on
  for (int16_t y = min_y; y <= max_y; y += 1) {
    i8x16 *row =
        (i8x16 *)(size_t)((uint8_t *)image + ((uint32_t)y << 8) + min_x);
    int16_t e0 = n0_x * (min_x - x0) + n0_y * (y - y0);
    int16_t e1 = n1_x * (min_x - x1) + n1_y * (y - y1);
    int16_t e2 = n2_x * (min_x - x2) + n2_y * (y - y2);
    i16x16 v_e0 = broadcast_i16x16(e0);
    i16x16 v_e1 = broadcast_i16x16(e1);
    i16x16 v_e2 = broadcast_i16x16(e2);
    v_e0 = add_si16x16(v_e0, v_e0_init);
    v_e1 = add_si16x16(v_e1, v_e1_init);
    v_e2 = add_si16x16(v_e2, v_e2_init);
    for (int16_t x = min_x; x <= max_x; x += 16) {

      uint32_t e0_sign = extract_sign_i16x16(v_e0);
      uint32_t e1_sign = extract_sign_i16x16(v_e1);
      uint32_t e2_sign = extract_sign_i16x16(v_e2);
      uint32_t mask = // e1_sign;
          (e0_sign | e1_sign | e2_sign);
      if (mask == 0) {
        *row = v_value_full;
      } else if (mask != 0xffffffffu) {
        *row = v_value_partial;
      }
      v_e0 = add_si16x16(v_e0, v_e0_delta);
      v_e1 = add_si16x16(v_e1, v_e1_delta);
      v_e2 = add_si16x16(v_e2, v_e2_delta);
      row += 1;
    }
  }
}

#ifdef RASTER_EXE
int main(int argc, char **argv) {
  call_pfc(pfcInit());
  call_pfc(pfcPinThread(3));
  uint32_t width_pow = 8;
  uint32_t width = 1 << width_pow;
  uint32_t height_pow = 8;
  uint32_t height = 1 << height_pow;
  uint8_t *_image_i8 = (uint8_t *)malloc(sizeof(uint8_t) * width * height + 32);
  uint8_t *image_i8 = (uint8_t *)(((size_t)_image_i8 + 0x1f) & (~0x1FULL));
  clear_image_2d_i8(image_i8, width * 1, width, height, 0x00);
  defer(free(_image_i8));
  float dp = 1.0f / (float)width;
  PRINT_CLOCKS({ (void)0; });
  float test_x0 = 0.0f;
  float test_y0 = 0.0f;
  float test_x1 = 0.25f;
  float test_y1 = 0.0f;
  float test_x2 = 0.25f;
  float test_y2 = 0.25f;
  PRINT_CLOCKS(rasterize_triangle_naive_0(
      // clang-format off
        test_x0, test_y0, // p0
        test_x1, test_y1, // p1
        test_x2, test_y2, // p2
        image_i8, width, height, 0xff
      // clang-format on
      ));

  PRINT_CLOCKS(rasterize_triangle_naive_2(
      // clang-format off
        test_x0 + 0.25f, test_y0, // p0
        test_x1 + 0.25f, test_y1, // p1
        test_x2 + 0.25f, test_y2, // p2
        image_i8, width_pow, height_pow, 0xff
      // clang-format on
      ));

  PRINT_CLOCKS(rasterize_triangle_naive_3(
      // clang-format off
        test_x0, test_y0 + 0.25f, // p0
        test_x1, test_y1 + 0.25f, // p1
        test_x2, test_y2 + 0.25f, // p2
        image_i8, width_pow, height_pow, 0xff
      // clang-format on
      ));
  PRINT_CLOCKS(rasterize_triangle_naive_4(
      // clang-format off
        test_x0 + 0.25f, test_y0 + 0.25f, // p0
        test_x1 + 0.25f, test_y1 + 0.25f, // p1
        test_x2 + 0.25f, test_y2 + 0.25f, // p2
        image_i8, width_pow, height_pow, 0xff
      // clang-format on
      ));
  PRINT_CLOCKS(rasterize_triangle_naive_256x256(
      // clang-format off
        test_x0 + 0.25f, test_y0 + 0.5f, // p0
        test_x1 + 0.25f, test_y1 + 0.5f, // p1
        test_x2 + 0.25f, test_y2 + 0.5f, // p2
        image_i8, 0xff
      // clang-format on
      ));
  //  ito(1000) {
  //    rasterize_triangle_naive_4(
  //        // clang-format off
  //      test_x0 + 0.25f, test_y0 + 0.25f, // p0
  //      test_x1 + 0.25f, test_y1 + 0.25f, // p1
  //      test_x2 + 0.25f, test_y2 + 0.25f, // p2
  //      image_i8, width_pow, height_pow, 0xff
  //        // clang-format on
  //    );
  //  }
  write_image_2d_i8_ppm("image.ppm", image_i8, width * 1, width, height);
  return 0;
}
#endif
