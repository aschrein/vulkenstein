// uEdit
// Toy project to explore the difficulty of a features set for a text editor
//
// ## TODO
// ### Shortcuts
// * C-z, C-Shift-Z - undo redo
// * C-f            - find/replace window, find selected region
// * alt-Shift      - block selection
// * C-c, C-c, C-v  - copy/cut/paste
// * C-d            - duplicate line
// * C-k            - kill line
// * C-n,C-alt-n    - new line at the bottom/top
// * C-x-p          - execute a python script
// * C-t            - highlight symbol under cursor/selected range
// * C-h            - goto next symbol under cursor/selected range
// * C-`            - open console at file location
// * Alt-t          - open file tree at file location
// * C-b-n          - new scratch buffer
// * C-s            - save current buffer
// * C-a            - select all
//
// ### Buffer types
// * text buffer
// * split screen vertical/horizontal
// * image viewer
// * console
// * file tree
// * csv table view
// * messages
//
// ### UX
// * auto reload modified files via inotify + options for conflict resolution

#include <GLES3/gl32.h>
#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <signal.h>
#include <thread>

#define UTILS_IMPL
#include "simplefont.h"
#include "utils.hpp"

#include <glm/glm.hpp>
using namespace glm;
using float2 = vec2;
using float3 = vec3;
using float4 = vec4;
using int2   = ivec2;
using int3   = ivec3;
using int4   = ivec4;
using uint2  = uvec2;
using uint3  = uvec3;
using uint4  = uvec4;
double my_clock() {
  std::chrono::time_point<std::chrono::system_clock> now      = std::chrono::system_clock::now();
  auto                                               duration = now.time_since_epoch();
  return 1.0e-3 * (double)std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}
static u16 f32_to_u16(f32 x) { return (u16)(clamp(x, 0.0f, 1.0f) * ((1 << 16) - 1)); }
/////////////
// GLOBALS //
/////////////
static Temporary_Storage<> ts                       = Temporary_Storage<>::create(256 * (1 << 20));
SDL_Window *               window                   = NULL;
SDL_GLContext              glc                      = 0;
static void *              g_buffers_to_dump[0x100] = {};
static size_t              g_buffers_to_dump_sizes[0x100] = {};
static u32                 g_num_buffers_to_dump          = 0;
////////////
void handle_segfault(int signal, siginfo_t *si, void *arg) {
  FILE *dump = fopen("segfault_dump", "wb");
  ito(g_num_buffers_to_dump) { fwrite(g_buffers_to_dump[i], 1, g_buffers_to_dump_sizes[i], dump); }
  fclose(dump);
  printf("Caught segfault at address %p\n", si->si_addr);
  exit(1);
}
extern "C" void handle_abort(int signal_number) {
  FILE *dump = fopen("segfault_dump", "wb");
  ito(g_num_buffers_to_dump) { fwrite(g_buffers_to_dump[i], 1, g_buffers_to_dump_sizes[i], dump); }
  fclose(dump);
  exit(1);
}

float3 parse_color_float3(char const *str) {
  ASSERT_ALWAYS(str[0] == '#');
  auto hex_to_decimal = [](char c) {
    if (c >= '0' && c <= '9') {
      return (u32)c - (u32)'0';
    } else if (c >= 'a' && c <= 'f') {
      return 10 + (u32)c - (u32)'a';
    } else if (c >= 'A' && c <= 'F') {
      return 10 + (u32)c - (u32)'A';
    }
    UNIMPLEMENTED;
  };
  u32 r = hex_to_decimal(str[1]) * 16 + hex_to_decimal(str[2]);
  u32 g = hex_to_decimal(str[3]) * 16 + hex_to_decimal(str[4]);
  u32 b = hex_to_decimal(str[5]) * 16 + hex_to_decimal(str[6]);
  return (float3){(f32)r / 255.0f, (f32)g / 255.0f, (f32)b / 255.0f};
}

u32 parse_color_u32(char const *str) {
  ASSERT_ALWAYS(str[0] == '#');
  auto hex_to_decimal = [](char c) {
    if (c >= '0' && c <= '9') {
      return (u32)c - (u32)'0';
    } else if (c >= 'a' && c <= 'f') {
      return 10 + (u32)c - (u32)'a';
    } else if (c >= 'A' && c <= 'F') {
      return 10 + (u32)c - (u32)'A';
    }
    UNIMPLEMENTED;
  };
  u32 r = hex_to_decimal(str[1]) * 16 + hex_to_decimal(str[2]);
  u32 g = hex_to_decimal(str[3]) * 16 + hex_to_decimal(str[4]);
  u32 b = hex_to_decimal(str[5]) * 16 + hex_to_decimal(str[6]);
  return r | (g << 8) | (b << 16);
}

// in pixels
static uint2 get_glyph_offset(u32 line, u32 col) {
  return uint2{(simplefont_bitmap_glyphs_width - 0) * col,
               (simplefont_bitmap_glyphs_height - 1) * line};
}
static u32 num_full_columns(u32 size) { return size / simplefont_bitmap_glyphs_width; }
static u32 num_full_lines(u32 size) { return size / (simplefont_bitmap_glyphs_height - 1); }

struct Text_Buffer {
  Temporary_Storage<char> char_storage;
  u32                     num_columns;
  u32                     num_lines;
  Text_Buffer() { memset(this, 0, sizeof(*this)); }
  void init(string_ref str, u32 line_width) {
    char_storage.release();
    char_storage                                   = Temporary_Storage<char>::create(1 << 20);
    g_buffers_to_dump_sizes[g_num_buffers_to_dump] = char_storage.capacity;
    g_buffers_to_dump[g_num_buffers_to_dump++]     = char_storage.ptr;
    ASSERT_ALWAYS(line_width > 0);
    num_columns = line_width;
    char_storage.reset();
    u32 line_cursor      = 0;
    num_lines            = 0;
    char *      cur_line = add_line();
    size_t      text_len = str.len;
    char const *text     = str.ptr;
    ito(text_len) {
      char c = text[i];
      if (c == '\0') {
        break;
      } else if (c == '\n') {
        cur_line    = add_line();
        line_cursor = 0;
      } else {
        c = (char)clamp((u32)c, 0x20u, 0x7fu);
        if (line_cursor == num_columns) {
          cur_line[num_columns] = '\0';
          cur_line              = add_line();
          line_cursor           = 0;
        }
        cur_line[line_cursor++] = c;
      }
    }
  }
  void shift_cell_right(u32 line_num, u32 col_num) {
    char *cur_line = get_line(line_num);
    // materialize empty cells
    if (cur_line[col_num] == '\0') {
      ito(col_num) {
        if (cur_line[i] == '\0') {
          cur_line[i] = ' ';
        }
      }
    }
    if (cur_line[num_columns - 1] != '\0') {
      duplicate_line(line_num);
      memset(get_line(line_num + 1), '\0', num_columns);
      get_line(line_num + 1)[0] = cur_line[num_columns - 1];
    }
    for (u32 i = num_columns - 1; i > col_num; i--) {
      cur_line[i] = cur_line[i - 1];
    }
  }
  void shift_cell_left(u32 line_num, u32 col_num) {
    char *cur_line = get_line(line_num);
    if (col_num == 0) return;
    for (u32 i = col_num - 1; i < num_columns - 1; i++) {
      cur_line[i] = cur_line[i + 1];
    }
  }
  u32 count_indent(u32 line_num) {
    char *line = get_line(line_num);
    ito(num_columns) {
      if (line[i] != ' ') return i;
    }
    return 0;
  }
  void concat_line_with_prev(u32 line_num) {
    if (line_num == 0) return;
    if (line_num > num_lines - 1) return;
    char *line      = get_line(line_num);
    char *prev_line = get_line(line_num - 1);
    u32   len0      = line_width(line_num - 1);
    u32   len1      = line_width(line_num);
    if (len0 + len1 < num_columns) {
      memcpy(prev_line + len0, line, len1);
      kill_line(line_num);
    } else {
      u32 copy_cnt = num_columns - len0;
      u32 move_cnt = (len0 + len1) - num_columns;
      memcpy(prev_line + len0, line, copy_cnt);
      memmove(line, line + copy_cnt, move_cnt);
      for (u32 i = move_cnt; i < num_columns; i++) line[i] = '\0';
    }
  }
  void break_line(u32 line_num, u32 col_num, u32 indent) {
    u32   len  = line_width(line_num);
    char *line = get_line(line_num);
    duplicate_line(line_num);
    char *next_line = get_line(line_num + 1);
    memset(next_line, '\0', num_columns);
    if (len > col_num) {
      u32 to_move = len - col_num;
      memcpy(next_line + indent, line + col_num, to_move);
      ito(indent) next_line[i] = ' ';
    }
    for (u32 i = col_num; i < len; i++) {
      line[i] = '\0';
    }
  }
  void kill_line(u32 line_num) {
    for (u32 i = line_num; i < num_lines - 1; i++) {
      memcpy(get_line(i), get_line(i + 1), num_columns);
    }
    num_lines--;
  }
  void clear_trailing_spaces(u32 line_num) {
    char *line = get_line(line_num);
    for (u32 i = num_columns - 1; i > 0; i--) {
      if (line[i] == ' ') {
        line[i] = '\0';
      } else if (line[i] != '\0') {
        return;
      }
    }
  }
  void clear_trailing_garbage(u32 line_num) {
    char *line     = get_line(line_num);
    bool  seen_nul = false;
    for (u32 i = 0; i < num_columns; i++) {
      if (line[i] == '\0') seen_nul = true;
      if (seen_nul) line[i] = '\0';
    }
  }
  u32 line_width(u32 line_num) {
    char *line = get_line(line_num);
    ito(num_columns) {
      if (line[i] == '\0') return i;
    }
    return num_columns;
  }
  void duplicate_line(u32 line_num) {
    char *new_line = char_storage.alloc(num_columns);
    for (u32 i = num_lines; i > line_num; i--) {
      memcpy(get_line(i), get_line(i - 1), num_columns);
    }
    num_lines++;
  }
  char *get_line(u32 i) { return char_storage.at(0) + (num_columns)*i; }
  void  resize(u32 new_line_width) {
    if (new_line_width == num_columns) return;
    UNIMPLEMENTED;
  }
  char *add_line() {
    char *new_line = char_storage.alloc(num_columns);
    memset(new_line, '\0', num_columns);
    num_lines++;
    return new_line;
  }
  void flush(char const *filename) {}
  void release() { char_storage.release(); }
};
struct Cell_Extent {
  u32 line_offset;
  u32 col_offset;
  u32 num_lines;
  u32 num_cols;
};
struct IO_Arbiter;
struct IO_Consumer {
  virtual void consume_event(SDL_Event event) = 0;
  virtual void on_focus_gain(){};
  virtual void on_focus_lost(){};

  IO_Consumer *parent = NULL;
  Cell_Extent  extent = {};

  virtual void update_extent(u32 x, u32 y, u32 width, u32 height) = 0;
};
struct IO_Arbiter {
  Cell_Extent get_cell_extent(IO_Consumer *consumer) { return consumer->extent; }
  uint4       get_screen_extent(IO_Consumer *consumer) {
    Cell_Extent ext = get_cell_extent(consumer);
    return uint4{get_glyph_offset(ext.line_offset, ext.col_offset).x,
                 get_glyph_offset(ext.line_offset, ext.col_offset).y,
                 get_glyph_offset(ext.num_lines, ext.num_cols).x,
                 get_glyph_offset(ext.num_lines, ext.num_cols).y};
  }
  IO_Consumer *root = NULL;

  void update_size(u32 viewport_width, u32 viewport_height) {
    if (root == NULL) return;
    root->update_extent(0, 0, viewport_width, viewport_height);
  }
};
struct Text_Buffer_View : public IO_Consumer {
  Text_Buffer *          text_buffer   = NULL;
  i32                    start_line    = 0;
  i32                    start_column  = 0;
  u32                    cur_line      = 0;
  u32                    cur_column    = 0;
  Temporary_Storage<u32> color_storage = {};

  void init(IO_Consumer *parent, Text_Buffer *text_buffer) {
    this->parent       = parent;
    this->extent       = {};
    this->text_buffer  = text_buffer;
    this->start_line   = 0;
    this->start_column = 0;
    this->cur_line     = 0;
    this->cur_column   = 0;
    color_storage.release();
    color_storage = Temporary_Storage<u32>::create(1 << 20);
  }
  void put_char(char c) {
    char *line = text_buffer->get_line(cur_line);
    if (line[text_buffer->num_columns - 1] != '\0') return;
    text_buffer->shift_cell_right(cur_line, cur_column);
    line[cur_column] = c;
    move_cursor_right();
    text_buffer->clear_trailing_spaces(cur_line);
    text_buffer->clear_trailing_garbage(cur_line);
  }
  void new_line() {
    u32 indent = text_buffer->count_indent(cur_line);
    text_buffer->break_line(cur_line, cur_column, indent);
    cur_column = indent;
    cur_line += 1;
  }
  void backspace() {
    if (cur_column == 0) {
      if (cur_line > 0) {
        u32 len0 = text_buffer->line_width(cur_line - 1);
        text_buffer->concat_line_with_prev(cur_line);
        cur_column = len0;
        cur_line--;
      }
    } else {
      text_buffer->shift_cell_left(cur_line, cur_column);
      move_cursor_left();
    }
  }
  void release() { color_storage.release(); }
  void scroll_down() {}
  void scroll_up() {}
  void update_colors() {}
  void tab() {
    put_char(' ');
    put_char(' ');
  }
  void move_cursor_up() {
    if (cur_line > 0) cur_line -= 1;
  }
  void move_cursor_down() {
    if (cur_line < text_buffer->num_lines - 1)
      cur_line += 1;
    else {
      text_buffer->add_line();
      cur_line += 1;
    }
  }
  void move_cursor_right() {
    if (cur_column < text_buffer->num_columns - 1) cur_column += 1;
  }
  void move_cursor_left() {
    if (cur_column > 0) cur_column -= 1;
  }
  // IO state
  struct IO_State {
    bool focused       = false;
    bool ldown         = false;
    i32  old_mp_x      = 0;
    i32  old_mp_y      = 0;
    bool hyper_pressed = false;
    bool skip_key      = false;
    bool ctrl          = false;
    bool shift         = false;
    bool hyper         = false;
    bool alt           = false;
  } io_state;
  virtual void update_extent(u32 x, u32 y, u32 viewport_width, u32 viewport_height) override {
    extent.col_offset  = num_full_columns(x);
    extent.line_offset = num_full_lines(y);
    extent.num_cols    = num_full_columns(viewport_width);
    extent.num_lines   = num_full_lines(viewport_height);
  }
  virtual void on_focus_gain() override { io_state.focused = true; }
  virtual void on_focus_lost() override { memset(&io_state, 0, sizeof(io_state)); }
  virtual void consume_event(SDL_Event event) override {
    switch (event.type) {
    case SDL_KEYUP: {
      if (event.key.keysym.sym == SDLK_LCTRL) {
        io_state.ctrl = false;
      }
      if (event.key.keysym.sym == SDLK_LSHIFT) {
        io_state.shift = false;
      }
      break;
    }
    case SDL_TEXTINPUT: {
      u32 c = event.text.text[0];
      if (c >= 0x20 && c <= 0x7e) {
        put_char(c);
      }
    } break;
    case SDL_KEYDOWN: {
      u32 c = event.key.keysym.sym;
      if (io_state.ctrl && c == SDLK_s) {
      }
      if (event.key.keysym.sym == SDLK_LCTRL) {
        io_state.ctrl = true;
      }
      if (event.key.keysym.sym == SDLK_LSHIFT) {
        io_state.shift = true;
      }
      if (io_state.ctrl && io_state.shift) {
        if (event.key.keysym.sym == SDLK_UP) {
          if (extent.num_lines > 1) extent.num_lines -= 1;
        }
        if (event.key.keysym.sym == SDLK_DOWN) {
          extent.num_lines += 1;
        }
        if (event.key.keysym.sym == SDLK_RIGHT) {
          extent.num_cols += 1;
        }
        if (event.key.keysym.sym == SDLK_LEFT) {
          if (extent.num_cols > 1) extent.num_cols -= 1;
        }
      } else if (io_state.ctrl) {
        if (event.key.keysym.sym == SDLK_UP) {
          start_line += 1;
        }
        if (event.key.keysym.sym == SDLK_DOWN) {
          start_line -= 1;
        }
        if (event.key.keysym.sym == SDLK_RIGHT) {
          start_column -= 1;
        }
        if (event.key.keysym.sym == SDLK_LEFT) {
          start_column += 1;
        }
      } else {
        if (event.key.keysym.sym == SDLK_BACKSPACE) {
          backspace();
        }
        if (event.key.keysym.sym == SDLK_UP) {
          move_cursor_up();
        }
        if (event.key.keysym.sym == SDLK_DOWN) {
          move_cursor_down();
        }
        if (event.key.keysym.sym == SDLK_RIGHT) {
          move_cursor_right();
        }
        if (event.key.keysym.sym == SDLK_TAB) {
          tab();
        }
        if (event.key.keysym.sym == SDLK_LEFT) {
          move_cursor_left();
        }
        if (event.key.keysym.sym == SDLK_RETURN) {
          new_line();
        }
      }

      break;
    }
    case SDL_MOUSEBUTTONDOWN: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      if (m->button == 3) {
      }
      if (m->button == 1) {
      }
      break;
    }
    case SDL_MOUSEBUTTONUP: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      break;
    }
    case SDL_MOUSEMOTION: {
      SDL_MouseMotionEvent *m = (SDL_MouseMotionEvent *)&event;

      int dx = m->x - io_state.old_mp_x;
      int dy = m->y - io_state.old_mp_y;

      io_state.old_mp_x = m->x;
      io_state.old_mp_y = m->y;

    } break;
    case SDL_MOUSEWHEEL: {
      u32 dz = (u32)event.wheel.y;
    } break;
    }
  }
};
struct Splitter : public IO_Consumer {
  IO_Consumer *children[2] = {NULL, NULL};
  u32          focused_id  = 1;
  enum class Splitter_t { Horizontal = 0, Vertical };
  Splitter_t   type = Splitter_t::Horizontal;
  virtual void consume_event(SDL_Event event) override {
    children[focused_id]->consume_event(event);
  }
  virtual void on_focus_gain() override { children[focused_id]->on_focus_gain(); };
  virtual void on_focus_lost() override { children[focused_id]->on_focus_lost(); };
  virtual void update_extent(u32 x, u32 y, u32 viewport_width, u32 viewport_height) override {
    if (children[0] != NULL || children[1] != NULL) {
      if (children[0] != NULL && children[1] != NULL) {
        if (type == Splitter_t::Vertical)
          viewport_height /= 2;
        else
          viewport_width /= 2;
      }
      if (children[0] != NULL) {
        children[0]->update_extent(x, y, viewport_width, viewport_height);
      }
      if (children[1] != NULL) {
        if (type == Splitter_t::Vertical)
          children[1]->update_extent(x, y + viewport_height, viewport_width, viewport_height);
        else
          children[1]->update_extent(x + viewport_width, y, viewport_width, viewport_height);
      }
    }
  }
};
struct Context2D {
  using Color = u32;
  struct URect2D {
    u32   x, y, z, width, height;
    Color color;
  };
  struct ULine2D {
    u32   x0, y0, x1, y1, z;
    Color color;
  };
  struct UString2D {
    string_ref str;
    u32        x, y, z;
    Color      color;
  };
  void draw_rect(URect2D p) { quad_storage.push(p); }
  void draw_line(ULine2D l) { line_storage.push(l); }
  void draw_string(UString2D s) {
    size_t len = s.str.len; // strlen(s.c_str);
    if (len == 0) return;
    char *dst = char_storage.alloc(len + 1);
    memcpy(dst, s.str.ptr, len);
    dst[len] = '\0';
    UString2D internal_string;
    internal_string.color   = s.color;
    internal_string.str.ptr = dst;
    internal_string.str.len = (u32)len;
    internal_string.x       = s.x;
    internal_string.y       = s.y;
    internal_string.z       = s.z;
    string_storage.push(internal_string);
  }
  void frame_start(f32 viewport_width, f32 viewport_height) {
    line_storage.enter_scope();
    quad_storage.enter_scope();
    string_storage.enter_scope();
    char_storage.enter_scope();
    this->viewport_width  = viewport_width;
    this->viewport_height = viewport_height;
    height_over_width     = ((f32)viewport_height / viewport_width);
    width_over_heigth     = ((f32)viewport_width / viewport_height);
    pixel_screen_width    = 1.0f / viewport_width;
    pixel_screen_height   = 1.0f / viewport_height;
    glyphs_screen_width   = glyph_scale * (f32)(simplefont_bitmap_glyphs_width) / viewport_width;
    glyphs_screen_height  = glyph_scale * (f32)(simplefont_bitmap_glyphs_height) / viewport_height;
  }
  void frame_end() {
    render_stuff();
    line_storage.exit_scope();
    quad_storage.exit_scope();
    string_storage.exit_scope();
    char_storage.exit_scope();
  }
  void init() { init_gl(); }
  void release() {
    line_storage.release();
    quad_storage.release();
    string_storage.release();
    char_storage.release();
    release_gl();
  }
  void render_stuff();
  void draw_text_buffer(Text_Buffer_View &view) {

    if ((i32)view.extent.num_cols + view.start_column < 0) return;
    if ((i32)view.text_buffer->num_columns - view.start_column < 0) return;

    u32 col_offset       = view.extent.col_offset;
    u32 inner_col_offset = 0;
    if (view.start_column < 0) inner_col_offset = -view.start_column;
    u32 string_offset = 0;
    if (view.start_column > 0) string_offset = view.start_column;
    u32 line_offset = view.extent.line_offset;
    ito(view.extent.num_lines) {
      i32 line_num = view.start_line + (i32)i;
      if (line_num < 0 || line_num > view.text_buffer->num_lines - 1) continue;
      if (view.text_buffer->get_line((u32)line_num)[string_offset] == '\0') continue;
      //      uint2 coord = get_glyph_offset(line_offset + i, col_offset);
      string_ref line_ref = stref_s(view.text_buffer->get_line((u32)line_num) + string_offset);

      line_ref.len = MIN(view.extent.num_cols - inner_col_offset, line_ref.len);
      draw_string({.str = // string_ref{.ptr =
                   line_ref,
                   //
                   //,.len = view.text_buffer->num_columns},
                   .x     = inner_col_offset + col_offset,
                   .y     = line_offset + i,
                   .z     = 1,
                   .color = parse_color_u32(dark_mode::g_text_color)});
    }
    {
      i32 line = -view.start_line + view.cur_line;
      i32 col  = -view.start_column + view.cur_column;
      if (line >= 0 && col >= 0 && line < view.extent.num_lines && col < view.extent.num_cols) {
        uint2 cursor_coord = get_glyph_offset(line + line_offset, col + col_offset);
        draw_rect({.x      = cursor_coord.x,
                   .y      = cursor_coord.y,
                   .z      = 0,
                   .width  = simplefont_bitmap_glyphs_width,
                   .height = simplefont_bitmap_glyphs_height,
                   .color  = parse_color_u32(dark_mode::g_search_result_color)});
      }
    }
    uint2 c00 = get_glyph_offset(line_offset, col_offset);
    uint2 c01 = get_glyph_offset(line_offset, col_offset + view.extent.num_cols);
    uint2 c10 = get_glyph_offset(line_offset + view.extent.num_lines, col_offset);
    uint2 c11 =
        get_glyph_offset(line_offset + view.extent.num_lines, col_offset + view.extent.num_cols);
    draw_line({.x0    = c00.x + 1,
               .y0    = c00.y + 1,
               .x1    = c01.x,
               .y1    = c01.y + 1,
               .color = parse_color_u32(dark_mode::g_selection_color)});
    draw_line({.x0    = c01.x,
               .y0    = c01.y + 1,
               .x1    = c11.x,
               .y1    = c11.y + 1,
               .color = parse_color_u32(dark_mode::g_selection_color)});
    draw_line({.x0    = c11.x,
               .y0    = c11.y + 1,
               .x1    = c10.x + 1,
               .y1    = c10.y + 1,
               .color = parse_color_u32(dark_mode::g_selection_color)});
    draw_line({.x0    = c10.x + 1,
               .y0    = c10.y + 1,
               .x1    = c00.x + 1,
               .y1    = c00.y + 1,
               .color = parse_color_u32(dark_mode::g_selection_color)});
  }
  void init_gl();
  void release_gl();
  // Fields
  Temporary_Storage<ULine2D>   line_storage      = Temporary_Storage<ULine2D>::create(1 << 17);
  Temporary_Storage<URect2D>   quad_storage      = Temporary_Storage<URect2D>::create(1 << 17);
  Temporary_Storage<UString2D> string_storage    = Temporary_Storage<UString2D>::create(1 << 18);
  Temporary_Storage<char>      char_storage      = Temporary_Storage<char>::create(1 * (1 << 20));
  u32                          viewport_width    = 0;
  u32                          viewport_height   = 0;
  bool                         force_update      = false;
  f32                          height_over_width = 0.0f;
  f32                          width_over_heigth = 0.0f;
  f32                          glyphs_screen_height = 0.0f;
  f32                          glyphs_screen_width  = 0.0f;
  f32                          pixel_screen_width   = 0.0f;
  f32                          pixel_screen_height  = 0.0f;
  u32                          glyph_scale          = 1;

  // GL state
  GLuint line_program;
  GLuint quad_program;
  GLuint line_vao;
  GLuint line_vbo;
  GLuint quad_vao;
  GLuint quad_instance_vbo;
  GLuint vao;
  GLuint vbo;
  GLuint instance_vbo;
  GLuint program;
  GLuint font_texture;
} c2d;
struct CV_Wrapper {
  std::mutex              cv_mutex;
  std::atomic<bool>       cv_predicate;
  std::condition_variable cv;
  void                    wait() {
    std::unique_lock<std::mutex> lk(cv_mutex);
    cv.wait(lk, [this] { return cv_predicate.load(); });
    cv_predicate = false;
  }
  void notify_one() {
    cv_predicate = true;
    cv.notify_one();
  }
};

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

void MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                     const GLchar *message, const void *userParam) {
  fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
          (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}

void main_loop() {
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(MessageCallback, 0);
  SDL_Event   event;
  i32         SCREEN_WIDTH  = 0;
  i32         SCREEN_HEIGHT = 0;
  Text_Buffer text_buffer_0;
  Text_Buffer text_buffer_1;
  text_buffer_0.init(stref_s("wefegqghqqqq46\n76357423:\n:\"'~!+_+_~\n?><L:{}[]<>?./"), 128);
  text_buffer_1.init(stref_s(R"(
void MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                   const GLchar *message, const void *userParam) {
  fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
          (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}
  )"),
                     128);
  Text_Buffer_View text_buffer_view_0;
  Text_Buffer_View text_buffer_view_1;
  Splitter         splitter;
  splitter.type = Splitter::Splitter_t::Horizontal;
  text_buffer_view_0.init(&splitter, &text_buffer_0);
  text_buffer_view_1.init(&splitter, &text_buffer_1);
  splitter.children[0]             = &text_buffer_view_0;
  splitter.children[1]             = &text_buffer_view_1;
  IO_Consumer *focused_io_consumer = NULL;
  focused_io_consumer              = &splitter;
  focused_io_consumer->on_focus_gain();
  struct State_Node {
    bool final;
  };
  SDL_StartTextInput();
  c2d.init();
  defer({
    text_buffer_0.release();
    text_buffer_view_0.release();
    text_buffer_1.release();
    text_buffer_view_1.release();

    c2d.release();
  });
  i32  old_mp_x     = 0;
  i32  old_mp_y     = 0;
  auto handle_event = [&]() {
    switch (event.type) {
    case SDL_MOUSEBUTTONDOWN: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      if (m->button == 3) {
      }
      if (m->button == 1) {
      }
      break;
    }
    case SDL_MOUSEBUTTONUP: {
      SDL_MouseButtonEvent *m = (SDL_MouseButtonEvent *)&event;
      break;
    }
    case SDL_MOUSEMOTION: {
      SDL_MouseMotionEvent *m = (SDL_MouseMotionEvent *)&event;

      int dx = m->x - old_mp_x;
      int dy = m->y - old_mp_y;

      old_mp_x = m->x;
      old_mp_y = m->y;

    } break;
    case SDL_QUIT: {
      exit(0);
      break;
    }
    case SDL_WINDOWEVENT_FOCUS_LOST: {
      if (focused_io_consumer != NULL) {
        focused_io_consumer->on_focus_lost();
      }
      break;
    }
    case SDL_KEYDOWN: {
      u32 c = event.key.keysym.sym;

      if (event.key.keysym.sym == SDLK_ESCAPE) {
        if (focused_io_consumer != NULL) {
          focused_io_consumer->on_focus_lost();
          UNIMPLEMENTED;
        }
      }

      break;
    }
    }
    if (focused_io_consumer != NULL) {
      focused_io_consumer->consume_event(event);
    }
  };
  while (SDL_WaitEvent(&event)) {
    SDL_GetWindowSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
    handle_event();
    while (SDL_PollEvent(&event)) {
      handle_event();
    }
    focused_io_consumer->update_extent(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    {
      // Update delta time
      double last_time = my_clock();
      c2d.frame_start(SCREEN_WIDTH, SCREEN_HEIGHT);
      c2d.draw_text_buffer(text_buffer_view_0);
      c2d.draw_text_buffer(text_buffer_view_1);

      c2d.frame_end();
      glFinish();
      SDL_GL_SwapWindow(window);
      double cur_time = my_clock();
      double dt       = cur_time - last_time;
      last_time       = cur_time;
      //      fprintf(stdout, "dt: %fms\n", (f32)(dt * 1.0e3));
    }
    SDL_UpdateWindowSurface(window);
  }
}
int main(int argc, char **argv) {
  struct sigaction sa;
  signal(SIGABRT, &handle_abort);
  memset(&sa, 0, sizeof(struct sigaction));
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = handle_segfault;
  sa.sa_flags     = SA_SIGINFO;

  sigaction(SIGSEGV, &sa, NULL);
  std::thread window_loop = std::thread([&] {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("uEdit", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 1024,
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
  });

  window_loop.join();
  return 0;
}
void Context2D::init_gl() {
  const GLchar *line_vs =
      R"(#version 420
  layout (location = 0) in vec4 vertex_position;
  layout (location = 1) in vec4 vertex_color;
  layout(location = 0) out vec3 color;
  void main() {
      color = vertex_color.xyz;
      gl_Position = vec4(vertex_position.xy * vec2(2.0, -2.0) + vec2(-1.0, 1.0), vertex_position.z, 1.0);
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
  layout (location = 0) in vec4 instance_offset;
  layout (location = 1) in vec2 instance_size;
  layout (location = 2) in vec4 instance_color;

  layout(location = 0) out vec3 color;

  uniform vec2 viewport_size;

  void main() {
      vec2 pos[] = {
            {0.0f, 0.0f}, //
            {1.0f, 0.0f}, //
            {1.0f, 1.0f}, //
            {0.0f, 0.0f}, //
            {1.0f, 1.0f}, //
            {0.0f, 1.0f}, //
      };
      color = instance_color.xyz;
      gl_Position =  vec4(
        pos[gl_VertexID] * instance_size * vec2(2.0, -2.0) +
                          instance_offset.xy * vec2(2.0, -2.0) +
                          vec2(-1.0, 1.0),
        instance_offset.z,
        1.0);
  })";
  const GLchar *quad_ps =
      R"(#version 420
  layout(location = 0) out vec4 SV_TARGET0;
  layout(location = 0) in vec3 color;
  void main() {
    SV_TARGET0 = vec4(color, 1.0);
  })";
  line_program = create_program(line_vs, line_ps);
  quad_program = create_program(quad_vs, quad_ps);
  int init_va0 = [&] {
    glGenVertexArrays(1, &line_vao);
    glBindVertexArray(line_vao);
    glGenBuffers(1, &line_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo);

    glGenVertexArrays(1, &quad_vao);

    glGenBuffers(1, &quad_instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_instance_vbo);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
  }();

  const GLchar *vsrc =
      R"(#version 420
  layout (location=0) in vec4 instance_offset;
  layout (location=1) in vec2 instance_uv_offset;
  layout (location=2) in vec4 instance_color;

  layout (location=0) out vec2 uv;
  layout (location=1) out vec3 color;

  uniform vec2 glyph_uv_size;
  uniform vec2 glyph_size;
  uniform vec2 viewport_size;
  void main() {
      vec2 pos[] = {
            {0.0f, 0.0f}, //
            {1.0f, 0.0f}, //
            {1.0f, 1.0f}, //
            {0.0f, 0.0f}, //
            {1.0f, 1.0f}, //
            {0.0f, 1.0f}, //
      };
      color = instance_color.xyz;
      uv = instance_uv_offset + (pos[gl_VertexID] * vec2(1.0, 1.0) + vec2(0.0, 0.0)) * glyph_uv_size;
      vec4 sspos =  vec4(pos[gl_VertexID] * glyph_size * vec2(2.0, -2.0) + instance_offset.xy * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
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
  program      = create_program(vsrc, fsrc);
  font_texture = [&] {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    uint8_t *r8_data = (uint8_t *)malloc(simplefont_bitmap_height * simplefont_bitmap_width);
    defer(free(r8_data));
    ito(simplefont_bitmap_height) {
      jto(simplefont_bitmap_width) {
        char c                                   = simplefont_bitmap[i][j];
        r8_data[(i)*simplefont_bitmap_width + j] = c == ' ' ? 0 : 0xff;
      }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, simplefont_bitmap_width, simplefont_bitmap_height, 0,
                 GL_RED, GL_UNSIGNED_BYTE, r8_data);
    return tex;
  }();

  int glyph_vao = [&] {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
  }();
}
void Context2D::release_gl() {}
void Context2D::render_stuff() {
  glViewport(0, 0, viewport_width, viewport_height);
  float3 bc = parse_color_float3(dark_mode::g_background_color);
  glClearColor(bc.x, bc.y, bc.z, 1.0f);
  glClearDepthf(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_GEQUAL);
  glDisable(GL_CULL_FACE);
  glLineWidth(1.0f);
  {

    // Draw quads
    if (quad_storage.cursor != 0) {
      glUseProgram(quad_program);
      glBindVertexArray(quad_vao);

      glBindBuffer(GL_ARRAY_BUFFER, quad_instance_vbo);
      struct Rect_Instance_GL {
        u16 x, y, z, w;
        u16 width, height;
        u32 color;
      };
      static_assert(sizeof(Rect_Instance_GL) == 16, "");
      u32 max_num_quads = quad_storage.cursor;
      u32 num_quads     = 0;
      ts.enter_scope();
      defer(ts.exit_scope());
      Rect_Instance_GL *qinstances =
          (Rect_Instance_GL *)ts.alloc(sizeof(Rect_Instance_GL) * max_num_quads);
      ito(max_num_quads) {
        URect2D          quad2d = *quad_storage.at(i);
        Rect_Instance_GL quadgl;
        quadgl.x                = f32_to_u16((float)quad2d.x / viewport_width);
        quadgl.y                = f32_to_u16((float)quad2d.y / viewport_height);
        quadgl.z                = (u16)quad2d.z;
        quadgl.width            = f32_to_u16((float)quad2d.width / viewport_width);
        quadgl.height           = f32_to_u16((float)quad2d.height / viewport_height);
        quadgl.color            = quad2d.color;
        qinstances[num_quads++] = quadgl;
      }
      if (num_quads == 0) goto skip_quads;
      glBufferData(GL_ARRAY_BUFFER, sizeof(Rect_Instance_GL) * num_quads, qinstances,
                   GL_DYNAMIC_DRAW);

      glEnableVertexAttribArray(0);
      glVertexAttribBinding(0, 0);
      glVertexAttribDivisor(0, 1);
      glVertexAttribPointer(0, 4, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Rect_Instance_GL), (void *)0);
      glEnableVertexAttribArray(1);
      glVertexAttribBinding(1, 0);
      glVertexAttribDivisor(1, 1);
      glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Rect_Instance_GL), (void *)8);
      glEnableVertexAttribArray(2);
      glVertexAttribBinding(2, 0);
      glVertexAttribDivisor(2, 1);
      glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rect_Instance_GL), (void *)12);

      glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_quads);

      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
      glDisableVertexAttribArray(2);
      glVertexAttribDivisor(0, 0);
      glVertexAttribDivisor(1, 0);
      glVertexAttribDivisor(2, 0);
    }
  skip_quads:
    // Draw lines
    if (line_storage.cursor != 0) {
      u32 num_lines = line_storage.cursor;
      struct Line_GL {
        u16 x0, y0, z0, w0;
        u32 color0;
        u16 x1, y1, z1, w1;
        u32 color1;
      };
      static_assert(sizeof(Line_GL) == 24, "");
      ts.enter_scope();
      defer(ts.exit_scope());
      Line_GL *lines = (Line_GL *)ts.alloc(sizeof(Line_GL) * num_lines);
      ito(num_lines) {
        ULine2D l = *line_storage.at(i);
        Line_GL lgl;

        lgl.x0     = f32_to_u16((float)l.x0 / viewport_width);
        lgl.y0     = f32_to_u16((float)l.y0 / viewport_height);
        lgl.z0     = (u16)l.z;
        lgl.w0     = 0;
        lgl.color0 = l.color;
        lgl.x1     = f32_to_u16((float)l.x1 / viewport_width);
        lgl.y1     = f32_to_u16((float)l.y1 / viewport_height);
        lgl.z1     = (u16)l.z;
        lgl.w1     = 0;
        lgl.color1 = l.color;

        lines[i] = lgl;
      }
      glBindVertexArray(line_vao);
      glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(Line_GL) * num_lines, lines, GL_DYNAMIC_DRAW);
      glUseProgram(line_program);

      glEnableVertexAttribArray(0);
      glVertexAttribBinding(0, 0);
      glVertexAttribPointer(0, 4, GL_UNSIGNED_SHORT, GL_TRUE, 12, (void *)0);
      glEnableVertexAttribArray(1);
      glVertexAttribBinding(1, 0);
      glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 12, (void *)8);
      glDrawArrays(GL_LINES, 0, 2 * num_lines);

      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
    }
    // Draw strings
    if (string_storage.cursor != 0) {

      struct Glyph_Instance_GL {
        u16 x, y, z, w;
        u16 u, v;
        u32 color;
      };
      static_assert(sizeof(Glyph_Instance_GL) == 16, "");
      u32 glyph_scale     = 2;
      f32 glyph_uv_width  = (f32)simplefont_bitmap_glyphs_width / simplefont_bitmap_width;
      f32 glyph_uv_height = (f32)simplefont_bitmap_glyphs_height / simplefont_bitmap_height;

      //      f32        glyph_pad_ss   = -1.0f / viewport_width;
      u32        max_num_glyphs = 0;
      u32        num_strings    = string_storage.cursor;
      UString2D *strings        = string_storage.at(0);
      kto(num_strings) { max_num_glyphs += (u32)strings[k].str.len; }
      ts.enter_scope();
      defer(ts.exit_scope());
      Glyph_Instance_GL *glyphs_gl =
          (Glyph_Instance_GL *)ts.alloc(sizeof(Glyph_Instance_GL) * max_num_glyphs);
      u32 num_glyphs = 0;

      kto(num_strings) {
        UString2D string = strings[k];
        if (string.str.len == 0) continue;
        //        float2 ss    = float2{string.x, string.y};
        //        ss.x         = ((f32)ss.x + 0.5f) / viewport_width;
        //        ss.y         = ((f32)ss.y + 0.5f) / viewport_height;
        //        f32 min_ss_x = ss.x;
        //        f32 min_ss_y = ss.y;
        //        f32 max_ss_x = ss.x + (glyphs_screen_width + glyph_pad_ss) * string.str.len;
        //        f32 max_ss_y = ss.y + glyphs_screen_height;
        //        if (min_ss_x > 1.0f || min_ss_y > 1.0f || max_ss_x < 0.0f || max_ss_y < 0.0f)
        //        continue;

        ito(string.str.len) {
          u32 c = (u32)string.str.ptr[i];

          // Printable characters only
          c       = glm::clamp(c, 0x20u, 0x7eu);
          u32 row = (c - 0x20) / simplefont_bitmap_glyphs_per_row;
          u32 col = (c - 0x20) % simplefont_bitmap_glyphs_per_row;
          f32 v0 =
              ((f32)row * (simplefont_bitmap_glyphs_height + simplefont_bitmap_glyphs_pad_y * 2) +
               simplefont_bitmap_glyphs_pad_y) /
              simplefont_bitmap_height;
          f32 u0 =
              ((f32)col * (simplefont_bitmap_glyphs_width + simplefont_bitmap_glyphs_pad_x * 2) +
               simplefont_bitmap_glyphs_pad_x) /
              simplefont_bitmap_width;
          Glyph_Instance_GL glyph;
          glyph.u = f32_to_u16(u0);
          glyph.v = f32_to_u16(v0);
          //          glyph.x= f32_to_u16(ss.x + (glyphs_screen_width + glyph_pad_ss) * i);
          //          glyph.y= f32_to_u16(ss.y);
          glyph.x = f32_to_u16((float)get_glyph_offset(string.y, string.x + i).x / viewport_width);
          glyph.y = f32_to_u16((float)get_glyph_offset(string.y, string.x + i).y / viewport_height);
          glyph.z = (u16)string.z;
          glyph.color             = string.color;
          glyphs_gl[num_glyphs++] = glyph;
        }
      }
      if (num_glyphs == 0) goto skip_strings;
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(Glyph_Instance_GL) * num_glyphs, glyphs_gl,
                   GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribBinding(0, 0);
      glVertexAttribDivisor(0, 1);
      glVertexAttribPointer(0, 4, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Glyph_Instance_GL), (void *)0);
      glEnableVertexAttribArray(1);
      glVertexAttribBinding(1, 0);
      glVertexAttribDivisor(1, 1);
      glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Glyph_Instance_GL), (void *)8);
      glEnableVertexAttribArray(2);
      glVertexAttribBinding(2, 0);
      glVertexAttribDivisor(2, 1);
      glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Glyph_Instance_GL), (void *)12);
      // Draw

      glUseProgram(program);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, font_texture);
      glUniform1i(glGetUniformLocation(program, "image"), 0);
      //      glUniformMatrix4fv(glGetUniformLocation(line_program,
      //      "projection"), 1,
      //                         GL_FALSE, (f32 *)&camera.proj[0]);
      glUniform2f(glGetUniformLocation(program, "viewport_size"), (f32)viewport_width,
                  (f32)viewport_height);
      glUniform2f(glGetUniformLocation(program, "glyph_size"), glyphs_screen_width,
                  glyphs_screen_height);
      glUniform2f(glGetUniformLocation(program, "glyph_uv_size"), glyph_uv_width, glyph_uv_height);

      glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_glyphs);
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
      glDisableVertexAttribArray(2);
    }
  skip_strings:
    (void)0;
  }
}
