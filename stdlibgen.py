from jinja2 import Template
import sys

subgroup_size = int(sys.argv[2])

module = \
"""

"""
module += open(sys.argv[1]).read()
utils  =  Template(\
"""
%float_t = type <{{1 * M}} x float>
%float2_t = type <{{2 * M}} x float>
%float3_t = type <{{3 * M}} x float>
%float4_t = type <{{4 * M}} x float>

%int_t = type <{{1 * M}} x i32>
%int2_t = type <{{2 * M}} x i32>
%int3_t = type <{{3 * M}} x i32>
%int4_t = type <{{4 * M}} x i32>

%half_t = type <{{1 * M}} x half>
%half2_t = type <{{2 * M}} x half>
%half3_t = type <{{3 * M}} x half>
%half4_t = type <{{4 * M}} x half>

%double_t = type <{{1 * M}} x double>
%double2_t = type <{{2 * M}} x double>
%double3_t = type <{{3 * M}} x double>
%double4_t = type <{{4 * M}} x double>

%float2x2_t = type [2 x %float2_t]
%float2x3_t = type [2 x %float3_t]
%float2x4_t = type [2 x %float4_t]

%float3x2_t = type [3 x %float2_t]
%float3x3_t = type [3 x %float3_t]
%float3x4_t = type [3 x %float4_t]

%float4x2_t = type [4 x %float2_t]
%float4x3_t = type [4 x %float3_t]
%float4x4_t = type [4 x %float4_t]

define %float4_t @spv_add_float4(%float4_t %a, %float4_t %b) {
  %res = fadd %float4_t %a, %b
  ret %float4_t %res
}

define %float4_t @spv_sub_float4(%float4_t %a, %float4_t %b) {
  %res = fsub %float4_t %a, %b
  ret %float4_t %res
}

{% for n in range(2, 5) %}\
define %float_t @spv_dot_float{{n}}(%float{{n}}_t %a, %float{{n}}_t %b) {
  %mul = fmul %float{{n}}_t %a, %b
  %init_0 = insertelement %float_t undef , float 0.0, i32 0
  %res_0 = shufflevector %float_t %init_0, %float_t undef, <{{M}} x i32> zeroinitializer
{% for i in range(0, n) %}\
  %sh_{{i}} = shufflevector %float{{n}}_t %mul, %float{{n}}_t undef, <{{M}} x i32> \
<i32 {{i}}{% for j in range(1, M) %}, i32 {{i + j * n}}{% endfor %}>
  %res_{{i+1}} = fadd %float_t %sh_{{i}}, %res_{{i}}
{% endfor %}\
  ret %float_t %res_{{n}}
}
{% endfor %}\

""")


#  matrices are packed with SOA(ideally)
#  [lane_0_row_0, lane_1_row_0 .. lane_M-1_row_0] .. [lane_0_row_N-1 .. lane_M-1_row_N-1]
#  so that it's easier to multiply with a vector which is packed as:
#  [lane_0_row_0 .. lane_N-1_row_0] vector has only row_0
#  matrices are read from memeory as AOS:
#  [lane_0_row_0, lane_0_row_1, .. lane_0_row_N-1] .. [lane_M-1_row_0 .. lane_M-1_row_N-1]
#  TODO: implement matrix*vector and maybe matrix aos -> sao transformation


mat_times_vec = Template(\
"""
define %float{{N}}_t @spv_mat{{K}}x{{N}}_soa_times_float{{N}}(%float{{K}}x{{N}}_t %a, %float{{N}}_t %b) {
  %res_0 = insertvalue [{{K}} x %float_t] undef, %float_t undef, 0
{% for n in range(0, K) %}\
  %row_{{n}} = extractvalue %float{{K}}x{{N}}_t %a, {{n}}
  %dot_{{n}} = call %float_t @spv_dot_float{{N}}(%float{{N}}_t %row_{{n}}, %float{{N}}_t %b)
  %res_{{n+1}} = insertvalue [{{K}} x %float_t] %res_{{n}}, %float_t %dot_{{n}}, {{n}}
{% endfor %}\
  %res = alloca %float{{N}}_t
  %res_arr = bitcast %float{{N}}_t *%res to [{{K}} x %float_t] *
  store [{{K}} x %float_t] %res_{{K}}, [{{K}} x %float_t] *%res_arr
  %final = load %float{{N}}_t, %float{{N}}_t *%res
  ret %float{{N}}_t %final
}
""")

module += utils.render(M=subgroup_size)
for N in [2, 3, 4]:
  for K in [2, 3, 4]:
    module += mat_times_vec.render(M=subgroup_size, N=N, K=K)
init_funs = Template(\
"""
define <{{N}} x {{type}}> \
@{{type}}x{{N}}_init({{type}} %in0\
{% for n in range(1, N) %}\
, {{type}} %in{{n}}\
{% endfor %}\
) {
allocas:
  %insert.0 = insertelement <{{N}} x {{type}}>  undef, {{type}} %in0, i32 0
{% for n in range(1, N) %}\
  %insert.{{n}} = insertelement <{{N}} x  {{type}}> %insert.{{n-1}}, {{type}} %in{{n}}, i32 {{n}}
{% endfor %}\
  ret <{{N}} x {{type}}> %insert.{{N-1}}
}
"""
)
#for type in ["float", "double", "i32", "i64"]:
#  for i in [1, 2, 3, 4]:
#    module += init_funs.render(N=i, type=type)
print(module)
