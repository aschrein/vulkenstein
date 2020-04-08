stdlib = \
r"""
#include <test_stdlib.cpp>

extern "C"{


int3 spv_get_global_invocation_id(void *state, uint32_t lane_id) {
  return (int3){0, 0, 0};
}

int3 spv_get_work_group_size(void *state) {
  return (int3){1, 1, 1};
}

float4 simple_value = (float4){666.0f, 777.0f, 0.0f, 1.0f};

float4 spv_image_read_f4(int *img, int2 in_coord) {
  return simple_value;
}

float4 g_res;

void spv_image_write_f4(int *img, int2 in_coord, float4 in_val) {
  g_res = in_val;
}

int in_image = 0;
int out_image = 1;

void *get_uniform_const_ptr(int set, int binding) {
  if (set == 0) return &in_image;
  if (set == 1) return &in_image;
  return NULL;
}

void shader_entry(void *);

void test_launch(void *_printf) {
  // typedef void (*success_t)();

  //success_t func = (success_t)dlsym(parent, "success");

  shader_entry(NULL);

  for (int i = 0; i < WAVE_WIDTH; i++) {
    //fprintf(stdout, "[%f %f %f %f]\n", g_output[i].x, g_output[i].y, g_output[i].z, g_output[i].w);
    //fprintf(stdout, "[(%f+%f)*%f]\n", g_input[0][i], g_input[1][i], uniforms.vec_4.x);
    TEST_EQ(g_res.x, simple_value.x);
    TEST_EQ(g_res.y, simple_value.y);
    TEST_EQ(g_res.z, simple_value.z);
    TEST_EQ(g_res.w, simple_value.w);
  }
  ((printf_t)_printf)("[SUCCESS]\n");
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
