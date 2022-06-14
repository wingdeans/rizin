from clang.cindex import TranslationUnit, CursorKind
from typing import List, OrderedDict
import os
import logging

from nodes import Node, Macro, Var, Func, Struct, Enum, Typedef

class HeaderConfig:
    extra_args: List[str] = []
    args: List[str] = [
        "-I" + os.path.join(os.path.dirname(__file__), "..", "include"),
        "-I" + os.path.join(os.path.dirname(__file__), "..", "..", "subprojects", "sdb", "src"),
        "-I" + os.path.join(os.path.dirname(__file__), "..", "..", "build")
    ]
    
    @staticmethod
    def set_extra_args(extra_args: str) -> None:
        HeaderConfig.extra_args = extra_args.split(" ")

class Header:
    nodes: OrderedDict[str, Node]
    
    def __init__(self, name: str) -> None:
        filename = os.path.join(os.path.dirname(__file__), "..", "include", name)
        tu = TranslationUnit.from_source(
            filename,
            args = HeaderConfig.extra_args + HeaderConfig.args,
            options = TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        )

        for diag in tu.diagnostics:
            logging.warn(diag)

        self.nodes = OrderedDict()
        for child in tu.cursor.get_children():
            # Skip nodes from other headers
            if not child.location.file or child.location.file.name != filename:
                continue

            # Skip `#include` and macro expansion
            if child.kind in [CursorKind.INCLUSION_DIRECTIVE, CursorKind.MACRO_INSTANTIATION]:
                continue

            mappings = {
                CursorKind.MACRO_DEFINITION: Macro,
                CursorKind.VAR_DECL: Var,
                CursorKind.FUNCTION_DECL: Func,
                CursorKind.STRUCT_DECL: Struct,
                CursorKind.ENUM_DECL: Enum,
                CursorKind.TYPEDEF_DECL: Typedef
            }
            
            if child.kind in mappings:
                node = mappings[child.kind](child)
                self.nodes[node.name] = node
            else:
                raise Exception("Unknown child node of kind: " + str(child.kind))
