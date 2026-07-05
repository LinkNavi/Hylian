hymir_inc = include_directories('include')

libhymir = static_library('hymir',
  'src/hymir.c',
  include_directories : hymir_inc,
  c_args : ['-D_POSIX_C_SOURCE=200809L'],
)

libhymir_dep = declare_dependency(
  link_with : libhymir,
  include_directories : hymir_inc,
)
