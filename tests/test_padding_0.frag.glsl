#version 450
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
