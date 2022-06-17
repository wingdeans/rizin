"""
Generates swig bindings from binder classes
"""

from typing import TextIO, List, Iterator
from string import Template
from contextlib import contextmanager

from clang.wrapper import CursorKind, TypeKind, Struct, StructField, StructUnionField

from binder import Module, FuncType


class Writer:
    """
    Helper class for formatting output
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


def generate_field(field: StructField, writer: Writer) -> None:
    # Rewrite array and function pointer types to put field name in correct place
    if field.type.kind == TypeKind.CONSTANTARRAY:
        writer.line(
            "$type $name[$length];",
            type=field.type.element_type.spelling,
            name=field.spelling,
            length=field.type.element_count,
        )
        return

    if field.type.kind == TypeKind.POINTER:
        pointee = field.type.get_pointee()
        if pointee.kind == TypeKind.FUNCTIONPROTO:
            writer.line(
                "$return_type (*$name)($args);",
                return_type=pointee.get_result().spelling,
                name=field.spelling,
                args=", ".join([arg.spelling for arg in pointee.argument_types()]),
            )
            return

    writer.line("$type $name;", type=field.type.spelling, name=field.spelling)


def generate_struct(struct: Struct, writer: Writer) -> None:
    def generate_union(field: StructUnionField) -> None:
        writer.line("union {")
        with writer.indent():
            for union_field in field.get_children():
                assert union_field.kind == CursorKind.FIELD_DECL
                generate_field(union_field, writer)
            writer.line("} $name;", name=field.spelling)

    writer.line("struct $name {", name=struct.spelling)
    with writer.indent():
        for field in struct.get_children():
            if field.kind == CursorKind.STRUCT_DECL:
                generate_struct(field, writer)
            elif field.kind == CursorKind.UNION_DECL:
                generate_union(field)
            elif field.kind == CursorKind.FIELD_DECL:
                generate_field(field, writer)
            else:
                raise Exception(f"Unexpected struct child of kind: {field.kind}")
    writer.line("};")


def generate_func(binder_func: Module.BinderClass.BinderFunc, writer: Writer) -> None:
    args_inner = []
    args_outer = []
    for arg in binder_func.func.get_arguments():
        assert arg.kind == CursorKind.PARM_DECL
        args_inner.append(arg.spelling)
        args_outer.append((arg.type.spelling, arg.spelling))

    if binder_func.type in [FuncType.THIS, FuncType.DESTRUCTOR]:
        args_outer = args_outer[1:]  # don't accept first argument
        args_inner[0] = "$self"  # instead use self

    args_outer_str = ", ".join([f"{type} {name}" for (type, name) in args_outer])
    if binder_func.type in [
        FuncType.CONSTRUCTOR,
        FuncType.DESTRUCTOR,
    ]:  # no return type
        writer.line("$name($args) {", name=binder_func.name, args=args_outer_str)
    else:
        writer.line(
            "$return_type $name($args) {",
            return_type=binder_func.func.result_type.spelling,
            name=binder_func.name,
            args=args_outer_str,
        )
    with writer.indent():
        writer.line(
            "return $name($args);",
            name=binder_func.func.spelling,
            args=", ".join(args_inner),
        )
    writer.line("};")


def generate_class(cls: Module.BinderClass, writer: Writer) -> None:
    """
    struct ... {
    };

    %extend ... {
    };
    """
    generate_struct(cls.struct, writer)

    writer.line("%extend $struct {", struct=cls.struct.spelling)
    with writer.indent():
        for func in cls.funcs:
            generate_func(func, writer)
    writer.line("};")


def generate(module: Module) -> Writer:
    """
    %module ...
    %{
    #include <...>
    #include <...>
    %}
    """
    writer = Writer()
    writer.line("%module $name", name=module.name)
    writer.line("%{")
    for header in module.headers:
        writer.line("#include <$header>", header=header.name)
    writer.line("%}")

    for cls in module.classes:
        generate_class(cls, writer)

    return writer
