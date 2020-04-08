stdlib = \
r"""
#include <test_stdlib.cpp>

extern "C"{

float4 g_input[3] = {
{1.0f, 2.0f, 3.0f, 4.0f},
{1.0f, 2.0f, 3.0f, 4.0f},
{2.0f, 1.0f, 2.0f, 1.0f},
};

#pragma pack(1)
struct UBO {
  float4 vec_4;
  float k;
} uniforms;
#pragma pop

void *get_uniform_ptr(int set, int binding) {
  uniforms.vec_4.x = 1.0f;
  uniforms.vec_4.y = 2.0f;
  uniforms.vec_4.z = 3.0f;
  uniforms.vec_4.w = 4.0f;
  uniforms.k = 3.0f;
  return &uniforms;
}

void *get_input_ptr(int id) {
  return &g_input[id];
}

float4 g_output;

void *get_output_ptr(int id) {
  return &g_output;
}

void shader_entry(void *);

void test_launch(void *_printf) {
  /*fprintf(stdout, "[%f, %f, %f, %f]\n",
  g_output.m[0],
  g_output.m[1],
  g_output.m[2],
  g_output.m[3]
  );*/
  float3 res = {};
  float4 param0 = g_input[0];
  float4 param1 = g_input[1];
  float4 param2 = g_input[2];
  res =
  (param0.xyz * param1.www)
  * spv_dot_f2(uniforms.vec_4.xy, param2.zw)
  * spv_dot_f3((float3){uniforms.k, uniforms.k, uniforms.k}, param2.xxx)
  * spv_dot_f4(param0, param1)
  / spv_length_f4(param0 + param1)
  * spv_sqrt(uniforms.k);
  shader_entry(NULL);
  TEST_EQ(g_output.x, res.x);
  TEST_EQ(g_output.y, res.y);
  TEST_EQ(g_output.z, res.z);
  TEST_EQ(g_output.w, 1.0f);

   ((printf_t)_printf)("[SUCCESS]\n");
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
  * sqrt(uniforms.k)
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
