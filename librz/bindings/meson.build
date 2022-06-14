clang_lib_path = get_option('clang_lib_path')

if clang_lib_path == ''
  clang_lib_path = run_command(find_program('llvm-config'), '--libdir').stdout().strip()
endif

clang_args = get_option('clang_extra_args')

custom_target(
  'generate_swig',
  input: 'generate_swig.py',
  output: 'bindings.i',
  command: [
    find_program('python3'), '@INPUT@',
    '-o', '@OUTPUT@',
    '--clang-lib-path', clang_lib_path,
  ] + (clang_args == '' ? [] : [ '--clang-args', clang_args ]),
  depend_files: [ rz_userconf_h ]
)
