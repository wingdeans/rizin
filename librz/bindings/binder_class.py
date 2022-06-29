from typing import Optional, List, Tuple, Set, TYPE_CHECKING

if TYPE_CHECKING:
    from binder import Module

from clang.wrapper import (
    CursorKind,
    Struct,
    Typedef,
    StructField,
    StructUnionField,
    Func,
    TypeKind,
)
from header import Header
from fragment import Fragment


class BinderClass:
    module: "Module"
    struct: Struct
    typedef: str
    funcs: List[Fragment]

    def __init__(self, module: "Module", typedef: Typedef):
        self.module = module
        struct = typedef.underlying_typedef_type.get_declaration()
        assert struct.kind == CursorKind.STRUCT_DECL
        self.struct = struct
        self.typedef = typedef.spelling
        self.funcs = []

    def get_function_args(self, func: Func) -> Tuple[List[str], List[str]]:
        args_outer = []
        args_inner = []
        for arg in func.get_arguments():
            assert arg.kind == CursorKind.PARM_DECL
            args_inner.append(arg.spelling)
            args_outer.append(self.module.stringify_decl(arg, arg.type))
        return args_outer, args_inner

    def get_function_attrs(self, func: Func) -> Set[str]:
        attrs = set()
        for child in func.get_children():
            if child.kind != CursorKind.ANNOTATE_ATTR:
                continue
            attrs.add(child.spelling)
        return attrs

    def add_constructor(self, header: Header, name: str) -> None:
        self.module.headers.add(header)
        header.used.add(name)
        func = header.funcs[name]

        args_outer, args_inner = self.get_function_args(func)
        args_outer_str = ", ".join(args_outer)
        args_inner_str = ", ".join(args_inner)

        frag = Fragment()
        frag.line(f"{self.struct.spelling}({args_outer_str}) {{")
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
        frag.line(f"~{self.struct.spelling}({args_outer_str}) {{")
        with frag.indent():
            frag.line(f"return {name}({args_inner_str});")
        frag.line("}")
        self.funcs.append(frag)

    def add_prefixed_methods(self, header: Header, prefix: str) -> None:
        self.module.headers.add(header)

        def predicate(func: Func) -> bool:
            if func.spelling in header.used:
                return False  # not used
            if not func.spelling.startswith(prefix):
                return False  # correct prefix
            if "RZ_API" not in self.get_function_attrs(func):
                return False  # RZ_API

            args = list(func.get_arguments())
            if len(args) == 0:
                return False

            arg = args[0]
            assert arg.kind == CursorKind.PARM_DECL

            if arg.type.kind != TypeKind.POINTER:
                return False
            return (
                arg.type.get_pointee().get_canonical().get_declaration() == self.struct
            )

        for func in filter(predicate, header.funcs.values()):
            header.used.add(func.spelling)

            args_outer, args_inner = self.get_function_args(func)
            args_outer_str = ", ".join(args_outer[1:])
            args_inner_str = ", ".join(["$self"] + args_inner[1:])
            decl = self.module.stringify_decl(
                func, func.result_type, name=func.spelling[len(prefix) :]
            )

            frag = Fragment()
            frag.line(f"{decl}({args_outer_str}) {{")
            with frag.indent():
                frag.line(f"return {func.spelling}({args_inner_str});")
            frag.line("}")
            self.funcs.append(frag)

    def add_prefixed_funcs(self, header: Header, prefix: str) -> None:
        self.module.headers.add(header)

        def predicate(func: Func) -> bool:
            if func.spelling in header.used:
                return False  # not used
            if not func.spelling.startswith(prefix):
                return False  # correct prefix
            if "RZ_API" not in self.get_function_attrs(func):
                return False  # RZ_API
            return True

        for func in filter(predicate, header.funcs.values()):
            header.used.add(func.spelling)

            args_outer, args_inner = self.get_function_args(func)
            args_outer_str = ", ".join(args_outer)
            args_inner_str = ", ".join(args_inner)
            decl = self.module.stringify_decl(
                func, func.result_type, name=func.spelling[len(prefix) :]
            )

            frag = Fragment()
            frag.line(f"static {decl}({args_outer_str}) {{")
            with frag.indent():
                frag.line(f"return {func.spelling}({args_inner_str});")
            frag.line("}")
            self.funcs.append(frag)

    def fragment_struct(self) -> Fragment:
        frag = Fragment()

        def generate_field(field: StructField) -> None:
            decl = self.module.stringify_decl(field, field.type)
            frag.line(f"{decl};")

        def generate_union(field: StructUnionField) -> None:
            frag.line("union {")
            with frag.indent():
                for union_field in field.get_children():
                    assert union_field.kind == CursorKind.FIELD_DECL
                    generate_field(union_field)
            frag.line("}")

        frag.line(f"struct {self.struct.spelling} {{")
        with frag.indent():
            for field in self.struct.get_children():
                if field.kind == CursorKind.STRUCT_DECL:
                    frag.merge(self.fragment_struct())
                elif field.kind == CursorKind.FIELD_DECL:
                    generate_field(field)
                elif field.kind == CursorKind.UNION_DECL:
                    generate_union(field)
                else:
                    raise Exception(f"Unexpected struct child of kind: {field.kind}")
        frag.line("};")
        return frag

    def fragment_extend(self) -> Fragment:
        frag = Fragment()
        frag.line(f"typedef struct {self.struct.spelling} {self.typedef};")

        # %extend should be applied to the struct name, not the typedef,
        # so to get correctly named constructor/destructor, we must %rename
        frag.line(f"%rename {self.struct.spelling} {self.typedef};")
        frag.line(f"%extend {self.struct.spelling} {{")
        with frag.indent():
            for func in self.funcs:
                frag.merge(func)
        frag.line("}")
        return frag

    def fragment(self) -> Fragment:
        frag = Fragment()
        frag.merge(self.fragment_struct())
        frag.merge(self.fragment_extend())
        return frag
