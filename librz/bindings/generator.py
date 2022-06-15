from clang.cindex import TypeKind
from typing import TextIO, List, Optional

from nodes import Struct
from binder import Module, FuncType

class Writer:
    _indent: int
    lines: List[str]
    
    def __init__(self) -> None:
        self.lines = []
        self._indent = 0

    def line(self, *values: object) -> None:
        self.lines.append("    " * self._indent + "".join(str(val) for val in values))

    def raw(self, *values: object) -> None:
        result = "".join(str(val) for val in values)
        if len(self.lines) == 0:
            self.lines[0] = result
        else:
            self.lines[-1] += result

    def write(self, io: TextIO) -> None:
        for line in self.lines:
            io.write(line)
            io.write("\n")

    def indent(self) -> None:
        self._indent += 1

    def dedent(self) -> None:
        self._indent -= 1
        assert self._indent >= 0
        
def generate_field(field: Struct.Field, writer: Writer) -> None:
    # Rewrite array and function pointer types to put field name in correct place
    if field.type.kind == TypeKind.CONSTANTARRAY:
        writer.line(field.type.element_type.spelling, " ", field.name, "[", field.type.element_count, "];")
    elif (field.type.kind == TypeKind.POINTER and
          (pointee := field.type.get_pointee()) and
          pointee.kind == TypeKind.FUNCTIONPROTO):
        args = pointee.argument_types()
        writer.line(pointee.get_result().spelling, " (*", field.name, ")",
                    "(", ", ".join([arg.spelling for arg in args]), ");")
    else:
        writer.line(field.type.spelling, " ", field.name, ";")
            
def generate_struct(struct: Struct, writer: Writer) -> None:
    writer.line("struct ", struct.name, " {")
    writer.indent()
    for field in struct.fields:
        if isinstance(field, Struct):
            generate_struct(field, writer)
        elif isinstance(field, Struct.UnionField):
            writer.line("union {")
            writer.indent()
            for union_field in field.fields:
                generate_field(union_field, writer)
            writer.dedent()
            writer.line("} ", field.name, ";")
        else:
            generate_field(field, writer)
    writer.dedent()
    writer.line("};")

def generate_func(fn: Module._Class._Func, writer: Writer) -> None:
    args_inner = [arg.name for arg in fn.func.args]
    args_outer = [(arg.type.spelling, arg.name) for arg in fn.func.args]

    if fn.type in [FuncType.THIS, FuncType.DESTRUCTOR]:
        args_outer = args_outer[1:] # don't accept first argument
        args_inner[0] = "$self" # instead use self

    args_outer_str = ", ".join([f"{type} {name}" for (type, name) in args_outer])
    if fn.type in [FuncType.CONSTRUCTOR, FuncType.DESTRUCTOR]: # no return type
        writer.line(fn.name, "(", args_outer_str, ") {")
    else:
        writer.line(fn.func.result_type.spelling, " ", fn.name, "(", args_outer_str, ") {")
    writer.indent()
    writer.line("return ", fn.func.name, "(", ", ".join(args_inner), ");")
    writer.dedent()
    writer.line("};")
    
def generate_class(cls: Module._Class, writer: Writer) -> None:
    """
    struct ... {
    };

    %extend ... {
    };
    """
    generate_struct(cls.struct, writer)

    writer.line("%extend ", cls.struct.name, " {")
    writer.indent()
    for func in cls.funcs:
        generate_func(func, writer)
    writer.dedent()
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
    writer.line("%module ", module.name)
    writer.line("%{")
    for header in module.headers:
        writer.line("#include <", header.name, ">")
    writer.line("%}")
        
    for cls in module.classes:
        generate_class(cls, writer)

    return writer
