#define FNATTR
#include <cmath>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <x86intrin.h>

extern "C" {

void ll_printf(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

}
