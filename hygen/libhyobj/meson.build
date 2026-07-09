hyobj_inc = include_directories('include')

libhyobj = static_library('hyobj',
  'src/obj_module.c',
  'src/elf_writer.c',
  include_directories : [hyobj_inc, hyx64_inc, hymir_inc, hyregalloc_inc],
  dependencies : [libhyx64_dep],
)

libhyobj_dep = declare_dependency(
  link_with : libhyobj,
  include_directories : hyobj_inc,
)
