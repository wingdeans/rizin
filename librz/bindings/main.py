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

"""
RzListIter, RzList
"""
list_h = Header("rz_list.h")
rz_list = rizin.Generic(list_h, "RzList")
rz_list.add_constructor(list_h, "rz_list_new")
rz_list.add_destructor(list_h, "rz_list_free")

vector_h = Header("rz_vector.h")
rz_vector = rizin.Generic(vector_h, "RzVector")
rz_pvector = rizin.Generic(vector_h, "RzPVector")

"""
rz_core_t
"""
core_h = Header("rz_core.h")
rizin.headers.add(Header("rz_cmp.h"))  # RzCoreCmpWatcher
rz_core = rizin.Class(core_h, "RzCore")
rz_core.add_constructor(core_h, "rz_core_new")
rz_core.add_destructor(core_h, "rz_core_free")
rz_core_file = rizin.Class(core_h, "RzCoreFile")

# Ignore format strings
for func_name in [
    "rz_core_notify_begin",
    "rz_core_notify_done",
    "rz_core_notify_error",
    "rz_core_cmd_strf",
    "rz_core_cmdf",
    "rz_core_syscallf",
]:
    assert func_name in core_h.nodes
    core_h.used.add(func_name)

rz_core.add_prefixed_methods(core_h, "rz_core_")
rz_core.add_prefixed_funcs(core_h, "rz_core_")

"""
rz_bin_t
"""
bin_h = Header("rz_bin.h")
rz_bin = rizin.Class(bin_h, "RzBin")
rz_bin.add_prefixed_methods(bin_h, "rz_bin_")
rz_bin.add_prefixed_funcs(bin_h, "rz_bin_")
rz_bin_options = rizin.Class(bin_h, "RzBinOptions")
rz_bin_info = rizin.Class(bin_h, "RzBinInfo")

with open(
    os.path.join(cast(str, args.output_dir), "rizin.i"), "w", encoding="utf8"
) as output:
    rizin.write(output)

# Header("rz_bin.h")
# Header("rz_asm.h")
# Header("rz_analysis.h")
