stdlib = \
r"""
#include <stdlib.cpp>

extern "C"{

vec4 g_input[3] = {
{1.0f, 2.0f, 3.0f, 4.0f},
{1.0f, 2.0f, 3.0f, 4.0f},
{2.0f, 1.0f, 2.0f, 1.0f},
};

#pragma pack(1)
struct UBO {
  vec4 vec_4;
  float k;
} uniforms;
#pragma pop

void *get_uniform_ptr(int set, int binding) {
  uniforms.vec_4.m[0] = 1.0f;
  uniforms.vec_4.m[1] = 2.0f;
  uniforms.vec_4.m[2] = 3.0f;
  uniforms.vec_4.m[3] = 4.0f;
  uniforms.k = 3.0f;
  return &uniforms;
}

void *get_input_ptr(int id) {
  return &g_input[id];
}

vec4 g_output;

void *get_output_ptr(int id) {
  return &g_output;
}

void spv_on_exit() {
  /*fprintf(stdout, "[%f, %f, %f, %f]\n",
  g_output.m[0],
  g_output.m[1],
  g_output.m[2],
  g_output.m[3]
  );*/
  vec3 res = {};
  res = mul(
          vec3{g_input[0].m[0], g_input[0].m[1], g_input[0].m[2]},
          vec3{g_input[1].m[3], g_input[1].m[3], g_input[1].m[3]}
        );
  vec2 uniforms_vec_4_xy = vec2{uniforms.vec_4.m[0], uniforms.vec_4.m[1]};
  vec2 param2_zw = vec2{g_input[2].m[2], g_input[2].m[3]};
  res = mul(res, spv_dot_f2(&uniforms_vec_4_xy, &param2_zw));
  vec3 uniforms_kkk = vec3{uniforms.k, uniforms.k, uniforms.k};
  vec3 param2_xxx = vec3{g_input[2].m[0], g_input[2].m[0], g_input[2].m[0]};
  res = mul(res, spv_dot_f3(&uniforms_kkk, &param2_xxx));
  res = mul(res, spv_dot_f4(&g_input[0], &g_input[1]));
  vec4 param0_param1 = add(g_input[0], g_input[1]);
  res = mul(res, 1.0f/length_f4(&param0_param1));

  TEST_EQ(g_output.m[0], res.m[0]);
  TEST_EQ(g_output.m[1], res.m[1]);
  TEST_EQ(g_output.m[2], res.m[2]);
  TEST_EQ(g_output.m[3], 1.0f);

  fprintf(stdout, "[SUCCESS]\n");
}

}
"""
shader = \
r"""#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec4 param0;
layout(location = 1) in vec4 param1;
layout(location = 2) in vec4 param2;

layout(set = 0, binding = 0, std140) uniform UBO {
  vec4 vec_4;
  float k;
} uniforms;

layout(location = 0) out vec4 result0;

void main() {
  result0.xyz =
  (param0.xyz * param1.www)
  * dot(uniforms.vec_4.xy, param2.zw)
  * dot(vec3(uniforms.k), param2.xxx)
  * dot(param0, param1)
  / length(param0 + param1)
  ;
  result0.w = 1.0;
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
print_spirv_text = "" #"--spirv-dis"
cmd = "glslangValidator -V " + shader_filename + " -o shader.spv"
#cmd = "glslangValidator -V  " + shader_filename + " > shader.S"
#print("running: " + cmd)
process = subprocess.Popen(cmd.split(' '), stdout=subprocess.PIPE)
stdout, stderr = process.communicate()
stdout = stdout.decode()
print(stdout)
if process.returncode != 0:
  exit(-1)
