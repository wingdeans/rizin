from header import Header
from nodes import Struct, Func
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
            
            def __init__(self, header: Header, func_name: str, *,
                         rename: Optional[str]=None,
                         type: Optional[FuncType]=None):
                self.func = header.get_only(name=func_name, type=Func)
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
            func = Module._Class._Func(header, func_name, rename=rename, type=type)
            self.funcs.append(func)

        def add_constructor(self, header: Header, func_name: str) -> None:
            self.add_func(header, func_name, rename=self.struct.name, type=FuncType.CONSTRUCTOR)

        def add_destructor(self, header: Header, func_name: str) -> None:
            self.add_func(header, func_name, rename="~" + self.struct.name, type=FuncType.DESTRUCTOR)
            
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
