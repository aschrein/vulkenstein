from jinja2 import Template
import sys

module = \
"""
%Render_Target = type { i32 }
%Sampler2D = type { i32, i32 }

declare void @store_rt_2d_f4(%Render_Target* %0, <2 x i32> %coord, <4 x float> %val)
declare <4 x float> @sample_2d_f4(%Sampler2D* %0, <2 x float> %uv)

"""
module += open(sys.argv[1]).read()
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
for type in ["float", "double", "i32", "i64"]:
  for i in [1, 2, 3, 4]:
    module += init_funs.render(N=i, type=type)
print(module)