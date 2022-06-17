from enum import Enum as PyEnum
import enum
from typing import Union, Literal, Iterator, final

"""
Type kinds/base
"""
class TypeKind(PyEnum):
    __placeholder__: TypeKind # union being exhaustive breaks things
    
    CONSTANTARRAY: TypeKind
    POINTER: TypeKind
    FUNCTIONPROTO: TypeKind

class TypeBase:
    spelling: str
    def get_canonical(self) -> Type: pass

"""
Types
"""
class Array(TypeBase):
    kind: Literal[TypeKind.CONSTANTARRAY]
    element_type: Type
    element_count: int

class Pointer(TypeBase):
    kind: Literal[TypeKind.POINTER]
    def get_pointee(self) -> Type: pass

class FuncType(TypeBase):
    kind: Literal[TypeKind.FUNCTIONPROTO]
    def get_result(self) -> Type: pass
    def argument_types(self) -> Iterator[Type]: pass

Type = Union[Array, Pointer, FuncType]

"""
Cursor kinds/base
"""
class CursorKind(PyEnum):
    __placeholder__: TypeKind # union being exhaustive breaks things
    
    TRANSLATION_UNIT: CursorKind

    INCLUSION_DIRECTIVE: CursorKind
    MACRO_INSTANTIATION: CursorKind

    MACRO_DEFINITION: CursorKind
    VAR_DECL: CursorKind
    FUNCTION_DECL: CursorKind
    STRUCT_DECL: CursorKind
    ENUM_DECL: CursorKind
    TYPEDEF_DECL: CursorKind

    FIELD_DECL: CursorKind
    UNION_DECL: CursorKind
    ENUM_CONSTANT_DECL: CursorKind
    PARM_DECL: CursorKind

class Token:
    spelling: str
    
class CursorBase:
    class SourceLocation:
        class File:
            name: str
        file: File
        line: int
        column: int
    location: SourceLocation
    spelling: str

class RootCursor(CursorBase):
    kind: Literal[CursorKind.TRANSLATION_UNIT]
    def get_children(self) -> Iterator[Cursor]: pass
    
"""
Main cursors
"""
class Macro(CursorBase):
    kind: Literal[CursorKind.MACRO_DEFINITION]
    def is_macro_functionlike(self) -> bool: pass
    def get_tokens(self) -> Iterator[Token]: pass
    
class Var(CursorBase):
    kind: Literal[CursorKind.VAR_DECL]
    type: Type

class Func(CursorBase):
    kind: Literal[CursorKind.FUNCTION_DECL]
    def get_arguments(self) -> Iterator[Cursor]: pass
    # def get_children(self) -> Iterator[Cursor]: pass
    result_type: Type

class Struct(CursorBase):
    kind: Literal[CursorKind.STRUCT_DECL]
    type: Type
    def get_children(self) -> Iterator[Cursor]: pass

class Enum(CursorBase):
    kind: Literal[CursorKind.ENUM_DECL]
    def get_children(self) -> Iterator[Cursor]: pass

class Typedef(CursorBase):
    kind: Literal[CursorKind.TYPEDEF_DECL]
    underlying_typedef_type: Type
    
"""
Additional cursors
"""
class Param(CursorBase):
    kind: Literal[CursorKind.PARM_DECL]
    type: Type

class StructField(CursorBase):
    kind: Literal[CursorKind.FIELD_DECL]
    type: Type

class StructUnionField(CursorBase):
    kind: Literal[CursorKind.UNION_DECL]
    def get_children(self) -> Iterator[Cursor]: pass

Cursor = Union[Macro, Var, Func, Struct, Enum, Typedef,
               Param, StructField, StructUnionField]
