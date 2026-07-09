hyregalloc_inc = include_directories('include')

libhyregalloc = static_library('hyregalloc',
  'src/hyregalloc.c',
  include_directories : [hyregalloc_inc, hymir_inc],
  dependencies : libhymir_dep,
)

libhyregalloc_dep = declare_dependency(
  link_with : libhyregalloc,
  include_directories : hyregalloc_inc,
)
