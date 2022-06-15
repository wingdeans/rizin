clang_lib_path = get_option('clang_lib_path')

if clang_lib_path == ''
  clang_lib_path = run_command(find_program('llvm-config'), '--libdir').stdout().strip()
endif

clang_args = get_option('clang_extra_args')

swig_modules = [
  'core'
]

swig_sources = custom_target(
  'generate_swig_sources',
  input: 'main.py',
  output: 'core.i',
  command: [
    find_program('python3'), '@INPUT@',
    '-o', '@OUTDIR@',
    '--clang-lib-path', clang_lib_path,
  ] + (clang_args == '' ? [] : [ '--clang-args', clang_args ]),
  depend_files: [ rz_userconf_h ]
)

swig_generator = generator(
  find_program('swig'),
  output: '@BASENAME@_wrap.c',
  arguments: ['-python', '-o', '@OUTPUT@', '@INPUT@']
)

pymod = import('python')
py_installation = pymod.find_installation('python')

swig_sources_list = swig_sources.to_list()
assert(swig_modules.length() == swig_sources_list.length(), 'swig_sources and swig_modules are not the same length')

i = 0 # hacky
foreach swig_source : swig_sources_list
  py_installation.extension_module(
    '_' + swig_modules[i],
    custom_target(
      'generate_swig_output_' + swig_modules[i],
      input: swig_sources,
      output: swig_modules[i] + '_wrap.c',
      command: [ find_program('swig'), '-python', '@INPUT@' ]
    ),
    dependencies: [py_installation.dependency(), rz_main_dep]
  )
  i += 1
endforeach
