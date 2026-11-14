# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Reject locale-dependent text in first-party C++ before it reaches Godot."""

import re
import sys
from pathlib import Path

# Consume comments, raw strings, and quoted literals as complete tokens.
TOKENS = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/'
    r'|(?:u8|U|u|L)?R"(?P<tag>[^\s()\\]{0,16})\([\s\S]*?\)(?P=tag)"'
    r'|(?P<prefix>u8|U|u|L)?(?P<quote>"|(?<!\w)\')(?P<literal>(?:\\[^\n]|(?!(?P=quote))[^\\\n])*)(?P=quote)'
)
ESCAPES = re.compile(r'\\(?:x([0-9a-fA-F]+)|u([0-9a-fA-F]{4})|U([0-9a-fA-F]{8})|([0-7]{1,3})|.)')
NATIVE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".hh", ".hxx", ".inl"}


def check_source(source):
    """Return the first line and reason that violate the native source text policy."""
    for line_number, line in enumerate(source.split("\n"), start=1):
        if not line.isascii():
            return line_number, "non-ASCII source text"
    # The compiler joins backslash-newline before lexing comments or literals.
    splices = [match.start() for match in re.finditer(r'\\\n', source)]
    logical_source = source.replace("\\\n", "")
    for token in TOKENS.finditer(logical_source):
        literal = token["literal"]
        if literal is None or token["prefix"] in {"U", "u", "L"}:
            continue
        for escape in ESCAPES.finditer(literal):
            for index, digits in enumerate(escape.groups()):
                if digits and int(digits, 8 if index == 3 else 16) >= 128:
                    location = token.start()
                    for splice in splices:
                        if splice > location:
                            break
                        location += 2
                    return source.count("\n", 0, location) + 1, "non-ASCII narrow literal escape"
    return None


def check_tree(source_root):
    """Print actionable errors for affected first-party files and report whether all passed."""
    passed = True
    for path in sorted(source_root.rglob("*")):
        if path.suffix not in NATIVE_SUFFIXES or not path.is_file():
            continue
        relative = path.relative_to(source_root)
        if "third_party" in relative.parts or relative.parts[0] == "gen":
            continue
        violation = check_source(path.read_text(encoding="latin-1"))
        if violation:
            line, reason = violation
            print(f'{path}:{line}: {reason}. Use ASCII source and Godot String(U"...\\uNNNN...") '
                  'for authored Unicode text. Decode incoming UTF-8 with String::utf8.', file=sys.stderr)
            passed = False
    return passed


if __name__ == "__main__":
    sys.exit(0 if check_tree(Path(__file__).resolve().parents[1] / "src") else 1)
