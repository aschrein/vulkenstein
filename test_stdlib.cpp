#include <cmath>
#include <stdint.h>
#include "spv_stdlib.h"

extern "C" {
typedef int (*printf_t)(const char *__restrict __format, ...);

#define TEST(x)                                                                \
  if (!(x)) {                                                                  \
    ((printf_t)_printf)("%s:%i [FAIL]\n", __FILE__, __LINE__);                 \
    abort();                                                                   \
  }
#define TEST_EQ(a, b)                                                          \
  if ((a) != (b)) {                                                            \
    ((printf_t)_printf)("%s:%i [FAIL] %s != %s \n", __FILE__, __LINE__, #a,    \
                        #b);                                                   \
    abort();                                                                   \
  }
}
