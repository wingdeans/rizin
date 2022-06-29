"""
Parses a rizin header
"""

from typing import List, OrderedDict, Set
import os

from clang.cindex import TranslationUnit
from clang.wrapper import CursorKind, Cursor, Macro, Var, Func, Struct, Enum, Typedef


class HeaderConfig:
    """
    Configuration singleton
    """

    extra_args: List[str] = []
    args: List[str] = [
        "-I" + os.path.join(os.path.dirname(__file__), *segments)
        for segments in [
            ["..", "include"],
            ["..", "..", "subprojects", "sdb", "src"],
            ["..", "..", "build"],
        ]
    ] + ["-DRZ_BINDINGS"]

    @staticmethod
    def set_extra_args(extra_args: str) -> None:
        HeaderConfig.extra_args = extra_args.split(" ")


class Header:
    """
    Contains nodes in a header, combined as well as sorted by type
    """

    name: str
    nodes: OrderedDict[str, Cursor]
    used: Set[str]

    # Specific node types
    macros: OrderedDict[str, Macro]
    variables: OrderedDict[str, Var]
    funcs: OrderedDict[str, Func]
    structs: OrderedDict[str, Struct]
    enums: OrderedDict[str, Enum]
    typedefs: OrderedDict[str, Typedef]

    def __init__(self, headername: str) -> None:
        self.name = headername
        filename = os.path.join(os.path.dirname(__file__), "..", "include", headername)

        try:
            translation_unit = TranslationUnit.from_source(
                filename,
                args=HeaderConfig.extra_args + HeaderConfig.args,
                options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
            )
        except:
            raise Exception(f"Error parsing translation unit from file: {self.name}")

        for diag in translation_unit.diagnostics:
            if diag.spelling in [
                "'openssl/bn.h' file not found",
                "'sdb_version.h' file not found",
            ]:
                continue
            print(diag)

        self.used = set()
        self.nodes = OrderedDict()

        self.macros = OrderedDict()
        self.variables = OrderedDict()
        self.funcs = OrderedDict()
        self.structs = OrderedDict()
        self.enums = OrderedDict()
        self.typedefs = OrderedDict()

        for cursor in translation_unit.cursor.get_children():
            # Skip nodes from other headers
            if not cursor.location.file or cursor.location.file.name != filename:
                continue

            # Skip `#include` and macro expansion
            if cursor.kind in [
                CursorKind.INCLUSION_DIRECTIVE,
                CursorKind.MACRO_INSTANTIATION,
            ]:
                continue

            # Rename anonymous declarations and check for redefinitions
            name = cursor.spelling
            if not name:
                name = f"anonymous_node_{len(self.nodes)}"
            elif cursor.spelling in self.nodes:  # Redefinition
                if cursor.kind == CursorKind.STRUCT_DECL:
                    prev = self.nodes[cursor.spelling]
                    assert prev.kind == CursorKind.STRUCT_DECL
                    assert not any(prev.get_children())  # Should be forward declaration
                else:
                    raise Exception(
                        f"Unexpected redefinition of symbol: {cursor.spelling}, "
                        f"with type: {cursor.kind} in header: {headername}:{cursor.location.line}"
                    )

            # Add to nodes OrderedDict
            self.nodes[cursor.spelling] = cursor

            # Add to specific node kind OrderedDict
            if cursor.kind == CursorKind.MACRO_DEFINITION:
                self.macros[name] = cursor
            elif cursor.kind == CursorKind.VAR_DECL:
                self.variables[name] = cursor
            elif cursor.kind == CursorKind.FUNCTION_DECL:
                self.funcs[name] = cursor
            elif cursor.kind == CursorKind.STRUCT_DECL:
                self.structs[name] = cursor
            elif cursor.kind == CursorKind.ENUM_DECL:
                self.enums[name] = cursor
            elif cursor.kind == CursorKind.TYPEDEF_DECL:
                self.typedefs[name] = cursor
            else:
                raise Exception(
                    f"Unexpected toplevel node of kind: {str(cursor.kind)} in file: {self.name}"
                )
