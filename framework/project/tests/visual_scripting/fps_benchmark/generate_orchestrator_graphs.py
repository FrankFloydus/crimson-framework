# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Translate the shared FPS benchmark manifest into one Orchestrator graph per scenario."""
from argparse import ArgumentParser
from dataclasses import dataclass
import json
from pathlib import Path
import re
import uuid


TYPES = {"variant": 0, "bool": 1, "int": 2, "float": 3, "string": 4, "vec3": 9, "name": 21, "object": 24}
NATIVE_TYPES = {"Variant": "variant", "bool": "bool", "int": "int", "float": "float", "String": "string",
                "StringName": "name", "Vector3": "vec3"}
OPERATIONS = {
    "eq": (0, "==", "Equal", None, "bool"),
    "ne": (1, "!=", "Not equal", None, "bool"),
    "lt": (2, "<", "Less-than", "int", "bool"),
    "le": (3, "<=", "Less-than or equal", "int", "bool"),
    "gt": (4, ">", "Greater-than", "int", "bool"),
    "ge": (5, ">=", "Greater-than or equal", "int", "bool"),
    "add": (6, "+", "Addition", "int", "int"),
    "sub": (7, "-", "Subtract", "int", "int"),
    "mul": (8, "*", "Multiply", "int", "int"),
    "mod": (12, "%", "Modulo", "int", "int"),
    "and": (20, "and", "And", "bool", "bool"),
    "or": (21, "or", "Or", "bool", "bool"),
    "not": (23, "not", "Not", "bool", "bool"),
}
USAGE_VARIANT = 131078


@dataclass(frozen=True)
class Symbol:
    """A serialized StringName value."""
    value: str


@dataclass(frozen=True)
class Raw:
    """A serialized Godot value whose grammar is not JSON."""
    text: str


@dataclass(frozen=True)
class Port:
    """Source or destination node ID and direction-local pin index."""
    node: int
    index: int


@dataclass(frozen=True)
class Term:
    """An expression source with its effective type and either a wire or a literal."""
    type: str
    value: object


def gd(value):
    """Encode the Godot Variant subset needed by the graph schema."""
    if isinstance(value, Symbol):
        return "&" + json.dumps(value.value, ensure_ascii=False)
    if isinstance(value, Raw):
        return value.text
    if isinstance(value, list):
        return "[" + ", ".join(map(gd, value)) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{gd(key)}: {gd(item)}" for key, item in value.items()) + "}"
    return "null" if value is None else json.dumps(value, ensure_ascii=False, allow_nan=False)


def array(type_name, values):
    """Encode a typed Godot array."""
    return f"Array[{type_name}]({gd(list(values))})"


def refs(type_name, names):
    """Encode a list of references to subresources in the same graph."""
    return array(type_name, [Raw(f"SubResource({gd(name)})") for name in names])


def block(type_name, resource_id, attributes):
    """Serialize a .torch subresource with its named attributes."""
    return "\n".join([f'[obj type="{type_name}" id="{resource_id}"]',
                      *(f"{name} = {gd(value)}" for name, value in attributes.items())])


def signature(name, type_name, object_class=None):
    """Encode reflected method and function arguments."""
    descriptor = {"name": Symbol(name)} if name else {}
    if type_name != "variant":
        descriptor["type"] = TYPES[type_name]
    else:
        descriptor["usage"] = USAGE_VARIANT
    if object_class:
        descriptor["class_name"] = Symbol(object_class)
    return descriptor


def pin(name, type_name=None, *, output=False, flags=None, object_class=None, default=...):
    """Encode execution and data pins in their numeric connection order."""
    descriptor = {"pin_name": Symbol(name), "flags": flags if flags is not None else (2 if type_name else 4)}
    if output:
        descriptor["dir"] = 1
    if type_name == "variant":
        descriptor["usage"] = USAGE_VARIANT
    elif type_name:
        descriptor["type"] = TYPES[type_name]
    if object_class:
        descriptor["target_class"] = object_class
    if default is not ...:
        descriptor["dv"] = default
    return descriptor


def parse_native_default(value, type_name):
    """Decode the handful of native argument defaults supported by the shared manifest."""
    if value in ("null", "false", "true") or type_name in {"int", "float"}:
        return json.loads(value)
    raise ValueError(f"Unrecognized native default {value!r} for {type_name}")


class Translator:
    """Emit stock Orchestrator nodes for every operation in a shared FPS scenario."""

    def __init__(self, scenario, methods, native_classes):
        self.scenario = scenario
        self.methods = methods
        self.native_classes = native_classes
        self.nodes = []
        self.wires = []
        self.functions = []
        self.graphs = []

    def node(self, kind, inputs, outputs, **attributes):
        """Add the next editor-visible node."""
        node_id = len(self.nodes)
        resource_id = f"{kind}_{node_id}"
        x = (node_id % 8) * 230
        y = (node_id // 8) * 200
        self.nodes.append((resource_id, block("OScriptNode" + kind, resource_id, {
            "id": node_id, "position": Raw(f"Vector2({x}, {y})"),
            "pin_data": Raw(array("Dictionary", inputs + outputs)), **attributes,
        })))
        return node_id

    def connect(self, source, target, target_pin=0):
        """Connect one numbered output pin to an input pin."""
        self.wires.extend([source.node, source.index, target, target_pin])

    def function(self, name):
        """Begin one authored Godot-callable graph with its typed function entry."""
        guid = str(uuid.uuid5(uuid.NAMESPACE_URL, f"crimson-fps:{self.scenario['id']}:{name}")).upper()
        entry = self.node("FunctionEntry", [], [pin("ExecOut", output=True), pin("body", "object", output=True,
                                                                                object_class="Node3D")],
                          function_id=guid)
        args = [signature("body", "object", "Node3D")]
        self.functions.append((name, block("OScriptFunction", name, {
            "guid": guid, "method": {"name": Symbol(name), "args": args}, "user_defined": True, "id": entry,
        })))
        return guid, entry

    def finish_function(self, name, guid, entry, exits):
        """Terminate every live path and register the function's owned nodes."""
        result = self.node("FunctionResult", [pin("ExecIn")], [], function_id=guid)
        for exit_port in exits:
            self.connect(exit_port, result)
        self.graphs.append(("Graph" + name, block("OScriptGraph", "Graph" + name, {
            "graph_name": Symbol(name), "flags": 22, "nodes": Raw(array("int", range(entry, result + 1))),
            "functions": Raw(array("int", [entry])),
        })))

    def expression(self, expr, values):
        """Resolve one shared expression into a literal or stock typed operator."""
        if isinstance(expr, bool):
            return Term("bool", expr)
        if isinstance(expr, int):
            return Term("int", expr)
        if isinstance(expr, float):
            return Term("float", expr)
        if isinstance(expr, dict):
            if "ref" in expr:
                return values[expr["ref"]]
            if "name" in expr:
                return Term("name", Symbol(expr["name"]))
            if "vec3" in expr:
                return Term("vec3", Raw("Vector3(" + ", ".join(map(str, expr["vec3"])) + ")"))
        if not isinstance(expr, list) or not expr:
            raise ValueError(f"Unsupported expression {expr!r}")
        operation, *operands = expr
        opcode, symbol, label, operand_type, result_type = OPERATIONS[operation]
        terms = [self.expression(term, values) for term in operands]
        if len(terms) != (1 if operation == "not" else 2):
            raise ValueError(f"Invalid operand count {expr!r}")
        if operand_type is None:
            operand_type = terms[0].type if terms[0].type != "variant" else terms[1].type
        left_type = operand_type
        right_type = operand_type if len(terms) == 2 else None
        inputs = [pin(name, operand_type, default=... if isinstance(term.value, Port) else term.value)
                  for name, term in zip(("a", "b"), terms)]
        node = self.node("Operator", inputs, [pin("result", result_type, output=True)], op=opcode, code=Symbol(symbol),
                         name=Symbol(label), left_type=TYPES[left_type], left_type_name=Symbol(left_type),
                         right_type=TYPES[right_type] if right_type else 0,
                         right_type_name=Symbol(right_type) if right_type else Symbol(""),
                         return_type=TYPES[result_type])
        for index, term in enumerate(terms):
            if isinstance(term.value, Port):
                self.connect(term.value, node, index)
        return Term(result_type, Port(node, 0))

    def method(self, step, values, execution, body, self_port):
        """Run a reflected native method once from an explicit execution edge."""
        alias = step["call"]
        spec = self.methods[alias]
        class_name = spec["class"]
        name = spec["name"]
        api = self.native_classes[class_name][name]
        arguments = api.get("arguments", [])
        native_return = api.get("return_value")
        actual_types = [NATIVE_TYPES[argument["type"]] for argument in arguments]
        return_type = NATIVE_TYPES[native_return["type"]] if native_return else None
        if return_type != spec["returns"]:
            raise ValueError(f"Return type for {alias} differs from the public Godot API")
        if [argument["type"] for argument in spec["args"]] != actual_types:
            raise ValueError(f"Parameter types for {alias} differ from the public Godot API")
        expressions = step.get("args", [])
        terms = [self.expression(expr, values) for expr in expressions]
        defaults = [parse_native_default(arg["default_value"], type_name)
                    for arg, type_name in zip(arguments, actual_types) if "default_value" in arg]
        min_args = len(arguments) - len(defaults)
        if not min_args <= len(terms) <= len(arguments):
            raise ValueError(f"Wrong number of inputs for {alias}")
        if return_type and not arguments and (name.startswith("get_") or name.startswith("is_")):
            raise ValueError(f"Zero-argument getter {name} needs a stock local assignment to execute exactly once")
        descriptor = {"name": Symbol(name), "args": [signature(arg["name"], type_name)
                                                       for arg, type_name in zip(arguments, actual_types)]}
        if defaults:
            descriptor["default_args"] = defaults
        if api["is_const"]:
            descriptor["flags"] = 5
        if return_type:
            descriptor["return"] = signature(None, return_type)
        input_pins = [pin("ExecIn"), pin("target", "object", flags=2050, object_class=class_name)]
        for index, (arg, type_name) in enumerate(zip(arguments, actual_types)):
            value = terms[index].value if index < len(terms) else defaults[index - min_args]
            input_pins.append(pin(arg["name"], type_name, default=... if isinstance(value, Port) else value))
        output_pins = [pin("ExecOut", output=True)]
        if return_type:
            output_pins.append(pin("return_value", return_type, output=True, flags=1026))
        node = self.node("CallMemberFunction", input_pins, output_pins, function_name=Symbol(name),
                         target_class_name=class_name, target_type=TYPES["object"], flags=522 if api["is_const"] else 520,
                         method=descriptor, chain=False)
        self.connect(execution, node)
        target = step.get("target", "body")
        if target not in {"body", "self"}:
            raise ValueError(f"Unsupported method target: {target!r}")
        self.connect(body if target == "body" else self_port, node, 1)
        for index, term in enumerate(terms):
            if isinstance(term.value, Port):
                self.connect(term.value, node, index + 2)
        if "store" in step:
            if not return_type:
                raise ValueError(f"{alias} returns no value")
            values[step["store"]] = Term(step.get("type", return_type), Port(node, 1))
        return Port(node, 0)

    def steps(self, steps, values, execution, body, self_port):
        """Emit explicit execution paths and independent branch data scopes."""
        for index, step in enumerate(steps):
            if "call" in step:
                execution = self.method(step, values, execution, body, self_port)
            elif "if" in step:
                if index + 1 != len(steps):
                    raise ValueError("Only terminal branches are supported in the shared FPS manifest")
                condition = self.expression(step["if"], values)
                node = self.node("Branch", [pin("ExecIn", flags=516),
                                            pin("condition", "bool", default=... if isinstance(condition.value, Port) else condition.value)],
                                 [pin("true", output=True, flags=516), pin("false", output=True, flags=516)])
                self.connect(execution, node)
                if isinstance(condition.value, Port):
                    self.connect(condition.value, node, 1)
                yes = self.steps(step.get("then", []), dict(values), Port(node, 0), body, self_port)
                no = self.steps(step.get("else", []), dict(values), Port(node, 1), body, self_port)
                return yes + no
            else:
                raise ValueError(f"Unsupported step {step!r}")
        return [execution]

    def render(self):
        """Translate signal handler and action function into an editable graph resource."""
        action_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, f"crimson-fps:{self.scenario['id']}:apply_action")).upper()
        handler_guid, handler_entry = self.function("_on_body_entered")
        caller = self.node("CallScriptFunction", [pin("ExecIn"), pin("body", "object", object_class="Node3D")],
                           [pin("ExecOut", output=True)], guid=action_guid, function_name=Symbol("apply_action"), flags=8)
        self.connect(Port(handler_entry, 0), caller)
        self.connect(Port(handler_entry, 1), caller, 1)
        self.finish_function("_on_body_entered", handler_guid, handler_entry, [Port(caller, 0)])
        action_guid, action_entry = self.function("apply_action")
        # The stock Self node is pure, so only paths using it trigger a runtime read.
        self_node = self.node("Self", [], [pin("self", "object", output=True, object_class="Area3D")])
        exits = self.steps(self.scenario["steps"], {}, Port(action_entry, 0), Port(action_entry, 1), Port(self_node, 0))
        self.finish_function("apply_action", action_guid, action_entry, exits)

        self.graphs.insert(0, ("EventGraph", block("OScriptGraph", "EventGraph", {
            "graph_name": Symbol("EventGraph"), "flags": 8})))
        resources = self.functions + self.graphs + self.nodes
        root = {
            "base_type": Symbol("Area3D"), "functions": Raw(refs("OScriptFunction", [name for name, _ in self.functions])),
            "graphs": Raw(refs("OScriptGraph", [name for name, _ in self.graphs])),
            "nodes": Raw(refs("OScriptNode", [name for name, _ in self.nodes])),
            "connections": Raw(array("int", self.wires)),
        }
        return "\n\n".join([f'[orchestration type="OScript" load_steps={len(resources) + 1} format=4]',
                             *(part for _, part in resources),
                             "\n".join(["[resource]", *(f"{key} = {gd(value)}" for key, value in root.items())])]) + "\n"


def main():
    """Generate stock Orchestrator files without modifying the source manifest or package."""
    cli = ArgumentParser(description=__doc__)
    cli.add_argument("manifest", type=Path)
    cli.add_argument("godot_api", type=Path)
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    if manifest["version"] != 1:
        raise SystemExit("Unsupported FPS benchmark manifest version")
    api = json.loads(args.godot_api.read_text(encoding="utf-8-sig"))
    classes = {record["name"]: {method["name"]: method for method in record.get("methods", [])} for record in api["classes"]}
    args.output.mkdir(parents=True, exist_ok=True)
    scenarios = set()
    for scenario in manifest["scenarios"]:
        scenario_id = scenario["id"]
        if not re.fullmatch(r"[a-z][a-z0-9_]*", scenario_id) or scenario_id in scenarios:
            raise ValueError(f"Invalid or duplicate scenario ID {scenario_id!r}")
        scenarios.add(scenario_id)
        try:
            torch = Translator(scenario, manifest["methods"], classes).render()
        except (KeyError, ValueError) as error:
            raise ValueError(f"{scenario_id}: {error}") from error
        (args.output / f"{scenario_id}.torch").write_text(torch, encoding="utf-8", newline="\n")
    print(f"Generated {len(scenarios)} Orchestrator scenarios.")


if __name__ == "__main__":
    main()
