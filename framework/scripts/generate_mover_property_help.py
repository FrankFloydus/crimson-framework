# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Generate Inspector property and controller choice help from MoverComponent's class XML."""

import json
from pathlib import Path
import re
import sys
from textwrap import wrap
from xml.etree import ElementTree


def tooltip_text(element):
    """Convert a Godot class description to compact plain text for a native tooltip."""
    description = "".join(element.itertext()).strip()
    description = re.sub(r"\[/?code\]", "", description)
    description = re.sub(r"\[(?:member|method|constant|enum) ([^\]]+)\]", r"\1", description)
    paragraphs = re.split(r"\n\s*\n|\[br\]", description)
    return "\n".join(
        "\n".join(wrap(" ".join(paragraph.split()), width=68, break_long_words=False, break_on_hyphens=False))
        for paragraph in paragraphs
    )


def generate(output, source):
    """Write private lookups for property help and controller choices by enum value."""
    documentation = ElementTree.parse(source)
    lines = [
        "// Copyright Francesco Di Blasi 2026 - All Rights Reserved",
        "// Generated from MoverComponent.xml.",
        "",
        "#include <godot_cpp/variant/string.hpp>",
        "#include <godot_cpp/variant/string_name.hpp>",
        "#include <cstdint>",
        "",
        "namespace crimson::mover {",
        "",
        "// Look up plain Inspector help for one documented property.",
        "godot::String get_mover_property_help(const godot::StringName &property) {",
    ]
    for member in documentation.findall("./members/member"):
        description = tooltip_text(member)
        if description:
            lines.extend((
                f"    if (property == godot::StringName({json.dumps(member.attrib['name'])})) {{",
                f"        return godot::String::utf8(u8{json.dumps(description, ensure_ascii=False)});",
                "    }",
            ))
    lines.extend((
        "    return {};",
        "}",
        "",
        "// Look up the help for the Controller Style enum value stored by Godot's editor.",
        "godot::String get_mover_controller_style_help(int64_t value) {",
        "    switch (value) {",
    ))
    for choice in documentation.findall("./constants/constant[@enum='ControllerStyle']"):
        description = tooltip_text(choice)
        if description:
            lines.extend((
                f"        case {int(choice.attrib['value'], 0)}:",
                f"            return godot::String::utf8(u8{json.dumps(description, ensure_ascii=False)});",
            ))
    lines.extend(("        default:", "            return {};", "    }", "}", "", "} // namespace crimson::mover", ""))
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    generate(*sys.argv[1:])
