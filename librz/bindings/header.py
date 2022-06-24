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
    ]

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

    macros: List[Macro]
    vars: List[Var]
    funcs: List[Func]
    structs: List[Struct]
    enums: List[Enum]
    typedefs: List[Typedef]

    def __init__(self, name: str) -> None:
        self.name = name
        filename = os.path.join(os.path.dirname(__file__), "..", "include", name)

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

        self.macros = []
        self.vars = []
        self.funcs = []
        self.structs = []
        self.enums = []
        self.typedefs = []

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

            if cursor.kind == CursorKind.MACRO_DEFINITION:
                self.macros.append(cursor)
            elif cursor.kind == CursorKind.VAR_DECL:
                self.vars.append(cursor)
            elif cursor.kind == CursorKind.FUNCTION_DECL:
                self.funcs.append(cursor)
            elif cursor.kind == CursorKind.STRUCT_DECL:
                self.structs.append(cursor)
            elif cursor.kind == CursorKind.ENUM_DECL:
                self.enums.append(cursor)
            elif cursor.kind == CursorKind.TYPEDEF_DECL:
                self.typedefs.append(cursor)

            # REMOVE
            elif cursor.kind in [CursorKind.UNION_DECL]:
                pass
            else:
                raise Exception(
                    f"Unexpected toplevel node of kind: {str(cursor.kind)} in file: {self.name}"
                )

            # Add to nodes OrderedDict
            if not cursor.spelling:
                name = f"anonymous_node_{len(self.nodes)}"
            elif cursor.spelling in self.nodes:  # Redefinition
                if cursor.kind == CursorKind.STRUCT_DECL:
                    prev = self.nodes[cursor.spelling]
                    assert prev.kind == CursorKind.STRUCT_DECL
                    assert not any(prev.get_children())  # Should be forward declaration
                else:
                    raise Exception(
                        f"Unexpected redefinition of symbol: {cursor.spelling}, "
                        f"with type: {cursor.kind} in file: {self.name}:{cursor.location.line}"
                    )

            self.nodes[cursor.spelling] = cursor
