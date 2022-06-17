from clang.cindex import TranslationUnit
from typing import List, OrderedDict, Set
import os

from clang.wrapper import CursorKind, Cursor, Macro, Var, Func, Struct, Enum, Typedef

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
        tu = TranslationUnit.from_source(
            filename,
            args = HeaderConfig.extra_args + HeaderConfig.args,
            options = TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        )

        for diag in tu.diagnostics:
            print(diag)

        self.used = set()
        self.nodes = OrderedDict()

        self.macros = []
        self.vars = []
        self.funcs = []
        self.structs = []
        self.enums = []
        self.typedefs = []
        
        for cursor in tu.cursor.get_children():
            # Skip nodes from other headers
            if not cursor.location.file or cursor.location.file.name != filename:
                continue

            # Skip `#include` and macro expansion
            if cursor.kind in [CursorKind.INCLUSION_DIRECTIVE, CursorKind.MACRO_INSTANTIATION]:
                continue

            # Macro
            elif cursor.kind == CursorKind.MACRO_DEFINITION:
                self.macros.append(cursor)

            # Variable
            elif cursor.kind == CursorKind.VAR_DECL:
                self.vars.append(cursor)

            # Function
            elif cursor.kind == CursorKind.FUNCTION_DECL:
                self.funcs.append(cursor)

            # Struct
            elif cursor.kind == CursorKind.STRUCT_DECL:
                self.structs.append(cursor)

            # Enum
            elif cursor.kind == CursorKind.ENUM_DECL:
                self.enums.append(cursor)

            # Typedef
            elif cursor.kind == CursorKind.TYPEDEF_DECL:
                self.typedefs.append(cursor)
                
            else:
                raise Exception(f"Unexpected toplevel node of kind: {str(cursor.kind)}")

            # Add to nodes OrderedDict
            if not cursor.spelling:
                name = f"anonymous_node_{len(self.nodes)}"
            elif cursor.spelling in self.nodes: # Redefinition
                if cursor.kind == CursorKind.STRUCT_DECL:
                    prev = self.nodes[cursor.spelling]
                    assert prev.kind == CursorKind.STRUCT_DECL
                    assert not any(prev.get_children()) # Should be forward declaration
                else:
                    raise Exception(f"Unexpected redefinition of symbol: {cursor.spelling}, "
                                    f"with type: {cursor.kind}")
                
            self.nodes[cursor.spelling] = cursor
