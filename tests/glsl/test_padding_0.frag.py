stdlib = \
r"""
#include <stdlib.h>
#include <stdio.h>

extern "C"{

float g_input[2] = {1.0f, 2.0f};

struct mat4 {float m[16];};
struct vec4 {float m[4];};

#pragma pack(1)
struct UBO {
  mat4 transform;
  int albedo_id;
  int normal_id;
  int arm_id;
  float metal_factor;
  float roughness_factor;
  char pad[12];
  vec4 vec_4;
} uniforms;
#pragma pop

void *get_uniform_ptr(int set, int binding) {
  uniforms.vec_4.m[0] = 1.0f;
  uniforms.vec_4.m[1] = 2.0f;
  uniforms.vec_4.m[2] = 3.0f;
  uniforms.vec_4.m[3] = 4.0f;
  return &uniforms;
}

void *get_input_ptr(int id) {
  return &g_input[id];
}

vec4 g_output;

void *get_output_ptr(int id) {
  return &g_output;
}

#define TEST(x) if (!(x)) {fprintf(stderr, "FAIL\n"); exit(1);}

void spv_on_exit() {
//  fprintf(stdout, "[%f, %f, %f, %f]\n",
//  g_output.m[0],
//  g_output.m[1],
//  g_output.m[2],
//  g_output.m[3]
//  );
  TEST(
    g_output.m[0] == 3.0f &&
    g_output.m[1] == 6.0f &&
    g_output.m[2] == 9.0f &&
    g_output.m[3] == 12.0f
  );

  fprintf(stdout, "[SUCCESS]\n");
}

}
"""
shader = \
r"""#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in float param0;
layout(location = 1) in float param1;

layout(set = 0, binding = 0, std140) uniform UBO {
  mat4 transform;
  int albedo_id;
  int normal_id;
  int arm_id;
  float metal_factor;
  float roughness_factor;
  vec4 vec_4;
} uniforms;

layout(location = 0) out vec4 result0;

void main() {
  result0 = (param0 + param1) * uniforms.vec_4;
}
"""
shader_filename = "shader.frag.glsl"
stdlib_c = open("stdlib.cpp", "w")
stdlib_c.write(stdlib)
stdlib_c.close()
shader_f = open(shader_filename, "w")
shader_f.write(shader)
shader_f.close()
import subprocess
cmd = "glslangValidator -V " + shader_filename + " -o shader.spv"
#print("running: " + cmd)
process = subprocess.Popen(cmd.split(' '), stdout=subprocess.PIPE)
stdout, stderr = process.communicate()
stdout = stdout.decode()
print(stdout)
if process.returncode != 0:
  exit(-1)
