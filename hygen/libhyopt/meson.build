hyopt_inc = include_directories('include')

libhyopt = static_library('hyopt',
  'src/hyopt.c',
  include_directories : [hyopt_inc, hymir_inc],
  dependencies : libhymir_dep,
)

libhyopt_dep = declare_dependency(
  link_with : libhyopt,
  include_directories : hyopt_inc,
)
