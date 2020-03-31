from jinja2 import Template
import sys

module = \
"""

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
