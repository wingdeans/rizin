from header import Header
from clang.wrapper import TypeKind, CursorKind, Cursor, Struct, Func
from enum import Enum

from typing import List, Set, Optional

class FuncType(Enum):
    FORWARD      = 0 # Every argument is unchanged (default)
    THIS         = 1 # Uses a class instance's `this` as the first argument
    CONSTRUCTOR  = 2
    DESTRUCTOR   = 3

class Module:
    class BinderClass:
        class BinderFunc:
            func: Func
            name: str
            type: FuncType
            
            def __init__(self, header: Header, func: Func,
                         rename: Optional[str]=None,
                         type: Optional[FuncType]=None):
                self.func = func
                self.name = rename or self.func.spelling
                self.type = type or FuncType.FORWARD
        
        struct: Struct
        funcs: List[BinderFunc]
        
        def __init__(self, outer: "Module", header: Header, struct_name: str):
            struct = header.nodes[struct_name]
            assert struct.kind == CursorKind.STRUCT_DECL
            self.struct = struct
            self.funcs = []

        def add_func(self, header: Header, func_name: str, *,
                     rename: Optional[str]=None,
                     type: Optional[FuncType]=None) -> None:
            func = header.nodes[func_name]
            assert func.kind == CursorKind.FUNCTION_DECL
            
            header.used.add(func.spelling)
            self.funcs.append(Module.BinderClass.BinderFunc(header, func, rename=rename, type=type))

        def add_constructor(self, header: Header, func_name: str) -> None:
            self.add_func(header, func_name, rename=self.struct.spelling, type=FuncType.CONSTRUCTOR)

        def add_destructor(self, header: Header, func_name: str) -> None:
            self.add_func(header, func_name, rename="~" + self.struct.spelling, type=FuncType.DESTRUCTOR)

        def add_prefixed_methods(self, header: Header, prefix: str) -> None:
            def predicate(cursor: Cursor) -> bool:
                if cursor.spelling in header.used: return False # not used
                if cursor.kind != CursorKind.FUNCTION_DECL: return False # is function
                if not cursor.spelling.startswith(prefix): return False # correct prefix

                args = list(cursor.get_arguments())
                if len(args) == 0: return False
                
                arg = args[0]
                assert arg.kind == CursorKind.PARM_DECL

                if arg.type.kind != TypeKind.POINTER: return False
                return arg.type.get_pointee().get_canonical() == self.struct.type

            for func in filter(predicate, header.nodes.values()):
                assert func.kind == CursorKind.FUNCTION_DECL
                self.funcs.append(Module.BinderClass.BinderFunc(header, func, type=FuncType.THIS,
                                                                rename=func.spelling[len(prefix):]))
            
    name: str
    headers: Set[Header]
    classes: List[BinderClass]

    def __init__(self, name: str):
        self.name = name
        self.headers = set()
        self.classes = []

    def Class(self, header: Header, struct_name: str) -> BinderClass:
        self.headers.add(header)
        
        result = Module.BinderClass(self, header, struct_name)
        self.classes.append(result)
        return result
