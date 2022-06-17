from clang.wrapper import CursorKind, TypeKind, Struct, StructField
from typing import TextIO, List, Optional

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
        
def generate_field(field: StructField, writer: Writer) -> None:
    # Rewrite array and function pointer types to put field name in correct place
    if field.type.kind == TypeKind.CONSTANTARRAY:
        writer.line(field.type.element_type.spelling, " ", field.spelling, "[", field.type.element_count, "];")
    elif (field.type.kind == TypeKind.POINTER and
          (pointee := field.type.get_pointee()) and
          pointee.kind == TypeKind.FUNCTIONPROTO):
        args = pointee.argument_types()
        writer.line(pointee.get_result().spelling, " (*", field.spelling, ")",
                    "(", ", ".join([arg.spelling for arg in args]), ");")
    else:
        writer.line(field.type.spelling, " ", field.spelling, ";")
            
def generate_struct(struct: Struct, writer: Writer) -> None:
    writer.line("struct ", struct.spelling, " {")
    writer.indent()
    for field in struct.get_children():
        if field.kind == CursorKind.STRUCT_DECL:
            generate_struct(field, writer)
        elif field.kind == CursorKind.UNION_DECL:
            writer.line("union {")
            writer.indent()
            for union_field in field.get_children():
                assert union_field.kind == CursorKind.FIELD_DECL
                generate_field(union_field, writer)
            writer.dedent()
            writer.line("} ", field.spelling, ";")
        elif field.kind == CursorKind.FIELD_DECL:
            generate_field(field, writer)
        else:
            raise Exception(f"Unexpected struct child of kind: {field.kind}")
    writer.dedent()
    writer.line("};")

def generate_func(fn: Module.BinderClass.BinderFunc, writer: Writer) -> None:
    args_inner = []
    args_outer = []
    for arg in fn.func.get_arguments():
        assert arg.kind == CursorKind.PARM_DECL
        args_inner.append(arg.spelling)
        args_outer.append((arg.type.spelling, arg.spelling))

    if fn.type in [FuncType.THIS, FuncType.DESTRUCTOR]:
        args_outer = args_outer[1:] # don't accept first argument
        args_inner[0] = "$self" # instead use self

    args_outer_str = ", ".join([f"{type} {name}" for (type, name) in args_outer])
    if fn.type in [FuncType.CONSTRUCTOR, FuncType.DESTRUCTOR]: # no return type
        writer.line(fn.name, "(", args_outer_str, ") {")
    else:
        writer.line(fn.func.result_type.spelling, " ", fn.name, "(", args_outer_str, ") {")
    writer.indent()
    writer.line("return ", fn.func.spelling, "(", ", ".join(args_inner), ");")
    writer.dedent()
    writer.line("};")
    
def generate_class(cls: Module.BinderClass, writer: Writer) -> None:
    """
    struct ... {
    };

    %extend ... {
    };
    """
    generate_struct(cls.struct, writer)

    writer.line("%extend ", cls.struct.spelling, " {")
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
