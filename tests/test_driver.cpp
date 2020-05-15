#include <stdlib.h>
#include <stdio.h>
#define UTILS_IMPL
#include "../utils.hpp"
#include <dlfcn.h>

extern "C" {
void _printf(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
  fflush(stdout);
}
}

int main(int argc, char **argv) {
  ASSERT_ALWAYS(argc == 2);
  typedef void (*main_t)(void*);

  //void *this_module = dlopen(NULL, NULL);
  //ASSERT_ALWAYS(this_module != NULL);
  void *myso = dlopen(argv[1], RTLD_NOW);
  if(myso == NULL) {
    fprintf(stderr, "[ERROR] dlopen: %s\n", dlerror());
    exit(1);
  }
  main_t func = (main_t)dlsym(myso, "test_launch");
  ASSERT_ALWAYS(func != NULL);
  func((void*)&_printf);
  dlclose(myso);
  return 0;
}
