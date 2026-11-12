# Copyright Francesco Di Blasi 2026 - All Rights Reserved

import os
import sys
from enum import Enum

# Colors are disabled in non-TTY environments such as pipes. This means
# that if output is redirected to a file, it won't contain color codes.
# Colors are always enabled on continuous integration.
_colorize = bool(sys.stdout.isatty() or os.environ.get("CI"))


class ANSI(Enum):
    """
    Enum class for adding ansi colorcodes directly into strings.
    Automatically converts values to strings representing their
    internal value, or an empty string in a non-colorized scope.
    """

    # Clear all terminal styles.
    RESET = "\x1b[0m"

    # Use a bold or bright terminal style.
    BOLD = "\x1b[1m"
    # Apply italic terminal styling.
    ITALIC = "\x1b[3m"
    # Underline terminal text.
    UNDERLINE = "\x1b[4m"
    # Strike through terminal text.
    STRIKETHROUGH = "\x1b[9m"
    # Restore normal terminal styling.
    REGULAR = "\x1b[22;23;24;29m"

    # Use the black terminal foreground.
    BLACK = "\x1b[30m"
    # Use the red terminal foreground.
    RED = "\x1b[31m"
    # Use the green terminal foreground.
    GREEN = "\x1b[32m"
    # Use the yellow terminal foreground.
    YELLOW = "\x1b[33m"
    # Use the blue terminal foreground.
    BLUE = "\x1b[34m"
    # Use the magenta terminal foreground.
    MAGENTA = "\x1b[35m"
    # Use the cyan terminal foreground.
    CYAN = "\x1b[36m"
    # Use the white terminal foreground.
    WHITE = "\x1b[37m"

    # Use the purple terminal foreground.
    PURPLE = "\x1b[38;5;93m"
    # Use the pink terminal foreground.
    PINK = "\x1b[38;5;206m"
    # Use the orange terminal foreground.
    ORANGE = "\x1b[38;5;214m"
    # Use the gray terminal foreground.
    GRAY = "\x1b[38;5;244m"

    def __str__(self) -> str:
        """Return the escape sequence when console color is enabled."""
        global _colorize
        return str(self.value) if _colorize else ""


def print_warning(*values: object) -> None:
    """Prints a warning message with formatting."""
    print(f"{ANSI.YELLOW}{ANSI.BOLD}WARNING:{ANSI.REGULAR}", *values, ANSI.RESET, file=sys.stderr)


def print_error(*values: object) -> None:
    """Prints an error message with formatting."""
    print(f"{ANSI.RED}{ANSI.BOLD}ERROR:{ANSI.REGULAR}", *values, ANSI.RESET, file=sys.stderr)
