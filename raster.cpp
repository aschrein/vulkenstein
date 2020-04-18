#include "utils.hpp"
#include "vk.hpp"

#define SPV_STDLIB_JUST_TYPES
#include "spv_stdlib/spv_stdlib.cpp"

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
      uint32_t offset = morton(j, i);
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
                    uint32_t tile_pow);
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
      uint32_t offset = tile_coord(x, size - y - 1, size_pow, tile_pow);
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

#include <mutex>

static int16_t *g_debug_grid[2] = {NULL, NULL};
static std::mutex g_debug_grid_mutexes[2];
static uint64_t g_current_grid = 0;
static uint32_t g_debug_grid_size = 0;
static uint32_t selected_texel_x = 0;
static uint32_t selected_texel_y = 0;
static uint64_t selected_texel_break = 0;
static float mouse_world_x = 0.0f;
static float mouse_world_y = 0.0f;

#endif // RASTER_EXE

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

uint32_t rgba32f_to_rgba8_unorm(float r, float g, float b, float a) {
  uint8_t r8 = (uint8_t)(vki::clamp(r, 0.0f, 1.0f) * 255.0f);
  uint8_t g8 = (uint8_t)(vki::clamp(g, 0.0f, 1.0f) * 255.0f);
  uint8_t b8 = (uint8_t)(vki::clamp(b, 0.0f, 1.0f) * 255.0f);
  uint8_t a8 = (uint8_t)(vki::clamp(a, 0.0f, 1.0f) * 255.0f);
  return                     //
      ((uint32_t)r8 << 0) |  //
      ((uint32_t)g8 << 8) |  //
      ((uint32_t)b8 << 16) | //
      ((uint32_t)a8 << 24);  //
}

uint32_t rgba32f_to_srgba8_unorm(float r, float g, float b, float a) {
  uint8_t r8 =
      (uint8_t)(vki::clamp(std::pow(r, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f);
  uint8_t g8 =
      (uint8_t)(vki::clamp(std::pow(g, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f);
  uint8_t b8 =
      (uint8_t)(vki::clamp(std::pow(b, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f);
  uint8_t a8 =
      (uint8_t)(vki::clamp(std::pow(a, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f);
  return                     //
      ((uint32_t)r8 << 0) |  //
      ((uint32_t)g8 << 8) |  //
      ((uint32_t)b8 << 16) | //
      ((uint32_t)a8 << 24);  //
}

extern "C" void clear_attachment(vki::VkImageView_Impl *attachment,
                                 VkClearValue val) {
  switch (attachment->format) {
  case VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT: {
    float *data_f32 = (float *)attachment->img->get_ptr();
    uint32_t *data_u32 = (uint32_t *)attachment->img->get_ptr();
    ito(attachment->img->extent.height) {
      jto(attachment->img->extent.width) {
        data_f32[i * attachment->img->extent.width * 2 + j * 2] =
            val.depthStencil.depth;
        data_u32[i * attachment->img->extent.width * 2 + j * 2 + 1] =
            val.depthStencil.stencil;
      }
    }
    break;
  }
  case VkFormat::VK_FORMAT_R8G8B8A8_SRGB: {
    uint32_t *data = (uint32_t *)attachment->img->get_ptr();
    uint32_t tval =
        rgba32f_to_rgba8_unorm(val.color.float32[0], val.color.float32[1],
                               val.color.float32[2], val.color.float32[3]);
    ito(attachment->img->extent.height) {
      jto(attachment->img->extent.width) {
        data[i * attachment->img->extent.width + j] = tval;
      }
    }
    break;
  }
  default:
    UNIMPLEMENTED;
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

using i32x8 = __m256i;
using i64x4 = __m256i;
using i16x16 = __m256i;
using i8x32 = __m256i;
using i8x16 = __m128i;
// Result is wrapped around! the carry is ignored
inline i32x8 add_i32x8(i32x8 a, i32x8 b) { return _mm256_add_epi32(a, b); }
inline i8x32 add_i8x32(i8x32 a, i8x32 b) { return _mm256_add_epi8(a, b); }
inline i8x16 add_i8x16(i8x16 a, i8x16 b) { return _mm_add_epi8(a, b); }
inline i8x16 or_si8x16(i8x16 a, i8x16 b) { return _mm_or_ps(a, b); }
inline i16x16 add_i16x16(i16x16 a, i16x16 b) { return _mm256_add_epi16(a, b); }
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
  mask_i8 = _pext_u32(
      mask_i8, (uint32_t)0b10'10'10'10'10'10'10'10'10'10'10'10'10'10'10'10u);
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
  ito(8) low |= ((0xff * (((uint64_t)mask >> i) & 1ull)) << (8 * i));
  mask >>= 8;
  ito(8) high |= ((0xff * (((uint64_t)mask >> i) & 1ull)) << (8 * i));
  return _mm_set_epi64(*(__m64 *)&high, *(__m64 *)&low);
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
  // the problem with just avx2 shuffle is that you can't move bytes
  // across 128 bit (16 byte) boundary.
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
  // address is less than 16(0b10000) (crosses 128 bits for the high lane)
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
  __m256i local_mask = add_i8x32(shuffle, K0);
  __m256i local_shuffle = shuffle_i8x32(value, local_mask);
  // swap low and high 128 bit lanes
  __m256i lowhigh = _mm256_permute4x64_epi64(value, 0x4E);
  __m256i far_mask = add_i8x32(shuffle, K1);
  __m256i far_pass = shuffle_i8x32(lowhigh, far_mask);
  return ymm_or(local_shuffle, far_pass);
}

#pragma pack(push, 1)
struct Classified_Tile {
  uint8_t x;
  uint8_t y;
  uint16_t mask;
};
#pragma pack(pop)

static_assert(sizeof(Classified_Tile) == 4, "incorrect padding");
#define debug_break __asm__("int3")
void rasterize_triangle_tiled_1x1_256x256_defer(float _x0, float _y0, float _x1,
                                                float _y1, float _x2, float _y2,
                                                Classified_Tile *tile_buffer,
                                                uint32_t *tile_count) {
  int16_t pixel_precision = 8;
  float k = (float)(1 << pixel_precision);
  int16_t upper_bound = (int16_t)((int16_t)1 << 8) - (int16_t)1;
  int16_t lower_bound = 0;
  int16_t x0 = vki::clamp((int16_t)(_x0 * k + 0.5f), lower_bound, upper_bound);
  int16_t y0 = vki::clamp((int16_t)(_y0 * k + 0.5f), lower_bound, upper_bound);
  int16_t x1 = vki::clamp((int16_t)(_x1 * k + 0.5f), lower_bound, upper_bound);
  int16_t y1 = vki::clamp((int16_t)(_y1 * k + 0.5f), lower_bound, upper_bound);
  int16_t x2 = vki::clamp((int16_t)(_x2 * k + 0.5f), lower_bound, upper_bound);
  int16_t y2 = vki::clamp((int16_t)(_y2 * k + 0.5f), lower_bound, upper_bound);

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

  min_x &= (~0b11);
  min_y &= (~0b11);
  max_x = (max_x + 0b11) & (~0b11);
  max_y = (max_y + 0b11) & (~0b11);

  int16_t e0_0 = n0_x * (min_x - x0) + n0_y * (min_y - y0);
  int16_t e1_0 = n1_x * (min_x - x1) + n1_y * (min_y - y1);
  int16_t e2_0 = n2_x * (min_x - x2) + n2_y * (min_y - y2);

  uint8_t min_y_tile = 0xff & (min_y >> 2);
  uint8_t max_y_tile = 0xff & (max_y >> 2);
  uint8_t min_x_tile = 0xff & (min_x >> 2);
  uint8_t max_x_tile = 0xff & (max_x >> 2);
  // clang-format on
  uint32_t _tile_count = *tile_count;
  // DEBUG
  uint64_t next_grid = (g_current_grid + 1) & 1;
  std::lock_guard<std::mutex> lock(g_debug_grid_mutexes[next_grid]);
  if (g_debug_grid[next_grid] == NULL) {
    g_debug_grid[next_grid] = (int16_t *)malloc(256 * 256 * 2 * 3);
    g_debug_grid_size = 256;
  }
  memset(g_debug_grid[next_grid], 0, 256 * 256 * 2 * 3);
  // DEBUG
  // ~15 cycles per tile
  for (uint8_t y = min_y_tile; y < max_y_tile; y += 1) {
    //    if (y == max_y_tile - 1)
    //      debug_break;
    int16_t e0_1 = e0_0;
    int16_t e1_1 = e1_0;
    int16_t e2_1 = e2_0;
    for (uint8_t x = min_x_tile; x < max_x_tile; x += 1) {
      uint16_t mask = 0;
      int16_t e0_2 = e0_1;
      int16_t e1_2 = e1_1;
      int16_t e2_2 = e2_1;
      ito(4) {
        int16_t e0_3 = e0_2;
        int16_t e1_3 = e1_2;
        int16_t e2_3 = e2_2;
        jto(4) {
          // DEBUG
          uint32_t texel_x = x * 4 + j;
          uint32_t texel_y = y * 4 + i;
          if (selected_texel_break == 1 && texel_x == selected_texel_x &&
              texel_y == selected_texel_y) {
            debug_break;
            selected_texel_break = 0;
          }
          g_debug_grid[next_grid][((y * 4 + i) * 256 + x * 4 + j) * 3 + 0] =
              e0_3;
          g_debug_grid[next_grid][((y * 4 + i) * 256 + x * 4 + j) * 3 + 1] =
              e1_3;
          g_debug_grid[next_grid][((y * 4 + i) * 256 + x * 4 + j) * 3 + 2] =
              e2_3;
          // DEBUG
          uint16_t e0_sign = (uint16_t)e0_3 >> 15;
          uint16_t e1_sign = (uint16_t)e1_3 >> 15;
          uint16_t e2_sign = (uint16_t)e2_3 >> 15;
          uint16_t mask_1 = (e0_sign | e1_sign | e2_sign);
          mask = mask | ((mask_1 & 1) << ((i << 2) | j));
          e0_3 += n0_x;
          e1_3 += n1_x;
          e2_3 += n2_x;
        }
        e0_2 += n0_y;
        e1_2 += n1_y;
        e2_2 += n2_y;
      }
      if (mask != 0xffff) {
        tile_buffer[_tile_count] = {x, y, (uint16_t)~mask};
        _tile_count = _tile_count + 1;
      }
      e0_1 += n0_x * 4;
      e1_1 += n1_x * 4;
      e2_1 += n2_x * 4;
    }
    e0_0 += n0_y * 4;
    e1_0 += n1_y * 4;
    e2_0 += n2_y * 4;
  };
  *tile_count = _tile_count;
  g_current_grid = next_grid;
}

void rasterize_triangle_tiled_4x4_256x256_defer(float _x0, float _y0, float _x1,
                                                float _y1, float _x2, float _y2,
                                                Classified_Tile *tile_buffer,
                                                uint32_t *tile_count) {
  int16_t pixel_precision = 8;
  float k = (float)(1 << pixel_precision);
  int16_t upper_bound = (int16_t)((int16_t)1 << 8) - (int16_t)1;
  int16_t lower_bound = 0;
  int16_t x0 = vki::clamp((int16_t)(_x0 * k + 0.5f), lower_bound, upper_bound);
  int16_t y0 = vki::clamp((int16_t)(_y0 * k + 0.5f), lower_bound, upper_bound);
  int16_t x1 = vki::clamp((int16_t)(_x1 * k + 0.5f), lower_bound, upper_bound);
  int16_t y1 = vki::clamp((int16_t)(_y1 * k + 0.5f), lower_bound, upper_bound);
  int16_t x2 = vki::clamp((int16_t)(_x2 * k + 0.5f), lower_bound, upper_bound);
  int16_t y2 = vki::clamp((int16_t)(_y2 * k + 0.5f), lower_bound, upper_bound);

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

  min_x &= (~0b11);
  min_y &= (~0b11);
  max_x = (max_x + 0b11) & (~0b11);
  max_y = (max_y + 0b11) & (~0b11);
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
  v_e0_0 = add_i16x16(v_e0_0, v_e0_init);
  v_e1_0 = add_i16x16(v_e1_0, v_e1_init);
  v_e2_0 = add_i16x16(v_e2_0, v_e2_init);
  uint8_t min_y_tile = 0xff & (min_y >> 2);
  uint8_t max_y_tile = 0xff & (max_y >> 2);
  uint8_t min_x_tile = 0xff & (min_x >> 2);
  uint8_t max_x_tile = 0xff & (max_x >> 2);
  // clang-format on
  uint32_t _tile_count = *tile_count;
  // ~15 cycles per tile
  for (uint8_t y = min_y_tile; y < max_y_tile; y += 1) {
    i16x16 v_e0_1 = v_e0_0;
    i16x16 v_e1_1 = v_e1_0;
    i16x16 v_e2_1 = v_e2_0;
    for (uint8_t x = min_x_tile; x < max_x_tile; x += 1) {
      // DEBUG
      //      if (x != 0)
      //        continue;
      //

      uint16_t e0_sign_0 = extract_sign_i16x16(v_e0_1);
      uint16_t e1_sign_0 = extract_sign_i16x16(v_e1_1);
      uint16_t e2_sign_0 = extract_sign_i16x16(v_e2_1);
      uint16_t mask_0 = (e0_sign_0 | e1_sign_0 | e2_sign_0);
      if (mask_0 == 0 || mask_0 != 0xffffu) {
        tile_buffer[_tile_count] = {x, y, (uint16_t)~mask_0};
        _tile_count = _tile_count + 1;
      }
      v_e0_1 = add_i16x16(v_e0_1, v_e0_delta_x);
      v_e1_1 = add_i16x16(v_e1_1, v_e1_delta_x);
      v_e2_1 = add_i16x16(v_e2_1, v_e2_delta_x);
    }
    v_e0_0 = add_i16x16(v_e0_0, v_e0_delta_y);
    v_e1_0 = add_i16x16(v_e1_0, v_e1_delta_y);
    v_e2_0 = add_i16x16(v_e2_0, v_e2_delta_y);
  };
  *tile_count = _tile_count;
}

void rasterize_triangle_tiled_4x4_256x256_defer_cull(
    float x0, float y0, float x1, float y1, float x2, float y2,
    Classified_Tile *tile_buffer, uint32_t *tile_count) {

  // there could be up to 6 vertices after culling
  uint32_t num_vertices = 0;
  float2 vertices[6 * 3];
  uint8_t vc[3] = {0, 0, 0};
  vc[0] |= x0 > 1.0f ? 1 : x0 < 0.0f ? 2 : 0;
  vc[0] |= y0 > 1.0f ? 4 : y0 < 0.0f ? 8 : 0;
  vc[1] |= x1 > 1.0f ? 1 : x1 < 0.0f ? 2 : 0;
  vc[1] |= y1 > 1.0f ? 4 : y1 < 0.0f ? 8 : 0;
  vc[2] |= x2 > 1.0f ? 1 : x2 < 0.0f ? 2 : 0;
  vc[2] |= y2 > 1.0f ? 4 : y2 < 0.0f ? 8 : 0;

  float min_x = MIN(x0, MIN(x1, x2));
  float min_y = MIN(y0, MIN(y1, y2));
  float max_x = MAX(x0, MAX(x1, x2));
  float max_y = MAX(y0, MAX(y1, y2));

  if (                // bounding box doesn't intersect the tile
      min_x > 1.0f || //
      min_y > 1.0f || //
      max_x < 0.0f || //
      max_y < 0.0f    //
  )
    return;

  // totally inside of the tile
  if (vc[0] == 0 && 0 == vc[1] && 0 == vc[2]) {
    num_vertices = 3;
    vertices[0] = (float2){x0, y0};
    vertices[1] = (float2){x1, y1};
    vertices[2] = (float2){x2, y2};
  } else {
    // vertices sorted by x value
    float2 u0, u1, u2;
    bool x01 = x0 < x1;
    bool x12 = x1 < x2;
    bool x20 = x2 < x0;
    if (x01) {
      if (x20) {
        u0 = (float2){x2, y2};
        u1 = (float2){x0, y0};
        u2 = (float2){x1, y1};
      } else {
        u0 = (float2){x0, y0};
        if (x12) {
          u1 = (float2){x1, y1};
          u2 = (float2){x2, y2};
        } else {
          u1 = (float2){x2, y2};
          u2 = (float2){x1, y1};
        }
      }
    } else {
      if (x12) {
        u0 = (float2){x1, y1};
        if (x20) {
          u1 = (float2){x2, y2};
          u2 = (float2){x0, y0};
        } else {
          u1 = (float2){x0, y0};
          u2 = (float2){x2, y2};
        }
      } else {
        u0 = (float2){x2, y2};
        u1 = (float2){x1, y1};
        u2 = (float2){x0, y0};
      }
    }

    // clockwise sorted vertices
    float2 cw0, cw1, cw2;
    cw0 = u0;
    if (u1.y > u2.y) {
      cw1 = u1;
      cw2 = u2;
    } else {
      cw2 = u1;
      cw1 = u2;
    }
    float dx0 = cw1.x - cw0.x;
    float dy0 = cw1.y - cw0.y;
    float dx1 = cw2.x - cw1.x;
    float dy1 = cw2.y - cw1.y;
    float dx2 = cw0.x - cw2.x;
    float dy2 = cw0.y - cw2.y;
    vc[0] = 0;
    vc[1] = 0;
    vc[2] = 0;
    vc[0] |= cw0.x > 1.0f ? 1 : cw0.x < 0.0f ? 2 : 0;
    vc[0] |= cw0.y > 1.0f ? 4 : cw0.y < 0.0f ? 8 : 0;
    vc[1] |= cw1.x > 1.0f ? 1 : cw1.x < 0.0f ? 2 : 0;
    vc[1] |= cw1.y > 1.0f ? 4 : cw1.y < 0.0f ? 8 : 0;
    vc[2] |= cw2.x > 1.0f ? 1 : cw2.x < 0.0f ? 2 : 0;
    vc[2] |= cw2.y > 1.0f ? 4 : cw2.y < 0.0f ? 8 : 0;
    //       |       |
    //    6  |   4   |  5
    //  _____|_______|_____
    //       |       |
    //    2  |   0   |  1
    //  _____|_______|_____
    //       |       |
    //   10  |   8   |  9
    //       |       |
    if ((vc[0] == 6 || vc[0] == 4 || vc[0] == 5) &&
        (vc[2] == 6 || vc[2] == 4 || vc[2] == 5))
      return;
    if ((vc[0] == 10 || vc[0] == 8 || vc[0] == 9) &&
        (vc[1] == 10 || vc[1] == 8 || vc[1] == 9))
      return;
    if (vc[0] != 0) {
      ASSERT_ALWAYS(vc[0] == 2 || vc[0] == 8 || vc[0] == 10);
      if (vc[0] == 2) {
        vertices[0].x = 0.0f;
        float t0 = -cw2.x / dx2;
        vertices[0].y = cw2.y + dy2 * t0;

        vertices[1].x = 0.0f;
        float t1 = -cw0.x / dx0;
        vertices[1].y = cw0.y + dy0 * t1;
        if (vc[1] == vc[2] && vc[1] == 0) {
          vertices[2] = cw1;
          vertices[3] = cw2;
          num_vertices += 4;
        }
      }
    }
  }
  if (num_vertices < 3)
    return;
  ito(num_vertices - 2) {
    // DEBUG
    //    if (i == 0)
    //      continue;
    //
    rasterize_triangle_tiled_1x1_256x256_defer( //
        vertices[i + 2].x, vertices[i + 2].y,   //
        vertices[i + 1].x, vertices[i + 1].y,   //
        vertices[0].x, vertices[0].y,           //
        tile_buffer, tile_count);
  }
}

float clamp(float x, float min, float max) {
  return x > max ? max : x < min ? min : x;
}

uint32_t rgba32f_to_rgba8(float4 in) {
  uint8_t r = (uint8_t)clamp(255.0f * in.x, 0.0f, 255.0f);
  uint8_t g = (uint8_t)clamp(255.0f * in.y, 0.0f, 255.0f);
  uint8_t b = (uint8_t)clamp(255.0f * in.z, 0.0f, 255.0f);
  uint8_t a = (uint8_t)clamp(255.0f * in.w, 0.0f, 255.0f);
  return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
         ((uint32_t)a << 24);
}

void draw_indexed(vki::cmd::GPU_State *state, uint32_t indexCount,
                  uint32_t instanceCount, uint32_t firstIndex,
                  int32_t vertexOffset, uint32_t firstInstance) {
  Shader_Symbols *vs_symbols =
      get_shader_symbols(state->graphics_pipeline->vs->jitted_code);
  Shader_Symbols *ps_symbols =
      get_shader_symbols(state->graphics_pipeline->ps->jitted_code);
  NOTNULL(vs_symbols);
  NOTNULL(ps_symbols);

  Invocation_Info info = {};
  void **descriptor_sets[0x10] = {};
  defer(ito(0x10) {
    if (descriptor_sets[i] != NULL)
      free(descriptor_sets[i]);
  });
#define TMP_POOL(type, name)                                                   \
  type name[0x100];                                                            \
  uint32_t num_##name = 0;
#define ALLOC_TMP(name)                                                        \
  &name[num_##name++];                                                         \
  ASSERT_ALWAYS(num_##name < 0x100);
  TMP_POOL(Combined_Image, combined_images);
  TMP_POOL(Image, images2d);
  TMP_POOL(Sampler, samplers);
  TMP_POOL(uint64_t, handle_slots);

  ito(0x10) {
    if (state->descriptor_sets[i] != NULL) {
      descriptor_sets[i] = (void **)malloc(
          sizeof(void *) * state->descriptor_sets[i]->slot_count);
      jto(state->descriptor_sets[i]->slot_count) {
        switch (state->descriptor_sets[i]->slots[j].type) {
        case VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
          vki::VkBuffer_Impl *buffer =
              state->descriptor_sets[i]->slots[j].buffer;
          NOTNULL(buffer);
          descriptor_sets[i][j] =
              buffer->get_ptr() + state->descriptor_sets[i]->slots[j].offset;
          break;
        }
        case VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
          ASSERT_ALWAYS(num_combined_images < 0x100);
          Combined_Image *ci = ALLOC_TMP(combined_images);
          Image *img = ALLOC_TMP(images2d);
          Sampler *s = ALLOC_TMP(samplers);
          s->address_mode = Sampler::Address_Mode::ADDRESS_MODE_REPEAT;
          s->filter = Sampler::Filter::NEAREST;
          s->mipmap_mode = Sampler::Mipmap_Mode::MIPMAP_MODE_LINEAR;

          vki::VkImageView_Impl *image_view =
              state->descriptor_sets[i]->slots[j].image_view;
          NOTNULL(image_view);
          vki::VkSampler_Impl *sampler =
              state->descriptor_sets[i]->slots[j].sampler;
          NOTNULL(sampler);
          vki::VkImage_Impl *image = image_view->img;
          NOTNULL(image);
          img->array_layers = image->arrayLayers;
          img->bpp = vki::get_format_bpp(image_view->format);
          img->data = image->get_ptr(0, 0);
          img->width = image->extent.width;
          img->height = image->extent.height;
          img->depth = image->extent.depth;
          img->mip_levels = image->mipLevels;
          img->pitch = image->extent.width * img->bpp;
          kto(img->array_layers) img->array_offsets[k] =
              image->array_offsets[k];
          kto(img->mip_levels) img->mip_offsets[k] = image->mip_offsets[k];
          ci->image_handle = (uint64_t)img;
          ci->sampler_handle = (uint64_t)s;
          uint64_t *slot = ALLOC_TMP(handle_slots);
          *slot = (uint64_t)ci;
          descriptor_sets[i][j] = slot;
          break;
        }
        default:
          UNIMPLEMENTED;
        }
      }
    }
  }
  ito(0x10) info.descriptor_sets[i] = descriptor_sets[i];
#undef ALLOC_TMP
#undef TMP_POOL

  // Vertex shading
  uint8_t *vs_output = NULL;
  float4 *vs_vertex_positions = NULL;
  defer(if (vs_output != NULL) free(vs_output));
  defer(if (vs_vertex_positions != NULL) free(vs_vertex_positions));
  vki::VkPipeline_Impl *pipeline = state->graphics_pipeline;
  {
    struct Attribute_Desc {
      uint8_t *src;
      uint32_t src_stride;
      uint32_t size;
      VkFormat format;
      bool per_vertex_rate;
    };
    VkVertexInputBindingDescription vertex_bindings[0x10] = {};
    Attribute_Desc attribute_descs[0x10] = {};

    ASSERT_ALWAYS(pipeline->IA_bindings.vertexBindingDescriptionCount < 0x10);
    ASSERT_ALWAYS(pipeline->IA_bindings.vertexAttributeDescriptionCount < 0x10);
    ito(pipeline->IA_bindings.vertexBindingDescriptionCount) {
      VkVertexInputBindingDescription desc =
          pipeline->IA_bindings.pVertexBindingDescriptions[i];
      vertex_bindings[desc.binding] = desc;
    }
    ito(pipeline->IA_bindings.vertexAttributeDescriptionCount) {
      VkVertexInputAttributeDescription desc =
          pipeline->IA_bindings.pVertexAttributeDescriptions[i];
      Attribute_Desc attribute_desc;
      VkVertexInputBindingDescription binding_desc =
          vertex_bindings[desc.binding];
      attribute_desc.format = desc.format;
      attribute_desc.src =
          state->vertex_buffers[desc.binding]->get_ptr() + desc.offset;
      attribute_desc.src_stride = binding_desc.stride;
      attribute_desc.per_vertex_rate =
          binding_desc.inputRate ==
          VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;
      attribute_desc.size = vki::get_format_bpp(desc.format);
      attribute_descs[desc.location] = attribute_desc;
    }
    ASSERT_ALWAYS(pipeline->IA_topology ==
                  VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    uint32_t subgroup_size = vs_symbols->subgroup_size;
    uint32_t num_invocations = (indexCount + subgroup_size - 1) / subgroup_size;
    uint32_t total_data_units_needed = num_invocations * subgroup_size;
    uint8_t *attributes =
        (uint8_t *)malloc(total_data_units_needed * vs_symbols->input_stride);
    defer(free(attributes));
    vs_output =
        (uint8_t *)malloc(total_data_units_needed * vs_symbols->output_stride);
    vs_vertex_positions = (float4 *)malloc(total_data_units_needed * 16);
    uint32_t *index_src_i32 =
        (uint32_t *)(((size_t)state->index_buffer->get_ptr() +
                      state->index_buffer_offset) &
                     (~0b11ull));
    uint16_t *index_src_i16 =
        (uint16_t *)(((size_t)state->index_buffer->get_ptr() +
                      state->index_buffer_offset) &
                     (~0b1ull));
    //    ASSERT_ALWAYS(state->index_type == VkIndexType::VK_INDEX_TYPE_UINT32);
    auto get_index = [&](uint32_t i) {
      switch (state->index_type) {
      case VkIndexType::VK_INDEX_TYPE_UINT16:
        return (size_t)index_src_i16[i];
      case VkIndexType::VK_INDEX_TYPE_UINT32:
        return (size_t)index_src_i32[i];
      default:
        UNIMPLEMENTED;
      }
      UNIMPLEMENTED;
    };
    kto(indexCount) {
      uint32_t index =
          (uint32_t)((int32_t)get_index(k + firstIndex) + vertexOffset);
      ito(vs_symbols->input_item_count) {
        auto item = vs_symbols->input_slots[i];
        Attribute_Desc attribute_desc = attribute_descs[item.location];
        ASSERT_ALWAYS(attribute_desc.per_vertex_rate);

        if ((VkFormat)item.format == VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT &&
            attribute_desc.format == VkFormat::VK_FORMAT_R32G32B32_SFLOAT) {
          float4 tmp = (float4){1.0f, 1.0f, 1.0f, 1.0f};
          memcpy(attributes + k * vs_symbols->input_stride + item.offset, &tmp,
                 16);
          memcpy(attributes + k * vs_symbols->input_stride + item.offset,
                 attribute_desc.src + attribute_desc.src_stride * index,
                 attribute_desc.size);
        } else if ((VkFormat)item.format ==
                       VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT &&
                   attribute_desc.format ==
                       VkFormat::VK_FORMAT_R8G8B8A8_UNORM) {
          float4 tmp = (float4){1.0f, 1.0f, 1.0f, 1.0f};
          uint32_t unorm;
          memcpy(&unorm, attribute_desc.src + attribute_desc.src_stride * index,
                 attribute_desc.size);
          tmp.x = ((float)((uint8_t)((unorm >> 0) & 0xff))) / 255.0f;
          tmp.y = ((float)((uint8_t)((unorm >> 8) & 0xff))) / 255.0f;
          tmp.z = ((float)((uint8_t)((unorm >> 16) & 0xff))) / 255.0f;
          tmp.w = ((float)((uint8_t)((unorm >> 24) & 0xff))) / 255.0f;
          memcpy(attributes + k * vs_symbols->input_stride + item.offset, &tmp,
                 16);

        } else {
          ASSERT_ALWAYS(item.format == attribute_desc.format);
          memcpy(attributes + k * vs_symbols->input_stride + item.offset,
                 attribute_desc.src + attribute_desc.src_stride * index,
                 attribute_desc.size);
        }
      }
    }

    info.work_group_size = (uint3){subgroup_size, 1, 1};
    info.invocation_count = (uint3){num_invocations, 1, 1};
    info.subgroup_size = (uint3){subgroup_size, 1, 1};
    info.subgroup_x_bits = 0xff;
    info.subgroup_x_offset = 0x0;
    info.subgroup_y_bits = 0x0;
    info.subgroup_y_offset = 0x0;
    info.subgroup_z_bits = 0x0;
    info.subgroup_z_offset = 0x0;
    info.input = NULL;
    info.output = NULL;
    info.builtin_output = NULL;
    info.push_constants = state->push_constants;
    info.print_fn = (void *)printf;

    //    descriptor_set_0[0] =
    //    state->descriptor_sets[0]->slots[0].buffer->get_ptr(); float4 *mat =
    //    (float4 *)descriptor_set_0[0]; ito(4) fprintf(stdout, "%f %f %f %f\n",
    //    mat[i].x, mat[i].y, mat[i].z,
    //                   mat[i].w);
    //    fprintf(stdout, "__________________\n");
    //    mat += 4;
    //    ito(4) fprintf(stdout, "%f %f %f %f\n", mat[i].x, mat[i].y, mat[i].z,
    //                   mat[i].w);
    //    fprintf(stdout, "__________________\n");
    //    mat += 4;
    //    ito(4) fprintf(stdout, "%f %f %f %f\n", mat[i].x, mat[i].y, mat[i].z,
    //                   mat[i].w);
    //    fprintf(stdout, "#################\n");
    ito(num_invocations) {
      info.invocation_id = (uint3){i, 0, 0};
      info.input = attributes + i * subgroup_size * vs_symbols->input_stride;
      info.output = vs_output + i * subgroup_size * vs_symbols->output_stride;
      // Assume there's only gl_Position
      info.builtin_output = vs_vertex_positions + i * subgroup_size;
      vs_symbols->spv_main(&info);
    }
  }
  // Assemble that into triangles
  ASSERT_ALWAYS(state->graphics_pipeline->IA_topology ==
                VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  ASSERT_ALWAYS(indexCount % 3 == 0);
  // so the triangle could be split in up to 6 triangles after culling
  float4 *screenspace_positions =
      (float4 *)malloc(sizeof(float4) * indexCount * 6);
  uint32_t rasterizer_triangles_count = 0;
  defer(free(screenspace_positions));
  ito(indexCount / 3) {
    float4 v0 = vs_vertex_positions[i * 3 + 0];
    float4 v1 = vs_vertex_positions[i * 3 + 1];
    float4 v2 = vs_vertex_positions[i * 3 + 2];
    // TODO: culling
    v0.xyz = v0.xyz / v0.w;
    v1.xyz = v1.xyz / v1.w;
    v2.xyz = v2.xyz / v2.w;
    screenspace_positions[i * 3 + 0] = v0;
    screenspace_positions[i * 3 + 1] = v1;
    screenspace_positions[i * 3 + 2] = v2;
    rasterizer_triangles_count++;
  }
  //  rasterizer_triangles_count = 2;
  // Rastrization
  struct Pixel_Invocation_Info {
    uint32_t triangle_id;
    // Barycentric coordinates
    float b_0, b_1, b_2;
    uint32_t x, y;
  };
  uint32_t max_pixel_invocations = 1 << 25;
  Pixel_Invocation_Info *pinfos = (Pixel_Invocation_Info *)malloc(
      sizeof(Pixel_Invocation_Info) * max_pixel_invocations);
  defer(free(pinfos));

  uint32_t num_pixel_invocations = 0;

  Classified_Tile *tiles =
      (Classified_Tile *)malloc(sizeof(Classified_Tile) * (1 << 20));
  defer(free(tiles));
  uint32_t tile_count = 0;
#if 0
  kto(rasterizer_triangles_count) {
    float4 v0 = screenspace_positions[k * 3 + 0];
    float4 v1 = screenspace_positions[k * 3 + 1];
    float4 v2 = screenspace_positions[k * 3 + 2];
    // naive rasterization
    float x0 = v0.x;
    float y0 = v0.y;
    float x1 = v1.x;
    float y1 = v1.y;
    float x2 = v2.x;
    float y2 = v2.y;
    float n0_x = -(y1 - y0);
    float n0_y = (x1 - x0);
    float n1_x = -(y2 - y1);
    float n1_y = (x2 - x1);
    float n2_x = -(y0 - y2);
    float n2_y = (x0 - x2);
    float area = (x1 - x0) * (y1 + y0) + //
                 (x2 - x1) * (y2 + y1) + //
                 (x0 - x2) * (y0 + y2);
    area = -area / 2.0f;
    bool cull =
        pipeline->RS_state.cullMode != VkCullModeFlagBits::VK_CULL_MODE_NONE;
    float cull_sign =
        pipeline->RS_state.frontFace == VkFrontFace::VK_FRONT_FACE_CLOCKWISE
            ? -1.0f
            : 1.0f;
    if (pipeline->RS_state.cullMode ==
        VkCullModeFlagBits::VK_CULL_MODE_FRONT_BIT) {
      cull_sign *= cull_sign;
    }
    if (cull && area < cull_sign) {
      continue;
    }
    float area_sign = area >= 0.0f ? 1.0 : -1.0f;
    ito(state->render_area.extent.height) {
      jto(state->render_area.extent.width) {
        float x = 2.0f * (((float)j) + 0.5f) /
                      (float)state->render_area.extent.width -
                  1.0f;
        float y = 2.0f * (((float)i) + 0.5f) /
                      (float)state->render_area.extent.height -
                  1.0f;
        float e0 = n0_x * (x - x0) + n0_y * (y - y0);
        float e1 = n1_x * (x - x1) + n1_y * (y - y1);
        float e2 = n2_x * (x - x2) + n2_y * (y - y2);
        if (e0 * area_sign >= 0.0f && e1 * area_sign >= 0.0f &&
            e2 * area_sign >= 0.0f) {
          float b0 =                  //
              (x1 - x) * (y1 + y) +   //
              (x2 - x1) * (y2 + y1) + //
              (x - x2) * (y + y2);
          float b1 =                //
              (x - x0) * (y + y0) + //
              (x2 - x) * (y2 + y) + //
              (x0 - x2) * (y0 + y2);
          float b2 =                  //
              (x1 - x0) * (y1 + y0) + //
              (x - x1) * (y + y1) +   //
              (x0 - x) * (y0 + y);
          b0 = fabsf(b0) / (2 * area);
          b1 = fabsf(b1) / (2 * area);
          b2 = fabsf(b2) / (2 * area);
          float bw = b0 / v0.w + b1 / v1.w + b2 / v2.w;
          b0 = b0 / v0.w / bw;
          b1 = b1 / v1.w / bw;
          b2 = b2 / v2.w / bw;
          pinfos[num_pixel_invocations] =
              Pixel_Invocation_Info{.triangle_id = k,
                                    .b_0 = b0,
                                    .b_1 = b1,
                                    .b_2 = b2,
                                    .x = j,
                                    .y = i};
          num_pixel_invocations++;
          ASSERT_ALWAYS(num_pixel_invocations < max_pixel_invocations);

        } else {
        }
      }
    }
  }
#endif
  // Pixel shading
  ASSERT_ALWAYS(state->framebuffer->attachmentCount == 2);
  vki::VkImageView_Impl *rt = NULL;    // state->framebuffer->pAttachments[0];
  vki::VkImageView_Impl *depth = NULL; // state->framebuffer->pAttachments[0];
  ASSERT_ALWAYS(state->render_pass->subpassCount == 1);
  uint32_t num_color_attachments = 0;
  ito(state->render_pass->pSubpasses[0].colorAttachmentCount) {
    rt =
        state->framebuffer->pAttachments
            [state->render_pass->pSubpasses[0].pColorAttachments[i].attachment];
    num_color_attachments++;
  }
  if (state->render_pass->pSubpasses[0].has_depth_stencil_attachment) {
    depth = state->framebuffer
                ->pAttachments[state->render_pass->pSubpasses[0]
                                   .pDepthStencilAttachment.attachment];
  }
  NOTNULL(depth);
  NOTNULL(rt);
  float4 *pixel_output = NULL;
  defer(if (pixel_output != NULL) free(pixel_output));
  ASSERT_ALWAYS(rt->format == VkFormat::VK_FORMAT_R8G8B8A8_SRGB);
  if (0) {
    uint32_t subgroup_size = ps_symbols->subgroup_size;
    uint32_t num_invocations =
        (num_pixel_invocations + subgroup_size - 1) / subgroup_size;
    pixel_output =
        (float4 *)malloc(sizeof(float4) * num_invocations * subgroup_size);
    // Perform relocation of data for vs->ps
    uint8_t *pixel_input = NULL;
    defer(if (pixel_input != NULL) free(pixel_input));
    pixel_input = (uint8_t *)malloc(3 * ps_symbols->input_stride *
                                    num_invocations * subgroup_size);
    ito(num_pixel_invocations) {
      // TODO: interpolate
      Pixel_Invocation_Info info = pinfos[i];
      jto(ps_symbols->input_item_count) {
        kto(3) {
          memcpy(                                                         //
              pixel_input +                                               //
                  ps_symbols->input_stride * (i * 3 + k) +                //
                  ps_symbols->input_slots[j].offset,                      //
              vs_output +                                                 //
                  vs_symbols->output_slots[j].offset +                    //
                  vs_symbols->output_stride * (info.triangle_id * 3 + k), //
              vki::get_format_bpp((VkFormat)ps_symbols->input_slots[j].format));
        }
      }
    }

    info.work_group_size = (uint3){subgroup_size, 1, 1};
    info.invocation_count = (uint3){num_invocations, 1, 1};
    info.subgroup_size = (uint3){subgroup_size, 1, 1};
    info.subgroup_x_bits = 0xff;
    info.subgroup_x_offset = 0x0;
    info.subgroup_y_bits = 0x0;
    info.subgroup_y_offset = 0x0;
    info.subgroup_z_bits = 0x0;
    info.subgroup_z_offset = 0x0;
    info.input = NULL;
    info.output = NULL;
    info.builtin_output = NULL;
    info.push_constants = state->push_constants;
    info.print_fn = (void *)printf;
    ito(num_invocations) {
      float barycentrics[0x100] = {};
      jto(subgroup_size) {
        Pixel_Invocation_Info pinfo = pinfos[j + i * subgroup_size];
        barycentrics[j * 3 + 0] = pinfo.b_0;
        barycentrics[j * 3 + 1] = pinfo.b_1;
        barycentrics[j * 3 + 2] = pinfo.b_2;
      }
      info.barycentrics = barycentrics;
      info.invocation_id = (uint3){i, 0, 0};
      info.input =
          pixel_input + i * subgroup_size * ps_symbols->input_stride * 3;
      info.output = pixel_output + i * subgroup_size;
      // Assume there's only gl_Position
      info.builtin_output = vs_vertex_positions + i * subgroup_size;
      ps_symbols->spv_main(&info);
    }
  }

  uint32_t x_num_tiles = (rt->img->extent.width + 255) / 256;
  uint32_t y_num_tiles = (rt->img->extent.height + 255) / 256;
  yto(y_num_tiles) {
    xto(x_num_tiles) {
      // So like we're covering the screen with 256x256 tiles and do
      // rasterization on them and then apply that to the screen
      //
      // |-----|-----|-----|
      //  _____________
      // ||    |       |
      // ||____|  ...  |
      // |     .       |
      // |_____._______|
      //
      float tile_x = ((float)x * 256.0f) / (float)rt->img->extent.width;
      float tile_y = ((float)y * 256.0f) / (float)rt->img->extent.height;
      float tile_size_x = 256.0f / (float)rt->img->extent.width;
      float tile_size_y = 256.0f / (float)rt->img->extent.height;
      tile_count = 0;
      kto(rasterizer_triangles_count) {
        float4 v0 = screenspace_positions[k * 3 + 0];
        float4 v1 = screenspace_positions[k * 3 + 1];
        float4 v2 = screenspace_positions[k * 3 + 2];
        float x0 = (v0.x * 0.5f + 0.5f - tile_x) / tile_size_x;
        float y0 = (v0.y * 0.5f + 0.5f - tile_y) / tile_size_y;
        float x1 = (v1.x * 0.5f + 0.5f - tile_x) / tile_size_x;
        float y1 = (v1.y * 0.5f + 0.5f - tile_y) / tile_size_y;
        float x2 = (v2.x * 0.5f + 0.5f - tile_x) / tile_size_x;
        float y2 = (v2.y * 0.5f + 0.5f - tile_y) / tile_size_y;
        rasterize_triangle_tiled_4x4_256x256_defer_cull( //
            x0, y0,                                      //
            x1, y1,                                      //
            x2, y2,                                      //
            tiles, &tile_count);
      }
      ito(tile_count) {
        uint32_t subtile_x = (uint32_t)tiles[i].x * 4 + x * 256;
        uint32_t subtile_y = (uint32_t)tiles[i].y * 4 + y * 256;
        kto(4) {
          jto(4) {
            uint32_t texel_y = (subtile_y + k);
            uint32_t texel_x = (subtile_x + j);
            if (texel_x >= rt->img->extent.width)
              break;
            if (texel_y >= rt->img->extent.height)
              break;
            ((uint32_t *)rt->img
                 ->get_ptr())[texel_y * rt->img->extent.width + texel_x] =
                (tiles[i].mask & (1 << ((k << 2) | j))) == 0 ? 0x0 : 0xff0000ff;
          }
        }
        //      ito(tile_count) {
        //        uint32_t x = ((uint32_t)tiles[i].x * 4 *
        //        rt->img->extent.width) / 256; uint32_t y =
        //        ((uint32_t)tiles[i].y * 4 * rt->img->extent.height) / 256;
        //        kto((4 * rt->img->extent.height + 255) / 256) {
        //          jto((4 * rt->img->extent.width + 255) / 256) {
        //            ((uint32_t *)rt->img
        //                 ->get_ptr())[(y + k) * rt->img->extent.width + (x +
        //                 j)] =
        //                0xff0000ff;
        //          }
        //        }
      }
    }
  }
  //  ito(256) {
  //    jto(256) {
  //      uint32_t offset = tile_coord(j, i, 8, 2);
  //      uint8_t r = *(uint8_t *)(void *)(((uint8_t *)image_i8) + offset);

  //    }
  //  }
  //  ito(num_pixel_invocations) {
  //    Pixel_Invocation_Info info = pinfos[i];
  //    ((uint32_t *)rt->img->get_ptr())[info.y * rt->img->extent.width +
  //    info.x] =
  //        0xffff00ff; // rgba32f_to_rgba8(pixel_output[i]);
  //  }

  //    ito(4) fprintf(stdout, "%f %f %f %f\n", vs_vertex_positions[i].x,
  //                   vs_vertex_positions[i].y, vs_vertex_positions[i].z,
  //                   vs_vertex_positions[i].w);
  //    fprintf(stdout, "#################\n");
}

#ifdef RASTER_EXE
#define UTILS_IMPL
#include "utils.hpp"
Shader_Symbols *get_shader_symbols(void *ptr) { return NULL; }
#include <chrono>
#include <GLES3/gl32.h>
#include <SDL2/SDL.h>
#include <thread>
void MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                     GLsizei length, const GLchar *message,
                     const void *userParam) {
  fprintf(stderr,
          "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
          (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity,
          message);
}

double my_clock() {
  std::chrono::time_point<std::chrono::system_clock> now =
      std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return 1.0e-3 *
         (double)std::chrono::duration_cast<std::chrono::milliseconds>(duration)
             .count();
}

struct Oth_Camera {
  float3 pos;
  float proj[16];
  void update(float viewport_width, float viewport_height) {
    float3 look = (float3){0.0f, 0.0f, -1.0f};
    float3 left = (float3){1.0f, 0.0f, 0.0f};
    float3 up = (float3){0.0f, 1.0f, 0.0f};
    float aspect = ((float)viewport_height / viewport_width);
    float e = (float)2.4e-7f;
    float fov = 2.0f;
    // clang-format off
  float proj[16] = {
    fov * aspect,     0.0f,          0.0f,      -2.0f * pos.x,

    0.0f,           fov,    0.0f,      -2.0f * pos.y,

    0.0f,           0.0f,          0.0f,      0.0f,

    0.0f,           0.0f,          0.0f,      pos.z
  };
    // clang-format on
    memcpy(&this->proj[0], &proj[0], sizeof(proj));
  }
} g_camera;

SDL_Window *window = NULL;
SDL_GLContext glc;
int SCREEN_WIDTH, SCREEN_HEIGHT;

void compile_shader(GLuint shader) {
  glCompileShader(shader);
  GLint isCompiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
  if (isCompiled == GL_FALSE) {
    GLint maxLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
    GLchar *errorLog = (GLchar *)malloc(maxLength);
    defer(free(errorLog));
    glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);

    glDeleteShader(shader);
    fprintf(stderr, "[ERROR]: %s\n", &errorLog[0]);
    exit(1);
  }
}

void link_program(GLuint program) {
  glLinkProgram(program);
  GLint isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
  if (isLinked == GL_FALSE) {
    GLint maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
    GLchar *infoLog = (GLchar *)malloc(maxLength);
    defer(free(infoLog));
    glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);
    glDeleteProgram(program);
    fprintf(stderr, "[ERROR]: %s\n", &infoLog[0]);
    exit(1);
  }
}

GLuint create_program(GLchar const *vsrc, GLchar const *fsrc) {
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vsrc, NULL);
  compile_shader(vertexShader);
  defer(glDeleteShader(vertexShader););
  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fsrc, NULL);
  compile_shader(fragmentShader);
  defer(glDeleteShader(fragmentShader););

  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  link_program(program);
  glDetachShader(program, vertexShader);
  glDetachShader(program, fragmentShader);

  return program;
}
#include "simplefont.h"
static Temporary_Storage ts = Temporary_Storage::create(64 * (1 << 20));

void draw_strings(char const **strings, float2 *positions, size_t num_strings) {
  float aspect = ((float)SCREEN_HEIGHT / SCREEN_WIDTH);
  uint32_t total_string_length = 0;
  kto(num_strings) { total_string_length += (uint32_t)strlen(strings[k]); }
  float *coords = (float *)ts.alloc(4 * 16 * total_string_length);
  uint32_t *indices = (uint32_t *)ts.alloc(4 * 6 * total_string_length);
  uint32_t cur_coord = 0;
  uint32_t cur_index = 0;
  uint32_t chars_to_render = 0;
  kto(num_strings) {
    float world_x = positions[k].x;
    float world_y = positions[k].y;
    char const *str = strings[k];
    float x = (world_x * aspect - g_camera.pos.x) / (g_camera.pos.z) + 0.5f;
    float y = (world_y - g_camera.pos.y) / (g_camera.pos.z) + 0.5f;
    if (x < 0.0f || y < 0.0f || x > 1.0f || y > 1.0f)
      continue;

    size_t string_length = strlen(str);
    chars_to_render += string_length;
    uint32_t string_pos_x_i = (uint32_t)(clamp(x, 0.0f, 1.0f) * SCREEN_WIDTH);
    uint32_t string_pos_y_i = (uint32_t)(clamp(y, 0.0f, 1.0f) * SCREEN_HEIGHT);
    float string_pos_x = 2.0f * (float)string_pos_x_i / SCREEN_WIDTH - 1.0f;
    float string_pos_y = 2.0f * (float)string_pos_y_i / SCREEN_HEIGHT - 1.0f;
    float pixel_size_x = 2.0f / (float)SCREEN_WIDTH;
    uint32_t index_offset = cur_coord / 4;
    ito(string_length) {
      uint32_t c = (uint32_t)str[i];

      // Printable characters only
      c = clamp(c, 0x20, 0x7e);
      uint32_t row = (c - 0x20) / simplefont_bitmap_glyphs_per_row;
      uint32_t col = (c - 0x20) % simplefont_bitmap_glyphs_per_row;
      float v0 = ((float)row * (simplefont_bitmap_glyphs_height +
                                simplefont_bitmap_glyphs_pad_y * 2) +
                  simplefont_bitmap_glyphs_pad_y) /
                 simplefont_bitmap_height;
      float u0 = ((float)col * (simplefont_bitmap_glyphs_width +
                                simplefont_bitmap_glyphs_pad_x * 2) +
                  simplefont_bitmap_glyphs_pad_x) /
                 simplefont_bitmap_width;
      float u1 =
          u0 + (float)simplefont_bitmap_glyphs_width / simplefont_bitmap_width;
      float v1 = v0 + (float)simplefont_bitmap_glyphs_height /
                          simplefont_bitmap_height;
      float glyph_ss_width =
          2.0f * (float)simplefont_bitmap_glyphs_width / SCREEN_WIDTH;
      float glyph_ss_height =
          2.0f * (float)simplefont_bitmap_glyphs_height / SCREEN_HEIGHT;
      int xk[] = {0, 1, 1, 0};
      int yk[] = {0, 0, 1, 1};
      float2 uv[] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
      jto(4) {
        coords[cur_coord++] = (float)i * pixel_size_x + string_pos_x +
                              2.0f * glyph_ss_width * (float)(i + xk[j]);
        coords[cur_coord++] = string_pos_y + 2.0f * glyph_ss_height * yk[j];
        coords[cur_coord++] = uv[j].x;
        coords[cur_coord++] = uv[j].y;
      }
      indices[cur_index++] = index_offset + i * 4 + 0;
      indices[cur_index++] = index_offset + i * 4 + 1;
      indices[cur_index++] = index_offset + i * 4 + 2;
      indices[cur_index++] = index_offset + i * 4 + 0;
      indices[cur_index++] = index_offset + i * 4 + 2;
      indices[cur_index++] = index_offset + i * 4 + 3;
    }
  }
  const GLchar *vsrc =
      R"(#version 420
  layout (location=0) in vec2 vertex_position;
  layout (location=1) in vec2 vertex_uv;
  layout (location=0) out vec2 uv;
  uniform mat4 projection;
  void main() {
      /*mat2 rot = mat2(
        cos(angle), sin(angle),
        -sin(angle), cos(angle)
      );
      color = vec3(1.0, 0.0, 0.0);*/
      uv = vertex_uv;
      gl_Position =  vec4(vertex_position, 0.0, 1.0);// * projection;

  })";
  const GLchar *fsrc =
      R"(#version 420
  layout(location = 0) out vec4 SV_TARGET0;
  layout(location = 0) in vec2 uv;
  uniform vec3 ucolor;
  uniform sampler2D image;
  void main() {
    /*float color = 1.0 / ((
        //pow(abs(dFdx(gl_FragCoord.z)) + abs(dFdy(gl_FragCoord.z)), 1.0)
        pow(abs(gl_FragCoord.z) * 0.5 + 0.5, 3.5)
    ) * 1.0e7 + 1.0);*/
    if (texture2D(image, uv).x > 0.0)
      SV_TARGET0 = vec4(0.0, 0.0, 0.0, 1.0);
    else
      discard;
  })";
  static GLuint program = create_program(vsrc, fsrc);
  struct Image_GL {
    GLuint vao;
    GLuint vbo;
    GLuint ibo;
  };
  static GLuint font_texture = [&] {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    uint8_t *r8_data =
        (uint8_t *)malloc(simplefont_bitmap_height * simplefont_bitmap_width);
    defer(free(r8_data));
    ito(simplefont_bitmap_height) {
      jto(simplefont_bitmap_width) {
        char c = simplefont_bitmap[i][j];
        r8_data[(i)*simplefont_bitmap_width + j] = c == ' ' ? 0 : 0xff;
      }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, simplefont_bitmap_width,
                 simplefont_bitmap_height, 0, GL_RED, GL_UNSIGNED_BYTE,
                 r8_data);
    return tex;
  }();
  static Image_GL image_vao = [&] {
    Image_GL out;
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLuint ibo;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    out.vbo = vbo;
    out.ibo = ibo;
    out.vao = vao;
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return out;
  }();
  glBindVertexArray(image_vao.vao);
  glBindBuffer(GL_ARRAY_BUFFER, image_vao.vbo);
  glBufferData(GL_ARRAY_BUFFER, 4 * 16 * chars_to_render, coords,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, image_vao.ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * 6 * chars_to_render, indices,
               GL_DYNAMIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  // Draw

  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, font_texture);
  glUniform1i(glGetUniformLocation(program, "image"), 0);
  glBindVertexArray(image_vao.vao);
  glBindBuffer(GL_ARRAY_BUFFER, image_vao.vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, image_vao.ibo);
  glEnableVertexAttribArray(0);
  glVertexAttribBinding(0, 0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0);
  glEnableVertexAttribArray(1);
  glVertexAttribBinding(1, 0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
  glDrawElements(GL_TRIANGLES, 6 * chars_to_render, GL_UNSIGNED_INT, NULL);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
}

void draw_grid_i16(int16_t *grid, uint32_t width, uint32_t height, float size_x,
                   float size_y) {
  const GLchar *line_vs =
      R"(#version 420
  layout (location=0) in vec2 vertex_position;
  uniform mat4 projection;
  void main() {
      gl_Position =  vec4(vertex_position, 0.0, 1.0) * projection;
  })";
  const GLchar *line_ps =
      R"(#version 420
  layout(location = 0) out vec4 SV_TARGET0;
  uniform vec3 ucolor;
  void main() {
    SV_TARGET0 = vec4(ucolor, 1.0);
  })";
  const GLchar *quad_vs =
      R"(#version 420
  layout (location=0) in vec2 vertex_position;
  layout (location=1) in vec2 instance_offset;
  layout(location = 2) in vec3 instance_color;
  layout(location = 0) out vec3 color;
  uniform mat4 projection;
  void main() {
      color = instance_color;
      gl_Position =  vec4(vertex_position + instance_offset, 0.0, 1.0) * projection;
  })";
  const GLchar *quad_ps =
      R"(#version 420
  layout(location = 0) out vec4 SV_TARGET0;
  layout(location = 0) in vec3 color;
  void main() {
    SV_TARGET0 = vec4(color, 1.0);
  })";
  static GLuint line_program = create_program(line_vs, line_ps);
  static GLuint quad_program = create_program(quad_vs, quad_ps);
  static GLuint line_vao;
  static GLuint line_vbo;
  static GLuint quad_vao;
  static GLuint quad_vbo;
  static GLuint quad_instance_vbo;
  static int init_va0 = [&] {
    glGenVertexArrays(1, &line_vao);
    glBindVertexArray(line_vao);
    glGenBuffers(1, &line_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo);

    glGenVertexArrays(1, &quad_vao);
    glBindVertexArray(quad_vao);
    float pos[] = {
        0.0f, 0.0f, //
        1.0f, 0.0f, //
        1.0f, 1.0f, //
        0.0f, 0.0f, //
        1.0f, 1.0f, //
        0.0f, 1.0f, //
    };
    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(pos), pos, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &quad_instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_instance_vbo);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
  }();
  float dx = size_x / (float)width;
  float dy = size_y / (float)height;
  // Draw quads
  {

    glUseProgram(quad_program);
    glUniformMatrix4fv(glGetUniformLocation(quad_program, "projection"), 1,
                       GL_FALSE, g_camera.proj);
    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribBinding(0, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, quad_instance_vbo);
    uint32_t max_num_quads = width * height;
    ts.enter_scope();
    float *quad_data = (float *)ts.alloc(4 * 20 * max_num_quads);
    uint32_t num_quads = 0;
    uint32_t num_floats = 0;
    bool has_selection = false;
    ito(height) {
      jto(width) {
        int16_t e0 = grid[i * width * 3 + j * 3 + 0];
        int16_t e1 = grid[i * width * 3 + j * 3 + 1];
        int16_t e2 = grid[i * width * 3 + j * 3 + 2];
        if (e0 == 0 && e1 == e0 && e2 == e0) {
          continue;
        }
        quad_data[num_floats++] = dx * j;
        quad_data[num_floats++] = dy * i;

        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;

        if (e0 < 0 || e1 < 0 || e2 < 0) {
          r = 1.0f;
          g = 0.0f;
          b = 0.0f;
        }

        if (mouse_world_x > dx * j && mouse_world_x < dx * (j + 1) &&
            mouse_world_y > dy * i && mouse_world_y < dy * (i + 1)) {
          r = 0.0f;
          g = 1.0f;
          b = 0.0f;
          selected_texel_x = j;
          selected_texel_y = i;
        }

        quad_data[num_floats++] = r;
        quad_data[num_floats++] = g;
        quad_data[num_floats++] = b;
        num_quads++;
      }
    }
    if (!has_selection) {
      //      selected_texel_x = 0xffffffffu;
      //      selected_texel_y = 0xffffffffu;
    }
    glBufferData(GL_ARRAY_BUFFER, 4 * num_floats, quad_data, GL_DYNAMIC_DRAW);
    ts.exit_scope();
    glEnableVertexAttribArray(1);
    glVertexAttribBinding(1, 0);
    glVertexAttribDivisor(1, 1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, 0);
    glEnableVertexAttribArray(2);
    glVertexAttribBinding(2, 0);
    glVertexAttribDivisor(2, 1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_quads);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);
  } // Draw lines
  {
    uint32_t num_lines = (width + 1) + (height + 1);
    float *coords = (float *)ts.alloc(4 * 4 * num_lines + 512);

    ito(width + 1) {
      // vertical lines
      coords[i * 4 + 0] = dx * (float)(i);
      coords[i * 4 + 1] = 0.0f;
      coords[i * 4 + 2] = dx * (float)(i);
      coords[i * 4 + 3] = size_y;
    }
    float *hcoords = coords + (width + 1) * 4;
    ito(height + 1) {
      // horisontal lines
      hcoords[i * 4 + 0] = 0.0f;
      hcoords[i * 4 + 1] = dy * (float)(i);
      hcoords[i * 4 + 2] = size_x;
      hcoords[i * 4 + 3] = dy * (float)(i);
    }

    glUseProgram(line_program);
    glUniformMatrix4fv(glGetUniformLocation(line_program, "projection"), 1,
                       GL_FALSE, g_camera.proj);
    glUniform3f(glGetUniformLocation(line_program, "ucolor"), 0.0f, 0.01f,
                0.1f);
    glBindVertexArray(line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * 4 * num_lines, coords, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribBinding(0, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINES, 0, 2 * num_lines);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(0);
  }
  // Draw strings if the camera is close enough
  if (g_camera.pos.z > 10.0f)
    return;
  {
    char **strings = (char **)ts.alloc(sizeof(char *) * width * 5);
    float2 *string_positions = (float2 *)ts.alloc(sizeof(float2) * width * 5);
    char tmp_buf[0x100];
    uint32_t num_str = 0;
    auto alloc_str = [&](char const *fmt, int16_t v, float x, float y) {
      snprintf(tmp_buf, sizeof(tmp_buf), fmt, v);
      size_t len = strnlen(tmp_buf, sizeof(tmp_buf));
      char *dst = (char *)ts.alloc(len + 1);
      memcpy(dst, tmp_buf, len);
      dst[len] = '\0';
      string_positions[num_str] = (float2){x, y};
      strings[num_str] = dst;
      num_str++;
    };

    ito(height) {
      num_str = 0;
      ts.enter_scope();
      jto(width) {

        alloc_str("x = %i", (int16_t)j, dx * j, dy * i);
        alloc_str("y = %i", (int16_t)i, dx * j, dy * i + dy * 0.1f);

        alloc_str("e0 = %i", grid[i * width * 3 + j * 3 + 0], dx * j + 0.1f,
                  dy * i + dy * 0.25f);
        alloc_str("e1 = %i", grid[i * width * 3 + j * 3 + 1], dx * j + 0.1f,
                  dy * i + dy * 0.5f);
        alloc_str("e2 = %i", grid[i * width * 3 + j * 3 + 2], dx * j + 0.1f,
                  dy * i + dy * 0.75f);
      }
      draw_strings((char const **)strings, string_positions, num_str);
      ts.exit_scope();
    }
  }
}

void render() {

  static int init = [] {
    g_camera.pos = (float3){0.0, 0.0, 2.0};
    return 0;
  }();

  // Update delta time
  double last_time = my_clock();
  double cur_time = my_clock();
  double dt = cur_time - last_time;
  last_time = cur_time;
  g_camera.update(SCREEN_WIDTH, SCREEN_HEIGHT);
  // Scoped temporary allocator
  ts.enter_scope();
  glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  glClearColor(0.0f, 0.3f, 0.4f, 1.0f);
  glClearDepthf(1000.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  //  char const *strings[] {
  //    "test string 12454 :{}a[0]",
  //    "&*(*&(()&()*&6",
  //  };
  //  float2 positions[] = {
  //    {0.5f, 0.5f},
  //    {1.5f, 0.5f},
  //  };
  //  draw_strings(strings, positions, 2);
  if (g_debug_grid[g_current_grid]) {
    std::lock_guard<std::mutex> lock(g_debug_grid_mutexes[g_current_grid]);
    draw_grid_i16(g_debug_grid[g_current_grid], 256, 256, 256.0f, 256.0f);
  }
  defer(ts.exit_scope());

  SDL_GL_SwapWindow(window);
}

static int quit_loop = 0;

int main_tick() {
  SDL_Event event;
  SDL_GetWindowSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
  float camera_speed = 1.0f;
  static bool ldown = false;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT: {
      quit_loop = 1;
      break;
    }
    case SDL_KEYDOWN: {
      switch (event.key.keysym.sym) {

      case SDLK_w: {

        break;
      }
      case SDLK_ESCAPE: {
        quit_loop = 1;
        break;
      }
      case SDLK_s: {

        break;
      }
      case SDLK_a: {

        break;
      }
      case SDLK_d: {

        break;
      }
      }
      break;
    }
    case SDL_MOUSEBUTTONDOWN: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      if (m->button == 3) {
        if (selected_texel_x != 0xffffffff && selected_texel_y != 0xffffffff) {
          selected_texel_break = 1;
        }
      }
      if (m->button == 1) {

        ldown = true;
      }
      break;
    }
    case SDL_MOUSEBUTTONUP: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      if (m->button == 1)
        ldown = false;
      break;
    }
    case SDL_WINDOWEVENT_FOCUS_LOST: {
      ldown = false;
      break;
    }
    case SDL_MOUSEMOTION: {
      SDL_MouseMotionEvent *m = (SDL_MouseMotionEvent *)&event;
      static int old_mp_x = m->x;
      static int old_mp_y = m->y;
      int dx = m->x - old_mp_x;
      int dy = m->y - old_mp_y;
      if (ldown) {
        g_camera.pos.x -=
            g_camera.pos.z * camera_speed * (float)dx / SCREEN_WIDTH;
        g_camera.pos.y +=
            g_camera.pos.z * camera_speed * (float)dy / SCREEN_HEIGHT;
      }

      old_mp_x = m->x;
      old_mp_y = m->y;
      float aspect = ((float)SCREEN_WIDTH / SCREEN_HEIGHT);
      mouse_world_x = (2.0f * (float)m->x / SCREEN_WIDTH - 1.0f) *
                          g_camera.pos.z * aspect * 0.5f +
                      g_camera.pos.x * aspect;
      mouse_world_y =
          (-2.0f * (float)m->y / SCREEN_HEIGHT + 1.0f) * g_camera.pos.z * 0.5f +
          g_camera.pos.y;
    } break;
    case SDL_MOUSEWHEEL: {
      g_camera.pos.z += g_camera.pos.z * (float)event.wheel.y * 1.0e-1;
      g_camera.pos.z = clamp(g_camera.pos.z, 0.1f, 256.0f);
    } break;
    }
  }

  render();
  SDL_UpdateWindowSurface(window);

  return 0;
}

void main_loop() {
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(MessageCallback, 0);
  while (0 == quit_loop) {
    main_tick();
  }
}
int main(int argc, char **argv) {

  std::thread window_loop = std::thread([] {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow(
        "RasterDBG", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 512, 512,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    // 3.2 is minimal requirement for renderdoc
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetSwapInterval(0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 23);
    glc = SDL_GL_CreateContext(window);
    ASSERT_ALWAYS(glc);

    main_loop();
    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(window);
    SDL_Quit();
  });

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
  float test_x1 = 0.1f;
  float test_y1 = 0.3f;
  float test_x2 = 0.5f;
  float test_y2 = 0.5f;
  // ~5k cycles
  Classified_Tile tiles[0x1000];
  uint32_t tile_count = 0;
  uint32_t i = 0;
  while (quit_loop == 0) {
    tile_count = 0;
    float dx = (float)((i >> 4) & 0xff) / 255.0f;
    rasterize_triangle_tiled_4x4_256x256_defer_cull(
        // clang-format off
          test_x0, test_y0, // p0
          test_x1, test_y1, // p1
          test_x2, test_y2, // p2
          &tiles[0], &tile_count
        // clang-format on
    );
    i++;
  }
  PRINT_CLOCKS({
    i8x16 *data = (i8x16 *)((size_t)image_i8 & (~0x1fULL));
    i8x16 v_value = broadcast_i8x16(0xff);
    ito(tile_count) {
      uint8_t x = tiles[i].x;
      uint8_t y = tiles[i].y;
      uint32_t offset = tile_coord((uint32_t)x * 4, (uint32_t)y * 4, 8, 2);
      data[offset / 16] =
          or_si8x16(data[offset / 16], unpack_mask_i1x16(tiles[i].mask));
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
  window_loop.join();
  return 0;
}
#endif // RASTER_EXE
