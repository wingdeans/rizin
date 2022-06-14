from clang.cindex import Config
from argparse import ArgumentParser

from header import Header, HeaderConfig

parser = ArgumentParser()
parser.add_argument("--clang-lib-path", required=True,
                    help="clang library path (accepts output by `llvm-config --libdir`)")
parser.add_argument("-o", "--output-file", required=True, help="output file")
parser.add_argument("--clang-args", help="arguments for libclang")
args = parser.parse_args()

Config.set_library_path(args.clang_lib_path)
HeaderConfig.set_extra_args(args.clang_args)

Header("rz_analysis.h")
