runner = \
r"""
#include <test_stdlib.cpp>

extern "C"{

const uint32_t NUM_INVOCATIONS = 128;

void spv_main(void *);

void test_launch(void *_printf) {
  #pragma pack(push,1)
  struct Attributes {
    float3 color;
  };
  #pragma pack(pop)
  static_assert (sizeof(Attributes) == 16, "Invalid packing of Attributes");
  Attributes attributes[NUM_INVOCATIONS * 3];
  float4 pixel_out[NUM_INVOCATIONS];

  for (uint32_t i = 0; i < NUM_INVOCATIONS * 3; i++) {
    attributes[i].color = (float3){(float)i, 0.0f, 0.0f};
    // ((printf_t)_printf)("[%i %i]\n", g_buf_0[i], g_buf_1[i]);
  }
  Invocation_Info info;
  for (uint32_t i = 1; i < sizeof(info); i++)
    ((uint8_t*)&info)[i] = 0;
  info.work_group_size    = (uint3){WAVE_WIDTH, 1, 1};
  info.invocation_count   = (uint3){NUM_INVOCATIONS / WAVE_WIDTH, 1, 1};
  info.subgroup_size      = (uint3){WAVE_WIDTH, 1, 1};
  info.subgroup_x_bits    = 0xff;
  info.subgroup_x_offset  = 0x0;
  info.subgroup_y_bits    = 0x0;
  info.subgroup_y_offset  = 0x0;
  info.subgroup_z_bits    = 0x0;
  info.subgroup_z_offset  = 0x0;

  for (uint32_t i = 0; i < NUM_INVOCATIONS/WAVE_WIDTH; i++) {
    float barycentrics[WAVE_WIDTH * 3] = {};
    for (uint32_t j = 0; j < WAVE_WIDTH; j++) {
      barycentrics[j * 3 + 0] = 0.0f;
      barycentrics[j * 3 + 1] = 0.0f;
      barycentrics[j * 3 + 2] = 1.0f;
    }
    info.invocation_id = (uint3){i, 0, 0};
    info.invocation_id = (uint3){i, 0, 0};
    info.input = &attributes[3 * i * WAVE_WIDTH];
    info.output = &pixel_out[i * WAVE_WIDTH];
    info.barycentrics = barycentrics;
    spv_main(&info);
  }

  for (uint32_t i = 0; i < NUM_INVOCATIONS; i++) {
    // ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
    TEST_EQ(pixel_out[i].x, attributes[3 * i + 2].color.x);
    TEST_EQ(pixel_out[i].y, attributes[3 * i + 2].color.y);
    TEST_EQ(pixel_out[i].z, attributes[3 * i + 2].color.z);
    TEST_EQ(pixel_out[i].w, 4.0f);
  }
  ((printf_t)_printf)("[SUCCESS]\n");
}

}
"""
shader = \
r"""#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main()
{
  outFragColor = vec4(inColor, 4.0);
}
"""
shader_filename = "shader.frag.glsl"
runner_c = open("runner.cpp", "w")
runner_c.write(runner)
runner_c.close()
shader_f = open(shader_filename, "w")
shader_f.write(shader)
shader_f.close()
import subprocess
print_spirv_text = "" #"--spirv-dis"
cmd = "glslangValidator -e main -V " + shader_filename + " -o shader.spv"
#cmd = "glslangValidator -V  " + shader_filename + " > shader.S"
#print("running: " + cmd)
process = subprocess.Popen(cmd.split(' '), stdout=subprocess.PIPE)
stdout, stderr = process.communicate()
stdout = stdout.decode()
print(stdout)
if process.returncode != 0:
  exit(-1)
