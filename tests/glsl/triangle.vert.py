runner = \
r"""
#include <test_stdlib.cpp>

extern "C"{

const uint32_t NUM_INVOCATIONS = 128;

struct UBO
{
  float4 projectionMatrix[4];
  float4 modelMatrix[4];
  float4 viewMatrix[4];
} ubo;

void spv_main(void *);

void test_launch(void *_printf) {
  #pragma pack(push,1)
  struct Attributes {
    float3 pos;
    float3 color;
  };
  #pragma pack(pop)
  static_assert (sizeof(Attributes) == 32, "Invalid packing of Attributes");
  Attributes attributes[NUM_INVOCATIONS];
  float3 colors[NUM_INVOCATIONS];
  float4 gl_PerVertex[NUM_INVOCATIONS];
  ubo.projectionMatrix[0] = (float4){1.0f, 0.0f, 0.0f, 0.0f};
  ubo.projectionMatrix[1] = (float4){0.0f, 1.0f, 0.0f, 0.0f};
  ubo.projectionMatrix[2] = (float4){0.0f, 0.0f, 1.0f, 0.0f};
  ubo.projectionMatrix[3] = (float4){0.0f, 0.0f, 0.0f, 1.0f};

  ubo.modelMatrix[0] = (float4){1.0f, 0.0f, 0.0f, 0.0f};
  ubo.modelMatrix[1] = (float4){0.0f, 1.0f, 0.0f, 0.0f};
  ubo.modelMatrix[2] = (float4){0.0f, 0.0f, 1.0f, 0.0f};
  ubo.modelMatrix[3] = (float4){0.0f, 0.0f, 0.0f, 1.0f};

  ubo.viewMatrix[0] = (float4){1.0f, 0.0f, 0.0f, 0.0f};
  ubo.viewMatrix[1] = (float4){0.0f, 1.0f, 0.0f, 0.0f};
  ubo.viewMatrix[2] = (float4){0.0f, 0.0f, 1.0f, 0.0f};
  ubo.viewMatrix[3] = (float4){0.0f, 0.0f, 0.0f, 1.0f};

  for (uint32_t i = 0; i < NUM_INVOCATIONS; i++) {
    attributes[i].pos = (float3){(float)i, 0.0f, 0.0f};
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
  info.input = &attributes;
  info.output = &colors;
  info.builtin_output = &gl_PerVertex;
  void *descriptor_set_0[] = {NULL};
  descriptor_set_0[0] = &ubo;
  info.descriptor_sets[0] = descriptor_set_0;

  for (uint32_t i = 0; i < NUM_INVOCATIONS/WAVE_WIDTH; i++) {
    info.invocation_id = (uint3){i, 0, 0};
    info.invocation_id = (uint3){i, 0, 0};
    info.input = &attributes[i * WAVE_WIDTH];
    info.output = &colors[i * WAVE_WIDTH];
    info.builtin_output = &gl_PerVertex[i * WAVE_WIDTH];
    spv_main(&info);
  }

  for (uint32_t i = 0; i < NUM_INVOCATIONS; i++) {
    float4 tmp = spv_matrix_times_float_4x4(
      &ubo.projectionMatrix[0],
      spv_matrix_times_float_4x4(
        &ubo.viewMatrix[0],
        spv_matrix_times_float_4x4(
          &ubo.modelMatrix[0],
          (float4){attributes[i].pos.x, attributes[i].pos.y, attributes[i].pos.z, 1.0f}
        )
      )
    );
    // ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
    TEST_EQ(tmp.x, gl_PerVertex[i].x);
    TEST_EQ(tmp.y, gl_PerVertex[i].y);
    TEST_EQ(tmp.z, gl_PerVertex[i].z);
    TEST_EQ(tmp.w, gl_PerVertex[i].w);
  }
  ((printf_t)_printf)("[SUCCESS]\n");
}

}
"""
shader = \
r"""#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;

layout (binding = 0) uniform UBO
{
        mat4 projectionMatrix;
        mat4 modelMatrix;
        mat4 viewMatrix;
} ubo;

layout (location = 0) out vec3 outColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
        outColor = inColor;
        gl_Position = ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
}
"""
shader_filename = "shader.vert.glsl"
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
