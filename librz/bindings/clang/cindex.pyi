from clang.wrapper import RootCursor

from typing import List, Tuple, Iterator, Optional

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
    cursor: RootCursor

class Config:
    @staticmethod
    def set_library_path(path: str) -> None:
        pass
