#include "utils.hpp"
#include "vk.hpp"

#define SPV_STDLIB_JUST_TYPES
#include "spv_stdlib/spv_stdlib.cpp"

#ifdef RASTER_EXE
#include "utils.hpp"
Shader_Symbols *get_shader_symbols(void *ptr) { return NULL; }
#include <GLES3/gl32.h>
#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
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

SDL_Window *window = NULL;
SDL_GLContext glc;
int SCREEN_WIDTH, SCREEN_HEIGHT;

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

#include "simplefont.h"
#include <mutex>
static Temporary_Storage<> ts = Temporary_Storage<>::create(64 * (1 << 20));

struct Context2D {
  struct Oth_Camera {
    float3 pos;
    float4 proj[4];
    float height_over_width;
    float width_over_heigth;
    uint32_t viewport_width;
    uint32_t viewport_height;
    float world_min_x;
    float world_min_y;
    float world_max_x;
    float world_max_y;
    float mouse_world_x;
    float mouse_world_y;
    float mouse_screen_x;
    float mouse_screen_y;
    float glyphs_world_height;
    float glyphs_screen_height;
    float glyphs_world_width;
    float glyphs_screen_width;
    float pixel_screen_width;
    float pixel_screen_height;
    float fovy, fovx;
    uint32_t glyph_scale = 1;
    void update(float viewport_width, float viewport_height) {
      this->viewport_width = viewport_width;
      this->viewport_height = viewport_height;
      height_over_width = ((float)viewport_height / viewport_width);
      width_over_heigth = ((float)viewport_width / viewport_height);
      float e = (float)2.4e-7f;
      fovy = 2.0f;
      fovx = 2.0f * height_over_width;
      // clang-format off
      float4 proj[4] = {
        {fovx/pos.z,   0.0f,          0.0f,      -fovx * pos.x/pos.z},
        {0.0f,         fovy/pos.z,    0.0f,      -fovy * pos.y/pos.z},
        {0.0f,         0.0f,          1.0f,      0.0f},
        {0.0f,         0.0f,          0.0f,      1.0f}
      };
      // clang-format on
      memcpy(&this->proj[0], &proj[0], sizeof(proj));
      float2 mwp = screen_to_world((float2){mouse_screen_x, mouse_screen_y});
      mouse_world_x = mwp.x;
      mouse_world_y = mwp.y;
      world_min_x = screen_to_world((float2){-1.0f, -1.0f}).x;
      world_min_y = screen_to_world((float2){-1.0f, -1.0f}).y;
      world_max_x = screen_to_world((float2){1.0f, 1.0f}).x;
      world_max_y = screen_to_world((float2){1.0f, 1.0f}).y;
      glyphs_world_height = glyph_scale *
                            (float)(simplefont_bitmap_glyphs_height) /
                            viewport_height * pos.z;
      glyphs_world_width = glyph_scale *
                           (float)(simplefont_bitmap_glyphs_width) /
                           viewport_width * pos.z;
      pixel_screen_width = 2.0f / viewport_width;
      pixel_screen_height = 2.0f / viewport_height;
      glyphs_screen_width = 2.0f * glyph_scale *
                            (float)(simplefont_bitmap_glyphs_width) /
                            viewport_width;
      glyphs_screen_height = 2.0f * glyph_scale *
                             (float)(simplefont_bitmap_glyphs_height) /
                             viewport_height;
    }
    float2 world_to_screen(float2 p) {
      float x0 = spv_dot_f4(proj[0], (float4){p.x, p.y, 0.0f, 1.0f});
      float x1 = spv_dot_f4(proj[1], (float4){p.x, p.y, 0.0f, 1.0f});
      float x2 = spv_dot_f4(proj[2], (float4){p.x, p.y, 0.0f, 1.0f});
      float x3 = spv_dot_f4(proj[3], (float4){p.x, p.y, 0.0f, 1.0f});
      float2 pos = (float2){x0, x1} / x3;
      return pos;
    }
    float2 screen_to_world(float2 p) {
      return (float2){
          //
          p.x * pos.z / fovx + pos.x,
          p.y * pos.z / fovy + pos.y,
      };
    }
    float2 window_to_world(int2 p) {
      return screen_to_world((float2){
          //
          2.0f * (float)p.x / viewport_width - 1.0f,
          -2.0f * (float)p.y / viewport_height + 1.0f,
      });
    }
    float2 window_to_screen(int2 p) {
      return (float2){
          //
          2.0f * (float)p.x / viewport_width - 1.0f,
          -2.0f * (float)p.y / viewport_height + 1.0f,
      };
    }
    float window_width_to_world(uint32_t size) {
      float screen_size = 2.0f * (float)size / viewport_width;
      return screen_to_world((float2){//
                                      screen_size, 0.0f})
                 .x -
             pos.x;
    }
    float window_height_to_world(uint32_t size) {
      float screen_size = 2.0f * (float)size / viewport_height;
      return screen_to_world((float2){//
                                      0.0f, screen_size})
                 .y -
             pos.y;
    }
    bool inbounds(float2 world_pos) {
      return //
          world_pos.x > world_min_x && world_pos.x < world_max_x &&
          world_pos.y > world_min_y && world_pos.y < world_max_y;
    }
    bool intersects(float min_x, float min_y, float max_x, float max_y) {
      return //
          !(max_x < world_min_x || min_x > world_max_x || max_y < world_min_y ||
            min_y > world_max_y);
    }
  } camera;
  struct Color {
    float r, g, b;
  };
  struct Rect2D {
    float x, y, z, width, height;
    Color color;
    bool world_space = true;
  };
  struct Line2D {
    float x0, y0, x1, y1, z;
    Color color;
    bool world_space = true;
  };
  struct String2D {
    char const *c_str;
    float x, y, z;
    Color color;
    bool world_space = true;
  };
  struct _String2D {
    char *c_str;
    uint32_t len;
    float x, y, z;
    Color color;
    bool world_space;
  };
  void draw_rect(Rect2D p) { quad_storage.push(p); }
  void draw_line(Line2D l) { line_storage.push(l); }
  void draw_string(String2D s) {
    size_t len = strlen(s.c_str);
    if (len == 0)
      return;
    char *dst = char_storage.alloc(len + 1);
    memcpy(dst, s.c_str, len);
    dst[len] = '\0';
    _String2D internal_string;
    internal_string.color = s.color;
    internal_string.c_str = dst;
    internal_string.len = (uint32_t)len;
    internal_string.x = s.x;
    internal_string.y = s.y;
    internal_string.z = s.z;
    internal_string.world_space = s.world_space;

    string_storage.push(internal_string);
  }
  void frame_start(float viewport_width, float viewport_height) {
    this->viewport_height = viewport_height;
    this->viewport_width = viewport_width;
    camera.update(viewport_width, viewport_height);
    line_storage.enter_scope();
    quad_storage.enter_scope();
    string_storage.enter_scope();
    char_storage.enter_scope();
  }
  void frame_end() {
    render_stuff();
    line_storage.exit_scope();
    quad_storage.exit_scope();
    string_storage.exit_scope();
    char_storage.exit_scope();
  }
  void render_stuff();
  // Fields
  Temporary_Storage<Line2D> line_storage =
      Temporary_Storage<Line2D>::create(1 << 17);
  Temporary_Storage<Rect2D> quad_storage =
      Temporary_Storage<Rect2D>::create(1 << 17);
  Temporary_Storage<_String2D> string_storage =
      Temporary_Storage<_String2D>::create(1 << 18);
  Temporary_Storage<char> char_storage =
      Temporary_Storage<char>::create(1 * (1 << 20));
  uint32_t viewport_width;
  uint32_t viewport_height;
  bool force_update = false;
  struct Console {
    char buffer[0x100][0x100];
    uint32_t column = 0;
    uint32_t scroll_id = 0;
    Console() { memset(this, 0, sizeof(*this)); }
    void unscroll() {
      if (scroll_id != 0) {
        memcpy(&buffer[0][0], &buffer[scroll_id][0], 0x100);
        scroll_id = 0;
      }
    }
    void backspace() {
      unscroll();
      if (column > 0) {
        ito(0x100 - column) {
          buffer[0][column + i - 1] = buffer[0][column + i];
        }
        column--;
      }
    }
    void newline() {
      unscroll();
      ito(0x100 - 1) {
        memcpy(&buffer[0x100 - 1 - i][0], &buffer[0x100 - 2 - i][0], 0x100);
      }
      memset(&buffer[0][0], 0, 0x100);
      column = 0;
    }
    void cursor_right() {
      unscroll();
      if (buffer[0][column] != '\0')
        column++;
    }
    void cursor_left() {
      unscroll();
      if (column > 0)
        column--;
    }
    void put_line(char const *str) {
      unscroll();
      while (str[0] != '\0') {
        put_char(str[0]);
        str++;
      }
      newline();
    }
    void put_char(char c) {
      unscroll();
      if (c >= 0x20 && c <= 0x7e && column < 0x100 - 1) {
        ito(0x100 - column - 1) {
          buffer[0][0x100 - i - 1] = buffer[0][0x100 - i - 2];
        }
        buffer[0][column++] = c;
      }
    }
    void scroll_up() {
      if (scroll_id < 0x100) {
        scroll_id++;
        column = strlen(buffer[scroll_id]);
      }
    }
    void scroll_down() {
      if (scroll_id > 0) {
        scroll_id--;
        column = strlen(buffer[scroll_id]);
      }
    }
  } console;
  bool console_mode = false;
  void draw_console() {
    float CONSOLE_TEXT_LAYER = 102.0f / 256.0f;
    float CONSOLE_CURSOR_LAYER = 101.0f / 256.0f;
    float CONSOLE_BACKGROUND_LAYER = 100.0f / 256.0f;
    float GLYPH_HEIGHT = camera.glyph_scale * simplefont_bitmap_glyphs_height;
    float GLYPH_WIDTH = camera.glyph_scale * simplefont_bitmap_glyphs_width;
    uint32_t console_lines = 6;
    float console_bottom = (GLYPH_HEIGHT + 1) * console_lines;
    ito(console_lines - 1) {
      draw_string({.c_str = console.buffer[console_lines - 1 - i],
                   .x = 0,
                   .y = (GLYPH_HEIGHT + 1) * (i + 1),
                   .z = CONSOLE_TEXT_LAYER,
                   .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f},
                   .world_space = false});
    }
    draw_string({.c_str = console.buffer[console.scroll_id],
                 .x = 0.0f,
                 .y = console_bottom,
                 .z = CONSOLE_TEXT_LAYER,
                 .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f},
                 .world_space = false});
    draw_rect({//
               .x = 0.0f,
               .y = 0.0f,
               .z = CONSOLE_BACKGROUND_LAYER,
               .width = (float)camera.viewport_width,
               .height = console_bottom + 1.0f,
               .color = {.r = 0.8f, .g = 0.8f, .b = 0.8f},
               .world_space = false});
    draw_line({//
               .x0 = 0.0f,
               .y0 = console_bottom + 2.0f,
               .x1 = (float)camera.viewport_width,
               .y1 = console_bottom + 2.0f,
               .z = CONSOLE_CURSOR_LAYER,
               .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f},
               .world_space = false});
    draw_rect({//
               .x = console.column * (GLYPH_WIDTH + 1.0f),
               .y = console_bottom,
               .z = CONSOLE_CURSOR_LAYER,
               .width = GLYPH_WIDTH,
               .height = -GLYPH_HEIGHT,
               .color = {.r = 1.0f, .g = 1.0f, .b = 1.0f},
               .world_space = false});
  }
} c2d;
struct Dbg_Command {
  enum class Command_t {
    GOTO,
    LOG_ALL,
    EXIT,
    NEXT,
    CONTINUE,
    START,
    BREAK,
    HARD_BREAK,
    CLEAR,
    SELECT_VERTEX,
    MOVE_VERTEX,
    MOVE_CAMERA,
    SET_MODE,
    DUMP,
    UNKNOWN
  };
  Command_t type;
  int32_t iarg0;
  int32_t iarg1;
  int32_t iarg2;
  int32_t iarg3;
  float farg0;
  float farg1;
  float farg2;
  float farg3;
  char c[0x20];
  bool cmp(char const *str) { return token_match(c, str, strlen(c)); }
  void copy_tkn(char const *tkn, size_t tkn_len) {
    memcpy(c, tkn, MIN(sizeof(c), tkn_len));
    c[MIN(sizeof(c) - 1, tkn_len)] = '\0';
  }
  bool parse_decimal_int(char const *str, size_t len, int32_t *result) {
    int32_t final = 0;
    int32_t pow = 1;
    int32_t sign = 1;
    uint32_t i = 0;
    if (str[0] == '-') {
      sign = -1;
      i = 1;
    }
    for (; i < len; ++i) {
      switch (str[len - 1 - i]) {
      case '0':
        break;
      case '1':
        final += 1 * pow;
        break;
      case '2':
        final += 2 * pow;
        break;
      case '3':
        final += 3 * pow;
        break;
      case '4':
        final += 4 * pow;
        break;
      case '5':
        final += 5 * pow;
        break;
      case '6':
        final += 6 * pow;
        break;
      case '7':
        final += 7 * pow;
        break;
      case '8':
        final += 8 * pow;
        break;
      case '9':
        final += 9 * pow;
        break;
      default:
        return false;
      }
      pow *= 10;
    }
    *result = sign * final;
    return true;
  }
  bool parse_float(char const *str, size_t len, float *result) {
    float final = 0.0f;
    uint32_t i = 0;
    float sign = 1.0f;
    if (str[0] == '-') {
      sign = -1.0f;
      i = 1;
    }
    for (; i < len; ++i) {
      if (str[i] == '.')
        break;
      switch (str[i]) {
      case '0':
        final = final * 10.0f;
        break;
      case '1':
        final = final * 10.0f + 1.0f;
        break;
      case '2':
        final = final * 10.0f + 2.0f;
        break;
      case '3':
        final = final * 10.0f + 3.0f;
        break;
      case '4':
        final = final * 10.0f + 4.0f;
        break;
      case '5':
        final = final * 10.0f + 5.0f;
        break;
      case '6':
        final = final * 10.0f + 6.0f;
        break;
      case '7':
        final = final * 10.0f + 7.0f;
        break;
      case '8':
        final = final * 10.0f + 8.0f;
        break;
      case '9':
        final = final * 10.0f + 9.0f;
        break;
      default:
        return false;
      }
    }
    i++;
    float pow = 1.0e-1f;
    for (; i < len; ++i) {
      switch (str[i]) {
      case '0':
        break;
      case '1':
        final += 1.0f * pow;
        break;
      case '2':
        final += 2.0f * pow;
        break;
      case '3':
        final += 3.0f * pow;
        break;
      case '4':
        final += 4.0f * pow;
        break;
      case '5':
        final += 5.0f * pow;
        break;
      case '6':
        final += 6.0f * pow;
        break;
      case '7':
        final += 7.0f * pow;
        break;
      case '8':
        final += 8.0f * pow;
        break;
      case '9':
        final += 9.0f * pow;
        break;
      default:
        return false;
      }
      pow *= 1.0e-1f;
    }
    *result = sign * final;
    return true;
  }
  bool token_match(char const *tkn, char const *str, size_t tkn_len) {
    size_t str_len = strlen(str);
    if (str_len != tkn_len)
      return false;
    ito(MIN(str_len, tkn_len)) {
      if (tkn[i] != str[i])
        return false;
    }
    return true;
  }
  bool parse(char const *str) {
    type = Command_t::UNKNOWN;
    ts.enter_scope();
    defer(ts.exit_scope());
    char const *cur_tkn_start = str;
    struct Token {
      char const *start;
      size_t len;
    };
    const uint32_t max_tokens = 0x100;
    uint32_t num_tokens = 0;
    Token *tkns = (Token *)ts.alloc(sizeof(Token) * max_tokens);
    uint32_t loop_counter = 0;
    do {
      loop_counter++;
      if (loop_counter > 10000) {
        UNIMPLEMENTED;
      }
      if (str[0] == ' ' || str[0] < 0x20 || str[0] > 0x7e) {
        if (cur_tkn_start != str) {
          tkns[num_tokens++] = {
              .start = cur_tkn_start,
              .len = (size_t)((intptr_t)str - (intptr_t)cur_tkn_start)};
        }
        if (str[0] == '\0')
          break;
        cur_tkn_start = str + 1;
        str++;
        continue;
      }
      str++;
    } while (1);
    if (num_tokens != 0) {
#define CMD_NOARG(cmd_mnemonics, cmd_enum)                                     \
  if (token_match(tkns[0].start, STRINGIFY(cmd_mnemonics), tkns[0].len) &&     \
      num_tokens == 1) {                                                       \
    type = Command_t::cmd_enum;                                                \
    return true;                                                               \
  }
      if (token_match(tkns[0].start, "goto", tkns[0].len)) {
        if (num_tokens != 3)
          return false;
        if (!parse_decimal_int(tkns[1].start, tkns[1].len, &iarg0))
          return false;
        if (!parse_decimal_int(tkns[2].start, tkns[2].len, &iarg1))
          return false;
        type = Command_t::GOTO;
        return true;
      }
      if (token_match(tkns[0].start, "selv", tkns[0].len)) {
        if (num_tokens != 2)
          return false;
        if (!parse_decimal_int(tkns[1].start, tkns[1].len, &iarg0))
          return false;
        type = Command_t::SELECT_VERTEX;
        return true;
      }
      if (token_match(tkns[0].start, "mov", tkns[0].len)) {
        if (num_tokens != 3)
          return false;
        if (!parse_float(tkns[1].start, tkns[1].len, &farg0))
          return false;
        if (!parse_float(tkns[2].start, tkns[2].len, &farg1))
          return false;
        type = Command_t::MOVE_VERTEX;
        return true;
      }
      if (token_match(tkns[0].start, "movc", tkns[0].len)) {
        if (num_tokens != 4)
          return false;
        if (!parse_float(tkns[1].start, tkns[1].len, &farg0))
          return false;
        if (!parse_float(tkns[2].start, tkns[2].len, &farg1))
          return false;
        if (!parse_float(tkns[3].start, tkns[3].len, &farg2))
          return false;
        type = Command_t::MOVE_CAMERA;
        return true;
      }
      if (token_match(tkns[0].start, "setm", tkns[0].len)) {
        if (num_tokens != 2)
          return false;
        copy_tkn(tkns[1].start, tkns[1].len);
        type = Command_t::SET_MODE;
        return true;
      }
      if (token_match(tkns[0].start, "clear", tkns[0].len)) {
        if (num_tokens != 2)
          return false;
        copy_tkn(tkns[1].start, tkns[1].len);
        type = Command_t::CLEAR;
        return true;
      }
      CMD_NOARG(exit, EXIT);
      CMD_NOARG(log_all, LOG_ALL);
      CMD_NOARG(dump, DUMP);
      CMD_NOARG(n, NEXT);
      CMD_NOARG(c, CONTINUE);
      CMD_NOARG(b, BREAK);
      CMD_NOARG(hb, HARD_BREAK);
      CMD_NOARG(s, START);

#undef CMD_NOARG
    }

    return false;
  }
};
struct CV_Wrapper {
  std::mutex cv_mutex;
  std::atomic<bool> cv_predicate;
  std::condition_variable cv;
  void wait() {
    std::unique_lock<std::mutex> lk(cv_mutex);
    cv.wait(lk, [this] { return cv_predicate.load(); });
    cv_predicate = false;
  }
  void notify_one() {
    cv_predicate = true;
    cv.notify_one();
  }
};
enum class Selection_t { NONE = 0, CELL, VERTEX };
enum class Impl_Mode_t { NONE = 0, REF, OPT };
template <uint32_t H, uint32_t W, typename Cell_t> //
struct Gridbg {
  Cell_t *grid;
  std::mutex grid_mutex;
  CV_Wrapper break_cv;
  CV_Wrapper pause_cv;
  std::atomic<bool> run;
  std::atomic<bool> log_all;
  std::atomic<size_t> current_grid;
  std::atomic<bool> break_requested;
  std::atomic<bool> hard_break_requested;
  uint32_t cur_i;
  uint32_t cur_j;

  // Debug triangle
  float v_x[3];
  float v_y[3];
  uint32_t cur_v;
  Selection_t selection_type;
  Impl_Mode_t impl_mode;

  bool select_clicked = false;
  void save() {
    FILE *save = fopen("save.txt", "wb");
    ito(3) {
      fprintf(save, "selv %i\n", i);
      fprintf(save, "mov %f %f\n", v_x[i], v_y[i]);
    }
    if (log_all) {
      fprintf(save, "log_all\n");
    }
    if (selection_type == Selection_t::CELL)
      fprintf(save, "goto %i %i\n", cur_i, cur_j);
    fprintf(save, "movc %f %f %f\n", c2d.camera.pos.x, c2d.camera.pos.y,
            c2d.camera.pos.z);
    if (impl_mode == Impl_Mode_t::REF) {
      fprintf(save, "setm ref\n");
    } else if (impl_mode == Impl_Mode_t::OPT) {
      fprintf(save, "setm opt\n");
    }
    fprintf(save, "s\n");
    fflush(save);
    fclose(save);
  }
  void init() {
    grid = (Cell_t *)malloc(W * H * sizeof(Cell_t));
    memset(grid, 0, W * H * sizeof(Cell_t));
    clear_all();
    {
      ts.enter_scope();
      defer(ts.exit_scope());
      FILE *save = fopen("save.txt", "rb");
      if (save != NULL) {
        fseek(save, 0, SEEK_END);
        long fsize = ftell(save);
        fseek(save, 0, SEEK_SET);
        size_t size = (size_t)fsize;
        char *data = (char *)ts.alloc((size_t)fsize);
        fread(data, 1, (size_t)fsize, save);
        fclose(save);
        ito((uint32_t)size) {
          if (data[i] == '\n') {
            try_command(c2d.console.buffer[0]);
            c2d.console.newline();
          } else {
            c2d.console.put_char(data[i]);
          }
        }
      } else {
        v_x[0] = 0.0f;
        v_y[0] = 0.0f;
        v_x[1] = 100.0f;
        v_y[1] = 100.0f;
        v_x[2] = 000.0f;
        v_y[2] = 100.0f;
      }
    }
  }
  void release() { free(grid); }
  void draw() {
    std::lock_guard<std::mutex> lock(grid_mutex);
    ts.enter_scope();
    c2d.frame_start(SCREEN_WIDTH, SCREEN_HEIGHT);
    defer({
      c2d.frame_end();
      ts.exit_scope();
    });
    if (select_clicked && selection_type == Selection_t::VERTEX) {
      selection_type = Selection_t::CELL;
      select_clicked = false;
    }
    float dx = 1.0f;
    float dy = 1.0f;
    float size_x = 256.0f;
    float size_y = 256.0f;
    float QUAD_LAYER = 1.0f / 256.0f;
    float GRID_LAYER = 2.0f / 256.0f;
    float TEXT_LAYER = 3.0f / 256.0f;
    if (c2d.camera.pos.z < 80.0f) {
      ito(W + 1) {
        c2d.draw_line({//
                       .x0 = dx * (float)(i),
                       .y0 = 0.0f,
                       .x1 = dx * (float)(i),
                       .y1 = size_y,
                       .z = GRID_LAYER,
                       .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      }
      ito(H + 1) {
        c2d.draw_line({//
                       .x0 = 0.0f,
                       .y0 = dy * (float)(i),
                       .x1 = size_x,
                       .y1 = dy * (float)(i),
                       .z = GRID_LAYER,
                       .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      }
    }
    if (selection_type == Selection_t::VERTEX) {
      v_x[cur_v] = c2d.camera.mouse_world_x;
      v_y[cur_v] = c2d.camera.mouse_world_y;
    }
    // Draw the debugged triangle
    {
      ito(3) c2d.draw_string({.c_str = &("v0\0v1\0v2\n"[i * 3]),
                              .x = v_x[i],
                              .y = v_y[i],
                              .z = TEXT_LAYER,
                              .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      c2d.draw_line({//
                     .x0 = v_x[0],
                     .y0 = v_y[0],
                     .x1 = v_x[1],
                     .y1 = v_y[1],
                     .z = TEXT_LAYER,
                     .color = {.r = 1.0f, .g = 0.0f, .b = 0.0f}});
      c2d.draw_line({//
                     .x0 = v_x[0],
                     .y0 = v_y[0],
                     .x1 = v_x[2],
                     .y1 = v_y[2],
                     .z = TEXT_LAYER,
                     .color = {.r = 1.0f, .g = 0.0f, .b = 0.0f}});
      c2d.draw_line({//
                     .x0 = v_x[2],
                     .y0 = v_y[2],
                     .x1 = v_x[1],
                     .y1 = v_y[1],
                     .z = TEXT_LAYER,
                     .color = {.r = 1.0f, .g = 0.0f, .b = 0.0f}});
    }
    auto draw_frame = [&](uint32_t i, uint32_t j, float r, float g, float b) {
      float thickness = 0.01f * c2d.camera.pos.z;
      c2d.draw_rect({//
                     .x = dx * j - thickness,
                     .y = dy * i - thickness,
                     .z = TEXT_LAYER,
                     .width = thickness,
                     .height = 1.0f + 2.0f * thickness,
                     .color = {.r = r, .g = g, .b = b}});
      c2d.draw_rect({//
                     .x = dx * (j + 1.0f),
                     .y = dy * i - thickness,
                     .z = TEXT_LAYER,
                     .width = thickness,
                     .height = 1.0f + 2.0f * thickness,
                     .color = {.r = r, .g = g, .b = b}});
      c2d.draw_rect({//
                     .x = dx * j - thickness,
                     .y = dy * (i + 1),
                     .z = TEXT_LAYER,
                     .width = 1.0f + 2.0f * thickness,
                     .height = thickness,
                     .color = {.r = r, .g = g, .b = b}});
      c2d.draw_rect({//
                     .x = dx * j - thickness,
                     .y = dy * i - thickness,
                     .z = TEXT_LAYER,
                     .width = 1.0f + 2.0f * thickness,
                     .height = thickness,
                     .color = {.r = r, .g = g, .b = b}});
    };
    if (selection_type == Selection_t::CELL) {
      uint32_t i = cur_i;
      uint32_t j = cur_j;
      draw_frame(i, j, 0.0f, 0.0f, 0.0f);
    }

    ito(H) {
      jto(W) {
        if (!c2d.camera.intersects(dx * j, dy * i, dx * (j + 1), dy * (i + 1)))
          continue;
        Cell_t *cell = get_cell(i, j);
        float r = cell->r;
        float g = cell->g;
        float b = cell->b;
        if (c2d.camera.mouse_world_x > dx * j &&
            c2d.camera.mouse_world_x < dx * (j + 1) &&
            c2d.camera.mouse_world_y > dy * i &&
            c2d.camera.mouse_world_y < dy * (i + 1)) {
          if (select_clicked && selection_type == Selection_t::CELL) {
            cur_i = i;
            cur_j = j;
            select_clicked = false;
          }
          draw_frame(i, j, 1.0f, 0.0f, 0.0f);
        }
        if (c2d.camera.pos.z < 160.0f) {
          c2d.draw_rect({//
                         .x = dx * j,
                         .y = dy * i,
                         .z = QUAD_LAYER,
                         .width = 1.0f,
                         .height = 1.0f,
                         .color = {.r = r, .g = g, .b = b}});
        }
        if (c2d.camera.pos.z < 60.0f) {
          float point_size = 1.0e-2f;
          c2d.draw_rect({//
                         .x = dx * (j + 0.5f) - point_size,
                         .y = dy * (i + 0.5f) - point_size,
                         .z = TEXT_LAYER,
                         .width = point_size,
                         .height = point_size,
                         .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
        }
      }
    }
    if (c2d.camera.pos.z > 160.0f) {
      c2d.draw_line({//
                     .x0 = 0.0f,
                     .y0 = 0.0f,
                     .x1 = size_x,
                     .y1 = 0.0f,
                     .z = QUAD_LAYER,
                     .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      c2d.draw_line({//
                     .x0 = 0.0f,
                     .y0 = 0.0f,
                     .x1 = 0.0f,
                     .y1 = size_y,
                     .z = QUAD_LAYER,
                     .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      c2d.draw_line({//
                     .x0 = size_x,
                     .y0 = size_y,
                     .x1 = 0.0f,
                     .y1 = size_y,
                     .z = QUAD_LAYER,
                     .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      c2d.draw_line({//
                     .x0 = size_x,
                     .y0 = size_y,
                     .x1 = size_x,
                     .y1 = 0.0f,
                     .z = QUAD_LAYER,
                     .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
    }
    c2d.draw_console();
    if (c2d.camera.pos.z < 10.0f) {
      char tmp_buf[0x100];
      auto alloc_str = [&](char const *fmt, int16_t v, float x, float y) {
        snprintf(tmp_buf, sizeof(tmp_buf), fmt, v);
        c2d.draw_string({.c_str = tmp_buf,
                         .x = x,
                         .y = y,
                         .z = TEXT_LAYER,
                         .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});
      };

      ito(H) {
        jto(W) {
          if (!c2d.camera.intersects(dx * j, dy * i, dx * (j + 1),
                                     dy * (i + 1)))
            continue;
          snprintf(tmp_buf, sizeof(tmp_buf), "(%i, %i)", j, i);
          c2d.draw_string({.c_str = tmp_buf,
                           .x = dx * j,
                           .y = dy * (i + 1) - c2d.camera.glyphs_world_height,
                           .z = TEXT_LAYER,
                           .color = {.r = 0.0f, .g = 0.0f, .b = 0.0f}});

          //          alloc_str("y = %i", (int16_t)i, dx * j,
          //                    dy * i + c2d.camera.glyphs_world_height);
          Cell_t *cell = get_cell(i, j);
          kto(Cell_t::LOG_SIZE) {
            if (cell->log[k] != NULL) {
              alloc_str(cell->log[k], 0, dx * j,
                        dy * i + k * (c2d.camera.glyphs_world_height +
                                      c2d.camera.pixel_screen_height));
            }
          }
          //        alloc_str("e0 = %i", grid[i * width * 3 + j * 3 + 0], dx *
          //        j
          //        + 0.1f,
          //                  dy * i + dy * 0.25f);
          //        alloc_str("e1 = %i", grid[i * width * 3 + j * 3 + 1], dx *
          //        j
          //        + 0.1f,
          //                  dy * i + dy * 0.5f);
          //        alloc_str("e2 = %i", grid[i * width * 3 + j * 3 + 2], dx *
          //        j
          //        + 0.1f,
          //                  dy * i + dy * 0.75f);
        }
      }
    }
  }
  void try_command(char const *str) {
    Dbg_Command cmd;
    if (cmd.parse(str)) {
      switch (cmd.type) {
      case Dbg_Command::Command_t::GOTO: {
        int32_t x = cmd.iarg0;
        int32_t y = cmd.iarg1;
        cur_i = y;
        cur_j = x;
        selection_type = Selection_t::CELL;
        if (x >= 0 && x < 256 && y >= 0 && y < 256) {
          c2d.camera.pos.x = (float)x;
          c2d.camera.pos.y = (float)y;
        }
        break;
      }
      case Dbg_Command::Command_t::NEXT: {
        next();
        break;
      }
      case Dbg_Command::Command_t::EXIT: {
        run = false;
        save();
        global_resume();
      }
      case Dbg_Command::Command_t::BREAK: {
        set_break();
        break;
      }
      case Dbg_Command::Command_t::SET_MODE: {
        if (cmd.cmp("ref")) {
          impl_mode = Impl_Mode_t::REF;
        } else if (cmd.cmp("opt")) {
          impl_mode = Impl_Mode_t::OPT;
        } else {
          c2d.console.put_line("unknown mode");
        }

        break;
      }
      case Dbg_Command::Command_t::LOG_ALL: {
        log_all = !log_all;
        if (log_all)
          c2d.console.put_line("log all enabled");
        else
          c2d.console.put_line("log all disabled");
        break;
      }
      case Dbg_Command::Command_t::HARD_BREAK: {
        hard_break_requested = true;
        break;
      }
      case Dbg_Command::Command_t::CONTINUE: {
        hard_break_requested = false;
        set_continue();
        break;
      }
      case Dbg_Command::Command_t::CLEAR: {
        if (cmd.cmp("all")) {
          clear_all();
        } else if (cmd.cmp("log")) {
          clear_logs();
        } else {
          c2d.console.put_line("unknown mode");
        }

        break;
      }
      case Dbg_Command::Command_t::SELECT_VERTEX: {
        selection_type = Selection_t::VERTEX;
        cur_v = cmd.iarg0;
        break;
      }
      case Dbg_Command::Command_t::START: {
        run = true;
        global_resume();
        break;
      }
      case Dbg_Command::Command_t::DUMP: {
        save();
        break;
      }
      case Dbg_Command::Command_t::MOVE_VERTEX: {
        if (selection_type == Selection_t::VERTEX) {
          v_x[cur_v] = cmd.farg0;
          v_y[cur_v] = cmd.farg1;
          selection_type = Selection_t::NONE;
        } else {
          c2d.console.put_line("[ERROR] No vertex is selected!");
        }
        break;
      }
      case Dbg_Command::Command_t::MOVE_CAMERA: {
        c2d.camera.pos.x = cmd.farg0;
        c2d.camera.pos.y = cmd.farg1;
        c2d.camera.pos.z = cmd.farg2;
        break;
      }
      default:
        UNIMPLEMENTED;
      }
    } else {
      c2d.console.put_line("[ERROR] Unknown command!");
    }
  }
  void clear_all() {
    ito(H) {
      jto(W) { get_cell(i, j)->clear_all(); }
    }
  }
  void clear_logs() {
    ito(H) {
      jto(W) { get_cell(i, j)->clear_logs(); }
    }
  }
  void next() { break_cv.notify_one(); }
  void set_continue() {
    break_requested = false;
    next();
  }
  void set_break() { break_requested = true; }
  Cell_t *get_cell(uint32_t i, uint32_t j) { return &grid[i * W + j]; }
  void global_resume() { pause_cv.notify_one(); }

  // client functions
  void put_line(uint32_t i, uint32_t j, char const *fmt, ...) {
    if (log_all || i == cur_i && j == cur_j) {
      std::lock_guard<std::mutex> lock(grid_mutex);
      char buffer[0x100];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buffer, sizeof(buffer), fmt, args);
      va_end(args);
      get_cell(i, j)->put_line(buffer);
    }
  }
  void set_color(uint32_t i, uint32_t j, float r, float g, float b) {
    // if (i == cur_i && j == cur_j) {
    std::lock_guard<std::mutex> lock(grid_mutex);
    get_cell(i, j)->set_color(r, g, b);
    //}
  }
  void on_start(uint32_t i, uint32_t j) {}
  bool on_pause() {
    pause_cv.wait();
    return run;
  }
  void on_end(uint32_t i, uint32_t j) {}
  void on_break(uint32_t i, uint32_t j) {
    if (break_requested && i == cur_i && j == cur_j) {
      break_cv.wait();
    }
  }
};
struct Raster_Cell {
  static const size_t LOG_SIZE = 0x30;
  // Need to use long lived allocations
  char *log[LOG_SIZE];
  float r, g, b;
  void release() {
    clear_logs();
    memset(this, 0, sizeof(*this));
  }
  void put_line(char const *str) {
    // shift the log up
    if (log[LOG_SIZE - 1] != NULL)
      free(log[LOG_SIZE - 1]);
    ito(LOG_SIZE - 1) { log[LOG_SIZE - i - 1] = log[LOG_SIZE - i - 2]; }
    size_t len = strlen(str);
    char *new_str = (char *)malloc(len + 1);
    memcpy(new_str, str, len);
    new_str[len] = '\0';
    log[0] = new_str;
  }
  void set_color(float r, float g, float b) {
    this->r = r;
    this->g = g;
    this->b = b;
  }
  void clear_all() {
    release();
    r = 0.4f;
    g = 0.4f;
    b = 0.4f;
  }
  void clear_logs() {

    ito(LOG_SIZE) {
      if (log[i] != NULL)
        free((void *)log[i]);
      log[i] = NULL;
    }
  }
};
Gridbg<256, 256, Raster_Cell> gridbg;
#define debug_break __asm__("int3")
#define GRIDBG_START(i, j)                                                     \
  {                                                                            \
    if (gridbg.hard_break_requested) {                                         \
      debug_break;                                                             \
    }                                                                          \
    gridbg.on_start(i, j);                                                     \
  }
#define GRIDBG_PAUSE() gridbg.on_pause()
#define GRIDBG_BREAK(i, j) gridbg.on_break(i, j)
#define GRIDBG_END(i, j) gridbg.on_end(i, j)
#define GRIDBG_PUTLINE(i, j, fmt, ...) gridbg.put_line(i, j, fmt, __VA_ARGS__)
#define GRIDBG_SETCOLOR(i, j, r, g, b) gridbg.set_color(i, j, r, g, b)
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
  // _mm256_permute4x64_epi64 is used to make different 128 bit lanes
  // accessible for the shuffle instruction. 0x4E == 01'00'11'10 == [1, 0, 3,
  // 2] effectively swaps the low and hight 128 bit lanes. Finally we just or
  // those passes together to get combined local+far moves
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

void rasterize_triangle_tiled_1x1_256x256_defer_ref(
    float _x0, float _y0, float _x1, float _y1, float _x2, float _y2,
    Classified_Tile *tile_buffer, uint32_t *tile_count) {
  float k = 256.0f;
  float x0 = vki::clamp(_x0 * k, 0.0f, 256.0f);
  float y0 = vki::clamp(_y0 * k, 0.0f, 256.0f);
  float x1 = vki::clamp(_x1 * k, 0.0f, 256.0f);
  float y1 = vki::clamp(_y1 * k, 0.0f, 256.0f);
  float x2 = vki::clamp(_x2 * k, 0.0f, 256.0f);
  float y2 = vki::clamp(_y2 * k, 0.0f, 256.0f);

  float n0_x = -(y1 - y0);
  float n0_y = (x1 - x0);
  float n1_x = -(y2 - y1);
  float n1_y = (x2 - x1);
  float n2_x = -(y0 - y2);
  float n2_y = (x0 - x2);

  float min_x = MIN(x0, MIN(x1, x2));
  float max_x = MAX(x0, MAX(x1, x2));
  float min_y = MIN(y0, MIN(y1, y2));
  float max_y = MAX(y0, MAX(y1, y2));

  int16_t imin_x = (int16_t)(min_x + 0.5f);
  int16_t imax_x = (int16_t)(max_x + 0.5f);
  int16_t imin_y = (int16_t)(min_y + 0.5f);
  int16_t imax_y = (int16_t)(max_y + 0.5f);

  imin_x &= (~0b11);
  imin_y &= (~0b11);
  imax_x = (imax_x + 0b11) & (~0b11);
  imax_y = (imax_y + 0b11) & (~0b11);

  min_x = (float)imin_x + 0.5f;
  max_x = (float)imax_x + 0.5f;
  min_y = (float)imin_y + 0.5f;
  max_y = (float)imax_y + 0.5f;

  float e0_0 = n0_x * (min_x - x0) + n0_y * (min_y - y0);
  float e1_0 = n1_x * (min_x - x1) + n1_y * (min_y - y1);
  float e2_0 = n2_x * (min_x - x2) + n2_y * (min_y - y2);

  uint8_t min_y_tile = 0xff & (imin_y >> 2);
  uint8_t max_y_tile = 0xff & (imax_y >> 2);
  uint8_t min_x_tile = 0xff & (imin_x >> 2);
  uint8_t max_x_tile = 0xff & (imax_x >> 2);
  // clang-format on
  uint32_t _tile_count = *tile_count;
  // ~15 cycles per tile
  for (uint8_t y = min_y_tile; y < max_y_tile; y += 1) {
    float e0_1 = e0_0;
    float e1_1 = e1_0;
    float e2_1 = e2_0;
    for (uint8_t x = min_x_tile; x < max_x_tile; x += 1) {
      uint16_t mask = 0;
      float e0_2 = e0_1;
      float e1_2 = e1_1;
      float e2_2 = e2_1;
      ito(4) {
        float e0_3 = e0_2;
        float e1_3 = e1_2;
        float e2_3 = e2_2;
        jto(4) {
          uint32_t idx = x * 4 + j;
          uint32_t idy = y * 4 + i;
          GRIDBG_START(idx, idy);
          uint16_t e0_sign = e0_3 < 0.0f ? 1 : 0;
          uint16_t e1_sign = e1_3 < 0.0f ? 1 : 0;
          uint16_t e2_sign = e2_3 < 0.0f ? 1 : 0;
          GRIDBG_PUTLINE(idx, idy, "e0=%f\n", e0_3);
          GRIDBG_PUTLINE(idx, idy, "e1=%f\n", e1_3);
          GRIDBG_PUTLINE(idx, idy, "e2=%f\n", e2_3);
          uint16_t mask_1 = (e0_sign | e1_sign | e2_sign);
          GRIDBG_SETCOLOR(idx, idy, mask_1 ? 0.0f : 0.8f, 0.8f, 0.6f);
          mask = mask | ((mask_1 & 1) << ((i << 2) | j));
          e0_3 += n0_x;
          e1_3 += n1_x;
          e2_3 += n2_x;
          GRIDBG_END(idx, idy);
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
}

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
  // ~15 cycles per tile
  for (uint8_t y = min_y_tile; y < max_y_tile; y += 1) {
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
          uint32_t idx = x * 4 + j;
          uint32_t idy = y * 4 + i;
          GRIDBG_START(idx, idy);
          uint16_t e0_sign = (uint16_t)e0_3 >> 15;
          uint16_t e1_sign = (uint16_t)e1_3 >> 15;
          uint16_t e2_sign = (uint16_t)e2_3 >> 15;
          GRIDBG_PUTLINE(idx, idy, "e0=%i\n", e0_3);
          GRIDBG_PUTLINE(idx, idy, "e1=%i\n", e1_3);
          GRIDBG_PUTLINE(idx, idy, "e2=%i\n", e2_3);
          uint16_t mask_1 = (e0_sign | e1_sign | e2_sign);
          GRIDBG_SETCOLOR(idx, idy, mask_1 ? 0.0f : 0.8f, 0.8f, 0.6f);
          mask = mask | ((mask_1 & 1) << ((i << 2) | j));
          e0_3 += n0_x;
          e1_3 += n1_x;
          e2_3 += n2_x;
          GRIDBG_END(idx, idy);
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
    if (gridbg.impl_mode == Impl_Mode_t::REF) {
      rasterize_triangle_tiled_1x1_256x256_defer_ref( //
          vertices[i + 2].x, vertices[i + 2].y,       //
          vertices[i + 1].x, vertices[i + 1].y,       //
          vertices[0].x, vertices[0].y,               //
          tile_buffer, tile_count);
    } else {
      rasterize_triangle_tiled_1x1_256x256_defer( //
          vertices[i + 2].x, vertices[i + 2].y,   //
          vertices[i + 1].x, vertices[i + 1].y,   //
          vertices[0].x, vertices[0].y,           //
          tile_buffer, tile_count);
    }
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
    //    ASSERT_ALWAYS(state->index_type ==
    //    VkIndexType::VK_INDEX_TYPE_UINT32);
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
    //    (float4 *)descriptor_set_0[0]; ito(4) fprintf(stdout, "%f %f %f
    //    %f\n", mat[i].x, mat[i].y, mat[i].z,
    //                   mat[i].w);
    //    fprintf(stdout, "__________________\n");
    //    mat += 4;
    //    ito(4) fprintf(stdout, "%f %f %f %f\n", mat[i].x, mat[i].y,
    //    mat[i].z,
    //                   mat[i].w);
    //    fprintf(stdout, "__________________\n");
    //    mat += 4;
    //    ito(4) fprintf(stdout, "%f %f %f %f\n", mat[i].x, mat[i].y,
    //    mat[i].z,
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

void render() {

  // Update delta time
  double last_time = my_clock();
  double cur_time = my_clock();
  double dt = cur_time - last_time;
  last_time = cur_time;

  // Scoped temporary allocator

  // submit the commands
  gridbg.draw();
  //  if (g_debug_grid[g_current_grid]) {
  //    std::lock_guard<std::mutex>
  //    lock(g_debug_grid_mutexes[g_current_grid]);
  //    draw_grid_i16(g_debug_grid[g_current_grid], 256, 256, 256.0f, 256.0f);
  //  }

  glFinish();
  SDL_GL_SwapWindow(window);
}

void main_loop() {
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(MessageCallback, 0);
  SDL_Event event;

  float camera_speed = 1.0f;
  static bool ldown = false;
  static int old_mp_x = 0;
  static int old_mp_y = 0;
  static bool hyper_pressed = false;
  static bool skip_key = false;
  SDL_StartTextInput();
  while (SDL_WaitEvent(&event)) {
    SDL_GetWindowSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
    switch (event.type) {
    case SDL_QUIT: {
      gridbg.try_command("exit");
      break;
    }
    case SDL_KEYUP: {
      //      fprintf(stdout, "up %i\n", event.key.keysym.scancode);
      //      fflush(stdout);
      if (event.key.keysym.scancode == 258)
        break;
      if (event.key.keysym.scancode == 57) {
        hyper_pressed = false;
        skip_key = false;
        break;
      }
      break;
    }
    case SDL_TEXTINPUT: {
      uint32_t c = event.text.text[0];
      //                printf("%s\n", event.text.text);
      if (c >= 0x20 && c <= 0x7e) {
        c2d.console.put_char((char)c);
      }
    } break;
    case SDL_KEYDOWN: {
      uint32_t c = event.key.keysym.sym;
      //            fprintf(stdout, "down %i\n", event.key.keysym.sym);
      //            fflush(stdout);
      if (event.key.keysym.scancode == 258)
        break;
      if (event.key.keysym.scancode == 57) {
        hyper_pressed = true;
        skip_key = true;
        break;
      }
      if (skip_key) {
        //        skip_key = false;
        break;
      }
      //      if (hyper_pressed)
      //        skip_key = true;

      if (event.key.keysym.sym == SDLK_BACKSPACE) {
        c2d.console.backspace();
      }
      if (event.key.keysym.sym == SDLK_UP) {
        c2d.console.scroll_up();
      }
      if (event.key.keysym.sym == SDLK_DOWN) {
        c2d.console.scroll_down();
      }
      if (event.key.keysym.sym == SDLK_RIGHT) {
        c2d.console.cursor_right();
      }
      if (event.key.keysym.sym == SDLK_LEFT) {
        c2d.console.cursor_left();
      }
      if (event.key.keysym.sym == SDLK_RETURN) {
        c2d.console.newline();
        gridbg.try_command(c2d.console.buffer[1]);
      }
      switch (event.key.keysym.sym) {

      case SDLK_ESCAPE: {
        gridbg.try_command("exit");
        break;
      }
      }
      break;
    }
    case SDL_MOUSEBUTTONDOWN: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      if (m->button == 3) {
        //        if (selected_texel_x != 0xffffffff && selected_texel_y !=
        //        0xffffffff) {
        //          selected_texel_break = 1;
        //        }
        gridbg.select_clicked = true;
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
      skip_key = false;
      hyper_pressed = false;
      ldown = false;
      break;
    }
    case SDL_MOUSEMOTION: {
      SDL_MouseMotionEvent *m = (SDL_MouseMotionEvent *)&event;

      int dx = m->x - old_mp_x;
      int dy = m->y - old_mp_y;
      if (ldown) {
        c2d.camera.pos.x -= c2d.camera.pos.z * (float)dx / SCREEN_HEIGHT;
        c2d.camera.pos.y += c2d.camera.pos.z * (float)dy / SCREEN_HEIGHT;
      }

      old_mp_x = m->x;
      old_mp_y = m->y;
      c2d.camera.mouse_screen_x = 2.0f * (float)m->x / SCREEN_WIDTH - 1.0f;
      c2d.camera.mouse_screen_y = -2.0f * (float)m->y / SCREEN_HEIGHT + 1.0f;
    } break;
    case SDL_MOUSEWHEEL: {
      float dz = c2d.camera.pos.z * (float)event.wheel.y * 1.0e-1;
      c2d.camera.pos.x +=
          -0.5f * dz *
          (c2d.camera.window_to_screen((int2){(int32_t)old_mp_x, 0}).x);
      c2d.camera.pos.y +=
          -0.5f * dz *
          (c2d.camera.window_to_screen((int2){0, (int32_t)old_mp_y}).y);
      c2d.camera.pos.z += dz;
      c2d.camera.pos.z = clamp(c2d.camera.pos.z, 0.1f, 256.0f);
    } break;
    }
    if (gridbg.run == false)
      break;
    render();
    SDL_UpdateWindowSurface(window);
  }
}
int main(int argc, char **argv) {
  uint64_t graphics_thread_finished = 0;
  gridbg.init();
  std::thread window_loop = std::thread([&] {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow(
        "2DBG", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 1024,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    // 3.2 is minimal requirement for renderdoc
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetSwapInterval(0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 23);
    glc = SDL_GL_CreateContext(window);
    ASSERT_ALWAYS(glc);

    main_loop();
    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(window);
    SDL_Quit();
    graphics_thread_finished = 1;
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

  // ~5k cycles
  Classified_Tile tiles[0x1000];
  uint32_t tile_count = 0;
  uint32_t i = 0;
  while (GRIDBG_PAUSE()) {
    if (graphics_thread_finished == 1)
      break;
    tile_count = 0;
    rasterize_triangle_tiled_4x4_256x256_defer_cull(
        // clang-format off
            gridbg.v_y[0] / 256.0f, gridbg.v_x[0] / 256.0f, // p0
            gridbg.v_y[1] / 256.0f, gridbg.v_x[1] / 256.0f, // p1
            gridbg.v_y[2] / 256.0f, gridbg.v_x[2] / 256.0f, // p2
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
void Context2D::render_stuff() {
  glViewport(0, 0, viewport_width, viewport_height);
  glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
  glClearDepthf(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_GEQUAL);
  glDisable(GL_CULL_FACE);
  glLineWidth(1.0f);
  {
    const GLchar *line_vs =
        R"(#version 420
  layout (location = 0) in vec4 vertex_position;
  layout (location = 1) in vec3 vertex_color;
  uniform mat4 projection;
  layout(location = 0) out vec3 color;
  void main() {
      color = vertex_color;
      if (vertex_position.w > 0.0)
        gl_Position = vertex_position * projection;
      else
        gl_Position = vec4(vertex_position.xyz, 1.0);
  })";
    const GLchar *line_ps =
        R"(#version 420
  layout(location = 0) out vec4 SV_TARGET0;
  layout(location = 0) in vec3 color;
  void main() {
    SV_TARGET0 = vec4(color, 1.0);
  })";
    const GLchar *quad_vs =
        R"(#version 420
  layout (location = 0) in vec2 vertex_position;
  layout (location = 1) in vec4 instance_offset;
  layout (location = 2) in vec3 instance_color;
  layout (location = 3) in vec2 instance_size;

  layout(location = 0) out vec3 color;
  uniform mat4 projection;
  void main() {
      color = instance_color;
      if (instance_offset.w > 0.0)
        gl_Position =  vec4(vertex_position * instance_size + instance_offset.xy, instance_offset.z, 1.0) * projection;
      else
        gl_Position =  vec4(vertex_position * instance_size + instance_offset.xy, instance_offset.z, 1.0);
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
    // Draw quads
    if (quad_storage.cursor != 0) {
      glUseProgram(quad_program);
      glUniformMatrix4fv(glGetUniformLocation(quad_program, "projection"), 1,
                         GL_FALSE, (float *)&camera.proj[0]);
      glBindVertexArray(quad_vao);
      glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
      glEnableVertexAttribArray(0);
      glVertexAttribBinding(0, 0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

      glBindBuffer(GL_ARRAY_BUFFER, quad_instance_vbo);
      struct Rect_Instance_GL {
        float x, y, z, w;
        float r, g, b;
        float width, height;
      };
      static_assert(sizeof(Rect_Instance_GL) == 36, "");
      //      uint32_t max_num_quads = width * height;
      uint32_t max_num_quads = quad_storage.cursor;
      uint32_t num_quads = 0;
      ts.enter_scope();
      defer(ts.exit_scope());
      Rect_Instance_GL *qinstances = (Rect_Instance_GL *)ts.alloc(
          sizeof(Rect_Instance_GL) * max_num_quads);
      ito(max_num_quads) {
        Rect2D quad2d = *quad_storage.at(i);
        if (quad2d.world_space &&
            !camera.intersects(quad2d.x, quad2d.y, quad2d.x + quad2d.width,
                               quad2d.y + quad2d.height))
          continue;
        Rect_Instance_GL quadgl;
        if (quad2d.world_space) {
          quadgl.x = quad2d.x;
          quadgl.y = quad2d.y;
          quadgl.z = quad2d.z;
          quadgl.w = 1.0f;
          quadgl.width = quad2d.width;
          quadgl.height = quad2d.height;
        } else {
          quadgl.x = 2.0f * quad2d.x / viewport_width - 1.0f;
          quadgl.y = -2.0f * quad2d.y / viewport_height + 1.0f;
          quadgl.z = quad2d.z;
          quadgl.w = 0.0f;
          quadgl.width = 2.0f * quad2d.width / viewport_width;
          quadgl.height = -2.0f * quad2d.height / viewport_height;
        }

        quadgl.r = quad2d.color.r;
        quadgl.g = quad2d.color.g;
        quadgl.b = quad2d.color.b;
        qinstances[num_quads++] = quadgl;
      }
      if (num_quads == 0)
        goto skip_quads;
      glBufferData(GL_ARRAY_BUFFER, sizeof(Rect_Instance_GL) * num_quads,
                   qinstances, GL_DYNAMIC_DRAW);

      glEnableVertexAttribArray(1);
      glVertexAttribBinding(1, 0);
      glVertexAttribDivisor(1, 1);
      glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Rect_Instance_GL),
                            0);
      glEnableVertexAttribArray(2);
      glVertexAttribBinding(2, 0);
      glVertexAttribDivisor(2, 1);
      glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Rect_Instance_GL),
                            (void *)16);
      glEnableVertexAttribArray(3);
      glVertexAttribBinding(3, 0);
      glVertexAttribDivisor(3, 1);
      glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Rect_Instance_GL),
                            (void *)28);

      glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_quads);

      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
      glDisableVertexAttribArray(2);
      glVertexAttribDivisor(1, 0);
      glVertexAttribDivisor(2, 0);
    }
  skip_quads:
    // Draw lines
    if (line_storage.cursor != 0) {
      uint32_t num_lines = line_storage.cursor;
      struct Line_GL {
        float x0, y0, z0, w0;
        float r0, g0, b0;
        float x1, y1, z1, w1;
        float r1, g1, b1;
      };
      static_assert(sizeof(Line_GL) == 56, "");
      ts.enter_scope();
      defer(ts.exit_scope());
      Line_GL *lines = (Line_GL *)ts.alloc(sizeof(Line_GL) * num_lines);
      ito(num_lines) {
        Line2D l = *line_storage.at(i);
        Line_GL lgl;
        if (l.world_space) {
          lgl.x0 = l.x0;
          lgl.y0 = l.y0;
          lgl.z0 = l.z;
          lgl.w0 = 1.0f;
          lgl.r0 = l.color.r;
          lgl.g0 = l.color.g;
          lgl.b0 = l.color.b;
          lgl.x1 = l.x1;
          lgl.y1 = l.y1;
          lgl.z1 = l.z;
          lgl.w1 = 1.0f;
        } else {
          lgl.x0 = 2.0f * l.x0 / viewport_width - 1.0f;
          lgl.y0 = -2.0f * l.y0 / viewport_height + 1.0f;
          lgl.z0 = l.z;
          lgl.w0 = 0.0f;
          lgl.r0 = l.color.r;
          lgl.g0 = l.color.g;
          lgl.b0 = l.color.b;
          lgl.x1 = 2.0f * l.x1 / viewport_width - 1.0f;
          lgl.y1 = -2.0f * l.y1 / viewport_height + 1.0f;
          lgl.z1 = l.z;
          lgl.w1 = 0.0f;
        }

        lgl.r1 = l.color.r;
        lgl.g1 = l.color.g;
        lgl.b1 = l.color.b;
        lines[i] = lgl;
      }
      glBindVertexArray(line_vao);
      glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(Line_GL) * num_lines, lines,
                   GL_DYNAMIC_DRAW);
      glUseProgram(line_program);
      glUniformMatrix4fv(glGetUniformLocation(line_program, "projection"), 1,
                         GL_FALSE, (float *)&camera.proj[0]);

      glEnableVertexAttribArray(0);
      glVertexAttribBinding(0, 0);
      glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 28, 0);
      glEnableVertexAttribArray(1);
      glVertexAttribBinding(1, 0);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 28, (void *)16);
      glDrawArrays(GL_LINES, 0, 2 * num_lines);

      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
    }
    // Draw strings
    if (string_storage.cursor != 0) {
      const GLchar *vsrc =
          R"(#version 420
  layout (location=0) in vec2 vertex_position;
  layout (location=1) in vec3 instance_offset;
  layout (location=2) in vec2 instance_uv_offset;
  layout (location=3) in vec3 instance_color;

  layout (location=0) out vec2 uv;
  layout (location=1) out vec3 color;

  uniform vec2 glyph_uv_size;
  uniform vec2 glyph_size;
  uniform vec2 viewport_size;
  void main() {
      color = instance_color;
      uv = instance_uv_offset + (vertex_position * vec2(1.0, -1.0) + vec2(0.0, 1.0)) * glyph_uv_size;
      vec4 sspos =  vec4(vertex_position * glyph_size + instance_offset.xy, 0.0, 1.0);
      int pixel_x = int(viewport_size.x * (sspos.x * 0.5 + 0.5) + 0.5);
      int pixel_y = int(viewport_size.y * (sspos.y * 0.5 + 0.5) + 0.5);
      sspos.x = 2.0 * float(pixel_x) / viewport_size.x - 1.0;
      sspos.y = 2.0 * float(pixel_y) / viewport_size.y - 1.0;
      sspos.z = instance_offset.z;
      gl_Position = sspos;

  })";
      const GLchar *fsrc =
          R"(#version 420
  layout(location = 0) out vec4 SV_TARGET0;

  layout (location=0) in vec2 uv;
  layout (location=1) in vec3 color;

  uniform sampler2D image;
  void main() {
    if (texture2D(image, uv).x > 0.0)
      SV_TARGET0 = vec4(color, 1.0);
    else
      discard;
  })";
      static GLuint program = create_program(vsrc, fsrc);
      static GLuint font_texture = [&] {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        uint8_t *r8_data = (uint8_t *)malloc(simplefont_bitmap_height *
                                             simplefont_bitmap_width);
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
      static GLuint vao;
      static GLuint vbo;
      static GLuint instance_vbo;
      static int glyph_vao = [&] {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        float pos[] = {
            0.0f, 0.0f, //
            1.0f, 0.0f, //
            1.0f, 1.0f, //
            0.0f, 0.0f, //
            1.0f, 1.0f, //
            0.0f, 1.0f, //
        };
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(pos), pos, GL_DYNAMIC_DRAW);
        glGenBuffers(1, &instance_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return 0;
      }();
      struct Glyph_Instance_GL {
        float x, y, z;
        float u, v;
        float r, g, b;
      };
      static_assert(sizeof(Glyph_Instance_GL) == 32, "");
      uint32_t glyph_scale = 2;
      float glyph_uv_width =
          (float)simplefont_bitmap_glyphs_width / simplefont_bitmap_width;
      float glyph_uv_height =
          (float)simplefont_bitmap_glyphs_height / simplefont_bitmap_height;

      float glyph_pad_ss = 2.0f / viewport_width;
      uint32_t max_num_glyphs = 0;
      uint32_t num_strings = string_storage.cursor;
      _String2D *strings = string_storage.at(0);
      kto(num_strings) { max_num_glyphs += (uint32_t)strings[k].len; }
      ts.enter_scope();
      defer(ts.exit_scope());
      Glyph_Instance_GL *glyphs_gl = (Glyph_Instance_GL *)ts.alloc(
          sizeof(Glyph_Instance_GL) * max_num_glyphs);
      uint32_t num_glyphs = 0;

      kto(num_strings) {
        _String2D string = strings[k];
        if (string.len == 0)
          continue;
        float2 ss = (float2){string.x, string.y};
        if (string.world_space) {
          ss = camera.world_to_screen(ss);
        } else {
          ss = camera.window_to_screen((int2){(int32_t)ss.x, (int32_t)ss.y});
        }
        float min_ss_x = ss.x;
        float min_ss_y = ss.y;
        float max_ss_x =
            ss.x + (camera.glyphs_screen_width + glyph_pad_ss) * string.len;
        float max_ss_y = ss.y + camera.glyphs_screen_height;
        if (min_ss_x > 1.0f || min_ss_y > 1.0f || max_ss_x < -1.0f ||
            max_ss_y < -1.0f)
          continue;

        ito(string.len) {
          uint32_t c = (uint32_t)string.c_str[i];

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
          Glyph_Instance_GL glyph;
          glyph.u = u0;
          glyph.v = v0;
          glyph.x = ss.x + (camera.glyphs_screen_width + glyph_pad_ss) * i;
          glyph.y = ss.y;
          glyph.z = string.z;
          glyph.r = string.color.r;
          glyph.g = string.color.g;
          glyph.b = string.color.b;
          glyphs_gl[num_glyphs++] = glyph;
        }
      }
      if (num_glyphs == 0)
        goto skip_strings;
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glEnableVertexAttribArray(0);
      glVertexAttribBinding(0, 0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(Glyph_Instance_GL) * num_glyphs,
                   glyphs_gl, GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(1);
      glVertexAttribBinding(1, 0);
      glVertexAttribDivisor(1, 1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
      glEnableVertexAttribArray(2);
      glVertexAttribBinding(2, 0);
      glVertexAttribDivisor(2, 1);
      glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)12);
      glEnableVertexAttribArray(3);
      glVertexAttribBinding(3, 0);
      glVertexAttribDivisor(3, 1);
      glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 32, (void *)20);
      // Draw

      glUseProgram(program);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, font_texture);
      glUniform1i(glGetUniformLocation(program, "image"), 0);
      //      glUniformMatrix4fv(glGetUniformLocation(line_program,
      //      "projection"), 1,
      //                         GL_FALSE, (float *)&camera.proj[0]);
      glUniform2f(glGetUniformLocation(program, "viewport_size"),
                  (float)viewport_width, (float)viewport_height);
      glUniform2f(glGetUniformLocation(program, "glyph_size"),
                  camera.glyphs_screen_width, camera.glyphs_screen_height);
      glUniform2f(glGetUniformLocation(program, "glyph_uv_size"),
                  glyph_uv_width, glyph_uv_height);

      glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_glyphs);
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
    }
  skip_strings:
    (void)0;
  }
}
#endif // RASTER_EXE
