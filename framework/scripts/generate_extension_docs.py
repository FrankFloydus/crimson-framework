# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Invoke godot-cpp's class documentation generator from a path manifest."""

from pathlib import Path
from runpy import run_path
import sys


def main():
    """Read XML paths without passing the complete list through the command line."""
    generator, output, manifest = sys.argv[1:]
    sources = Path(manifest).read_text(encoding="utf-8").splitlines()
    run_path(generator)["generate_doc_source"](output, sources)


if __name__ == "__main__":
    main()
