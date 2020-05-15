runner = \
r"""
#include <test_stdlib.cpp>

extern "C"{

const uint32_t NUM_INVOCATIONS = 128;

void spv_main(void *, uint64_t);

void test_launch(void *_printf) {
  uint32_t g_buf_0[NUM_INVOCATIONS];
  uint32_t g_buf_1[NUM_INVOCATIONS];
  for (uint32_t i = 0; i < NUM_INVOCATIONS; i++) {
    g_buf_1[i] = i & 1;
    g_buf_0[i] = 100500;
  }
  Invocation_Info info;
  for (uint32_t i = 0; i < sizeof(info); i++)
    ((uint8_t*)&info)[i] = 0;
  info.work_group_size    = (uint3){256, 1, 1};
  info.invocation_count   = (uint3){NUM_INVOCATIONS / 256, 1, 1};
  info.subgroup_size      = (uint3){WAVE_WIDTH, 1, 1};
  info.subgroup_x_bits    = 0xff;
  info.subgroup_x_offset  = 0x0;
  info.subgroup_y_bits    = 0x0;
  info.subgroup_y_offset  = 0x0;
  info.subgroup_z_bits    = 0x0;
  info.subgroup_z_offset  = 0x0;
    info.print_fn = _printf;
    info.wave_width = WAVE_WIDTH;
  void *descriptor_set_0[] = {NULL, NULL};
  Image img_0;
  Image img_1;
  img_0.data = (uint8_t *)(void*)&g_buf_0;
  img_0.bpp = 4;
  img_1.data = (uint8_t *)(void*)&g_buf_1;
  img_1.bpp = 4;
  void *images[] = {&img_0, &img_1};
  descriptor_set_0[0] = &images[0];
  descriptor_set_0[1] = &images[1];

  info.descriptor_sets[0] = descriptor_set_0;

  for (uint32_t i = 0; i < NUM_INVOCATIONS/WAVE_WIDTH; i++) {
    info.invocation_id = (uint3){i, 0, 0};
    spv_main(&info, ~0ull);
  }

  for (uint32_t i = 0; i < NUM_INVOCATIONS; i++) {
    TEST_EQ(g_buf_0[i], (i & 1) ? 2 : 100500);
  }
  ((printf_t)_printf)("[SUCCESS]\n");
}

}
"""
shader = \
r"""
[[vk::binding(0, 0)]] RWBuffer <uint> g_buf_0;
[[vk::binding(1, 0)]] RWBuffer <uint> g_buf_1;
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
  int initial = g_buf_0[tid.x];
  int val = initial;
  if (g_buf_1[tid.x] > 0) {
    g_buf_0[tid.x] = 2;
    return;
  } else {
  }
  g_buf_0[tid.x] = initial;
}
"""
shader_filename = "shader.comp.hlsl"
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

#[[vk::binding(0, 0)]] RWBuffer <uint> g_buf_0;
#[[vk::binding(1, 0)]] RWBuffer <uint> g_buf_1;


#// uint get_num(uint t) {
#//   if (t < 888) {
#//     while (t < 666) {
#//       t = t + 1;
#//       if (t == 444)
#//         continue;
#//       t = t * (t - 1) + 1;
#//       if (t > 100500)
#//         return t;
#//       if (t == 777)
#//         break;
#//
#//     }
#//     t = t / 44;
#//     return t;
#//   } else {
#//     return t + t * t * t;
#//   }
#// }


#[numthreads(4, 1, 1)]
#void main(uint3 tid : SV_DispatchThreadID)
#{
#  // int val = 666;
#  // if (g_buf_1[tid.x] > 0) {
#  //   val = 1;
#  //   for (uint i = 0; i < g_buf_1[tid.x]; i++) {
#  //     val += g_buf_1[tid.x + i];
#  //   }
#  // } else {
#  //   val = (int)get_num(g_buf_1[tid.x]);
#  // }
#  int t = g_buf_1[tid.x];
#  for (uint i = 20; i < t; i++) {
#    t = t + t * (t / 2 + 1) * (t / 3 + 2);
#  }
#  t = t + t ^ 0xffff70u;
#  g_buf_0[tid.x] = t;
#}
