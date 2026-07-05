project('hygen', 'c',
  version : '0.1.0',
  default_options : [
    'c_std=c11',
    'buildtype=release',
    'optimization=3',
    'warning_level=2',
  ]
)

# lto helps a lot for a codegen library made of many small hot functions
add_project_arguments('-flto', language : 'c')
add_project_link_arguments('-flto', language : 'c')

subdir('libhymir')
subdir('tests')
