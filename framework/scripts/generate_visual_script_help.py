# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Build plain-text graph tooltips from the stock Godot API and Crimson class XML."""

import argparse
import json
from pathlib import Path
import re
import textwrap
import xml.etree.ElementTree as ET


def summary(documentation: str) -> str:
    """Keep one compact paragraph and render class-reference links as plain text."""
    prose = textwrap.dedent(documentation).strip()
    prose = re.split(r"\n\s*\n|\[(?:codeblock|codeblocks|gdscript|csharp)\b", prose, maxsplit=1)[0]
    prose = re.sub(r"\[(?:param|method|member|signal|constant|enum|constructor|operator|annotation|theme_item) ([^\]]+)\]", r"\1", prose)
    prose = re.sub(r"\[br\]", " ", prose)
    prose = re.sub(r"\[/?(?:b|i|u|s|code|kbd|lb|rb|url)(?:[ =][^\]]+)?\]", "", prose)
    prose = re.sub(r"\[([A-Za-z@][\w.@]*)\]", r"\1", prose)
    prose = " ".join(prose.split())
    if len(prose) > 360:
        prose = prose[:357].rsplit(" ", 1)[0] + "..."
    return "\n".join(textwrap.wrap(prose, width=72, break_long_words=False, break_on_hyphens=False))


def main() -> None:
    """Write a deterministic index of documented runtime methods and signals."""
    framework = Path(__file__).resolve().parent.parent
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("api", type=Path, help="extension_api.json from --dump-extension-api-with-docs")
    cli.add_argument("--output", type=Path, default=framework / "project/addons/crimson-framework/editor/visual_script_help.json")
    args = cli.parse_args()
    api = json.loads(args.api.read_text(encoding="utf-8"))
    header = api["header"]
    if (header["version_major"], header["version_minor"], header["version_patch"], header["version_status"]) != (4, 7, 0, "stable"):
        cli.error("Use the stock Godot 4.7 stable API dump with documentation.")
    index: dict[str, str] = {}

    def add(owner: str, kind: str, name: str, documentation: str) -> None:
        """Store only nonempty descriptions under a key specific to the declaring member."""
        description = summary(documentation)
        if description:
            index[f"{owner}/{kind}/{name}"] = description

    for engine_class in api["classes"]:
        if engine_class["api_type"] != "core":
            continue
        for kind in ("method", "signal"):
            for member in engine_class.get(f"{kind}s", []):
                add(engine_class["name"], kind, member["name"], member.get("description", ""))
    for value_type in api["builtin_classes"]:
        for method in value_type.get("methods", []):
            add(value_type["name"], "method", method["name"], method.get("description", ""))
    for method in api["utility_functions"]:
        add("@GlobalScope", "method", method["name"], method.get("description", ""))
    if not all(key in index for key in (
        "AudioStreamPlayer3D/method/play", "Range/signal/value_changed", "String/method/begins_with",
    )):
        cli.error("Expected method and signal descriptions. Run Godot with --dump-extension-api-with-docs.")

    for xml in sorted((framework / "src").glob("**/doc_classes/*.xml")):
        if "third_party" in xml.parts:
            continue
        cls = ET.parse(xml).getroot()
        for kind in ("method", "signal"):
            for member in cls.findall(f"{kind}s/{kind}"):
                add(cls.attrib["name"], kind, member.attrib["name"], member.findtext("description", ""))

    output = {
        "_notice": "Copyright Francesco Di Blasi 2026 - All Rights Reserved. Includes Godot Engine class reference excerpts. See licenses/godot-class-reference.txt.",
        "_engine": header["version_full_name"],
        "descriptions": index,
    }
    args.output.write_text(json.dumps(output, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
