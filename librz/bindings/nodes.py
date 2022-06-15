from typing import List, Union

from clang.cindex import Cursor, Type, CursorKind

class Node:
    "A generic node"
    line: int
    col: int
    name: str

    def __init__(self, cursor: Cursor):
        self.line = cursor.location.line
        self.col = cursor.location.column
        self.name = cursor.spelling

class TypedNode(Node):
    "A node with a type"
    type: Type

    def __init__(self, cursor: Cursor):
        super().__init__(cursor)
        self.type = cursor.type
        
class Macro(Node):
    value: List[str]
    functionlike: bool
    
    def __init__(self, cursor: Cursor):
        super().__init__(cursor)
        assert self.name # must have name
        assert not any(cursor.get_children())

        self.functionlike = cursor.is_macro_functionlike()
        toks = [tok.spelling for tok in cursor.get_tokens()]
        self.value = toks[1:]

class Var(TypedNode):
    def __init__(self, cursor: Cursor):
        super().__init__(cursor)
        assert self.name # must have name

class Func(Node):
    class Arg(TypedNode): pass
        
    result_type: Type
    args: List[Arg]
    
    def __init__(self, cursor: Cursor):
        super().__init__(cursor)
        assert self.name # must have name
        self.result_type = cursor.result_type

        self.args = []
        for arg in cursor.get_arguments():
            self.args.append(Func.Arg(arg))

class Struct(Node):
    class Field(TypedNode): pass
    class UnionField(Field):
        fields: List["Struct.Field"]
        
        def __init__(self, cursor: Cursor):
            super().__init__(cursor)

            self.fields = []
            for child in cursor.get_children():
                assert child.kind == CursorKind.FIELD_DECL
                self.fields.append(Struct.Field(child))

    fields: List[Union["Struct", Field]]
    
    def __init__(self, cursor: Cursor):
        super().__init__(cursor)

        self.fields = []
        for child in cursor.get_children():
            if child.kind == CursorKind.FIELD_DECL:
                self.fields.append(Struct.Field(child))
            elif child.kind == CursorKind.UNION_DECL:
                self.fields.append(Struct.UnionField(child))
            elif child.kind == CursorKind.STRUCT_DECL:
                self.fields.append(Struct(child))
            else:
                raise Exception(f"Unexpected struct field node of kind: {str(child.kind)}")

class Enum(Node):
    class Field(Node):
        value: int
        
        def __init__(self, cursor: Cursor):
            super().__init__(cursor)
            self.value = cursor.enum_value
        
    fields: List[Field]
    
    def __init__(self, cursor: Cursor):
        super().__init__(cursor)

        fields = []
        for child in cursor.get_children():
            assert child.kind == CursorKind.ENUM_CONSTANT_DECL
            fields.append(Enum.Field(child))

class Typedef(Node):
    underlying_type: Type
    
    def __init__(self, cursor: Cursor):
        super().__init__(cursor)
        assert self.name # must have name
        self.underlying_type = cursor.underlying_typedef_type
