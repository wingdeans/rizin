from clang.cindex import TypeKind

from header import Header, Query
from nodes import Node, Struct, Func
from enum import Enum

from typing import List, Set, Optional

class FuncType(Enum):
    FORWARD      = 0 # Every argument is unchanged (default)
    THIS         = 1 # Uses a class instance's `this` as the first argument
    CONSTRUCTOR  = 2
    DESTRUCTOR   = 3

class Module:
    class _Class:
        class _Func:
            func: Func
            name: str
            type: FuncType
            
            def __init__(self, header: Header, func: Func,
                         rename: Optional[str]=None,
                         type: Optional[FuncType]=None):
                self.func = func
                self.name = rename or self.func.name
                self.type = type or FuncType.FORWARD
        
        struct: Struct
        funcs: List[_Func]
        
        def __init__(self, outer: "Module", header: Header, struct_name: str):
            self.struct = header.get_only(name=struct_name, type=Struct)
            self.funcs = []

        def add_func(self, header: Header, func_name: str, *,
                     rename: Optional[str]=None,
                     type: Optional[FuncType]=None) -> None:
            func = header.get_only(name=func_name, type=Func)
            
            header.used.add(func)
            self.funcs.append(Module._Class._Func(header, func, rename=rename, type=type))

        def add_constructor(self, header: Header, func_name: str) -> None:
            self.add_func(header, func_name, rename=self.struct.name, type=FuncType.CONSTRUCTOR)

        def add_destructor(self, header: Header, func_name: str) -> None:
            self.add_func(header, func_name, rename="~" + self.struct.name, type=FuncType.DESTRUCTOR)

        def add_prefixed_methods(self, header: Header, prefix: str) -> None:
            def first_arg_check(node: Node) -> bool:
                assert type(node) is Func
                if len(node.args) == 0:
                    return False
                arg = node.args[0]
                return (arg.type.kind == TypeKind.POINTER and
                        arg.type.get_pointee().get_canonical() == self.struct.type)

            query = Query.all(
                Query.startswith(prefix),
                first_arg_check,
                Query.unused(header)
            )
            
            for func in header.get_all(query, type=Func):
                self.funcs.append(Module._Class._Func(header, func, type=FuncType.THIS,
                                                      rename=func.name[len(prefix):]))
            
    name: str
    headers: Set[Header]
    classes: List[_Class]

    def __init__(self, name: str):
        self.name = name
        self.headers = set()
        self.classes = []

    def Class(self, header: Header, struct_name: str) -> _Class:
        self.headers.add(header)
        
        result = Module._Class(self, header, struct_name)
        self.classes.append(result)
        return result
