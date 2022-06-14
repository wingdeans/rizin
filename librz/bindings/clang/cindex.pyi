from typing import List, Tuple, Iterator, Optional

class CursorKind:
    INCLUSION_DIRECTIVE: "CursorKind"
    MACRO_INSTANTIATION: "CursorKind"
    MACRO_DEFINITION: "CursorKind"
    VAR_DECL: "CursorKind"
    FUNCTION_DECL: "CursorKind"
    STRUCT_DECL: "CursorKind"
    ENUM_DECL: "CursorKind"
    TYPEDEF_DECL: "CursorKind"

class File:
    name: str
class SourceLocation:
    file: File

class Cursor:
    def get_children(self) -> Iterator[Cursor]:
        pass

    kind: CursorKind
    location: SourceLocation

class Diagnostic: pass
class Index: pass
class TranslationUnit:
    @classmethod
    def from_source(cls,
                    filename: str,
                    args: Optional[List[str]] = None,
                    unsaved_files: Optional[List[Tuple[str, str]]] = None,
                    options: Optional[int] = None,
                    index: Optional[Index] = None) -> "TranslationUnit":
        pass

    # Options
    PARSE_DETAILED_PROCESSING_RECORD: int

    # Properties
    diagnostics: List[Diagnostic]
    cursor: Cursor
