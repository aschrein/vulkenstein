from jinja2 import Template
import sys

subgroup_width = int(sys.argv[2])

module = \
"""

"""
module += open(sys.argv[1]).read()
utils  =  Template(\
"""
%float = type <{{1 * M}} x float>
%float2 = type <{{2 * M}} x float>
%float3 = type <{{3 * M}} x float>
%float4 = type <{{4 * M}} x float>

%int = type <{{1 * M}} x i32>
%int2 = type <{{2 * M}} x i32>
%int3 = type <{{3 * M}} x i32>
%int4 = type <{{4 * M}} x i32>

%half = type <{{1 * M}} x half>
%half2 = type <{{2 * M}} x half>
%half3 = type <{{3 * M}} x half>
%half4 = type <{{4 * M}} x half>

%double = type <{{1 * M}} x double>
%double2 = type <{{2 * M}} x double>
%double3 = type <{{3 * M}} x double>
%double4 = type <{{4 * M}} x double>

%float2x2 = type [2 x %float2]
%float2x3 = type [2 x %float3]
%float2x4 = type [2 x %float4]

%float3x2 = type [3 x %float2]
%float3x3 = type [3 x %float3]
%float3x4 = type [3 x %float4]

%float4x2 = type [4 x %float2]
%float4x3 = type [4 x %float3]
%float4x4 = type [4 x %float4]

define %float4 @spv_add_float4(%float4 %a, %float4 %b) {
  %res = fadd %float4 %a, %b
  ret %float4 %res
}

define %float4 @spv_sub_float4(%float4 %a, %float4 %b) {
  %res = fsub %float4 %a, %b
  ret %float4 %res
}

{% for n in range(2, 5) %}\
define %float @spv_dot_float{{n}}(%float{{n}} %a, %float{{n}} %b) {
  %mul = fmul %float{{n}} %a, %b
  %init_0 = insertelement %float undef , float 0.0, i32 0
  %res_0 = shufflevector %float %init_0, %float undef, <{{M}} x i32> zeroinitializer
{% for i in range(0, n) %}\
  %sh_{{i}} = shufflevector %float{{n}} %mul, %float{{n}} undef, <{{M}} x i32> \
<i32 {{i}}{% for j in range(1, M) %}, i32 {{i + j * n}}{% endfor %}>
  %res_{{i+1}} = fadd %float %sh_{{i}}, %res_{{i}}
{% endfor %}\
  ret %float %res_{{n}}
}
{% endfor %}\

""")

"""
matrices are packed with SOA
[lane_0_row_0, lane_1_row_0 .. lane_M-1_row_0] .. [lane_0_row_N-1 .. lane_M-1_row_N-1]
so that it's easier to multiply with a vector which is packed as:
[lane_0_row_0 .. lane_N-1_row_0] vector has only row_0
"""

mat_times_vec = Template(\
"""
define %float{{N}} @spv_mat{{K}}x{{N}}_times_float{{N}}(%float{{K}}x{{N}} %a, %float{{N}} %b) {
  %res_0 = insertvalue [{{K}} x %float] undef, %float undef, 0
{% for n in range(0, K) %}\
  %row_{{n}} = extractvalue %float{{K}}x{{N}} %a, {{n}}
  %dot_{{n}} = call %float @spv_dot_float{{N}}(%float{{N}} %row_{{n}}, %float{{N}} %b)
  %res_{{n+1}} = insertvalue [{{K}} x %float] %res_{{n}}, %float %dot_{{n}}, {{n}}
{% endfor %}\
  %res = alloca %float{{N}}
  %res_arr = bitcast %float{{N}} *%res to [{{K}} x %float] *
  store [{{K}} x %float] %res_{{K}}, [{{K}} x %float] *%res_arr
  %final = load %float{{N}}, %float{{N}} *%res
  ret %float{{N}} %final
}
""")

module += utils.render(M=subgroup_width)
for N in [2, 3, 4]:
  for K in [2, 3, 4]:
    module += mat_times_vec.render(M=subgroup_width, N=N, K=K)
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
