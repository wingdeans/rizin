"""
Entrypoint which is called from meson

Responsible for layout and generation of bindings
"""

from argparse import ArgumentParser
from typing import cast
import os

from clang.cindex import Config

from header import Header, HeaderConfig
from binder import Module
from generator import Generator

parser = ArgumentParser()
parser.add_argument(
    "--clang-lib-path",
    required=True,
    help="clang library path (accepts output by `llvm-config --libdir`)",
)
parser.add_argument("-o", "--output-dir", required=True, help="output directory")
parser.add_argument("--clang-args", help="arguments for libclang")
args = parser.parse_args()

Config.set_library_path(cast(str, args.clang_lib_path))
HeaderConfig.set_extra_args(cast(str, args.clang_args))

rizin = Module("rizin")


list_h = Header("rz_list.h")
rz_list_iter = rizin.Generic(
    list_h,
    "rz_list_iter_t",
    ["data", "n", "p"],
    rename="RzListIter",
)

rz_list = rizin.Generic(
    list_h,
    "rz_list_t",
    ["head", "tail"],
    rename="RzList",
)
rz_list.add_constructor(list_h, "rz_list_new")
rz_list.add_constructor(list_h, "rz_list_newf")
rz_list.add_destructor(list_h, "rz_list_free")
rz_list.add_prefixed_methods(list_h, "rz_list_")

core_h = Header("rz_core.h")

"""
rz_core_t
"""
rz_core = rizin.Class(core_h, "rz_core_t", rename="RzCore")
rz_core.add_constructor(core_h, "rz_core_new")
rz_core.add_destructor(core_h, "rz_core_free")

for func_name in [
    # ignore format strings
    "rz_core_notify_begin",
    "rz_core_notify_done",
    "rz_core_notify_error",
    "rz_core_cmd_strf",
    "rz_core_cmdf",
    "rz_core_syscallf",
]:
    core_h.used.add(core_h.nodes[func_name])

rz_core.add_prefixed_methods(core_h, "rz_core_")
rz_core.add_prefixed_funcs(core_h, "rz_core_")

with open(
    os.path.join(cast(str, args.output_dir), "rizin.i"), "w", encoding="utf8"
) as output:
    Generator(rizin).write(output)

# Header("rz_bin.h")
# Header("rz_asm.h")
# Header("rz_analysis.h")
