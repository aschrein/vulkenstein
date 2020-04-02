#include <cmath>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

extern "C" {

struct mat4 {
  float m[16];
};
struct vec4 {
  float m[4];
};
struct vec3 {
  float m[3];
};
struct vec2 {
  float m[2];
};
float spv_sqrt(float a) { return sqrtf(a); }

float length_f4(vec4 *a) {
  return sqrtf(a->m[0] * a->m[0] + a->m[1] * a->m[1] + a->m[2] * a->m[2] +
               a->m[3] * a->m[3] + 0.0f);
}

float spv_dot_f2(vec2 *a, vec2 *b) {
  return
  a->m[0] * b->m[0] +
  a->m[1] * b->m[1] +
  0.0f;
}

float spv_dot_f3(vec3 *a, vec3 *b) {
  return
  a->m[0] * b->m[0] +
  a->m[1] * b->m[1] +
  a->m[2] * b->m[2] +
  0.0f;
}

float spv_dot_f4(vec4 *a, vec4 *b) {
  return
  a->m[0] * b->m[0] +
  a->m[1] * b->m[1] +
  a->m[2] * b->m[2] +
  a->m[3] * b->m[3] +
  0.0f;
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


vec3 mul(vec3 a, float b) {
  return vec3 {
  a.m[0] * b,
  a.m[1] * b,
  a.m[2] * b,
  };
}

vec3 mul(vec3 a, vec3 b) {
  return vec3 {
  a.m[0] * b.m[0],
  a.m[1] * b.m[1],
  a.m[2] * b.m[2],
  };
}

vec3 add(vec3 a, vec3 b) {
  return vec3 {
  a.m[0] + b.m[0],
  a.m[1] + b.m[1],
  a.m[2] + b.m[2],
  };
}

vec4 mul(vec4 a, vec4 b) {
  return vec4 {
  a.m[0] * b.m[0],
  a.m[1] * b.m[1],
  a.m[2] * b.m[2],
  a.m[3] * b.m[3],
  };
}

vec4 add(vec4 a, vec4 b) {
  return vec4 {
  a.m[0] + b.m[0],
  a.m[1] + b.m[1],
  a.m[2] + b.m[2],
  a.m[3] + b.m[3],
  };
}

