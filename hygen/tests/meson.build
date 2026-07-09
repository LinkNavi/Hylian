test_mir = executable('test_mir',
  'testmir.c',
  dependencies : libhymir_dep,
)

test('mir', test_mir)

test_regalloc = executable('test_regalloc',
  'test_regalloc.c',
  dependencies : [libhymir_dep, libhyregalloc_dep],
)

test('regalloc', test_regalloc)

test_x64 = executable('test_x64',
  'test_x64.c',
  dependencies : [libhymir_dep, libhyregalloc_dep, libhyx64_dep],
)

test('x64', test_x64)

test_obj = executable('test_obj',
  'test_obj.c',
  dependencies : [libhymir_dep, libhyregalloc_dep, libhyx64_dep, libhyobj_dep],
)

test('obj', test_obj)

test_syscall = executable('test_syscall',
  'test_syscall.c',
  dependencies : [libhymir_dep, libhyregalloc_dep, libhyx64_dep, libhyobj_dep],
)

test('syscall', test_syscall)

test_miniasm = executable('test_miniasm',
  'test_miniasm.c',
  dependencies : [libhymir_dep, libhyregalloc_dep, libhyx64_dep, libhyobj_dep],
)

test('miniasm', test_miniasm)

test_floats = executable('test_floats',
  'test_floats.c',
  dependencies : [libhymir_dep, libhyregalloc_dep, libhyx64_dep, libhyobj_dep],
)

test('floats', test_floats)
