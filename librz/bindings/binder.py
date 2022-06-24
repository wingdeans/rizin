"""
Helper classes to construct bindings
"""
from enum import Enum
from typing import List, Set, Optional, Dict

from clang.wrapper import TypeKind, CursorKind, Cursor, Struct, Func
from header import Header


class FuncType(Enum):
    """
    Represents the type of a wrapped function
    """

    FORWARD = 0  # Every argument is unchanged (default)
    THIS = 1  # Uses a class instance's `this` as the first argument
    CONSTRUCTOR = 2
    DESTRUCTOR = 3


class BinderFunc:
    """
    Represents a function in the guest language
    """

    func: Func
    name: str
    type: FuncType
    generic: bool

    def __init__(
        self,
        func: Func,
        *,
        rename: Optional[str] = None,
        type_: FuncType = FuncType.FORWARD,
        generic: bool = False,
    ):
        self.func = func
        self.name = rename or func.spelling
        self.type = type_
        self.generic = generic


class Module:
    """
    Represents a module in the guest language
    Can contain multiple classes from different headers
    """

    class BinderClass:
        """
        Represents a class in the guest language
        Can contain class and instance methods
        """

        struct: Struct
        rename: Optional[str]
        funcs: List[BinderFunc]

        def __init__(
            self,
            header: Header,
            struct: Struct,
            *,
            rename: Optional[str] = None,
        ):
            self.rename = rename
            self.struct = struct
            self.funcs = []

        def add_func(
            self,
            header: Header,
            func_name: str,
            *,
            rename: Optional[str] = None,
            type_: FuncType = FuncType.FORWARD,
        ) -> None:
            """
            Add function to class
            """
            func = header.nodes[func_name]
            assert func.kind == CursorKind.FUNCTION_DECL

            header.used.add(func.spelling)
            self.funcs.append(BinderFunc(func, rename=rename, type_=type_))

        def add_constructor(self, header: Header, func_name: str) -> None:
            """
            Add a constructor function for the class
            """
            self.add_func(
                header,
                func_name,
                rename=self.struct.spelling,
                type_=FuncType.CONSTRUCTOR,
            )

        def add_destructor(self, header: Header, func_name: str) -> None:
            """
            Add a destructor function for the class
            """
            self.add_func(
                header,
                func_name,
                rename="~" + self.struct.spelling,
                type_=FuncType.DESTRUCTOR,
            )

        def add_prefixed_methods(self, header: Header, prefix: str) -> None:
            """
            Add all functions in the header which match the specified prefix
            and take in a pointer to the class as their first argument
            """

            def predicate(cursor: Func) -> bool:
                if cursor.spelling in header.used:
                    return False  # not used
                if not cursor.spelling.startswith(prefix):
                    return False  # correct prefix

                args = list(cursor.get_arguments())
                if len(args) == 0:
                    return False

                arg = args[0]
                assert arg.kind == CursorKind.PARM_DECL

                if arg.type.kind != TypeKind.POINTER:
                    return False
                return (
                    arg.type.get_pointee().get_canonical().get_declaration()
                    == self.struct
                )

            for func in filter(predicate, header.funcs):
                self.funcs.append(
                    BinderFunc(
                        func,
                        type_=FuncType.THIS,
                        rename=func.spelling[len(prefix) :],
                    )
                )
                header.used.add(func.spelling)

        def add_prefixed_funcs(self, header: Header, prefix: str) -> None:
            """
            Add all functions in the header which match the specified prefix
            as static methods of the class
            """

            def predicate(cursor: Func) -> bool:
                if cursor.spelling in header.used:
                    return False  # not used
                if not cursor.spelling.startswith(prefix):
                    return False  # correct prefix
                return True

            for func in filter(predicate, header.funcs):
                self.funcs.append(
                    BinderFunc(
                        func,
                        rename=func.spelling[len(prefix) :],
                    )
                )
                header.used.add(func.spelling)

    class BinderGeneric(BinderClass):
        generic_fields: List[str]

        def __init__(
            self,
            header: Header,
            struct: Struct,
            generic_fields: List[str],
            *,
            rename: Optional[str] = None,
        ):
            super().__init__(header, struct, rename=rename)
            self.generic_fields = generic_fields

        def add_generic_funcs(
            self, header: Header, prefix: str, returning: str
        ) -> None:
            pass

    name: str
    headers: Set[Header]
    classes: List[BinderClass]
    generics: List[BinderGeneric]
    generic_structs: Set[str]

    def __init__(self, name: str):
        self.name = name
        self.headers = set()
        self.classes = []

        self.generics = []
        self.generic_structs = set()

    def Class(
        self,
        header: Header,
        struct_name: str,
        *,
        rename: Optional[str] = None,
    ) -> BinderClass:
        """
        Create a class from a struct in the given header
        """
        self.headers.add(header)

        struct = header.nodes[struct_name]
        assert struct.kind == CursorKind.STRUCT_DECL

        result = Module.BinderClass(header, struct, rename=rename)
        self.classes.append(result)

        return result

    def Generic(
        self,
        header: Header,
        struct_name: str,
        generic_fields: List[str],
        *,
        rename: Optional[str] = None,
    ) -> BinderClass:
        self.headers.add(header)

        struct = header.nodes[struct_name]
        assert struct.kind == CursorKind.STRUCT_DECL

        result = Module.BinderGeneric(header, struct, generic_fields, rename=rename)
        self.generics.append(result)
        self.generic_structs.add(struct.spelling)

        return result
