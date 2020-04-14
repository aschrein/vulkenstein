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

uint32_t morton_naive(uint32_t x, uint32_t y) {
  uint32_t offset = 0;
  kto(16) {
    offset |=
        //
        (((x >> k) & 1) << (2 * k + 0)) | //
        (((y >> k) & 1) << (2 * k + 1)) | //
        //
        0;
  }
  return offset;
}

uint32_t morton_pdep32(uint32_t x, uint32_t y) {
  uint32_t x_dep =
      _pdep_u32(x, 0b01'01'01'01'01'01'01'01'01'01'01'01'01'01'01'01);
  uint32_t y_dep =
      _pdep_u32(y, 0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10);
  return x_dep | y_dep;
}

void unmorton_pext32(uint32_t address, uint32_t *x, uint32_t *y) {
  *x = _pext_u32(address, 0b01'01'01'01'01'01'01'01'01'01'01'01'01'01'01'01);
  *y = _pext_u32(address, 0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10);
}

void write_image_2d_i8_ppm_zcurve(const char *file_name, void *data,
                                  uint32_t size_pow) {
  FILE *file = fopen(file_name, "wb");
  ASSERT_ALWAYS(file);
  fprintf(file, "P6\n");
  uint32_t size = 1 << size_pow;
  fprintf(file, "%d %d\n", size, size);
  fprintf(file, "255\n");
  uint32_t mask = size - 1;
  ito(size) {
    jto(size) {
      uint32_t offset = morton_pdep32(j, i);
      uint8_t r = *(uint8_t *)(void *)(((uint8_t *)data) + offset);
      //       fputc(((offset >> 0) & 0xff), file);
      //       fputc(((offset >> 8) & 0xff), file);
      //       fputc(((offset >> 16) & 0xff), file);
      fputc(r, file);
      fputc(r, file);
      fputc(r, file);
    }
  }
  fclose(file);
}

uint32_t tile_coord(uint32_t x, uint32_t y, uint32_t size_pow,
                    uint32_t tile_pow) {
  uint32_t tile_mask = (1 << tile_pow) - 1;
  uint32_t tile_x = (x >> tile_pow);
  uint32_t tile_y = (y >> tile_pow);
  return                                                   //
      (x & tile_mask) |                                    //
      ((y & tile_mask) << tile_pow) |                      //
      (tile_x << (tile_pow * 2)) |                         //
      (tile_y << (tile_pow * 2 + (size_pow - tile_pow))) | //
      0;
}

void untile_coord(uint32_t offset, uint32_t *x, uint32_t *y, uint32_t size_pow,
                  uint32_t tile_pow) {
  uint32_t tile_mask = (1 << tile_pow) - 1;
  uint32_t size_mask = ((1 << size_pow) - 1) >> tile_pow;
  uint32_t local_x = ((offset >> 0) & tile_mask);
  uint32_t local_y = ((offset >> tile_pow) & tile_mask);
  uint32_t tile_x = ((offset >> (tile_pow * 2)) & size_mask);
  uint32_t tile_y =
      ((offset >> (tile_pow * 2 + (size_pow - tile_pow))) & size_mask);
  *x = (tile_x << tile_pow) | local_x;
  *y = (tile_y << tile_pow) | local_y;
}

void write_image_2d_i8_ppm_tiled(const char *file_name, void *data,
                                 uint32_t size_pow, uint32_t tile_pow) {
  FILE *file = fopen(file_name, "wb");
  ASSERT_ALWAYS(file);
  fprintf(file, "P6\n");
  uint32_t size = 1 << size_pow;
  fprintf(file, "%d %d\n", size, size);
  fprintf(file, "255\n");
  uint32_t mask = size - 1;
  ito(size) {
    jto(size) {
      uint32_t x = j;
      uint32_t y = i;
      uint32_t offset = tile_coord(x, y, size_pow, tile_pow);
      //      untile_coord(offset, &x, &y, size_pow, tile_pow);
      //      ASSERT_ALWAYS(x == j && y == i);
      uint8_t r = *(uint8_t *)(void *)(((uint8_t *)data) + offset);
      //      fputc(((offset >> 0) & 0xff), file);
      //      fputc(((offset >> 8) & 0xff), file);
      //      fputc(((offset >> 16) & 0xff), file);
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
      if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
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
#define init_i64x2 _mm_setr_epi64x
#define shuffle_i8x32 _mm256_shuffle_epi8
#define ymm_or(a, b) _mm256_or_si256(a, b)
inline int32_t extract_sign_i32x8(i32x8 v) {
  return _mm256_movemask_ps(reinterpret_cast<__m256>((v)));
}
// two bits for each lane with total of 32 bits per 16 lanes
inline uint16_t extract_sign_i16x16(i16x16 v) {
  uint32_t mask_i8 = (uint32_t)_mm256_movemask_epi8(v);
  mask_i8 = _pext_u32(mask_i8, (uint32_t)0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10u);
  return (uint16_t)mask_i8;
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

inline i8x16 unpack_mask_i1x16(uint16_t mask) {
  uint64_t low = 0;
  uint64_t high = 0;
  ito(8)
    low |= ((0xff * (((uint64_t)mask >> i) & 1ull)) << (8 * i));
  mask >>= 8;
  ito(8)
    high |=  ((0xff * (((uint64_t)mask >> i) & 1ull)) << (8 * i));
  return _mm_set_epi64(*(__m64*)&high, *(__m64*)&low);
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

struct Classified_Tile {
  uint8_t x;
  uint8_t y;
  uint16_t mask;
};

void rasterize_triangle_tiled_4x4_256x256_defer(float _x0, float _y0, float _x1,
                                                float _y1, float _x2, float _y2,
                                                Classified_Tile *tile_buffer,
                                                uint16_t *tile_count) {
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
  // Work on 4 x 4 tiles
  //   x 00 01 10 11
  //  y _____________
  // 00 |_0|_1|_2|_3|
  // 01 |_4|_5|_6|_7|
  // 10 |_8|_9|10|11|
  // 11 |12|13|14|15|
  //
  // clang-format off
  i16x16 v_e0_init = init_i16x16(
    n0_x * 0 + n0_y * 0,
    n0_x * 1 + n0_y * 0,
    n0_x * 2 + n0_y * 0,
    n0_x * 3 + n0_y * 0,
    n0_x * 0 + n0_y * 1,
    n0_x * 1 + n0_y * 1,
    n0_x * 2 + n0_y * 1,
    n0_x * 3 + n0_y * 1,
    n0_x * 0 + n0_y * 2,
    n0_x * 1 + n0_y * 2,
    n0_x * 2 + n0_y * 2,
    n0_x * 3 + n0_y * 2,
    n0_x * 0 + n0_y * 3,
    n0_x * 1 + n0_y * 3,
    n0_x * 2 + n0_y * 3,
    n0_x * 3 + n0_y * 3
  );
  i16x16 v_e1_init = init_i16x16(
    n1_x * 0 + n1_y * 0,
    n1_x * 1 + n1_y * 0,
    n1_x * 2 + n1_y * 0,
    n1_x * 3 + n1_y * 0,
    n1_x * 0 + n1_y * 1,
    n1_x * 1 + n1_y * 1,
    n1_x * 2 + n1_y * 1,
    n1_x * 3 + n1_y * 1,
    n1_x * 0 + n1_y * 2,
    n1_x * 1 + n1_y * 2,
    n1_x * 2 + n1_y * 2,
    n1_x * 3 + n1_y * 2,
    n1_x * 0 + n1_y * 3,
    n1_x * 1 + n1_y * 3,
    n1_x * 2 + n1_y * 3,
    n1_x * 3 + n1_y * 3
  );
  i16x16 v_e2_init = init_i16x16(
    n2_x * 0 + n2_y * 0,
    n2_x * 1 + n2_y * 0,
    n2_x * 2 + n2_y * 0,
    n2_x * 3 + n2_y * 0,
    n2_x * 0 + n2_y * 1,
    n2_x * 1 + n2_y * 1,
    n2_x * 2 + n2_y * 1,
    n2_x * 3 + n2_y * 1,
    n2_x * 0 + n2_y * 2,
    n2_x * 1 + n2_y * 2,
    n2_x * 2 + n2_y * 2,
    n2_x * 3 + n2_y * 2,
    n2_x * 0 + n2_y * 3,
    n2_x * 1 + n2_y * 3,
    n2_x * 2 + n2_y * 3,
    n2_x * 3 + n2_y * 3
  );
  i16x16 v_e0_delta_x = broadcast_i16x16(
    n0_x * 4
  );
  i16x16 v_e0_delta_y = broadcast_i16x16(
    n0_y * 4
  );
  i16x16 v_e1_delta_x = broadcast_i16x16(
    n1_x * 4
  );
  i16x16 v_e1_delta_y = broadcast_i16x16(
    n1_y * 4
  );
  i16x16 v_e2_delta_x = broadcast_i16x16(
    n2_x * 4
  );
  i16x16 v_e2_delta_y = broadcast_i16x16(
    n2_y * 4
  );
  int16_t e0_0 = n0_x * (min_x - x0) + n0_y * (min_y - y0);
  int16_t e1_0 = n1_x * (min_x - x1) + n1_y * (min_y - y1);
  int16_t e2_0 = n2_x * (min_x - x2) + n2_y * (min_y - y2);
  i16x16 v_e0_0 = broadcast_i16x16(e0_0);
  i16x16 v_e1_0 = broadcast_i16x16(e1_0);
  i16x16 v_e2_0 = broadcast_i16x16(e2_0);
  v_e0_0 = add_si16x16(v_e0_0, v_e0_init);
  v_e1_0 = add_si16x16(v_e1_0, v_e1_init);
  v_e2_0 = add_si16x16(v_e2_0, v_e2_init);
  uint8_t min_y_tile = 0xff & (min_y >> 2);
  uint8_t max_y_tile = 0xff & (max_y >> 2);
  uint8_t min_x_tile = 0xff & (min_x >> 2);
  uint8_t max_x_tile = 0xff & (max_x >> 2);
  // clang-format on
  *tile_count = 0;
  // ~15 cycles per tile
  for (uint8_t y = min_y_tile; y < max_y_tile; y += 1) {
    i16x16 v_e0_1 = v_e0_0;
    i16x16 v_e1_1 = v_e1_0;
    i16x16 v_e2_1 = v_e2_0;
    for (uint8_t x = min_x_tile; x < max_x_tile; x += 1) {
      uint16_t e0_sign_0 = extract_sign_i16x16(v_e0_1);
      uint16_t e1_sign_0 = extract_sign_i16x16(v_e1_1);
      uint16_t e2_sign_0 = extract_sign_i16x16(v_e2_1);
      uint16_t mask_0 = (e0_sign_0 | e1_sign_0 | e2_sign_0);
      if (mask_0 == 0 || mask_0 != 0xffffu) {
        tile_buffer[*tile_count] = {x, y, (uint16_t)~mask_0};
        *tile_count = *tile_count + 1;
      }
      v_e0_1 = add_si16x16(v_e0_1, v_e0_delta_x);
      v_e1_1 = add_si16x16(v_e1_1, v_e1_delta_x);
      v_e2_1 = add_si16x16(v_e2_1, v_e2_delta_x);
    }
    v_e0_0 = add_si16x16(v_e0_0, v_e0_delta_y);
    v_e1_0 = add_si16x16(v_e1_0, v_e1_delta_y);
    v_e2_0 = add_si16x16(v_e2_0, v_e2_delta_y);
  };
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
  // ~5k cycles
  Classified_Tile tiles[0x1000];
  uint16_t tile_count = 0;
  PRINT_CLOCKS(rasterize_triangle_tiled_4x4_256x256_defer(
      // clang-format off
          test_x0, test_y0, // p0
          test_x1, test_y1, // p1
          test_x2 + 0.5f, test_y2 + 0.5f, // p2
          &tiles[0], &tile_count
      // clang-format on
      ));
  PRINT_CLOCKS({
    i8x16 *data = (i8x16 *)((size_t)image_i8 & (~0x1fULL));
    i8x16 v_value = broadcast_i8x16(0xff);
    ito(tile_count) {
      uint8_t x = tiles[i].x;
      uint8_t y = tiles[i].y;
      uint32_t offset = tile_coord((uint32_t)x * 4, (uint32_t)y * 4, 8, 2);
      data[offset / 16] = unpack_mask_i1x16(tiles[i].mask);
    }
  });
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
  write_image_2d_i8_ppm_tiled("image.ppm", image_i8, width_pow, 2);
  return 0;
}
#endif
