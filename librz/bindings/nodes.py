from clang.cindex import Cursor

class Node:
    pass

class Macro(Node):
    def __init__(self, cursor: Cursor) -> None:
        pass

class Var(Node):
    def __init__(self, cursor: Cursor) -> None:
        pass

class Func(Node):
    def __init__(self, cursor: Cursor) -> None:
        pass

class Struct(Node):
    def __init__(self, cursor: Cursor) -> None:
        pass

class Enum(Node):
    def __init__(self, cursor: Cursor) -> None:
        pass

class Typedef(Node):
    def __init__(self, cursor: Cursor) -> None:
        pass
