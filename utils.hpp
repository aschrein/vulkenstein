#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// #define f32 float
// #define i32 int32_t
// #define u32 uint32_t
// #define u64 uint64_t
// #define u8 uint8_t

#define ASSERT_ALWAYS(x) { if (!(x)) {fprintf(stderr, "[FAIL] at %s:%i %s\n", __FILE__, __LINE__, #x); (void)(*(volatile int*)(NULL)=0);} }
#define ASSERT_DEBUG(x) ASSERT_ALWAYS(x)

template <typename F> struct __Defer__ {
  F f;
  __Defer__(F f) : f(f) {}
  ~__Defer__() { f(); }
};

template <typename F> __Defer__<F> defer_func(F f) { return __Defer__<F>(f); }

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x) DEFER_2(x, __COUNTER__)
#define defer(code) auto DEFER_3(_defer_) = defer_func([&]() { code; })

#define ito(N) for (uint32_t i = 0; i < N; ++i)
#define jto(N) for (uint32_t j = 0; j < N; ++j)
#define kto(N) for (uint32_t k = 0; k < N; ++k)

#define PERF_HIST_ADD(name, val)
#define PERF_ENTER(name)
#define PERF_EXIT(name)
#define OK_FALLTHROUGH (void)0;


#ifdef __cplusplus
extern "C" {
#endif
// [0 ..                     .. N-1]
// [stack bytes...][memory bytes...]
struct Temporary_Storage {
  uint8_t *ptr;
  size_t cursor;
  size_t capacity;
  size_t stack_capacity;
  size_t stack_cursor;
};

Temporary_Storage Temporary_Storage_new(size_t capacity);

void Temporary_Storage_delete(Temporary_Storage *ts);

void *Temporary_Storage_alloc(Temporary_Storage *ts, size_t size);

void Temporary_Storage_enter_scope(Temporary_Storage *ts);

void Temporary_Storage_exit_scope(Temporary_Storage *ts);

void Temporary_Storage_reset(Temporary_Storage *ts);

/** Allocates 'size' bytes using thread local allocator
 */
void *tl_alloc(size_t size);
/** Reallocates deleting `ptr` as a result
 */
void *tl_realloc(void *ptr, size_t oldsize, size_t newsize);
void tl_free(void *ptr);
/** Allocates 'size' bytes using thread local temporal storage
 */
void *tl_alloc_tmp(size_t size);
/** Record the current state of thread local temporal storage
 */
void tl_alloc_tmp_enter();
/** Restore the previous state of thread local temporal storage
 */
void tl_alloc_tmp_exit();

#ifdef __cplusplus
}
#endif

#endif

#ifdef UTILS_IMPL
#include <string.h>

Temporary_Storage Temporary_Storage_new(size_t capacity) {
  ASSERT_DEBUG(capacity > 0);
  Temporary_Storage out;
  size_t STACK_CAPACITY = 0x100 * sizeof(size_t);
  out.ptr = (uint8_t *)malloc(STACK_CAPACITY + capacity);
  out.capacity = capacity;
  out.cursor = 0;
  out.stack_capacity = STACK_CAPACITY;
  out.stack_cursor = 0;
  return out;
}

void Temporary_Storage_delete(Temporary_Storage *ts) {
  ASSERT_DEBUG(ts != nullptr);
  free(ts->ptr);
  memset(ts, 0, sizeof(Temporary_Storage));
}

void *Temporary_Storage_alloc(Temporary_Storage *ts, size_t size) {
  ASSERT_DEBUG(ts != nullptr);
  ASSERT_DEBUG(size != 0);
  void *ptr = (void *)(ts->ptr + ts->stack_capacity + ts->cursor);
  ts->cursor += size;
  ASSERT_DEBUG(ts->cursor < ts->capacity);
  return ptr;
}

void Temporary_Storage_enter_scope(Temporary_Storage *ts) {
  ASSERT_DEBUG(ts != nullptr);
  // Save the cursor to the stack
  size_t *top = (size_t *)(ts->ptr + ts->stack_cursor);
  *top = ts->cursor;
  // Increment stack cursor
  ts->stack_cursor += sizeof(size_t);
  ASSERT_DEBUG(ts->stack_cursor < ts->stack_capacity);
}

void Temporary_Storage_exit_scope(Temporary_Storage *ts) {
  ASSERT_DEBUG(ts != nullptr);
  // Decrement stack cursor
  ASSERT_DEBUG(ts->stack_cursor >= sizeof(size_t));
  ts->stack_cursor -= sizeof(size_t);
  // Restore the cursor from the stack
  size_t *top = (size_t *)(ts->ptr + ts->stack_cursor);
  ts->cursor = *top;
}

void Temporary_Storage_reset(Temporary_Storage *ts) {
  ASSERT_DEBUG(ts != nullptr);
  ts->cursor = 0;
  ts->stack_cursor = 0;
}

struct Thread_Local {
  Temporary_Storage temporal_storage;
  bool initialized = false;
  ~Thread_Local() { Temporary_Storage_delete(&temporal_storage); }
};

// TODO(aschrein): Change to __thread?
thread_local Thread_Local g_tl{};

Thread_Local *get_tl() {
  if (g_tl.initialized == false) {
    g_tl.initialized = true;
    g_tl.temporal_storage = Temporary_Storage_new(1 << 24);
  }
  return &g_tl;
}

void *tl_alloc_tmp(size_t size) {
  return Temporary_Storage_alloc(&get_tl()->temporal_storage, size);
}

void tl_alloc_tmp_enter() {
  Temporary_Storage *ts = &get_tl()->temporal_storage;
  Temporary_Storage_enter_scope(ts);
}
void tl_alloc_tmp_exit() {
  Temporary_Storage *ts = &get_tl()->temporal_storage;
  Temporary_Storage_exit_scope(ts);
}

void *tl_alloc(size_t size) { return malloc(size); }

void *tl_realloc(void *ptr, size_t oldsize, size_t newsize) {
  if (oldsize == newsize)
    return ptr;
  size_t min_size = oldsize < newsize ? oldsize : newsize;
  void *new_ptr = NULL;
  if (newsize != 0)
    new_ptr = malloc(newsize);
  if (min_size != 0) {
    memcpy(new_ptr, ptr, min_size);
  }
  if (ptr != NULL)
    free(ptr);
  return new_ptr;
}

void tl_free(void *ptr) { free(ptr); }
#endif
