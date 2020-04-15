#include <stdlib.h>
#include <stdio.h>
#define UTILS_IMPL
#include "../utils.hpp"
#include <dlfcn.h>

extern "C" {
//void printf(char const *fmt, ...) {
//  fprintf(stdout, "[SUCCESS]\n");
//}
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
  func((void*)&printf);
  dlclose(myso);
  return 0;
}
