stdlib = \
r"""
#include <stdlib.cpp>

extern "C"{


void spv_get_global_invocation_id(ivec3 *out) {
  *out = ivec3{0, 0, 0};
}

void spv_get_work_group_size(ivec3 *out) {
  *out = ivec3{1, 1, 1};
}

vec4 simple_value = vec4{666.0f, 777.0f, 0.0f, 1.0f};

void spv_image_read_f4(int *img, ivec2 *in_coord, vec4 *out_val) {
  *out_val = simple_value;
}

vec4 g_res;

void spv_image_write_f4(int *img, ivec2 *in_coord, vec4 *in_val) {
  g_res = *in_val;
}

int in_image = 0;
int out_image = 1;

void *get_uniform_const_ptr(int set, int binding) {
  if (set == 0) return &in_image;
  if (set == 1) return &in_image;
  TEST(false);
  exit(-1);
}

void spv_on_exit() {
  /*
  fprintf(stdout, "[%f, %f, %f, %f]\n",
    g_res.x, g_res.y, g_res.z, g_res.w
  );
  */
  TEST_EQ(g_res.x, simple_value.x);
  TEST_EQ(g_res.y, simple_value.y);
  TEST_EQ(g_res.z, simple_value.z);
  TEST_EQ(g_res.w, simple_value.w);

  fprintf(stdout, "[SUCCESS]\n");
}

}
"""
shader = r"""#version 450
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout (set = 0, binding = 0, rgba32f) uniform readonly image2D in_image;
layout (set = 1, binding = 0, rgba32f) uniform image2D out_image;

void main() {
  vec4 loaded_val = imageLoad(in_image, ivec2(gl_GlobalInvocationID.xy));
  imageStore(out_image, ivec2(gl_GlobalInvocationID.xy), loaded_val);
}
"""
shader_filename = "shader.comp.glsl"
stdlib_c = open("stdlib.cpp", "w")
stdlib_c.write(stdlib)
stdlib_c.close()
shader_f = open(shader_filename, "w")
shader_f.write(shader)
shader_f.close()
import subprocess
cmd = "glslangValidator -V " + shader_filename + " -o shader.spv"
process = subprocess.Popen(cmd.split(' '), stdout=subprocess.PIPE)
stdout, stderr = process.communicate()
stdout = stdout.decode()
print(stdout)
if process.returncode != 0:
  exit(-1)
