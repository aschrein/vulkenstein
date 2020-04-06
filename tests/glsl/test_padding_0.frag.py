stdlib = \
r"""
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.cpp>

extern "C"{

float g_input[2][WAVE_WIDTH] = {};
vec4 g_output[WAVE_WIDTH] = {};

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

void *get_uniform_ptr(void *state, int set, int binding) {
  return &uniforms;
}

void *get_input_ptr(void *state) {
  return (void*)(&g_input[0]);
}

void *get_output_ptr(void *state, int id) {
  return &g_output;
}

void spv_on_exit(void *state) {
}

void shader_entry(void *);

void test_launch() {
  for (int i = 0; i < WAVE_WIDTH; i++) {
    g_input[0][i] = (float)i;
    g_input[1][i] = (float)i + 1.0f;
  }

  uniforms.vec_4[0] = 2.0f;
  uniforms.vec_4[1] = 2.0f;
  uniforms.vec_4[2] = 3.0f;
  uniforms.vec_4[3] = 4.0f;

  shader_entry(NULL);

  for (int i = 0; i < WAVE_WIDTH; i++) {
    //fprintf(stdout, "[%f %f %f %f]\n", g_output[i].x, g_output[i].y, g_output[i].z, g_output[i].w);
    //fprintf(stdout, "[(%f+%f)*%f]\n", g_input[0][i], g_input[1][i], uniforms.vec_4.x);
    TEST_EQ(g_output[i].x, (g_input[0][i] + g_input[1][i]) * uniforms.vec_4.x);
    TEST_EQ(g_output[i].y, (g_input[0][i] + g_input[1][i]) * uniforms.vec_4.y);
    TEST_EQ(g_output[i].z, (g_input[0][i] + g_input[1][i]) * uniforms.vec_4.z);
    TEST_EQ(g_output[i].w, (g_input[0][i] + g_input[1][i]) * uniforms.vec_4.w);
  }
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
