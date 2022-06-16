from clang.cindex import TranslationUnit, CursorKind
from typing import List, DefaultDict, OrderedDict, Set, Callable, TypeVar, Type, Optional, overload, Union
import os
import logging

from nodes import Node, Macro, Var, Func, Struct, Enum, Typedef

class HeaderConfig:
    extra_args: List[str] = []
    args: List[str] = [
        "-I" + os.path.join(os.path.dirname(__file__), "..", "include"),
        "-I" + os.path.join(os.path.dirname(__file__), "..", "..", "subprojects", "sdb", "src"),
        "-I" + os.path.join(os.path.dirname(__file__), "..", "..", "build")
    ]
    
    @staticmethod
    def set_extra_args(extra_args: str) -> None:
        HeaderConfig.extra_args = extra_args.split(" ")

QueryType = Callable[[Node], bool]
class Query:
    "Query helpers"

    @staticmethod
    def startswith(prefix: str) -> QueryType:
        return lambda node: node.name.startswith(prefix)

    @staticmethod
    def all(*queries: QueryType) -> QueryType:
        return lambda node: all(query(node) for query in queries)

    @staticmethod
    def unused(header: "Header") -> QueryType:
        return lambda node: node not in header.used

T = TypeVar("T", bound=Node) # Used for queries with a specified type
class Header:
    name: str
    nodes: OrderedDict[str, Node]
    nodes_by_type: DefaultDict[Type[Node], OrderedDict[str, Node]]
    used: Set[Node]
    
    def __init__(self, name: str) -> None:
        self.name = name
        filename = os.path.join(os.path.dirname(__file__), "..", "include", name)
        tu = TranslationUnit.from_source(
            filename,
            args = HeaderConfig.extra_args + HeaderConfig.args,
            options = TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        )

        for diag in tu.diagnostics:
            logging.warn(diag)

        self.used = set()
        self.nodes = OrderedDict()
        self.nodes_by_type = DefaultDict(OrderedDict)
        for child in tu.cursor.get_children():
            # Skip nodes from other headers
            if not child.location.file or child.location.file.name != filename:
                continue

            # Skip `#include` and macro expansion
            if child.kind in [CursorKind.INCLUSION_DIRECTIVE, CursorKind.MACRO_INSTANTIATION]:
                continue

            mappings = {
                CursorKind.MACRO_DEFINITION: Macro,
                CursorKind.VAR_DECL: Var,
                CursorKind.FUNCTION_DECL: Func,
                CursorKind.STRUCT_DECL: Struct,
                CursorKind.ENUM_DECL: Enum,
                CursorKind.TYPEDEF_DECL: Typedef
            }
            
            if child.kind in mappings:
                node = mappings[child.kind](child)

                if not node.name:
                    node.name = f"anonymous_node_{len(self.nodes)}"
                elif node.name in self.nodes: # Redefinition
                    if type(node) is Struct:
                        prev = self.nodes[node.name]
                        assert type(prev) is Struct
                        assert len(prev.fields) == 0 # Should be forward declaration
                    else:
                        raise Exception(f"Unexpected redefinition of symbol: {node.name}, "
                                        f"with type: {type(node).__name__}")
                
                self.nodes[node.name] = node
                self.nodes_by_type[type(node)][node.name] = node
            else:
                raise Exception(f"Unexpected toplevel node of kind: {str(child.kind)}")


    """
    Query functions
    """

    @overload
    def get_all(self, query: QueryType) -> List[Node]: pass
    @overload
    def get_all(self, query: QueryType, *, type: Type[T]) -> List[T]: pass

    def get_all(self, query: QueryType, *, type: Optional[Type[T]]=None) -> Union[List[T], List[Node]]:
        target = self.nodes_by_type[type] if type else self.nodes
        return list(filter(query, target.values()))

    @overload
    def get_first(self, query: QueryType) -> Node: pass
    @overload
    def get_first(self, query: QueryType, *, type: Type[T]) -> T: pass
    
    def get_first(self, query: QueryType, *, type: Optional[Type[Node]]=None) -> Union[T, Node]:
        target = self.nodes_by_type[type] if type else self.nodes
        return next(filter(query, target.values()))

    @overload
    def get_only(self, *, name: str) -> Node: pass
    @overload
    def get_only(self, *, name: str, type: Type[T]) -> T: pass
    @overload
    def get_only(self, query: QueryType, *, name: Optional[str]=None) -> Node: pass
    @overload
    def get_only(self, query: Optional[QueryType], *, name: Optional[str]=None, type: Type[T]) -> T: pass

    def get_only(self, query: Optional[QueryType]=None, *, name: Optional[str]=None,
                 type: Optional[Type[Node]]=None) -> Union[T, Node]:
        if name is not None:
            return self.nodes[name]
        else:
            target = self.nodes_by_type[type] if type else self.nodes
            result = list(filter(query, target.values()))
            
            assert len(result) == 1
            return result[0]
