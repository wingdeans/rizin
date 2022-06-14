from clang.cindex import Cursor

class Node:
    name: str

class Macro(Node):
    def __init__(self, cursor: Cursor):
        pass

class Var(Node):
    def __init__(self, cursor: Cursor):
        pass

class Func(Node):
    def __init__(self, cursor: Cursor):
        pass

class Struct(Node):
    def __init__(self, cursor: Cursor):
        pass

class Enum(Node):
    def __init__(self, cursor: Cursor):
        pass

class Typedef(Node):
    def __init__(self, cursor: Cursor):
        pass
