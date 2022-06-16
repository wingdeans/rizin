from typing import List, Tuple, Iterator, Optional

class CursorKind:
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

class File:
    name: str
class SourceLocation:
    file: File
    line: int
    column: int

class Token:
    spelling: str

class TypeKind:
    CONSTANTARRAY: TypeKind
    POINTER: TypeKind
    FUNCTIONPROTO: TypeKind
    
class Type:
    def get_pointee(self) -> Type: pass
    def get_result(self) -> Type: pass
    def argument_types(self) -> Iterator[Type]: pass
    def get_canonical(self) -> Type: pass
    
    spelling: str
    kind: TypeKind
    element_type: Type
    element_count: int
    
class Cursor:
    def get_children(self) -> Iterator[Cursor]: pass
    def get_tokens(self) -> Iterator[Token]: pass
    def get_arguments(self) -> Iterator[Cursor]: pass

    def is_macro_functionlike(self) -> bool: pass
    
    kind: CursorKind
    location: SourceLocation
    spelling: str
    type: Type
    result_type: Type
    enum_value: int
    underlying_typedef_type: Type

class Diagnostic: pass
class Index: pass
class TranslationUnit:
    @classmethod
    def from_source(cls,
                    filename: str,
                    args: Optional[List[str]] = None,
                    unsaved_files: Optional[List[Tuple[str, str]]] = None,
                    options: Optional[int] = None,
                    index: Optional[Index] = None) -> TranslationUnit:
        pass

    # Options
    PARSE_DETAILED_PROCESSING_RECORD: int

    # Properties
    diagnostics: List[Diagnostic]
    cursor: Cursor

class Config:
    @staticmethod
    def set_library_path(path: str) -> None:
        pass
