from clang.cindex import Config
from argparse import ArgumentParser
from typing import cast
import os

from header import Header, HeaderConfig
from binder import Module, FuncType
from generator import generate

parser = ArgumentParser()
parser.add_argument("--clang-lib-path", required=True,
                    help="clang library path (accepts output by `llvm-config --libdir`)")
parser.add_argument("-o", "--output-dir", required=True, help="output directory")
parser.add_argument("--clang-args", help="arguments for libclang")
args = parser.parse_args()

Config.set_library_path(cast(str, args.clang_lib_path))
HeaderConfig.set_extra_args(cast(str, args.clang_args))

core_h = Header("rz_core.h")
core = Module("core")
rz_core = core.Class(core_h, "rz_core_t")

rz_core.add_constructor(core_h, "rz_core_new")
rz_core.add_destructor(core_h, "rz_core_free")

# ignore format strings
for func_name in ["rz_core_notify_begin", "rz_core_notify_done", "rz_core_notify_error",
        "rz_core_cmd_strf", "rz_core_cmdf", "rz_core_syscallf"]:
    core_h.used.add(func_name)

# undefined symbols (?)
for func_name in ["rz_core_pseudo_code", "rz_core_echo", "rz_core_config_eval_and_print"]:
    core_h.used.add(func_name)
    
rz_core.add_prefixed_methods(core_h, "rz_core_")

with open(os.path.join(cast(str, args.output_dir), "core.i"), "w") as output:
    generate(core).write(output)

# Header("rz_bin.h")
# Header("rz_asm.h")
# Header("rz_analysis.h")
