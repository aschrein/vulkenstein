stdlib = \
r"""
#include <stdlib.h>
#include <stdio.h>
#include <test_stdlib.cpp>

extern "C"{

float4 g_input[2][WAVE_WIDTH] = {};
float g_input_flat[2][WAVE_WIDTH * 4] = {};

float g_output[WAVE_WIDTH * 4] = {};

#pragma pack(1)
struct UBO {
  int pad_0;
  char pad_1[12];
  float4 vec_4;
} uniforms;
#pragma pop

void *get_uniform_ptr(void *state, int set, int binding) {
  return &uniforms;
}

void *get_input_ptr(void *state) {
#if DEINTERLEAVE
  return (void*)(&g_input_flat[0]);
#else
  return (void*)(&g_input[0]);
#endif
}

void *get_output_ptr(void *state, int id) {
  return &g_output;
}

void shader_entry(void *);

void test_launch(void *_printf) {

  for (int i = 0; i < WAVE_WIDTH; i++) {
    g_input[0][i].x = (float)i;
    g_input[0][i].y = (float)i;
    g_input[0][i].z = (float)i;
    g_input[0][i].w = (float)i;
    g_input[1][i].x = (float)i + 1.0f;
    g_input[1][i].y = (float)i + 2.0f;
    g_input[1][i].z = (float)i + 3.0f;
    g_input[1][i].w = (float)i + 4.0f;
  }

#if DEINTERLEAVE
  deinterleave(&g_input[0][0], &g_input_flat[0][0]);
  deinterleave(&g_input[1][0], &g_input_flat[1][0]);
#endif

  uniforms.vec_4[0] = 2.0f;
  uniforms.vec_4[1] = 2.0f;
  uniforms.vec_4[2] = 3.0f;
  uniforms.vec_4[3] = 4.0f;

  shader_entry(NULL);

#if DEINTERLEAVE
  const size_t stride = 1;
  const size_t x_offset = WAVE_WIDTH * 0;
  const size_t y_offset = WAVE_WIDTH * 1;
  const size_t z_offset = WAVE_WIDTH * 2;
  const size_t w_offset = WAVE_WIDTH * 3;
#else
  const size_t stride = 4;
  const size_t x_offset = 0;
  const size_t y_offset = 1;
  const size_t z_offset = 2;
  const size_t w_offset = 3;
#endif

  for (int i = 0; i < WAVE_WIDTH; i++) {
#define out_x g_output[i * stride + x_offset]
#define out_y g_output[i * stride + y_offset]
#define out_z g_output[i * stride + z_offset]
#define out_w g_output[i * stride + w_offset]
    // ((printf_t)_printf)("[%f %f %f %f]\n", out_x, out_y, out_z, out_w);
    // ((printf_t)_printf)("[(%f+%f)*%f]\n", g_input[0][i], g_input[1][i], uniforms.vec_4.x);
    TEST_EQ(out_x, (g_input[0][i] + g_input[1][i]).x * uniforms.vec_4.x);
    TEST_EQ(out_y, (g_input[0][i] + g_input[1][i]).y * uniforms.vec_4.y);
    TEST_EQ(out_z, (g_input[0][i] + g_input[1][i]).z * uniforms.vec_4.z);
    TEST_EQ(out_w, (g_input[0][i] + g_input[1][i]).w * uniforms.vec_4.w);
  }
  ((printf_t)_printf)("[SUCCESS]\n");
}

}
"""
shader = \
r"""#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec4 param0;
layout(location = 1) in vec4 param1;

layout(set = 0, binding = 0, std140) uniform UBO {
  int pad_0;
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
