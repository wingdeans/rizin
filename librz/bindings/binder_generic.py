from typing import List, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    from binder import Module

from clang.wrapper import CursorKind, Typedef, Func

from header import Header
from fragment import Fragment


class BinderGeneric:
    module: "Module"
    typedef: str
    funcs: List[Fragment]

    def __init__(self, module: "Module", typedef: Typedef):
        self.module = module
        self.typedef = typedef.spelling
        self.funcs = []

    def get_function_args(self, func: Func) -> Tuple[List[str], List[str]]:
        args_outer = []
        args_inner = []
        for arg in func.get_arguments():
            assert arg.kind == CursorKind.PARM_DECL
            args_inner.append(arg.spelling)
            args_outer.append(self.module.stringify_decl(arg, arg.type, generic=True))
        return args_outer, args_inner

    def add_constructor(self, header: Header, name: str) -> None:
        self.module.headers.add(header)
        header.used.add(name)
        func = header.funcs[name]

        args_outer, args_inner = self.get_function_args(func)
        args_outer_str = ", ".join(args_outer)
        args_inner_str = ", ".join(args_inner)

        frag = Fragment()
        frag.line(f"{self.typedef}_##TYPE({args_outer_str}) {{")
        with frag.indent():
            frag.line(f"return {name}({args_inner_str});")
        frag.line("}")
        self.funcs.append(frag)

    def add_destructor(self, header: Header, name: str) -> None:
        self.module.headers.add(header)
        header.used.add(name)
        func = header.funcs[name]

        args_outer, args_inner = self.get_function_args(func)
        args_outer_str = ", ".join(args_outer[1:])
        args_inner_str = ", ".join(["$self"] + args_inner[1:])

        frag = Fragment()
        frag.line(f"~{self.typedef}_##TYPE({args_outer_str}) {{")
        with frag.indent():
            frag.line(f"return {name}({args_inner_str});")
        frag.line("}")
        self.funcs.append(frag)

    def fragment_extend(self) -> Fragment:
        frag = Fragment()
        frag.line(f"%extend {self.typedef}_##TYPE {{")
        with frag.indent():
            for func in self.funcs:
                frag.merge(func)
        frag.line("}")
        return frag

    def fragment(self) -> Fragment:
        frag = Fragment()
        frag.line(f"%define %{self.typedef}(TYPE)")
        with frag.indent():
            frag.line("%{")
            frag.line(f"typedef {self.typedef} {self.typedef}_##TYPE;")
            frag.line("%}")
            frag.line(f"typedef struct {{}} {self.typedef}_##TYPE;")
            frag.merge(self.fragment_extend())
        frag.line("%enddef")
        return frag
