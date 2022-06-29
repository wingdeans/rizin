from typing import List, Tuple, Union, Iterator, TextIO

from contextlib import contextmanager


class Fragment:
    """
    Helper to store lines with indents
    """

    _indent: int
    _lines: List[Tuple[int, Union[str, "Fragment"]]]
    _seen: bool

    def __init__(self) -> None:
        self._indent = 0
        self._lines = []
        self._seen = False

    @contextmanager
    def indent(self) -> Iterator[None]:
        """
        Returns a context where the indentation level is increased
        """
        self._indent += 1
        yield
        self._indent -= 1

    def line(self, text: str) -> None:
        """
        Add a string as a line
        """
        self._lines.append((self._indent, text))

    def merge(self, fragment: "Fragment") -> None:
        """
        Add another fragment as a line
        """
        self._lines.append((self._indent, fragment))

    def write(self, output: TextIO, base_indent: int = 0) -> None:
        """
        Recursively outputs a fragment
        """
        assert not self._seen
        self._seen = True

        for indent, text in self._lines:
            if isinstance(text, Fragment):
                text.write(output, indent)
            else:
                output.write("  " * (base_indent + indent + self._indent))
                output.write(text)
                output.write("\n")
