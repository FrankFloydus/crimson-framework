# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Generate Visual Script method signatures and constants from the pinned Godot extension API."""

import argparse
import json
from pathlib import Path


def literal(value: str) -> str:
    """Quote an API name or literal as ASCII C++ source."""
    return json.dumps(value, ensure_ascii=True)


def integer_literal(value: int) -> str:
    """Emit a signed 64-bit C++ literal, including the minimum value."""
    return "(-9223372036854775807LL - 1)" if value == -(1 << 63) else f"{value}LL"


def main() -> None:
    """Write sorted catalogues and native utility wrappers as one C++ translation unit."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("api", type=Path)
    args = parser.parse_args()
    api = json.loads(args.api.read_text(encoding="utf-8"))
    header = api["header"]
    if (header["version_major"], header["version_minor"]) != (4, 7):
        parser.error("Expected the pinned Godot 4.7 extension API.")

    lines = [
        "// Copyright Francesco Di Blasi 2026 - All Rights Reserved",
        "// Generated from the pinned Godot extension API. Do not edit.",
        '#include "modules/visual_scripting/visual_script_builtin_data.hpp"',
        "#include <godot_cpp/variant/utility_functions.hpp>",
        "",
        "namespace crimson::visual_scripting {",
        "namespace {",
        "using namespace godot;",
    ]
    rows = []
    owners = [(entry["name"], entry.get("methods", [])) for entry in api["builtin_classes"]]
    owners.append(("@GlobalScope", api["utility_functions"]))
    for owner, methods in sorted(owners):
        for method in sorted(methods, key=lambda item: item["name"]):
            name = method["name"]
            arguments = method.get("arguments", [])
            suffix = f"_{len(rows)}"
            vararg = method["is_vararg"]
            utility = owner == "@GlobalScope"
            static = utility or method["is_static"]
            result = method.get("return_type", "")
            if arguments:
                lines.append(f"constexpr BuiltinArgument arguments{suffix}[] = {{")
                for argument in arguments:
                    default = literal(argument["default_value"]) if "default_value" in argument else "nullptr"
                    lines.append(f"    {{{literal(argument['name'])}, {literal(argument['type'])}, {default}}},")
                lines.append("};")
            wrapper = "nullptr"
            if utility and not vararg:
                wrapper = f"call{suffix}"
                cpp_numeric = {"float": "double", "int": "int64_t"}
                forwarded = ", ".join(f"static_cast<{cpp_numeric.get(argument['type'], argument['type'])}>(*args[{index}])"
                                      for index, argument in enumerate(arguments))
                signature = "const godot::Variant *const *args" if arguments else "const godot::Variant *const *"
                function = "type_of" if name == "typeof" else name
                expression = f"UtilityFunctions::{function}({forwarded})"
                if name == "is_instance_valid":
                    # godot-cpp omits this utility because raw Object wrappers cannot guarantee liveness.
                    expression = "args[0]->get_type() == Variant::OBJECT && args[0]->get_validated_object() != nullptr"
                lines.extend((
                    f"/// Invoke Godot's typed wrapper for the {name} utility function.",
                    f"Variant {wrapper}({signature}) {{",
                    f"    {'return ' if result else ''}{expression};",
                ))
                if not result:
                    lines.append("    return {};")
                lines.append("}")
            rows.append((owner, name, result, f"arguments{suffix}" if arguments else "nullptr", len(arguments),
                         method.get("is_const", False), static, vararg, method["hash"], wrapper))

    lines.extend(("} // namespace", "", "extern const BuiltinMethod builtin_methods[] = {"))
    for owner, name, result, argument_data, count, const, static, vararg, method_hash, wrapper in rows:
        flags = ", ".join(str(flag).lower() for flag in (const, static, vararg))
        lines.append(f"    {{{literal(owner)}, {literal(name)}, {literal(result)}, {argument_data}, {count}, "
                     f"{flags}, {method_hash}u, {wrapper}}},")
    lines.extend(("};", "extern const size_t builtin_methods_count = sizeof(builtin_methods) / sizeof(builtin_methods[0]);", "",
                  "extern const GlobalConstant global_constants[] = {"))
    constants = [(item["name"], "", item["value"], item["is_bitfield"]) for item in api["global_constants"]]
    for enumeration in api["global_enums"]:
        constants.extend((item["name"], enumeration["name"], item["value"], enumeration["is_bitfield"])
                         for item in enumeration["values"])
    for name, group, value, bitfield in sorted(constants):
        lines.append(f"    {{{literal(name)}, {literal(group)}, {integer_literal(value)}, {str(bitfield).lower()}}},")
    lines.extend(("};", "extern const size_t global_constants_count = sizeof(global_constants) / sizeof(global_constants[0]);", "",
                  "extern const BuiltinSingleton builtin_singletons[] = {"))
    for item in sorted(api["singletons"], key=lambda singleton: singleton["name"]):
        lines.append(f"    {{{literal(item['name'])}, {literal(item['type'])}}},")
    lines.extend(("};", "extern const size_t builtin_singletons_count = sizeof(builtin_singletons) / sizeof(builtin_singletons[0]);", "",
                  "} // namespace crimson::visual_scripting", ""))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
