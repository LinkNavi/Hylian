hyx64_inc = include_directories('include')

libhyx64 = static_library('hyx64',
  'src/hyx64.c',
  'src/encode_x64.c',
  'src/lower_x64.c',
  'src/miniasm.c',
  include_directories : [hyx64_inc, hymir_inc, hyregalloc_inc],
  dependencies : [libhymir_dep, libhyregalloc_dep],
)

libhyx64_dep = declare_dependency(
  link_with : libhyx64,
  include_directories : hyx64_inc,
)
