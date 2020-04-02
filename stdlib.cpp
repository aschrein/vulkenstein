#include <cmath>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <glm/glm.hpp>
#include <glm/gtx/vec_swizzle.hpp>

using namespace glm;

extern "C" {

float spv_sqrt(float a) { return sqrtf(a); }

float length_f4(vec4 *a) {
  return glm::length(*a);
}

float spv_dot_f2(vec2 *a, vec2 *b) {
  return glm::dot(*a, *b);
}

float spv_dot_f3(vec3 *a, vec3 *b) {
  return glm::dot(*a, *b);
}

float spv_dot_f4(vec4 *a, vec4 *b) {
  return glm::dot(*a, *b);
}

#define TEST(x)                                                                \
  if (!(x)) {                                                                  \
    fprintf(stderr, "FAIL\n");                                                 \
    exit(1);                                                                   \
  }
#define TEST_EQ(a, b)                                                          \
  if ((a) != (b)) {                                                            \
    std::cerr << "FAIL at " << __FILE__ << ":" << __LINE__ << " " << (a)       \
              << " != " << (b) << "\n";                                        \
    exit(1);                                                                   \
  }
}
