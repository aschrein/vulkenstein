#define UTILS_IMPL
#include "3rdparty/libpfc/include/libpfc.h"
#include "utils.hpp"
#include <x86intrin.h>

#define call_pfc(call)                                                         \
  {                                                                            \
    int err = call;                                                            \
    if (err < 0) {                                                             \
      const char *msg = pfcErrorString(err);                                   \
      fprintf(stderr, "failed %s\n", msg);                                     \
    }                                                                          \
  }

template <typename T> uint64_t measure_fn(T t, uint64_t N = 2) {
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
    if (i > 0) {
      uint64_t diff = 0u;
      diff = (uint64_t)cnt[4];
      sum += diff;
      counter++;
    }
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

void clear_image_2d_i32(void *image, uint32_t pitch, uint32_t width,
                        uint32_t height, uint32_t value) {
  ito(height) {
    jto(width) {
      *(uint32_t *)(void *)(((uint8_t *)image) + i * pitch + j * 4) = value;
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

void rasterize_triangle_naive_0(float x0, float y0, float x1, float y1,
                                float x2, float y2, uint32_t *image,
                                uint32_t width, uint32_t height,
                                uint32_t value) {
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

void rasterize_triangle_naive_1(float _x0, float _y0, float _x1, float _y1,
                                float _x2, float _y2, uint32_t *image,
                                uint32_t width_pow, uint32_t height_pow,
                                uint32_t value) {
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
    uint32_t *row = image + ((y >> height_subpixel_precision) << width_pow);
    for (int32_t x = min_x; x <= max_x; x += x_delta) {
      int32_t e0 = n0_x * (x - x0) + n0_y * (y - y0);
      int32_t e1 = n1_x * (x - x1) + n1_y * (y - y1);
      int32_t e2 = n2_x * (x - x2) + n2_y * (y - y2);
      if (e0 > 0 && e1 > 0 && e2 > 0) {
        *row++ = value;
      }
    }
  }
}

#ifdef RASTER_EXE
int main(int argc, char **argv) {
  uint32_t width_pow = 9;
  uint32_t width = 1 << width_pow;
  uint32_t height_pow = 9;
  uint32_t height = 1 << height_pow;
  uint32_t *image = (uint32_t *)malloc(sizeof(uint32_t) * width * height);
  clear_image_2d_i32(image, width * 4, width, height, 0xff000000);
  defer(free(image));
  float dp = 1.0f / (float)width;
  PRINT_CLOCKS(rasterize_triangle_naive_0(
      // clang-format off
     0.0f, 0.0f, // p0
     0.7f, 0.1f, // p1
     0.3f, 0.5f, // p2
     image, width, height, 0xff0000ff
      // clang-format on
      ));
  rasterize_triangle_naive_1(
      // clang-format off
     0.0f, 0.0f + dp * 256.0f, // p0
     0.7f, 0.1f + dp * 256.0f, // p1
     0.3f, 0.5f + dp * 256.0f, // p2
     image, width_pow, height_pow, 0xff00ff00
      // clang-format on
  );
  write_image_2d_i32_ppm("image.ppm", image, width * 4, width, height);
  return 0;
}
#endif
