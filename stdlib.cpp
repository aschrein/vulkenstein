#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtx/vec_swizzle.hpp>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

using namespace glm;

extern "C" {

struct GPU_State {

};

// stack allocated struct passed per each entry invocation
struct Invocation_Info {
  ivec3 gid;
  ivec3 lid;
  ivec3 work_group_size;
  int subgroup_size; // eventually we'd want to emulate subgroup extension and
                     // dFdx etc
  GPU_State *gpu_state;
};

float spv_sqrt(float a) { return sqrtf(a); }

float length_f4(vec4 *a) { return glm::length(*a); }

float spv_dot_f2(vec2 *a, vec2 *b) { return glm::dot(*a, *b); }

float spv_dot_f3(vec3 *a, vec3 *b) { return glm::dot(*a, *b); }

float spv_dot_f4(vec4 *a, vec4 *b) { return glm::dot(*a, *b); }

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
