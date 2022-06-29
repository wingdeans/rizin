from typing import List, Set, Optional, Tuple, Dict, TextIO

from clang.cindex import SourceRange
from clang.wrapper import Type, TypeKind, Cursor, CursorKind

from header import Header
from fragment import Fragment

from binder_class import BinderClass
from binder_generic import BinderGeneric


class Module:
    name: str
    headers: Set[Header]

    classes: List[BinderClass]

    generics: List[BinderGeneric]
    generic_names: Dict[str, str]  # maps struct name -> generic name
    generic_specializations: Fragment
    generic_specializations_set: Set[Tuple[str, str]]

    def __init__(self, name: str):
        self.name = name
        self.headers = set()
        self.classes = []

        self.generics = []
        self.generic_names = {}
        self.generic_specializations = Fragment()
        self.generic_specializations_set = set()

    def Class(self, header: Header, name: str) -> BinderClass:
        self.headers.add(header)

        result = BinderClass(self, header.typedefs[name])
        self.classes.append(result)
        return result

    def Generic(self, header: Header, name: str) -> BinderGeneric:
        self.headers.add(header)
        typedef = header.typedefs[name]
        struct = typedef.underlying_typedef_type.get_declaration()
        self.generic_names[struct.spelling] = name

        result = BinderGeneric(self, typedef)
        self.generics.append(result)
        return result

    def get_generic_name(self, type_: Type) -> Optional[str]:
        while type_.kind == TypeKind.POINTER:
            type_ = type_.get_pointee()

        name = type_.get_canonical().get_declaration().spelling
        if name in self.generic_names:
            return self.generic_names[name]
        return None

    def add_generic_specialization(self, cursor: Cursor, generic_name: str) -> str:
        # Extract generic /*<type>*/ comment
        assert (
            cursor.kind == CursorKind.FIELD_DECL
            or cursor.kind == CursorKind.FUNCTION_DECL
            or cursor.kind == CursorKind.PARM_DECL
        )

        typeref = next(
            child
            for child in cursor.get_children()
            if child.kind == CursorKind.TYPE_REF
        )
        src_range = SourceRange.from_locations(typeref.extent.end, cursor.location)
        token = next(cursor.translation_unit.get_tokens(extent=src_range)).spelling

        if not token.startswith("/*<") or not token.endswith(">*/"):
            raise Exception(
                f"{generic_name} at {cursor.location} lacks /*<type>*/ annotation"
            )
        name = token[3:-3]

        # Generics of type char* screw up tokenization
        if name == "char*":
            name = "String"

        # Add to specializations
        mapping = (generic_name, name)
        if mapping not in self.generic_specializations_set:
            self.generic_specializations.line(f"%{generic_name}({name})")
            self.generic_specializations_set.add(mapping)
        return name

    def stringify_decl(
        self,
        cursor: Cursor,
        type_: Type,
        *,
        name: Optional[str] = None,
        generic: bool = False,
    ) -> str:
        """
        Combine a name and a type to form a declaration string
        eg. (anArray, int[10]) -> "int anArray[10]"
        eg. (aFunctionPointer, (*int)(int a, int b)) -> "int (*aFunctionPointer)(int a, int b)"

        If the type is generic, get the inner type from the comment
        and generate the correct specialization

        If being called from a generic %define, use ##TYPE as the
        inner type
        """

        # Get generic typename if applicable
        generic_name = self.get_generic_name(type_)
        if generic_name:  # Is generic type?
            if generic:
                type_name = f"{generic_name}_##TYPE"
            else:
                type_name = f"{generic_name}_{self.add_generic_specialization(cursor, generic_name)}"
        else:
            type_name = None

        name = name or cursor.spelling

        while type_.kind == TypeKind.POINTER:
            type_ = type_.get_pointee()
            name = "*" + name

        # Reorder array and function pointer declarations
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
        elif type_.kind == TypeKind.BOOL:
            type_name = "bool"  # _Bool -> bool

        return f"{type_name or type_.spelling} {name}"

    def write(self, output: TextIO) -> None:
        frag = Fragment()
        frag.line(f"%module {self.name}")
        frag.line("%{")
        for header in self.headers:
            frag.line(f"#include <{header.name}>")
        frag.line("%}")

        # Generics of type char* screw up tokenization
        frag.line("%{")
        frag.line("typedef char* String;")
        frag.line("%}")

        for generic in self.generics:
            frag.merge(generic.fragment())
        frag.merge(self.generic_specializations)
        for cls in self.classes:
            frag.merge(cls.fragment())

        frag.write(output)
