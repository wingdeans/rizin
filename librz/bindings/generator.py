"""
Generates swig bindings from binder classes
"""

from typing import TextIO, List, Iterator, Union, Optional
from string import Template
from contextlib import contextmanager

from clang.wrapper import (
    CursorKind,
    TypeKind,
    Struct,
    StructField,
    StructUnionField,
    Type,
    Cursor,
)
from clang.cindex import SourceRange

from binder import Module, BinderFunc, FuncType


class Writer:
    """
    Helpers for indentation and formatting
    """

    _indent: int
    lines: List[str]

    def __init__(self) -> None:
        self.lines = []
        self._indent = 0

    def line(self, template: str, **kwargs: object) -> None:
        """
        Adds a new line according to the template string/args and with the current indentation
        """
        self.lines.append("    " * self._indent + Template(template).substitute(kwargs))

    def write(self, output: TextIO) -> None:
        """
        Outputs lines to file or sys.stdout or other TextIO
        """
        for line in self.lines:
            output.write(line)
            output.write("\n")

    @contextmanager
    def indent(self) -> Iterator[None]:
        """
        Adds an indentation level within the with block
        """
        self._indent += 1
        yield
        self._indent -= 1


class Generator:
    """
    SWIG code generation class
    """

    module: Module

    header: Writer
    generic: Writer
    main: Writer

    def __init__(self, module: Module):
        super().__init__()

        self.module = module

        self.header = Writer()
        self.generic = Writer()
        self.main = Writer()

        self.generate_module()

    def generate_module(self) -> None:
        """
        %module ...
        %{
        #include <...>
        #include <...>
        %}
        """
        writer = self.header

        self.main.line("%module $name", name=self.module.name)
        self.main.line("%{")
        for header in self.module.headers:
            self.main.line("#include <$header>", header=header.name)
        self.main.line("%}")

        for generic in self.module.generics:
            self.generate_generic(generic)

        for cls in self.module.classes:
            self.generate_class(cls)

    def generate_generic(self, cls: Module.BinderGeneric) -> None:
        """
        %define %rz_list_t_generic(TYPE)
        typedef struct {} rz_list_t_##TYPE;
        %extend rz_list_t_##TYPE {
        ...
        }
        %enddef
        """
        writer = self.generic

        writer.line("%define %${name}_generic(TYPE)", name=cls.struct.spelling)
        with writer.indent():
            writer.line("typedef struct {} ${name}_##TYPE;", name=cls.struct.spelling)
            writer.line("%extend ${name}_##TYPE {", name=cls.struct.spelling)
            with writer.indent():
                self.generate_struct_fields(cls.struct, generic=True)
            writer.line("}")
        writer.line("%enddef")

    def generate_class(self, cls: Module.BinderClass) -> None:
        """
        struct ... {
        };

        %extend ... {
        };
        """
        writer = self.main

        self.generate_struct(cls.struct, generic=False)

        if cls.rename:
            writer.line("%rename ($new) $old;", new=cls.rename, old=cls.struct.spelling)
        writer.line("%extend $struct {", struct=cls.struct.spelling)
        with writer.indent():
            for func in cls.funcs:
                self.generate_func(func)
        writer.line("};")

    def generate_struct_fields(self, struct: Struct, *, generic: bool) -> None:
        writer = self.generic if generic else self.main

        def generate_field(field: StructField) -> None:
            writer.line(
                "$decl;", decl=self.stringify_decl(field, field.type, generic=generic)
            )

        def generate_union(field: StructUnionField) -> None:
            writer.line("union {")
            with writer.indent():
                for union_field in field.get_children():
                    assert union_field.kind == CursorKind.FIELD_DECL
                    generate_field(union_field)
                writer.line("} $name;", name=field.spelling)

        for field in struct.get_children():
            if field.kind == CursorKind.STRUCT_DECL:
                self.generate_struct(field, generic=generic)
            elif field.kind == CursorKind.UNION_DECL:
                generate_union(field)
            elif field.kind == CursorKind.FIELD_DECL:
                generate_field(field)
            else:
                raise Exception(f"Unexpected struct child of kind: {field.kind}")

    def generate_struct(self, struct: Struct, *, generic: bool) -> None:
        writer = self.generic if generic else self.main

        writer.line("struct $name {", name=struct.spelling)
        with writer.indent():
            self.generate_struct_fields(struct, generic=generic)
        writer.line("};")

    def write(self, output: TextIO) -> None:
        self.header.write(output)
        self.generic.write(output)
        self.main.write(output)

    def stringify_decl(self, cursor: Cursor, type_: Type, *, generic: bool) -> str:
        name = cursor.spelling
        pointers = ""

        while type_.kind == TypeKind.POINTER:
            type_ = type_.get_pointee()
            pointers += "*"

        if type_.kind == TypeKind.CONSTANTARRAY:
            name = f"{name}[{type_.element_count}]"
            type_ = type_.element_type
        elif type_.kind == TypeKind.INCOMPLETEARRAY:
            name = f"{name}[]"
            type_ = type_.element_type
        elif type_.kind == TypeKind.FUNCTIONPROTO:
            args = ", ".join(arg.spelling for arg in type_.argument_types())
            name = f"({name})({args})"
            type_ = type_.get_result()

        decl = type_.get_canonical().get_declaration()
        if decl.spelling in self.module.generic_structs:
            if generic:
                name = f"{name}_##TYPE"
            else:
                assert cursor.kind in [
                    CursorKind.FIELD_DECL,
                    CursorKind.FUNCTION_DECL,
                    CursorKind.PARM_DECL,
                ]
                typerefs = [
                    child
                    for child in cursor.get_children()
                    if child.kind == CursorKind.TYPE_REF
                ]
                assert len(typerefs) == 1
                src_range = SourceRange.from_locations(
                    typerefs[0].extent.end, cursor.location
                )
                token = next(
                    cursor.translation_unit.get_tokens(extent=src_range)
                ).spelling
                if not (token.startswith("/*") and token.endswith("*/")):
                    print(cursor.location)

        return f"{type_.spelling} {pointers}{name}"

    def generate_func(self, binder_func: BinderFunc) -> None:
        writer = self.main
        args_inner = []
        args_outer = []
        for arg in binder_func.func.get_arguments():
            assert arg.kind == CursorKind.PARM_DECL
            args_inner.append(arg.spelling)
            args_outer.append(
                self.stringify_decl(arg, arg.type, generic=binder_func.generic)
            )

        if binder_func.type in [FuncType.THIS, FuncType.DESTRUCTOR]:
            args_outer = args_outer[1:]  # don't accept first argument
            args_inner[0] = "$self"  # instead use self

        args_outer_str = ", ".join(args_outer)
        if binder_func.type in [
            FuncType.CONSTRUCTOR,
            FuncType.DESTRUCTOR,
        ]:  # no return type
            writer.line("$name($args) {", name=binder_func.name, args=args_outer_str)
        else:
            writer.line(
                "$visibility$decl($args) {",
                visibility="" if binder_func.type == FuncType.THIS else "static ",
                decl=self.stringify_decl(
                    binder_func.func,
                    binder_func.func.result_type,
                    generic=binder_func.generic,
                ),
                args=args_outer_str,
            )

        with writer.indent():
            writer.line(
                "return $name($args);",
                name=binder_func.func.spelling,
                args=", ".join(args_inner),
            )
        writer.line("}")
