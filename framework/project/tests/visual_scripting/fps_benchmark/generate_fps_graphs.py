# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Generate equivalent Crimson and GDScript pickups from the shared FPS scenarios."""

import argparse
import json
import re
from pathlib import Path


PIN_TYPES = {"variant": 0, "bool": 1, "int": 2, "vec3": 9, "name": 21, "object": 24}
GD_TYPES = {"variant": "Variant", "bool": "bool", "int": "int", "vec3": "Vector3", "name": "StringName"}
OPERATORS = {
    "add": ("add", "+"), "sub": ("subtract", "-"),
    "lt": ("less", "<"), "le": ("less_equal", "<="), "eq": ("equal", "=="),
    "gt": ("greater", ">"), "ge": ("greater_equal", ">="),
}
COPYRIGHT = "Copyright Francesco Di Blasi 2026 - All Rights Reserved"


class GodotLiteral(str):
    """Store an already formatted Godot source literal."""


def resource_value(value, indent=0):
    """Write a Godot Variant literal, including its typed names and vectors."""
    if isinstance(value, GodotLiteral):
        return str(value)
    if isinstance(value, dict):
        if not value:
            return "{}"
        if all(not isinstance(item, (dict, list)) for item in value.values()):
            return "{" + ", ".join(
                f"{json.dumps(key)}: {resource_value(item)}" for key, item in value.items()
            ) + "}"
        prefix = "\t" * (indent + 1)
        lines = [
            f"{prefix}{json.dumps(key)}: {resource_value(item, indent + 1)}"
            for key, item in value.items()
        ]
        return "{\n" + ",\n".join(lines) + "\n" + "\t" * indent + "}"
    if isinstance(value, list):
        if not value:
            return "[]"
        prefix = "\t" * (indent + 1)
        return "[\n" + ",\n".join(
            prefix + resource_value(item, indent + 1) for item in value
        ) + "\n" + "\t" * indent + "]"
    return json.dumps(value)


def name_literal(value):
    return GodotLiteral("&" + json.dumps(value))


def literal_type(value):
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, int):
        return "int"
    if isinstance(value, dict) and set(value) == {"name"}:
        return "name"
    if isinstance(value, dict) and set(value) == {"vec3"}:
        return "vec3"
    raise ValueError(f"Unsupported literal {value!r}")


def literal_value(value):
    kind = literal_type(value)
    if kind == "name":
        return name_literal(value["name"])
    if kind == "vec3":
        if len(value["vec3"]) != 3:
            raise ValueError("Vector3 literals must contain three coordinates.")
        return GodotLiteral(f"Vector3({', '.join(map(str, value['vec3']))})")
    return value


def pin(pin_id, label, kind, class_name=None):
    descriptor = {"id": name_literal(pin_id), "label": label, "type": PIN_TYPES[kind]}
    if class_name is not None:
        descriptor["class"] = name_literal(class_name)
    return descriptor


def wire(producer, output_pin, consumer, input_pin):
    return {"from": producer, "from_pin": name_literal(output_pin), "to": consumer, "to_pin": name_literal(input_pin)}


class CrimsonFunction:
    """Lower the scenario to the same native member and control nodes used by the graph editor."""

    def __init__(self, methods):
        self.methods = methods
        self.nodes = []
        self.connections = []
        self.row = 0
        self.entry = self.node("function_entry")

    def node(self, kind, **data):
        node_id = len(self.nodes) + 1
        if kind == "operator":
            x_pos, y_pos = max(0, (node_id - 2) * 260), 325 + (self.row % 3) * 160
            self.row += 1
        else:
            x_pos, y_pos = (node_id - 1) * 260, 0
        self.nodes.append({
            "id": node_id, "kind": kind, "position": GodotLiteral(f"Vector2({x_pos}, {y_pos})"), **data
        })
        return node_id

    def connect(self, producer, output_pin, consumer, input_pin):
        self.connections.append(wire(producer, output_pin, consumer, input_pin))

    def input(self, consumer, input_pin, expression, scope):
        if isinstance(expression, dict) and set(expression) == {"ref"}:
            self.connect(*scope[expression["ref"]], consumer, input_pin)
        elif isinstance(expression, list):
            operator, *operands = expression
            if operator not in OPERATORS or len(operands) != 2:
                raise ValueError(f"Invalid operator expression {expression!r}")
            result_node = self.node(
                "operator", operator=OPERATORS[operator][0],
                pin=pin("value", "Value", "int"),
            )
            for pin_id, operand in zip(("a", "b"), operands):
                self.input(result_node, pin_id, operand, scope)
            self.connect(result_node, "result", consumer, input_pin)
        else:
            self.nodes[consumer - 1].setdefault("values", {})[input_pin] = literal_value(expression)

    def call(self, step, entry, scope):
        method = self.methods[step["call"]]
        if len(step["args"]) != len(method["args"]):
            raise ValueError(f"Wrong argument count for {step['call']}")
        member = {
            "class": name_literal(method["class"]), "name": name_literal(method["name"]),
            "inputs": [pin(f"arg_{index}", arg["name"], arg["type"]) for index, arg in enumerate(method["args"])],
        }
        if method.get("const", False):
            member["const"] = True
        if method["returns"] is not None:
            member["output"] = pin("result", "Result", method["returns"])
        node_id = self.node("call_method", member=member)
        self.connect(*entry, node_id, "in")
        target = step["target"]
        if target == "body":
            self.connect(self.entry, "body", node_id, "target")
        elif target != "self":
            raise ValueError(f"Unsupported target {target!r}")
        for index, argument in enumerate(step["args"]):
            self.input(node_id, f"arg_{index}", argument, scope)
        if "store" in step:
            if method["returns"] is None:
                raise ValueError(f"Method {step['call']} does not return a value.")
            scope[step["store"]] = (node_id, "result")
        return node_id, "then"

    def flow(self, steps, entry, scope):
        for index, step in enumerate(steps):
            if "call" in step:
                entry = self.call(step, entry, scope)
            elif "if" in step:
                if index != len(steps) - 1:
                    raise ValueError("Branches must end their list of steps.")
                branch = self.node("branch")
                self.connect(*entry, branch, "in")
                self.input(branch, "condition", step["if"], scope)
                self.flow(step["then"], (branch, "true"), scope.copy())
                self.flow(step["else"], (branch, "false"), scope.copy())
            else:
                raise ValueError(f"Unsupported step {step!r}")

    def build(self, steps):
        self.flow(steps, (self.entry, "then"), {})
        return {"version": 1, "nodes": self.nodes, "connections": self.connections}


def crimson_graph(methods, scenario):
    argument = pin("body", "Body", "object", "Node3D")
    signal_arg = pin("arg_0", "Body", "object", "Node3D")
    graph = {
        "version": 1,
        "nodes": [
            {"id": 1, "kind": "native_signal", "class": name_literal("Area3D"),
             "source": GodotLiteral('NodePath(".")'), "signal": name_literal("body_entered"),
             "inputs": [signal_arg], "position": GodotLiteral("Vector2(0, 0)")},
            {"id": 2, "kind": "call_function", "definition": 1, "position": GodotLiteral("Vector2(300, 0)")},
        ],
        "connections": [wire(1, "then", 2, "in"), wire(1, "arg_0", 2, "body")],
        "functions": [{"id": 1, "name": name_literal("apply_action"), "inputs": [argument], "outputs": [],
                       "graph": CrimsonFunction(methods).build(scenario["steps"])}],
    }
    return f"; {COPYRIGHT}\n\n" + resource_value({"base": name_literal("Area3D"), "graph": graph}) + "\n"


def gd_expression(expression):
    if isinstance(expression, dict) and set(expression) == {"ref"}:
        return expression["ref"]
    if isinstance(expression, list):
        operator, *operands = expression
        return f"({gd_expression(operands[0])} {OPERATORS[operator][1]} {gd_expression(operands[1])})"
    return resource_value(literal_value(expression))


def gd_steps(steps, methods, indent):
    prefix = "\t" * indent
    if not steps:
        return [prefix + "pass"]
    lines = []
    for step in steps:
        if "call" in step:
            method = methods[step["call"]]
            arguments = ", ".join(gd_expression(arg) for arg in step["args"])
            call = f"{step['target']}.{method['name']}({arguments})"
            if "store" in step:
                result_type = GD_TYPES[step.get("type", method["returns"])]
                call = f"var {step['store']}: {result_type} = {call}"
            lines.append(prefix + call)
        else:
            lines.append(prefix + f"if {gd_expression(step['if'])}:")
            lines.extend(gd_steps(step["then"], methods, indent + 1))
            if step["else"]:
                lines.append(prefix + "else:")
                lines.extend(gd_steps(step["else"], methods, indent + 1))
    return lines


def gdscript(methods, scenario):
    return "\n".join([
        f"# {COPYRIGHT}", "extends Area3D", "", "",
        "## Forward the native interaction signal to the scenario function.",
        "func _on_body_entered(body: Node3D) -> void:", "\tapply_action(body)", "", "",
        f"## {scenario['label']} using the same native calls as the visual graph.",
        "func apply_action(body: Node3D) -> void:", *gd_steps(scenario["steps"], methods, 1), "",
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("scenarios.json"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    if manifest["version"] != 1:
        raise ValueError("Unsupported gameplay scenario manifest version.")
    args.output.mkdir(parents=True, exist_ok=True)
    scenario_ids = set()
    for scenario in manifest["scenarios"]:
        scenario_id = scenario["id"]
        if not re.fullmatch(r"[a-z][a-z0-9_]*", scenario_id) or scenario_id in scenario_ids:
            raise ValueError(f"Invalid or duplicate scenario ID {scenario_id!r}")
        scenario_ids.add(scenario_id)
        graph = args.output / f"{scenario_id}.crimsonscript"
        script = args.output / f"{scenario_id}.gd"
        graph.write_text(crimson_graph(manifest["methods"], scenario), encoding="utf-8", newline="\n")
        script.write_text(gdscript(manifest["methods"], scenario), encoding="utf-8", newline="\n")
    print(f"Generated {len(scenario_ids)} Crimson and GDScript scenario pairs.")


if __name__ == "__main__":
    main()
