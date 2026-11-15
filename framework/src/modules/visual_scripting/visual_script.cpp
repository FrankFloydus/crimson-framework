// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script.hpp"

#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_builtin_data.hpp"
#include "modules/visual_scripting/visual_script_cast_input.hpp"
#include "modules/visual_scripting/visual_script_collections.hpp"
#include "modules/visual_scripting/visual_script_custom_source.hpp"
#include "modules/visual_scripting/visual_script_debugger.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_language.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"
#include "modules/visual_scripting/visual_script_value_nodes.hpp"
#include <godot_cpp/classes/class_db_singleton.hpp>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant_internal.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace crimson::visual_scripting {

/// Generated helper classes for the current authored graph revision.
struct CustomScriptCache {
    /// Distinguishes helpers that compiled from helpers with GDScript syntax or type errors.
    struct Entry {
        /// Script class instantiated independently for each node execution.
        godot::Ref<godot::GDScript> script;
        /// Error returned by Godot's GDScript compiler.
        godot::Error error = godot::ERR_PARSE_ERROR;
    };

    /// Protect lookup and publication across simultaneous runtime preparations.
    std::mutex mutex;
    /// Identical generated source within one graph revision shares a compiled resource.
    std::map<godot::String, Entry> entries;
};

/// A stable revision of the authored properties exposed to Godot.
struct VisualScriptExports {
    /// One graph variable exposed to the owner without changing its declaration or slot.
    struct Variable {
        /// Stable ID of the existing graph declaration.
        int64_t id;
        /// Inspector and script reflection metadata.
        godot::PropertyInfo property;
        /// Authored graph pin used to restore defaults, including external resources.
        godot::Dictionary pin;
        /// Serialized default stored in the graph.
        godot::Variant value;
    };

    /// Source fields that can change exported properties without changing unrelated graph data.
    godot::Array declarations;
    /// Graph enums supplying names and values for exported enum fields.
    godot::Array enums;
    /// The owner base class used to check native property names.
    godot::StringName base;
    /// Distinct native classes currently editing this script.
    std::set<godot::StringName> owner_types;
    /// Stored values and defaults kept in authored order.
    std::vector<Variable> variables;
    /// Published property rows and group headings, in Inspector display order.
    std::vector<godot::PropertyInfo> properties;
    /// Lookup by the Godot property name, including defaults that are null.
    std::map<godot::StringName, size_t> indices;
};

namespace {

using namespace godot;

/// The file and editor transaction schema for the initial graph format.
constexpr int graph_version = 1;
/// Limit synchronous Ready work even if a graph asset is edited by hand.
constexpr int64_t max_graph_nodes = 4096;

/// Accept only native Godot node classes as script attachment bases.
bool is_node_class(const StringName &class_name) {
    return ClassDB::class_exists(class_name) && ClassDB::is_parent_class(class_name, "Node");
}

/// Treat plain text and interned text equally in authored graph records.
bool is_text(const Variant &value) {
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME;
}

/// Resolve an edge endpoint without accepting numeric casts or a missing ID.
int64_t read_id(const Variant &value) {
    return value.get_type() == Variant::INT ? static_cast<int64_t>(value) : 0;
}

/// Check the graph header while allowing an editor draft with disconnected ports.
bool has_graph_header(const Dictionary &value) {
    const Variant version = value.get("version", Variant());
    return version.get_type() == Variant::INT && static_cast<int64_t>(version) == graph_version &&
           value.get("nodes", Variant()).get_type() == Variant::ARRAY &&
           value.get("connections", Variant()).get_type() == Variant::ARRAY;
}

struct Instruction;
struct Program;
struct Runtime;
struct SuspendedDispatch;

/// Invalid index for an unconnected data pin or an unresolved variable.
constexpr size_t no_slot = std::numeric_limits<size_t>::max();

/// An execution edge names the destination and input, and caches its instruction.
struct Edge {
    /// Stable destination node, or zero at the end of a path.
    int64_t node = 0;
    /// Destination execution pin.
    StringName pin;
    /// Resolved after the graph is wired. Program node addresses remain stable for its lifetime.
    const Instruction *target = nullptr;
};

/// Dispatch codes derived from authored node names during compilation.
enum class Op {
    MAKE_VALUE, BREAK_VALUE, COLLECTION, PRELOAD_RESOURCE, LOAD_RESOURCE, CREATE_SCRIPT_INSTANCE,
    OTHER, SELF, SCENE_NODE, SINGLETON, GET_PROPERTY, GET_VARIABLE, OPERATOR, LITERAL, SELECT, REROUTE,
    RETURN, BREAK, CONTINUE, DO_ONCE, DO_N, FLIP_FLOP, GATE, BRANCH, SWITCH, SWITCH_ENUM,
    SEQUENCE, WHILE, FOR, FOREACH, PRINT, CREATE_NODE, ADD_CHILD, SET_PROPERTY, CALL_METHOD, CAST, INPUT_POLL,
    SET_VARIABLE, CALL_MACRO, CALL_FUNCTION, EMIT_EVENT, CUSTOM_SCRIPT, DELAY, AWAIT_SIGNAL,
};

/// Compile one input source and its value rules.
enum class InputSource { MISSING, VALUE, COPY, ASSET, OWNER, WIRED, CODE_DEFAULT, SCRIPT_DEFAULT, COLLECTION_PLACEHOLDER };

/// One data input and its optional producer.
struct Input {
    /// Original pin descriptor used for wire validation and debugger display.
    Dictionary pin;
    /// Stable pin identity for named call boundaries and debugger snapshots.
    StringName name;
    /// Decoded runtime type and optional native object class.
    RuntimePinType type;
    /// Source node and its output pin.
    Edge wire;
    /// Indexed output of the producer when wired.
    size_t slot = no_slot;
    /// Authored value or reflected default.
    Variant literal;
    /// How to obtain this value at runtime.
    InputSource source = InputSource::MISSING;
    /// Typed primitive wires and constants have already been checked by the producer or compiler.
    bool validate_value = true;
    /// Both pins establish a compatible native object type; recheck existence on each read.
    bool validate_object_lifetime = false;
#ifdef DEBUG_ENABLED
    /// Human-readable origin for a local literal or reflected default.
    String debug_source;
#endif
};

/// A compiled output retains its saved identity only for dynamic boundaries and debugging.
struct Output {
    /// Original descriptor for wire validation and debugger display.
    Dictionary pin;
    /// Runtime type checked when publishing a value.
    RuntimePinType type;
    /// Index of the value in the invocation's slot buffer.
    size_t slot = no_slot;
};

/// One compiled argument of a Godot signal, in the source signal's order.
struct SignalArgument {
    /// Saved argument identity for debugger snapshots and unusual descriptors.
    StringName name;
    /// Descriptor used for authored defaults.
    Dictionary pin;
    /// Compiled type checked at Godot's call boundary.
    RuntimePinType type;
    /// Output to publish, if one survived graph pin validation.
    const Output *output = nullptr;
    /// Whether the authored descriptor includes a trailing default.
    bool has_default = false;
    /// Whether boundary validation already checks the output's exact type.
    bool validates_output = false;
};

/// How much of a call's argument validation remains at entry.
enum class EntryCheck { FULL, LIFETIME, NONE };

/// Callee slot and its remaining validation in caller argument order.
struct EntryOutput {
    /// Stable compiled output on the function entry node.
    const Output *output;
    /// Caller type proof, with object liveness checked again after the other arguments.
    EntryCheck check;
};

/// Bound the amount of duplicated expression code and the runtime scratch space per action.
constexpr size_t max_scalar_steps = 64;

/// Input reads and operations in a compiled Int or Bool expression.
enum class ScalarOp : uint8_t {
    CONSTANT, WIRE_INT, WIRE_BOOL, CHECKED_WIRE_INT, CHECKED_WIRE_BOOL, LOCAL_INT, LOCAL_BOOL, MEMBER_INT, MEMBER_BOOL,
    ADD, SUBTRACT, MULTIPLY, DIVIDE, REMAINDER, EQUAL, NOT_EQUAL, LESS, LESS_EQUAL,
    GREATER, GREATER_EQUAL, AND, OR, NOT, NEGATE,
};

/// A scalar instruction writes to its own result index; operands refer to earlier instructions.
struct ScalarStep {
    /// Constant value or the indexed input/variable to read.
    int64_t operand = 0;
    /// Primitive operation or value source.
    ScalarOp op = ScalarOp::CONSTANT;
    /// Indices for the left and right arguments of an operation.
    uint8_t left = 0, right = 0;
};

/// A bounded expression over constants, typed variables, and checked published slots.
struct ScalarPlan {
    /// Postorder instructions over exact scalar values, validating dynamic slots on read.
    std::vector<ScalarStep> steps;
    /// Unique pure nodes evaluated by the standard interpreter for the same input.
    size_t dispatch_cost = 0;
    /// Index of the finished expression value.
    uint8_t result = 0;
    /// The required type of the input and, for a setter, the stored value.
    Variant::Type type = Variant::NIL;
};

/// The compiled helper and output layout of a Custom Script action.
struct CustomAction {
    /// Generated GDScript class, independent of any owner or invocation.
    Ref<GDScript> script;
    /// Graph output slots in the helper's return order.
    std::vector<const Output *> outputs;
    /// Byte per data input when at least one input can initialize itself in the body.
    PackedByteArray missing_inputs;
};

/// A checked node ready to execute in a graph instance.
struct Instruction {
    /// Authored node kind.
    StringName kind;
    /// Dispatch opcode and optional Godot operator resolved once.
    Op op = Op::OTHER;
    /// The operator sentinel keeps invalid operations harmless until reached.
    Variant::Operator variant_operator = Variant::OP_MAX;
    /// Native type to assemble or split for value operations.
    Variant::Type value_type = Variant::VARIANT_MAX;
    /// Collection operation resolved once, independent of the authored node's name.
    CollectionOperation collection = CollectionOperation::NONE;
    /// Empty typed dictionary, prepared once and duplicated by each constructor evaluation.
    std::optional<Dictionary> dictionary_template;
    /// Resource retained by a Preload or Create from Script node for the graph instance.
    Ref<Resource> preloaded_resource;
    /// The compiled query for an Input Map polling node.
    InputPollOperation input_poll = InputPollOperation::Invalid;
    /// Data-only producers evaluate on demand and cache one result per action.
    bool pure = false;
    /// Local or instance variable index, when used by this node.
    size_t variable = no_slot;
    /// Whether the variable index addresses invocation-local storage.
    bool local = false;
    /// Whether a typed variable can have an asset reference as its default value.
    bool variable_asset = false;
    /// Cached property, method or class name used by native operations.
    StringName native_name;
    /// Declaring engine class for a receiver-free native ClassDB call.
    StringName native_owner;
    /// Saved Script declaring this member; its presence forces checked dynamic dispatch.
    String member_script_path;
    /// Tag of the built-in receiver, or VARIANT_MAX for Object and global utilities.
    Variant::Type builtin_receiver = Variant::VARIANT_MAX;
    /// A compiled native utility to invoke without a receiver.
    const BuiltinMethod *native_utility = nullptr;
    /// The call has no target pin and dispatches to its declaring type.
    bool native_static = false;
    /// Expand the last Array pin into a variable number of extra method arguments.
    bool native_variadic = false;
    /// Expose a packed array after a mutating method, independently of its result.
    bool native_updated_target = false;
    /// True when a method can borrow all arguments from immutable literals or published slots.
    bool borrow_method_arguments = false;
    /// One native argument may need a compiled scalar expression instead of a borrowed value.
    size_t scalar_method_argument = no_slot;
    /// Cached declaration ID for function, macro and event calls.
    int64_t declaration = 0;
    /// Internal function and corresponding entry pins linked after every program is compiled.
    const Program *callee = nullptr;
    /// Callee outputs in caller argument order. Keep name binding for unusual saved declarations.
    std::vector<EntryOutput> entry_outputs;
    /// Native signal arguments in their Godot order.
    std::vector<SignalArgument> signal_arguments;
    /// Optional synthetic Sender output of a native signal node.
    const Output *signal_sender = nullptr;
    /// Signal payload can be published directly when every output has one source.
    bool signal_slots_match = false;
    /// Data input pins in dispatch order.
    std::vector<Input> inputs;
    /// Data output descriptors keyed by pin ID.
    std::map<StringName, Output> outputs;
    /// Available execution input IDs.
    std::set<StringName> entries;
    /// Execution destinations keyed by the source pin.
    std::map<StringName, Edge> next;
    /// Cached destinations for the normal, branch, and loop paths.
    Edge then_edge, true_edge, false_edge, body_edge, completed_edge;
    /// Named loop outputs, resolved once so their order is independent of StringName ordering.
    const Output *index_output = nullptr;
    const Output *item_output = nullptr;
    /// Dictionary key output, present only for loops over a known Dictionary source.
    const Output *key_output = nullptr;
    /// Cached sequence destinations in authored execution order.
    std::vector<Edge> sequence;
    /// Authored switch case values and their cached destinations.
    std::vector<std::pair<Variant, Edge>> cases;
    /// Saved enum case outputs keyed by the integer Godot supplies at runtime.
    std::map<int64_t, StringName> enum_cases;
    /// A native signal with an explicitly wired, disabled emitter must remain unsubscribed.
    bool signal_target_disabled = false;
    /// Original node metadata needed by this operation.
    Dictionary record;
    /// Compiled helper and output layout when this is a Custom Script action.
    std::unique_ptr<CustomAction> custom_script;
    /// Optional scalar expression compiled for a branch or variable setter.
    ScalarPlan scalar;
#ifdef DEBUG_ENABLED
    /// The designer-facing card title used in debugger descriptions.
    String debug_title;
#endif
};

/// Dense variable storage and the cold lookup from authored IDs to indices.
struct Variables {
    /// Slot index by stable ID, for compilation and debugger labels.
    std::map<int64_t, size_t> indices;
    /// Values in slot order.
    std::vector<Variant> values;
    /// Pin descriptors in slot order for dynamically bound variable reads and writes.
    std::vector<Dictionary> pins;
};

/// One compiled event, function, or macro graph.
struct Program {
    /// Nodes indexed by stable ID.
    std::map<int64_t, Instruction> nodes;
    /// Fresh variable defaults for each invocation.
    Variables locals;
    /// Number of data output slots shared by all invocations of this program.
    size_t slot_count = 0;
    /// Entry ID for a function or macro.
    int64_t entry = 0;
    /// Stable owning declaration ID, or zero for the Event Graph.
    int64_t definition = 0;
    /// True for authored definitions with data-only call sites.
    bool pure = false;
};

/// Signal subscription released with the owning script instance.
struct SignalConnection {
    /// Visual Script instance that owns the compiled receiving instruction.
    std::weak_ptr<Runtime> runtime;
    /// Owner used for Godot's incoming connection tracking.
    ObjectID owner;
    /// Emitter recorded when the signal was connected.
    ObjectID source;
    /// Stable receiving instruction and saved node ID.
    const Instruction *entry;
    int64_t node;
    /// False before disconnect, including while Godot holds an emission snapshot.
    std::atomic<bool> active{true};
};

/// Signal subscription released with the owning script instance.
struct Subscription {
    /// Source object, without extending its lifetime.
    ObjectID source;
    /// Native signal name.
    StringName signal;
    /// Callable retained by the source until disconnection.
    Callable handler;
    /// Non-owning view of userdata kept alive by this callable.
    SignalConnection *connection = nullptr;
};

/// Metadata for a callable event or function exposed by the attached node.
struct GraphMethod {
    /// Graph definition ID for authored callables.
    int64_t definition = 0;
    /// Lifecycle entry or custom event entry, if the method has one.
    int64_t entry = 0;
    /// True for functions with their own program.
    bool function = false;
    /// Ordered runtime argument descriptors.
    Array inputs;
    /// Ordered declared result descriptors.
    Array outputs;
};

/// Cached programs and instance-owned state shared across reentrant callbacks.
struct Runtime {
    /// Attached node, which Godot may free from a graph call.
    ObjectID owner;
    /// Weak self reference for engine signal callbacks without a reference cycle.
    std::weak_ptr<Runtime> self;
    /// Source graph resource for diagnostics and metadata.
    Ref<VisualScript> script;
    /// Checked primary event graph.
    Program events;
    /// Compiled function and macro graphs indexed separately.
    std::map<int64_t, Program> functions, macros;
    /// Godot and authored method signatures cached at attachment.
    std::map<StringName, GraphMethod> methods;
    /// Graph functions and events callable from a Custom Script by declaration ID.
    std::map<std::pair<bool, int64_t>, GraphMethod> custom_methods;
    /// ABI method records and their backing storage remain valid for the life of the instance.
    std::vector<GDExtensionMethodInfo> method_list;
    /// Engine-facing property entries for each callable's argument list.
    std::vector<std::vector<GDExtensionPropertyInfo>> method_arguments;
    /// Engine-facing Variant pointers for each callable's trailing defaults.
    std::vector<std::vector<GDExtensionVariantPtr>> method_defaults;
    /// Owns the method names, argument names, defaults, and return metadata.
    std::vector<MethodInfo> method_storage;
    /// Persistent per-owner variable values.
    Variables variables;
    /// Retains Inspector metadata for this instance's compiled declaration slots.
    std::shared_ptr<const VisualScriptExports> exports;
    /// Godot property metadata backed by the retained export revision.
    std::vector<GDExtensionPropertyInfo> property_list;
    /// State for gate/once/flip nodes, keyed by the owning graph and node.
    std::map<std::pair<int64_t, int64_t>, int64_t> flow_state;
    /// Native subscriptions active while the owner is in the tree.
    std::map<int64_t, Subscription> subscriptions;
    /// Own suspended frames until one emission resumes them or their owner leaves the tree.
    std::map<uint64_t, std::shared_ptr<SuspendedDispatch>> suspended;
    /// Unique per-instance IDs used by queued callbacks, including already canceled callbacks.
    uint64_t next_wait = 0;
    /// A temporary process-frame callback checking emitters that disappear without emitting.
    Callable wait_monitor;
    /// Tree running the temporary monitor for this owner.
    ObjectID wait_tree;
    /// Exit Tree callbacks may execute, but they cannot start a new wait.
    bool exiting_tree = false;
    /// Wired signal entries indexed by actions or event entries their receivers read.
    std::map<int64_t, std::vector<int64_t>> signal_producers;
    /// First structural graph error.
    String error;
    /// Shared dispatch step budget, including reentrant signals.
    size_t steps = 0;
    /// Number of active event/call dispatches.
    size_t depth = 0;
    /// True once native signal binding has run in this tree lifetime.
    bool signals_bound = false;
    /// Re-entering a tree can skip Godot's one-time Ready callback.
    bool reached_ready = false;
    /// Prevents the current graph continuing when its script is replaced reentrantly.
    bool detached = false;

    /// Check that the attached engine object still exists without obtaining a C++ object binding.
    bool has_live_owner() const {
        return !detached && gdextension_interface::object_get_instance_from_id(owner) != nullptr;
    }
};

#ifdef DEBUG_ENABLED
/// Identity of a pin in one invocation, independent of card labels and coordinates.
using DebugPinKey = std::tuple<int64_t, bool, StringName>;

/// A safe display snapshot and the action for which the pin was last observed.
struct DebugPinValue {
    /// Formatted data only, with no references that can keep game objects alive.
    Dictionary row;
    /// Increments when this invocation starts another execution action.
    uint64_t sample = 0;
};
#endif

/// A live slot can hold NIL; an empty slot has not produced a value in this scope.
using ValueSlot = std::optional<Variant>;

/// Previous value of an execution output written inside a loop body.
struct SlotWrite {
    /// Index in the frame's output buffer.
    size_t slot;
    /// Value before the write, including absence.
    ValueSlot previous;
};

/// Store the small argument lists used by most graph nodes in the invocation frame.
class ArgumentValues {
public:
    /// Prepare enough storage for one instruction's inputs.
    void reserve(size_t count) {
        if (count > inline_values.size()) {
            overflow.reserve(count);
            use_overflow = true;
        }
    }

    /// Append an evaluated argument after reserve, copying or transferring its Variant ownership.
    template <typename Value>
    void push_back(Value &&value) {
        if (use_overflow) {
            overflow.emplace_back(std::forward<Value>(value));
        } else {
            inline_values[used++].emplace(std::forward<Value>(value));
        }
    }

    /// Number of active arguments in binding order.
    size_t size() const { return use_overflow ? overflow.size() : used; }
    /// Whether evaluation supplied an argument.
    bool empty() const { return size() == 0; }
    /// Read an argument by compiled index.
    const Variant &operator[](size_t index) const {
        return use_overflow ? overflow[index] : *inline_values[index];
    }
    /// Access an argument for Godot's mutable Variant call interface.
    Variant &operator[](size_t index) {
        return use_overflow ? overflow[index] : *inline_values[index];
    }
    /// Access the first argument.
    const Variant &front() const { return (*this)[0]; }
    /// Access the first argument for Godot's mutable Variant call interface.
    Variant &front() { return (*this)[0]; }

private:
    /// Inline payloads construct only the Variant elements that are used.
    std::array<ValueSlot, 4> inline_values;
    /// Owned spill storage for higher-arity nodes.
    std::vector<Variant> overflow;
    /// Live inline argument count.
    size_t used = 0;
    /// This instruction's arguments occupy the spill buffer.
    bool use_overflow = false;
};

/// Keep short graph invocations' output slots local and allocate larger graphs on demand.
class OutputSlots {
public:
    /// Allocate only when the compiled graph needs more than six slots.
    explicit OutputSlots(size_t count) : overflow(count > local.size() ? count : 0) {}

    /// Access a compiled output slot without moving its stored value.
    ValueSlot &operator[](size_t index) {
        return overflow.empty() ? local[index] : overflow[index];
    }

    /// Read a compiled output slot for inspection.
    const ValueSlot &operator[](size_t index) const {
        return overflow.empty() ? local[index] : overflow[index];
    }

private:
    /// Local storage for event handlers and short functions.
    std::array<ValueSlot, 6> local;
    /// Slot storage for graphs larger than the inline capacity.
    std::vector<ValueSlot> overflow;
};

/// One event or function invocation with fresh locals and outputs.
struct Frame {
    /// Instance and persistent variables shared across calls.
    Runtime &runtime;
    /// Compiled graph for this invocation.
    const Program &program;
    /// Local variables owned by this call.
    std::vector<Variant> locals;
    /// Indexed execution outputs and, during input gathering, pure results.
    OutputSlots slots;
    /// Pure slots occupied in a short expression before the next execution action.
    std::array<size_t, 8> pure_slots;
    /// Number of live indices in the inline pure-slot list.
    size_t pure_slot_count = 0;
    /// Additional pure slots for longer expressions, cleared at the same boundary.
    std::vector<size_t> extra_pure_slots;
    /// Previous values of execution slots written within an active loop.
    std::vector<SlotWrite> loop_writes;
    /// The journal only records writes while at least one loop body is active.
    size_t loop_depth = 0;
    /// Values exposed through the selected Return node.
    std::optional<Dictionary> result;
    /// A nested synchronous call cannot leave work behind after returning to its caller.
    bool can_suspend;
#ifdef DEBUG_ENABLED
    /// Parent graph invocation, valid while this synchronous frame executes.
    const Frame *debug_caller = nullptr;
    /// Current instruction used when a nested graph breaks.
    int64_t debug_node = 0;
    /// Most recent value of every observed pin in this invocation.
    std::map<DebugPinKey, DebugPinValue> debug_pins;
    /// Output pins actually used by the current action, including previously published values.
    std::set<DebugPinKey> debug_reads;
    /// Sequence of the current event or execution action.
    uint64_t debug_sample = 0;
    /// Ignore recording when there is no attached debugger on this thread.
    bool debug_recording = false;
    /// Marks that some pins could not fit the bounded trace.
    bool debug_limited = false;
#endif

    /// Start with empty outputs and independent copies of authored local defaults.
    Frame(Runtime &owner, const Program &graph, const Frame *caller = nullptr);
};

/// Update subscriptions after a root graph action publishes new output values.
void refresh_signal_outputs(Frame &frame, int64_t producer);

/// Control result propagated out of a structured execution path.
enum class Flow { END, RETURN, BREAK, CONTINUE, SUSPEND, ERROR };

/// Resume a caller from a macro tunnel without ending the macro's remaining paths.
using MacroExit = std::function<Flow(const StringName &, const Dictionary &)>;

#ifdef DEBUG_ENABLED
/// Recover authored variable names without evaluating any additional graph nodes at a break.
void inspect_variables(Dictionary &values, const Array &declarations, const Variables &layout, const std::vector<Variant> &stored) {
    for (const Dictionary &declaration : declarations) {
        const auto found = layout.indices.find(static_cast<int64_t>(declaration.get("id", 0)));
        if (found != layout.indices.end()) {
            values[declaration.get("name", String::num_int64(found->first))] = stored[found->second];
        }
    }
}

/// Resolve a callable or event graph's display name for a frame in the call stack.
String debug_graph_title(const Dictionary &document, int64_t graph) {
    if (graph == 0) {
        return "Event Graph";
    }
    for (const char *section : {"functions", "macros"}) {
        const Dictionary declaration = find_graph_record(document.get(section, Array()), graph);
        if (!declaration.is_empty()) {
            return declaration.get("name", String("Graph"));
        }
    }
    return "Graph";
}

/// Use the same names the author sees on graph cards for common and reflected nodes.
String debug_title(const Dictionary &root, const Dictionary &graph, const Dictionary &record, const StringName &kind) {
    if (kind == StringName("make_value") || kind == StringName("break_value")) {
        return (kind == StringName("make_value") ? String("Make ") : String("Break ")) +
                Variant::get_type_name(visual_value_type(record));
    }
    if (kind == StringName("ready")) return "On Ready";
    if (kind == StringName("literal") && record.has("global_constant")) return record.get("global_constant", String());
    if (kind == StringName("custom_script")) return custom_script_title(record);
    if (kind == StringName("lifecycle")) return "On " + String(record.get("callback", StringName())).trim_prefix("_").capitalize();
    if (kind == StringName("create_node")) return "Create " + String(record.get("class", StringName()));
    if (kind == StringName("create_script_instance")) return "Create " + String(record.get("script_name", StringName("Script Instance")));
    if (kind == StringName("scene_node")) return String(record.get("source", NodePath()));
    if (kind == StringName("singleton")) return record.get("singleton", String());
    if (kind == StringName("native_signal")) return "On " + String(record.get("signal", StringName())).capitalize();
    if (kind == StringName("operator")) return String(record.get("operator", String())).capitalize();
    if (kind == StringName("get_property") || kind == StringName("set_property") || kind == StringName("call_method")) {
        const Dictionary member = record.get("member", Dictionary());
        const String prefix = kind == StringName("get_property") ? "Get " : kind == StringName("set_property") ? "Set " : "";
        return prefix + String(member.get("name", StringName())).capitalize();
    }
    if (kind == StringName("get_variable") || kind == StringName("set_variable")) {
        const bool local = StringName(record.get("scope", StringName())) == StringName("local");
        const Dictionary declared = find_graph_record((local ? graph : root).get(local ? "locals" : "variables", Array()),
                record.get("variable", 0));
        const String label = declared.get("name", "Variable");
        return (kind == StringName("get_variable") ? String("Get ") : String("Set ")) + label;
    }
    if (kind == StringName("call_function") || kind == StringName("call_macro") ||
            kind == StringName("custom_event") || kind == StringName("emit_event")) {
        const char *section = kind == StringName("call_function") ? "functions" :
                kind == StringName("call_macro") ? "macros" : "events";
        const Dictionary declared = find_graph_record(root.get(section, Array()), record.get("definition", 0));
        const String compact = declared.get("compact_title", String());
        const String prefix = kind == StringName("custom_event") ? "On " : kind == StringName("emit_event") ? "Emit " : "";
        return prefix + (compact.is_empty() ? String(declared.get("name", String(kind).capitalize())) : compact);
    }
    return String(kind).capitalize();
}

/// Recover the declared label for one data port without evaluating its node.
String debug_pin_label(const Dictionary &pin, const StringName &key) {
    return pin.get("label", String(key));
}

/// Explain how the graph resolved an input value.
String debug_input_source(const Frame &frame, const Input &input, const Variant &value) {
    if (input.wire.node != 0) {
        const auto producer = frame.program.nodes.find(input.wire.node);
        if (producer != frame.program.nodes.end()) {
            const auto pin = producer->second.outputs.find(input.wire.pin);
            if (pin != producer->second.outputs.end()) {
                const String title = producer->second.debug_title;
                const String label = debug_pin_label(pin->second.pin, input.wire.pin);
                return title == label ? title : title + String(U" \u2192 ") + label;
            }
        }
        return "Connected pin";
    }
    if (input.source == InputSource::VALUE || input.source == InputSource::COPY || input.source == InputSource::ASSET) {
        return input.debug_source;
    }
    return value.get_type() == Variant::OBJECT ? String("This instance (Self)") : String("No connection or default");
}

/// Record values on the normal evaluation path without retaining their runtime object references.
void record_debug_pin(Frame &frame, int64_t id, bool output, const StringName &key, const Dictionary &descriptor,
        const Variant &value, const String &source, bool available = true) {
    if (!frame.debug_recording) return;
    const DebugPinKey identity{id, output, key};
    auto found = frame.debug_pins.find(identity);
    if (found == frame.debug_pins.end()) {
        if (frame.debug_pins.size() >= 512) {
            frame.debug_limited = true;
            return;
        }
        found = frame.debug_pins.try_emplace(identity).first;
    }
    Dictionary row = visual_debug_row(debug_pin_label(descriptor, key), value);
    if (!available) {
        row["value"] = "Unavailable";
        row["detail"] = "";
        row["type"] = Variant::get_type_name(static_cast<Variant::Type>(static_cast<int64_t>(descriptor.get("type", Variant::NIL))));
        row["state"] = "unavailable";
    }
    row["graph"] = frame.program.definition;
    row["node"] = id;
    row["output"] = output;
    row["pin"] = key;
    row["title"] = frame.program.nodes.at(id).debug_title.left(192);
    row["source"] = source.left(192);
    found->second = {row, frame.debug_sample};
}

/// Store outputs only after they are provided or actually computed.
void record_debug_outputs(Frame &frame, int64_t id, const Instruction &node, const Dictionary &values, const String &source) {
    if (!frame.debug_recording) return;
    for (const auto &[pin, output] : node.outputs) {
        if (values.has(pin) || values.has(String(pin))) {
            record_debug_pin(frame, id, true, pin, output.pin, values.get(pin, values.get(String(pin), Variant())), source);
        }
    }
}

/// Record published values by stable authored pin ID, independently of slot numbering.
void record_debug_outputs(Frame &frame, int64_t id, const Instruction &node, const String &source) {
    if (!frame.debug_recording) return;
    for (const auto &[pin, output] : node.outputs) {
        const auto &value = frame.slots[output.slot];
        if (value) record_debug_pin(frame, id, true, pin, output.pin, *value, source);
    }
}

/// Inspect the current invocation without running extra graph actions and verify its owner after resuming.
bool inspect_instruction(Frame &frame, const Dictionary &pin_values, godot::EngineDebugger &debugger,
        const String &input_error = {}) {
    auto *owner = Object::cast_to<Node>(ObjectDB::get_instance(frame.runtime.owner));
    if (!owner || frame.runtime.detached) {
        return false;
    }
    const Dictionary document = frame.runtime.script->get_graph();
    const Dictionary body = graph_body(document, frame.program.definition);
    VisualDebugFrame snapshot;
    snapshot.script = frame.runtime.script->get_path();
    snapshot.graph = frame.program.definition;
    snapshot.node = frame.debug_node;
    snapshot.instance = owner->is_inside_tree() ? String(owner->get_path()) : String(owner->get_name());
    snapshot.instance_label = owner->get_name();
    snapshot.title = debug_graph_title(document, snapshot.graph);
    snapshot.node_title = frame.program.nodes.at(frame.debug_node).debug_title.left(192);
    snapshot.inputs = pin_values;
    snapshot.input_error = input_error;
    for (const Frame *active = &frame; active && snapshot.callers.size() < 32; active = active->debug_caller) {
        const auto node = active->program.nodes.find(active->debug_node);
        const String graph_title = debug_graph_title(document, active->program.definition);
        const String card = node == active->program.nodes.end() ? graph_title : node->second.debug_title;
        Dictionary row = visual_debug_row(graph_title, card);
        row["title"] = card.left(192);
        row["source"] = active == &frame ? "Paused here" : "Caller";
        row["detail"] = snapshot.instance;
        row["script"] = snapshot.script;
        row["graph"] = active->program.definition;
        row["node"] = active->debug_node;
        snapshot.callers.append(row);
        if (snapshot.active_graphs.has(active->program.definition)) {
            continue;
        }
        snapshot.active_graphs.append(active->program.definition);
        bool limited = active->debug_limited;
        for (const auto &[identity, captured] : active->debug_pins) {
            const auto &[id, output, key] = identity;
            Dictionary pin_row = captured.row.duplicate();
            const bool current = active == &frame && captured.sample == frame.debug_sample;
            if (!pin_row.has("state")) {
                pin_row["state"] = current ? "current" : "last";
            }
            pin_row["graph_title"] = graph_title;
            const bool read_now = active == &frame && output && frame.debug_reads.count(identity) != 0;
            if (read_now) pin_row["used_by_current"] = true;
            if (current && id == frame.debug_node && snapshot.input_rows.size() < 64) {
                snapshot.input_rows.append(pin_row);
            } else if (active == &frame && output && (current || read_now) &&
                    snapshot.computed.size() < 48) {
                snapshot.computed.append(pin_row);
            }
            if (snapshot.pins.size() < 1024) {
                snapshot.pins.append(pin_row);
            } else {
                limited = true;
            }
        }
        if (limited) snapshot.limited_graphs.append(active->program.definition);
    }
    inspect_variables(snapshot.locals, body.get("locals", Array()), frame.program.locals, frame.locals);
    inspect_variables(snapshot.members, document.get("variables", Array()), frame.runtime.variables, frame.runtime.variables.values);
    visual_debug_break(debugger, snapshot);
    return frame.runtime.has_live_owner();
}
#endif

/// Limit execution and pure data evaluation across a synchronous dispatch.
constexpr size_t max_dispatch_steps = 32768;
/// Bound nested calls, control blocks and dependency recursion.
constexpr size_t max_dispatch_depth = 64;
/// Internal script method used by native signal subscriptions.
constexpr char signal_dispatch[] = "__crimson_visual_signal";

/// Scene graph execution and native signal dispatch use Godot's main thread.
bool on_main_thread() {
    const auto *os = OS::get_singleton();
    static const auto main_thread = os->get_main_thread_id();
    return os->get_thread_caller_id() == main_thread;
}

/// Read a dictionary key accepting String and StringName keys.
Variant named_value(const Dictionary &dictionary, const StringName &id, const Variant &fallback = {}) {
    const Variant key(id);
    return dictionary.has(key) ? dictionary[key] : dictionary.get(String(id), fallback);
}

/// Test dictionary key presence even when its value is null.
bool has_named_value(const Dictionary &dictionary, const StringName &id) {
    return dictionary.has(id) || dictionary.has(String(id));
}

/// Read a pin or operation name without accepting non-text variants.
StringName name_field(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return is_text(value) ? StringName(value) : StringName();
}

/// Identify an authored execution pin.
bool exec_pin(const Dictionary &pin) {
    return pin.get("exec", false);
}

/// Make the type and method metadata used for external graph calls.
PropertyInfo pin_info(const Dictionary &pin) {
    const Variant::Type type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", 0)));
    const StringName script_class = type == Variant::OBJECT ? global_script_pin_name(pin) : StringName();
    return PropertyInfo(type, name_field(pin, "id"), PROPERTY_HINT_NONE, {},
            type == Variant::NIL ? PROPERTY_USAGE_NIL_IS_VARIANT : PROPERTY_USAGE_DEFAULT,
            script_class.is_empty() ? name_field(pin, "class") : script_class);
}

/// Return true for lifecycle callbacks whose arguments are supplied by Godot.
bool lifecycle(const StringName &name) {
    static const std::set<StringName> names = {"_ready", "_enter_tree", "_exit_tree", "_process",
            "_physics_process", "_input", "_unhandled_input", "_unhandled_key_input", "_shortcut_input"};
    return names.count(name) != 0;
}

/// Match a definition ID within an optional list of definitions.
Dictionary definition_record(const Dictionary &root, const char *key, int64_t id) {
    const Variant records = root.get(key, Variant());
    return records.get_type() == Variant::ARRAY ? find_graph_record(records, id) : Dictionary();
}

/// Validate a pin or variable literal without loading project assets.
bool asset_literal(const Dictionary &pin, const Variant &value) {
    const int64_t type = pin.get("type", 0);
    const StringName class_name = name_field(pin, "class");
    return type == Variant::OBJECT && value.get_type() == Variant::DICTIONARY &&
            !class_name.is_empty() && ClassDB::is_parent_class(class_name, "Resource");
}

/// Check whether a native object Variant once pointed to an object that has been freed.
bool is_expired(const Variant &value) {
    if (value.get_type() != Variant::OBJECT) return false;
    const ObjectID id = value;
    return id != ObjectID() && !gdextension_interface::object_get_instance_from_id(id);
}

/// Resolve a live object, excluding one already queued for deletion.
Object *live_object(const Variant &value) {
    Object *object = value.get_validated_object();
    return object && !object->is_queued_for_deletion() ? object : nullptr;
}

/// Detach collection defaults from authored source and other instances.
Variant fresh_value(const Variant &value) {
    if (value.get_type() == Variant::ARRAY) {
        return Array(value).duplicate(true);
    }
    if (value.get_type() == Variant::DICTIONARY) {
        return Dictionary(value).duplicate(true);
    }
    if (value.get_type() >= Variant::PACKED_BYTE_ARRAY) {
        return value.duplicate();
    }
    return value;
}

/// Load a typed asset or copy a literal into runtime state.
bool resolve_literal(const Dictionary &pin, const Variant &literal, Variant &value) {
    if (asset_literal(pin, literal)) {
        const Ref<Resource> asset = load_asset_reference(literal, name_field(pin, "class"));
        if (asset.is_null()) {
            return false;
        }
        value = asset;
    } else if (resource_collection_pin(pin)) {
        return load_resource_collection(pin, fresh_value(literal), value);
    } else {
        value = fresh_value(literal);
        if ((literal.get_type() == Variant::ARRAY || literal.get_type() == Variant::DICTIONARY) &&
                pin_has_collection_schema(pin)) {
            return resolve_collection_value(pin, value, value);
        }
    }
    return !is_expired(value) && pin_accepts_value(pin, value);
}

/// Return published native property names, excluding Inspector section labels.
std::set<StringName> native_property_names(const StringName &base) {
    std::set<StringName> names = {"script"};
    constexpr int64_t sections = PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP;
    for (const Dictionary &property : ClassDB::class_get_property_list(base)) {
        if ((static_cast<int64_t>(property.get("usage", 0)) & sections) == 0) {
            names.insert(name_field(property, "name"));
        }
    }
    return names;
}

/// Check that a declaration can use Godot's standard Inspector editors and scene storage.
bool exportable_type(const Dictionary &pin) {
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", 0)));
    switch (type) {
        case Variant::OBJECT: {
            const StringName base = name_field(pin, "class");
            return !base.is_empty() && (is_node_class(base) || ClassDB::is_parent_class(base, "Resource"));
        }
        case Variant::ARRAY:
            return !pin.has("element") || exportable_type(pin.get("element", Dictionary()));
        case Variant::DICTIONARY:
            return (!pin.has("key") || exportable_type(pin.get("key", Dictionary()))) &&
                    (!pin.has("value") || exportable_type(pin.get("value", Dictionary())));
        case Variant::RID:
        case Variant::CALLABLE:
        case Variant::SIGNAL:
            return false;
        default:
            return type >= Variant::NIL && type < Variant::VARIANT_MAX;
    }
}

/// Read enum data in its saved order for a Godot Inspector selection property.
String enum_property_hint(const Array &enums, const StringName &type) {
    Array entries = native_enum_entries(type);
    const String prefix("GraphEnum.Enum_");
    const String name = type;
    if (name.begins_with(prefix)) {
        const String suffix = name.trim_prefix(prefix);
        if (suffix.is_valid_int()) {
            const Dictionary record = find_graph_record(enums, suffix.to_int());
            const Variant source = record.get("entries", Variant());
            if (source.get_type() == Variant::ARRAY) entries = source;
        }
    }
    String hint;
    for (const Variant &item : entries) {
        if (item.get_type() != Variant::DICTIONARY) continue;
        const Dictionary entry = item;
        const String label = entry.get("label", entry.get("name", String()));
        if (label.is_empty() || !entry.has("value")) continue;
        if (!hint.is_empty()) hint += ",";
        hint += label.capitalize().replace(",", " ").replace(":", " ").xml_escape() + ":" + String::num_int64(entry["value"]);
    }
    return hint;
}

/// Encode one collection terminal using Godot's type/hint format.
String export_terminal(const PropertyInfo &property) {
    String terminal = String::num_int64(property.type);
    if (property.hint != PROPERTY_HINT_NONE) terminal += "/" + String::num_int64(property.hint);
    return terminal + ":" + property.hint_string;
}

/// Translate an authored pin into metadata for the normal Godot Inspector.
PropertyInfo export_property(const Array &enums, const Dictionary &pin, const StringName &name = {}) {
    PropertyInfo result = pin_info(pin);
    result.name = name;
    result.usage |= PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE;
    if (result.type == Variant::OBJECT) {
        const StringName base = name_field(pin, "class");
        result.hint = is_node_class(base) ? PROPERTY_HINT_NODE_TYPE : PROPERTY_HINT_RESOURCE_TYPE;
        result.hint_string = result.class_name;
    } else if (result.type == Variant::INT && pin.has("enum_type")) {
        const StringName type = name_field(pin, "enum_type");
        result.hint_string = enum_property_hint(enums, type);
        if (!result.hint_string.is_empty()) {
            result.hint = PROPERTY_HINT_ENUM;
            result.class_name = type;
            result.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
        }
    } else if (result.type == Variant::ARRAY && pin.has("element")) {
        result.hint = PROPERTY_HINT_TYPE_STRING;
        result.hint_string = export_terminal(export_property(enums, pin.get("element", Dictionary())));
    } else if (result.type == Variant::DICTIONARY && (pin.has("key") || pin.has("value"))) {
        result.hint = PROPERTY_HINT_TYPE_STRING;
        result.hint_string = export_terminal(export_property(enums, pin.get("key", Dictionary()))) + ";" +
                export_terminal(export_property(enums, pin.get("value", Dictionary())));
    }
    return result;
}

/// Read optional text metadata without converting malformed values into labels.
String inspector_text(const Dictionary &inspector, const char *key) {
    const Variant value = inspector.get(key, Variant());
    if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) return {};
    return String(value).strip_edges();
}

/// Add numeric editor presentation without affecting the graph pin's type or stored values.
void apply_numeric_inspector_options(PropertyInfo &property, const Dictionary &pin, const Dictionary &inspector) {
    if (!has_numeric_inspector_options(pin)) return;
    const Variant authored_range = inspector.get("range", Variant());
    if (authored_range.get_type() == Variant::DICTIONARY && is_valid_inspector_range(pin, authored_range)) {
        const Dictionary range = authored_range;
        const bool slider = range.get("slider", true);
        const bool integer = property.type == Variant::INT;
        const auto format = [integer](const Variant &value) {
            return integer ? String::num_int64(static_cast<int64_t>(value)) : String::num_scientific(static_cast<double>(value));
        };
        property.hint = PROPERTY_HINT_RANGE;
        property.hint_string = format(range["min"]) + "," + format(range["max"]) + "," + format(range["step"]) +
                ",or_less,or_greater";
        property.hint_string += slider ? ",prefer_slider" : ",hide_control";
    }
    const String suffix = inspector_text(inspector, "suffix");
    if (!suffix.is_empty() && is_valid_inspector_suffix(suffix)) {
        if (!property.hint_string.is_empty()) property.hint_string += ",";
        property.hint_string += "suffix:" + suffix;
    }
}

/// Snapshot only the declarations that can affect Godot Inspector properties.
Array export_declarations(const Dictionary &graph) {
    Array declarations;
    const Variant records = graph.get("variables", Variant());
    if (records.get_type() == Variant::ARRAY) {
        for (const Variant &item : Array(records)) {
            if (item.get_type() != Variant::DICTIONARY) continue;
            const Dictionary record = item;
            if (record.get("exported", false) != Variant(true)) continue;
            declarations.append(record);
        }
    }
    return declarations;
}

/// Publish only valid and unambiguous authored names as Godot properties.
void build_exports(VisualScriptExports &exports) {
    if (exports.declarations.is_empty() || !is_node_class(exports.base)) return;
    auto names = native_property_names(exports.base);
    std::map<StringName, StringName> owner_properties;
    for (const StringName &type : exports.owner_types) {
        if (type == exports.base) continue;
        for (const StringName &name : native_property_names(type)) owner_properties.try_emplace(name, type);
    }
    std::vector<String> group_order;
    std::map<String, std::vector<size_t>> groups;
    for (const Dictionary &record : TypedArray<Dictionary>(exports.declarations)) {
        const auto name = name_field(record, "name");
        const Variant raw_pin = record.get("pin", Variant());
        const int64_t id = read_id(record.get("id", Variant()));
        if (id <= 0 || !String(name).is_valid_identifier() || raw_pin.get_type() != Variant::DICTIONARY ||
                !exportable_type(raw_pin) || !names.insert(name).second) continue;
        const auto collision = owner_properties.find(name);
        if (collision != owner_properties.end()) {
            UtilityFunctions::push_warning("[Crimson] Exported variable '" + String(name) + "' conflicts with a property of " +
                    String(collision->second) + ". Rename the graph variable to expose it in the Inspector.");
            continue;
        }
        PropertyInfo property = export_property(exports.enums, raw_pin, name);
        const Variant authored_inspector = record.get("inspector", Variant());
        const Dictionary inspector = authored_inspector.get_type() == Variant::DICTIONARY ? Dictionary(authored_inspector) : Dictionary();
        apply_numeric_inspector_options(property, raw_pin, inspector);

        const size_t index = exports.variables.size();
        exports.indices.emplace(name, index);
        exports.variables.push_back({id, property, raw_pin, record.get("value", Variant())});
        const String group = inspector_text(inspector, "group");
        if (group.is_empty() || !is_valid_inspector_group(group)) {
            exports.properties.push_back(property);
        } else {
            auto [entry, added] = groups.try_emplace(group);
            if (added) group_order.push_back(group);
            entry->second.push_back(index);
        }
    }
    for (const String &group : group_order) {
        exports.properties.emplace_back(Variant::NIL, group, PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_GROUP);
        for (const size_t index : groups.at(group)) {
            exports.properties.push_back(exports.variables[index].property);
        }
    }
    if (!group_order.empty()) {
        exports.properties.emplace_back(Variant::NIL, String(), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_GROUP);
    }
}

/// Produce Inspector metadata used by both the script and its edit-time instances.
TypedArray<Dictionary> export_properties(const VisualScriptExports &exports) {
    TypedArray<Dictionary> properties;
    for (const PropertyInfo &property : exports.properties) properties.append(Dictionary(property));
    return properties;
}

/// Materialize a fresh Inspector default without executing an instance graph.
Variant export_default(const VisualScriptExports::Variable &variable) {
    Variant value;
    return resolve_literal(variable.pin, variable.value, value) ? value : Variant();
}

/// Give the stock placeholder the source defaults for initial population and type changes.
Dictionary placeholder_defaults(const VisualScriptExports &exports) {
    Dictionary defaults;
    for (const auto &variable : exports.variables) defaults[variable.property.name] = export_default(variable);
    return defaults;
}

/// Normalize Inspector-native assignments into the existing graph variable representation.
bool set_export_value(const Dictionary &pin, Variant &stored, const Variant &value) {
    if (pin_has_collection_schema(pin)) return resolve_collection_value(pin, value, stored);
    if (!pin_accepts_value(pin, value)) return false;
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", 0)));
    stored = type == Variant::NIL || type == Variant::OBJECT || type == value.get_type() ? value :
            UtilityFunctions::type_convert(value, type);
    return true;
}

/// Read typed variable declarations, leaving asset loading until a variable is read.
String read_variables(const Variant &records, Variables &output) {
    if (records.get_type() == Variant::NIL) {
        return {};
    }
    if (records.get_type() != Variant::ARRAY) {
        return "Variable declarations must be a list.";
    }
    for (const Variant &item : Array(records)) {
        if (item.get_type() != Variant::DICTIONARY) {
            return "A variable declaration is invalid.";
        }
        const Dictionary variable = item;
        const int64_t id = read_id(variable.get("id", Variant()));
        const Variant raw_pin = variable.get("pin", Variant());
        const Variant value = variable.get("value", Variant());
        if (id <= 0 || output.indices.count(id) != 0 || raw_pin.get_type() != Variant::DICTIONARY) {
            return "A variable has an invalid ID, type, or initial value.";
        }
        const Dictionary pin = raw_pin;
        Variant initial;
        if (asset_literal(pin, value)) {
            initial = fresh_value(value);
        } else if (resource_collection_pin(pin) && valid_resource_collection_storage(pin, value)) {
            initial = fresh_value(value);
        } else if (!resolve_literal(pin, value, initial)) {
            return "A variable has an invalid ID, type, or initial value.";
        }
        output.indices.emplace(id, output.values.size());
        output.values.push_back(std::move(initial));
        output.pins.emplace_back(pin);
    }
    return {};
}

/// Decode saved node kinds once, leaving event entries and unknown nodes as OTHER.
Op compile_op(const StringName &kind) {
    static const std::map<StringName, Op> operations{
        {"make_value", Op::MAKE_VALUE}, {"break_value", Op::BREAK_VALUE},
        {"self", Op::SELF}, {"scene_node", Op::SCENE_NODE}, {"singleton", Op::SINGLETON}, {"get_property", Op::GET_PROPERTY},
        {"get_variable", Op::GET_VARIABLE}, {"operator", Op::OPERATOR}, {"literal", Op::LITERAL},
        {"select", Op::SELECT}, {"reroute", Op::REROUTE}, {"return", Op::RETURN}, {"break", Op::BREAK},
        {"delay", Op::DELAY}, {"await_signal", Op::AWAIT_SIGNAL},
        {"cast", Op::CAST}, {"input_poll", Op::INPUT_POLL},
        {"load_resource", Op::LOAD_RESOURCE},
        {"create_script_instance", Op::CREATE_SCRIPT_INSTANCE},
        {"continue", Op::CONTINUE}, {"do_once", Op::DO_ONCE}, {"do_n", Op::DO_N}, {"flip_flop", Op::FLIP_FLOP},
        {"gate", Op::GATE}, {"branch", Op::BRANCH}, {"switch", Op::SWITCH}, {"switch_enum", Op::SWITCH_ENUM},
        {"sequence", Op::SEQUENCE}, {"while", Op::WHILE}, {"for", Op::FOR}, {"foreach", Op::FOREACH},
        {"print", Op::PRINT}, {"create_node", Op::CREATE_NODE}, {"add_child", Op::ADD_CHILD},
        {"set_property", Op::SET_PROPERTY}, {"call_method", Op::CALL_METHOD}, {"set_variable", Op::SET_VARIABLE},
        {"call_macro", Op::CALL_MACRO}, {"call_function", Op::CALL_FUNCTION}, {"emit_event", Op::EMIT_EVENT},
        {"custom_script", Op::CUSTOM_SCRIPT},
    };
    const auto entry = operations.find(kind);
    return entry == operations.end() ? Op::OTHER : entry->second;
}

/// Recognize scalar types with exact runtime tags and no value ownership or coercion.
bool is_scalar_type(Variant::Type type) {
    return type == Variant::INT || type == Variant::BOOL;
}

/// Match only integer operators and boolean operators whose Godot semantics are known.
std::optional<ScalarOp> scalar_operator(Variant::Operator op, Variant::Type type) {
    if (type == Variant::BOOL) {
        switch (op) {
            case Variant::OP_EQUAL: return ScalarOp::EQUAL;
            case Variant::OP_NOT_EQUAL: return ScalarOp::NOT_EQUAL;
            case Variant::OP_AND: return ScalarOp::AND;
            case Variant::OP_OR: return ScalarOp::OR;
            case Variant::OP_NOT: return ScalarOp::NOT;
            default: return std::nullopt;
        }
    }
    if (type != Variant::INT) return std::nullopt;
    switch (op) {
        case Variant::OP_ADD: return ScalarOp::ADD;
        case Variant::OP_SUBTRACT: return ScalarOp::SUBTRACT;
        case Variant::OP_MULTIPLY: return ScalarOp::MULTIPLY;
        case Variant::OP_DIVIDE: return ScalarOp::DIVIDE;
        case Variant::OP_MODULE: return ScalarOp::REMAINDER;
        case Variant::OP_EQUAL: return ScalarOp::EQUAL;
        case Variant::OP_NOT_EQUAL: return ScalarOp::NOT_EQUAL;
        case Variant::OP_LESS: return ScalarOp::LESS;
        case Variant::OP_LESS_EQUAL: return ScalarOp::LESS_EQUAL;
        case Variant::OP_GREATER: return ScalarOp::GREATER;
        case Variant::OP_GREATER_EQUAL: return ScalarOp::GREATER_EQUAL;
        case Variant::OP_NEGATE: return ScalarOp::NEGATE;
        default: return std::nullopt;
    }
}

/// Lower the immutable part of an exact scalar expression in original input evaluation order.
class ScalarCompiler {
public:
    /// Return no plan if any input depends on data that needs general evaluation.
    ScalarPlan compile(const Instruction &node) {
        if ((node.op != Op::BRANCH && (node.op != Op::SET_VARIABLE || node.variable == no_slot)) ||
                node.inputs.size() != 1 || !is_scalar_type(node.inputs.front().type.type)) {
            return {};
        }
        return compile(node.inputs.front());
    }

    /// Lower one input while preserving the producer's exact integer or boolean type.
    ScalarPlan compile(const Input &input) {
        if (input.type.type == Variant::NIL && input.source == InputSource::WIRED && input.wire.target->pure) {
            const auto &outputs = input.wire.target->outputs;
            if (outputs.size() != 1 || !is_scalar_type(outputs.begin()->second.type.type)) return {};
            Input typed = input;
            typed.type.type = outputs.begin()->second.type.type;
            typed.validate_value = false;
            return compile(typed);
        }
        const auto result = compile_input(input, 0);
        if (!result) return {};
        plan.type = input.type.type;
        plan.result = *result;
        return std::move(plan);
    }

private:
    /// Steps shared by pure producers reached from more than one input.
    std::map<const Instruction *, uint8_t> producers;
    /// Pure producers currently being expanded; a repeat is a cycle.
    std::set<const Instruction *> visiting;
    /// Instructions and unique dispatch cost collected for the current action.
    ScalarPlan plan;

    /// Append an operation only if it fits the fixed-size runtime scratch.
    std::optional<uint8_t> append(ScalarStep step) {
        if (plan.steps.size() == max_scalar_steps) return std::nullopt;
        const auto index = static_cast<uint8_t>(plan.steps.size());
        plan.steps.push_back(step);
        return index;
    }

    /// Resolve a constant, a checked execution output, or a pure expression step.
    std::optional<uint8_t> compile_input(const Input &input, size_t depth) {
        const auto type = input.type.type;
        if (!is_scalar_type(type)) return std::nullopt;
        if (input.source == InputSource::VALUE) {
            if (input.validate_value) return std::nullopt;
            const int64_t value = type == Variant::INT ? static_cast<int64_t>(input.literal) :
                    static_cast<int64_t>(static_cast<bool>(input.literal));
            return append({value, ScalarOp::CONSTANT});
        }
        if (input.source != InputSource::WIRED || depth >= max_dispatch_depth) return std::nullopt;
        const Instruction &producer = *input.wire.target;
        if (!producer.pure) {
            if (input.validate_value) {
                // A dynamically typed execution result may still carry the exact tag this expression needs.
                return append({static_cast<int64_t>(input.slot),
                        type == Variant::INT ? ScalarOp::CHECKED_WIRE_INT : ScalarOp::CHECKED_WIRE_BOOL});
            }
            return append({static_cast<int64_t>(input.slot), type == Variant::INT ? ScalarOp::WIRE_INT : ScalarOp::WIRE_BOOL});
        }
        if (input.validate_value) return std::nullopt;
        if (const auto found = producers.find(&producer); found != producers.end()) return found->second;
        if (!visiting.insert(&producer).second) return std::nullopt;
        auto result = compile_producer(producer, type, depth + 1);
        visiting.erase(&producer);
        if (result) {
            producers.emplace(&producer, *result);
            ++plan.dispatch_cost;
        }
        return result;
    }

    /// Resolve supported pure nodes without executing native calls or changing values.
    std::optional<uint8_t> compile_producer(const Instruction &node, Variant::Type type, size_t depth) {
        const int64_t index = static_cast<int64_t>(node.variable);
        switch (node.op) {
            case Op::GET_VARIABLE:
                if (node.variable == no_slot || node.variable_asset) return std::nullopt;
                if (node.local) return append({index, type == Variant::INT ? ScalarOp::LOCAL_INT : ScalarOp::LOCAL_BOOL});
                return append({index, type == Variant::INT ? ScalarOp::MEMBER_INT : ScalarOp::MEMBER_BOOL});
            case Op::LITERAL:
            case Op::REROUTE:
                if (node.inputs.size() != 1 || node.inputs.front().type.type != type) return std::nullopt;
                return compile_input(node.inputs.front(), depth);
            case Op::OPERATOR: {
                if (node.inputs.empty() || node.inputs.size() > 2) return std::nullopt;
                const auto operand_type = node.inputs[0].type.type;
                const auto op = scalar_operator(node.variant_operator, operand_type);
                if (!op) return std::nullopt;
                const auto left = compile_input(node.inputs[0], depth);
                if (!left) return std::nullopt;
                if (node.inputs.size() == 1) return append({0, *op, *left, *left});
                if (node.inputs[1].type.type != operand_type) return std::nullopt;
                const auto right = compile_input(node.inputs[1], depth);
                if (!right) return std::nullopt;
                return append({0, *op, *left, *right});
            }
            default: return std::nullopt;
        }
    }
};

/// Decode the Godot evaluator to use for a saved operator.
Variant::Operator compile_operator(const StringName &name) {
    static const std::map<StringName, Variant::Operator> operators = {
        {"add", Variant::OP_ADD}, {"subtract", Variant::OP_SUBTRACT}, {"multiply", Variant::OP_MULTIPLY},
        {"divide", Variant::OP_DIVIDE}, {"modulo", Variant::OP_MODULE}, {"equal", Variant::OP_EQUAL},
        {"not_equal", Variant::OP_NOT_EQUAL}, {"less", Variant::OP_LESS}, {"less_equal", Variant::OP_LESS_EQUAL},
        {"greater", Variant::OP_GREATER}, {"greater_equal", Variant::OP_GREATER_EQUAL}, {"and", Variant::OP_AND},
        {"or", Variant::OP_OR}, {"not", Variant::OP_NOT}, {"negate", Variant::OP_NEGATE}
    };
    const auto operation = operators.find(name);
    return operation == operators.end() ? Variant::OP_MAX : operation->second;
}

/// Classify pure nodes after their pins have been compiled.
bool compile_purity(const Instruction &node) {
    switch (node.op) {
        case Op::COLLECTION: return !collection_operation_edits(node.collection);
        case Op::PRELOAD_RESOURCE: return true;
        case Op::MAKE_VALUE: case Op::BREAK_VALUE: return true;
        case Op::SELF: case Op::SCENE_NODE: case Op::SINGLETON: case Op::GET_PROPERTY: case Op::GET_VARIABLE:
        case Op::OPERATOR: case Op::LITERAL: case Op::SELECT: case Op::INPUT_POLL: return true;
        case Op::CALL_FUNCTION: case Op::CALL_MACRO: return node.entries.empty();
        case Op::REROUTE: return name_field(node.record, "mode") != StringName("exec");
        default: return false;
    }
}

/// Find named destinations while compiling or dispatching a dynamic tunnel.
const Edge &next_edge(const Instruction &node, const StringName &pin) {
    static const Edge end;
    const auto edge = node.next.find(pin);
    return edge == node.next.end() ? end : edge->second;
}

/// Format a runtime error with the corresponding node ID.
String node_error(int64_t id, const String &message) {
    return "Node " + String::num_int64(id) + ". " + message;
}

/// Reuse the compiled helper across owners of this revision without holding a lock inside Godot.
CustomScriptCache::Entry compile_custom_source(CustomScriptCache &cache, const String &source) {
    {
        std::lock_guard lock(cache.mutex);
        if (const auto found = cache.entries.find(source); found != cache.entries.end()) {
            return found->second;
        }
    }
    CustomScriptCache::Entry entry;
    entry.script.instantiate();
    entry.script->set_source_code(source);
    entry.error = entry.script->reload(false);
    std::lock_guard lock(cache.mutex);
    return cache.entries.try_emplace(source, std::move(entry)).first->second;
}

/// Resolve each disabled chain once, then route active instructions to their next active destination.
void bypass_disabled_actions(Program &program, const std::map<int64_t, Edge> &disabled_actions) {
    std::map<int64_t, Edge> resolved;
    for (const auto &[id, next] : disabled_actions) {
        if (resolved.count(id) != 0) {
            continue;
        }
        std::set<int64_t> path{id};
        Edge destination = next;
        while (true) {
            const auto action = disabled_actions.find(destination.node);
            if (action == disabled_actions.end()) {
                break;
            }
            const auto cached = resolved.find(destination.node);
            if (cached != resolved.end()) {
                destination = cached->second;
                break;
            }
            if (!path.insert(destination.node).second) {
                // A hand-edited cycle of disabled actions cannot reach an enabled node.
                destination = {};
                break;
            }
            destination = action->second;
        }
        for (const int64_t skipped : path) {
            resolved.emplace(skipped, destination);
        }
    }
    for (auto &[id, instruction] : program.nodes) {
        for (auto &[pin, edge] : instruction.next) {
            const auto destination = resolved.find(edge.node);
            if (destination != resolved.end()) {
                edge = destination->second;
            }
        }
    }
}

/// Compile all wires against the same ports shown in the editor.
String compile_program(const Dictionary &root, const Dictionary &graph, const StringName &base,
        const Variables &members, Program &program, CustomScriptCache &custom_scripts) {
    const Variant records = graph.get("nodes", Variant());
    const Variant wires = graph.get("connections", Variant());
    if (records.get_type() != Variant::ARRAY || wires.get_type() != Variant::ARRAY) {
        return "The graph has invalid node or connection lists.";
    }
    const Array nodes = records;
    const Array connections = wires;
    if (nodes.size() > max_graph_nodes || connections.size() > max_graph_nodes * 8) {
        return "The graph exceeds the node or connection limit.";
    }
    if (const String failure = read_variables(graph.get("locals", Variant()), program.locals); !failure.is_empty()) {
        return failure;
    }
    // Only their execution bridge is needed. Their descriptors, data, and defaults are never compiled.
    std::map<int64_t, Edge> disabled_actions;
    for (const Variant &item : nodes) {
        if (item.get_type() != Variant::DICTIONARY) {
            return "A graph node record is invalid.";
        }
        const Dictionary record = item;
        const int64_t id = read_id(record.get("id", Variant()));
        const StringName kind = name_field(record, "kind");
        if (id <= 0 || kind.is_empty() || program.nodes.count(id) != 0 || disabled_actions.count(id) != 0) {
            return "A graph node has a missing or duplicate ID or kind.";
        }
        const Variant disabled = record.get("disabled", false);
        if (disabled.get_type() != Variant::BOOL) {
            return node_error(id, "The disabled state must be a boolean.");
        }
        if (disabled) {
            if (!can_disable_graph_node(root, graph, record, base)) {
                return node_error(id, "Only a node with one In and one Then execution pin can be disabled. Enable the node to use its current pins.");
            }
            disabled_actions.emplace(id, Edge());
            continue;
        }
        Instruction &node = program.nodes[id];
        node.kind = kind;
        node.record = record;
        node.op = compile_op(kind);
        if (node.op == Op::OTHER) {
            node.collection = collection_operation(kind);
            if (node.collection != CollectionOperation::NONE) {
                node.op = Op::COLLECTION;
                if (collection_source_input(node.collection).is_empty() && !valid_collection_arity(record)) {
                    return node_error(id, "Choose a supported number of collection entries.");
                }
                if (node.collection == CollectionOperation::MAKE_DICTIONARY && record.has("dictionary_types")) {
                    if (record["dictionary_types"].get_type() != Variant::DICTIONARY) {
                        return node_error(id, "Choose valid Key and Value types for Make Dictionary.");
                    }
                    const Array pins = collection_operation_pins(node.collection, record, true, {});
                    const Variant empty = default_collection_value(pins[0]);
                    if (empty.get_type() != Variant::DICTIONARY) {
                        return node_error(id, "Choose valid Key and Value types for Make Dictionary.");
                    }
                    node.dictionary_template.emplace(Dictionary(empty));
                }
            } else if (kind == StringName("preload_resource")) {
                node.op = Op::PRELOAD_RESOURCE;
                const Dictionary values = record.get("values", Dictionary());
                const Variant asset = values.get(StringName("asset"), values.get("asset", Variant()));
                if (asset_reference_path(asset).is_empty()) {
                    return node_error(id, "Choose a saved project resource for Preload Resource.");
                }
                const StringName expected_class = preload_output_pin(record)["class"];
                node.preloaded_resource = preload_asset_reference(asset, expected_class);
                if (node.preloaded_resource.is_null()) {
                    return node_error(id, "Preload Resource could not load a " + String(expected_class) + " from '" +
                            asset_reference_path(asset) + "'. Choose an available resource.");
                }
            }
        }
        if (node.op == Op::MAKE_VALUE || node.op == Op::BREAK_VALUE) {
            node.value_type = visual_value_type(record);
            if (node.value_type == Variant::VARIANT_MAX) {
                return node_error(id, "Choose a supported Godot type for Make or Break.");
            }
        }
        if ((node.op == Op::DELAY || node.op == Op::AWAIT_SIGNAL) && program.definition != 0) {
            return node_error(id, "Delay and Await Signal can only run in the Event Graph.");
        }
        node.declaration = read_id(record.get("definition", Variant()));
        if (node.op == Op::CAST) {
            if (const String failure = validate_cast_node(record); !failure.is_empty()) return node_error(id, failure);
        } else if (node.op == Op::INPUT_POLL) {
            node.input_poll = input_poll_operation(name_field(record, "operation"));
            if (node.input_poll == InputPollOperation::Invalid) return node_error(id, "Choose a valid Input Map query.");
        } else if (node.op == Op::LOAD_RESOURCE) {
            node.native_name = record.get("class", StringName("Resource"));
            if (!ClassDB::class_exists(node.native_name) || !ClassDB::is_parent_class(node.native_name, "Resource")) {
                return node_error(id, "Choose a Resource output type for Load Resource.");
            }
        } else if (node.op == Op::CREATE_SCRIPT_INSTANCE) {
            node.member_script_path = record.get("script_path", String());
            const Ref<Script> constructor = load_script_type(node.member_script_path);
            if (constructor.is_null() || !constructor->can_instantiate() || !matches_script_constructor(record, constructor)) {
                return node_error(id, "The script or its constructor changed. Choose Create from Script again to update its pins.");
            }
            node.preloaded_resource = constructor;
        }
        if (node.op == Op::SINGLETON) {
            node.native_name = name_field(record, "singleton");
            const StringName source = name_field(record, "source");
            if (!String(node.native_name).is_valid_identifier() || (source != StringName("engine") && source != StringName("autoload"))) {
                return node_error(id, "Choose a named Godot singleton or project Autoload.");
            }
        }
        if (node.op == Op::LITERAL && record.has("global_constant")) {
            const StringName symbol = name_field(record, "global_constant");
            const GlobalConstant *constant = find_global_constant(symbol);
            if (!constant) return node_error(id, "Unknown Godot global constant '" + String(symbol) + "'. Pick a supported symbol.");
            // Keep the saved symbol authoritative and reuse the fixed-literal evaluator.
            node.record = record.duplicate();
            node.record["value"] = constant->value;
        }
        if (node.op == Op::CUSTOM_SCRIPT) {
            const auto source = build_custom_script_source(root, graph, record, String(base));
            if (!source.error.is_empty()) {
                return node_error(id, source.error);
            }
            const auto cached = compile_custom_source(custom_scripts, source.text);
            if (cached.error != OK) {
                return node_error(id, "Custom Script could not compile. Check its GDScript errors. The node body begins on generated line " +
                        String::num_int64(source.body_line + 1) + ".");
            }
            node.custom_script = std::make_unique<CustomAction>();
            node.custom_script->script = cached.script;
            if (source.uses_input_defaults) {
                node.custom_script->missing_inputs.resize(Array(record.get("inputs", Array())).size());
                node.custom_script->missing_inputs.fill(0);
            }
        }
        if (node.op == Op::OPERATOR) node.variant_operator = compile_operator(name_field(record, "operator"));
        if (node.op == Op::GET_VARIABLE || node.op == Op::SET_VARIABLE) {
            node.local = name_field(record, "scope") == StringName("local");
            const auto &indices = (node.local ? program.locals : members).indices;
            const auto variable = indices.find(read_id(record.get("variable", Variant())));
            if (variable != indices.end()) node.variable = variable->second;
        }
#ifdef DEBUG_ENABLED
        node.debug_title = debug_title(root, graph, record, kind);
#endif
        if (kind == StringName("function_entry") || kind == StringName("macro_entry")) {
            if (program.entry != 0) {
                return "A callable graph can have only one entry node.";
            }
            program.entry = id;
        }
        if (kind == StringName("get_property") || kind == StringName("set_property") || kind == StringName("call_method")) {
            const Variant member = record.get("member", Variant());
            if (member.get_type() != Variant::DICTIONARY) {
                return node_error(id, "A native member descriptor is missing.");
            }
            if (const String failure = validate_native_member(kind, member); !failure.is_empty()) {
                return node_error(id, failure);
            }
            node.native_name = name_field(member, "name");
            node.native_owner = name_field(member, "class");
            const Dictionary descriptor = member;
            node.native_static = descriptor.get("static", false);
            node.native_variadic = descriptor.get("vararg", false);
            node.member_script_path = descriptor.get("script_path", String());
            node.builtin_receiver = builtin_receiver_type(descriptor);
            node.native_updated_target = builtin_updates_target(descriptor);
            if (bool(descriptor.get("utility", false))) {
                node.native_utility = find_utility_method(node.native_name);
            }
        }
        if (kind == StringName("create_node")) {
            const StringName class_name = name_field(record, "class");
            if (!is_node_class(class_name) || !ClassDB::can_instantiate(class_name)) {
                return node_error(id, "Choose a Godot Node class that can be instantiated.");
            }
            node.native_name = class_name;
        }
        if ((kind == StringName("native_signal") || node.op == Op::AWAIT_SIGNAL) && record.has("script_path")) {
            if (const String failure = validate_script_signal(record); !failure.is_empty()) return node_error(id, failure);
            node.member_script_path = record.get("script_path", String());
        }
        if (kind == StringName("switch_enum")) {
            if (const String failure = validate_enum_switch(record); !failure.is_empty()) {
                return node_error(id, failure);
            }
            for (const Dictionary &entry : Array(record["cases"])) {
                node.enum_cases.emplace(read_id(entry["value"]), name_field(entry, "id"));
            }
        }
        const Dictionary values = record.get("values", Dictionary());
        for (const Dictionary &pin : graph_pins(root, graph, record, false, base)) {
            const StringName name = name_field(pin, "id");
            if (exec_pin(pin)) {
                node.entries.insert(name);
                continue;
            }
            Input input;
            input.pin = pin;
            input.name = name;
            input.type = RuntimePinType::from_pin(pin);
            if (node.custom_script && bool(pin.get("source_default", false))) {
                // A wire can override this later. Without one, GDScript evaluates its own initializer.
                input.source = InputSource::CODE_DEFAULT;
#ifdef DEBUG_ENABLED
                input.debug_source = "Default declared in Edit Code";
#endif
            } else if (has_named_value(values, name)) {
                input.literal = named_value(values, name);
                input.source = InputSource::VALUE;
#ifdef DEBUG_ENABLED
                input.debug_source = "Value set on this pin";
#endif
            } else if (kind == StringName("print") && name == StringName("message")) {
                input.literal = record.get("message", String());
                input.source = InputSource::VALUE;
#ifdef DEBUG_ENABLED
                input.debug_source = record.has("message") ? "Value set on this pin" : "Default value";
#endif
            } else if (kind == StringName("literal") && name == StringName("value") && record.has("value")) {
                input.literal = record["value"];
                input.source = InputSource::VALUE;
#ifdef DEBUG_ENABLED
                input.debug_source = "Value set on this pin";
#endif
            } else if (pin.has("default") && node.op != Op::CREATE_SCRIPT_INSTANCE) {
                input.literal = pin["default"];
                input.source = InputSource::VALUE;
#ifdef DEBUG_ENABLED
                input.debug_source = "Default value";
#endif
            }
            if (input.source == InputSource::VALUE) {
                const auto literal_type = input.literal.get_type();
                if (asset_literal(pin, input.literal) ||
                        (resource_collection_pin(pin) && valid_resource_collection_storage(pin, input.literal))) input.source = InputSource::ASSET;
                else if (literal_type == Variant::ARRAY || literal_type == Variant::DICTIONARY) {
                    if (input.type.collection && !resolve_collection_value(pin, input.literal, input.literal)) {
                        return node_error(id, "The collection default does not match its pin type.");
                    }
                    input.source = InputSource::COPY;
                }
                else if (literal_type >= Variant::PACKED_BYTE_ARRAY) input.source = InputSource::COPY;
                else if (literal_type != Variant::OBJECT && input.type.accepts(input.literal)) input.validate_value = false;
            } else if (!node.member_script_path.is_empty() && bool(pin.get("script_optional", false))) {
                input.source = InputSource::SCRIPT_DEFAULT;
#ifdef DEBUG_ENABLED
                input.debug_source = "Default evaluated by the script";
#endif
            } else if ((name == StringName("target") && (node.op == Op::GET_PROPERTY || node.op == Op::SET_PROPERTY ||
                    (node.op == Op::CALL_METHOD && node.builtin_receiver == Variant::VARIANT_MAX))) ||
                    (name == StringName("parent") && node.op == Op::ADD_CHILD)) {
                input.source = InputSource::OWNER;
            }
            node.inputs.push_back(std::move(input));
        }
        for (const Dictionary &pin : graph_pins(root, graph, record, true, base)) {
            const StringName name = name_field(pin, "id");
            if (exec_pin(pin)) {
                node.next[name] = {};
            } else {
                const auto found =
                        node.outputs.emplace(name, Output{pin, RuntimePinType::from_pin(pin), program.slot_count++}).first;
                if (node.custom_script) {
                    node.custom_script->outputs.push_back(&found->second);
                }
            }
        }
        if (node.op == Op::GET_VARIABLE && !node.outputs.empty() && node.variable != no_slot) {
            const auto &variables = node.local ? program.locals : members;
            const Dictionary &pin = node.outputs.begin()->second.pin;
            node.variable_asset = asset_literal(pin, variables.values[node.variable]) || resource_collection_pin(pin);
        }
        if (kind == StringName("native_signal") ||
                (node.op == Op::AWAIT_SIGNAL && !name_field(record, "signal").is_empty())) {
            std::set<const Output *> assigned;
            bool matched = true;
            for (const Variant &item : Array(record.get("inputs", Array()))) {
                if (item.get_type() != Variant::DICTIONARY) continue;
                const Dictionary pin = item;
                if (exec_pin(pin)) continue;
                const auto name = name_field(pin, "id");
                const auto output = node.outputs.find(name);
                if (output == node.outputs.end() || !assigned.insert(&output->second).second) matched = false;
                auto type = RuntimePinType::from_pin(pin);
                const Output *slot = output == node.outputs.end() ? nullptr : &output->second;
                const bool exact_type = slot && type.type == slot->type.type && type.object_class == slot->type.object_class &&
                        type.script_path == slot->type.script_path;
                node.signal_arguments.push_back({name, pin, std::move(type), slot, pin.has("default"), exact_type});
            }
            if (const auto sender = node.outputs.find("sender"); sender != node.outputs.end()) {
                node.signal_sender = &sender->second;
                if (!assigned.insert(node.signal_sender).second) matched = false;
            }
            node.signal_slots_match = matched && assigned.size() == node.outputs.size();
        }
        node.pure = compile_purity(node);
    }
    for (const Variant &item : connections) {
        if (item.get_type() != Variant::DICTIONARY) {
            return "A graph connection record is invalid.";
        }
        const Dictionary wire = item;
        const int64_t from = read_id(wire.get("from", Variant()));
        const int64_t to = read_id(wire.get("to", Variant()));
        const StringName source = name_field(wire, "from_pin");
        const StringName target = name_field(wire, "to_pin");
        const auto from_node = program.nodes.find(from);
        const auto to_node = program.nodes.find(to);
        const auto disabled_from = from_node == program.nodes.end() ? disabled_actions.find(from) : disabled_actions.end();
        const auto disabled_to = to_node == program.nodes.end() ? disabled_actions.find(to) : disabled_actions.end();
        if ((from_node == program.nodes.end() && disabled_from == disabled_actions.end()) ||
                (to_node == program.nodes.end() && disabled_to == disabled_actions.end())) {
            return "A graph wire references a missing node.";
        }
        const bool from_disabled = disabled_from != disabled_actions.end();
        const bool to_disabled = disabled_to != disabled_actions.end();
        Edge *outgoing = nullptr;
        if (from_disabled) {
            if (source == StringName("then")) {
                outgoing = &disabled_from->second;
            }
        } else if (const auto next = from_node->second.next.find(source); next != from_node->second.next.end()) {
            outgoing = &next->second;
        }
        const bool execution_target = to_disabled ? target == StringName("in") : to_node->second.entries.count(target) != 0;
        if (outgoing && execution_target && outgoing->node == 0) {
            *outgoing = {to, target};
            continue;
        }
        if (outgoing || execution_target) {
            return node_error(to, "A wire has incompatible or occupied pins.");
        }
        if (from_disabled || to_disabled) {
            // The active consumer keeps its authored or reflected default for this data pin.
            if (!to_disabled && target == StringName("target") && to_node->second.kind == StringName("native_signal")) {
                to_node->second.signal_target_disabled = true;
            }
            continue;
        }
        auto output = from_node->second.outputs.find(source);
        auto &inputs = to_node->second.inputs;
        auto input = std::find_if(inputs.begin(), inputs.end(), [&](const Input &candidate) {
            return candidate.name == target;
        });
        if (output == from_node->second.outputs.end() || input == inputs.end() || input->wire.node != 0 ||
                !pins_compatible(output->second.pin, input->pin)) {
            return node_error(to, "A wire has incompatible or occupied pins.");
        }
        input->wire = {from, source, &from_node->second};
        input->slot = output->second.slot;
        input->source = InputSource::WIRED;
        input->validate_value = input->type.iteration_collection || output->second.type.type != input->type.type ||
                input->type.type == Variant::NIL || input->type.type == Variant::OBJECT || input->type.collection;
        input->validate_object_lifetime = output->second.type.type == Variant::OBJECT &&
                (input->type.type == Variant::NIL || (input->type.type == Variant::OBJECT && input->type.script_path.is_empty() &&
                 ClassDBSingleton::get_singleton()->class_get_api_type(input->type.object_class) == ClassDB::API_CORE));
    }
    if (!disabled_actions.empty()) {
        bypass_disabled_actions(program, disabled_actions);
    }
    // Preserve automatic fallback when compiling a graph with too many scalar paths.
    size_t scalar_budget = static_cast<size_t>(max_graph_nodes) * 4;
    for (auto &[id, node] : program.nodes) {
        for (auto &[pin, edge] : node.next) {
            if (edge.node != 0) edge.target = &program.nodes.at(edge.node);
        }
        if (node.custom_script && !node.custom_script->missing_inputs.is_empty()) {
            for (size_t index = 0; index < node.inputs.size(); ++index) {
                node.custom_script->missing_inputs.set(index, node.inputs[index].source == InputSource::CODE_DEFAULT ? 1 : 0);
            }
        }
        if (node.dictionary_template) {
            for (size_t index = 0; index < node.inputs.size(); index += 2) {
                Input &key = node.inputs[index];
                Input &value = node.inputs[index + 1];
                if (key.source == InputSource::MISSING || value.source == InputSource::MISSING) {
                    key.source = InputSource::COLLECTION_PLACEHOLDER;
                    value.source = InputSource::COLLECTION_PLACEHOLDER;
                }
            }
        }
        node.then_edge = next_edge(node, "then");
        if (node.op == Op::CALL_METHOD && node.builtin_receiver == Variant::VARIANT_MAX && !node.native_utility &&
                !node.native_static && !node.native_variadic && node.member_script_path.is_empty() &&
                !node.inputs.empty() && node.inputs.size() <= 9) {
            node.borrow_method_arguments = true;
            for (size_t index = 0; index < node.inputs.size(); ++index) {
                const auto &input = node.inputs[index];
                if (input.source == InputSource::VALUE || (input.source == InputSource::WIRED && !input.wire.target->pure)) {
                    continue;
                }
                if (index != 0 && node.scalar_method_argument == no_slot && input.source == InputSource::WIRED) {
                    auto plan = ScalarCompiler().compile(input);
                    if (!plan.steps.empty() && plan.steps.size() <= scalar_budget) {
                        scalar_budget -= plan.steps.size();
                        node.scalar = std::move(plan);
                        node.scalar_method_argument = index;
                        continue;
                    }
                }
                node.borrow_method_arguments = false;
                break;
            }
            if (!node.borrow_method_arguments) {
                scalar_budget += node.scalar.steps.size();
                node.scalar = {};
                node.scalar_method_argument = no_slot;
            }
        }
        if (node.op == Op::BRANCH) {
            node.true_edge = next_edge(node, "true");
            node.false_edge = next_edge(node, "false");
        } else if (node.op == Op::FOR || node.op == Op::WHILE || node.op == Op::FOREACH) {
            node.body_edge = next_edge(node, "body");
            node.completed_edge = next_edge(node, "completed");
            if (const auto index = node.outputs.find("index"); index != node.outputs.end()) node.index_output = &index->second;
            if (const auto item = node.outputs.find("item"); item != node.outputs.end()) node.item_output = &item->second;
            if (const auto key = node.outputs.find("key"); key != node.outputs.end()) node.key_output = &key->second;
        } else if (node.op == Op::SEQUENCE) {
            const int64_t count = std::clamp(static_cast<int64_t>(node.record.get("count", 2)), int64_t(1), int64_t(64));
            for (int64_t index = 0; index < count; ++index) {
                node.sequence.push_back(next_edge(node, StringName("then_" + String::num_int64(index))));
            }
        } else if (node.op == Op::SWITCH) {
            const Array cases = node.record.get("cases", Array());
            for (int64_t index = 0; index < std::min(cases.size(), int64_t(64)); ++index) {
                node.cases.emplace_back(cases[index], next_edge(node, StringName("case_" + String::num_int64(index))));
            }
        }
        if (scalar_budget != 0 && node.op != Op::CALL_METHOD) {
            auto plan = ScalarCompiler().compile(node);
            if (plan.steps.size() <= scalar_budget) {
                scalar_budget -= plan.steps.size();
                node.scalar = std::move(plan);
            }
        }
    }
    return {};
}



/// Compile reusable definition bodies before dispatch, so Process does not scan or rebuild graphs.
String compile_definitions(const Dictionary &root, const char *key, const StringName &base,
        const Variables &members, std::map<int64_t, Program> &programs, CustomScriptCache &custom_scripts) {
    const Variant records = root.get(key, Variant());
    if (records.get_type() == Variant::NIL) {
        return {};
    }
    if (records.get_type() != Variant::ARRAY) {
        return String(key).capitalize() + " must be a list.";
    }
    for (const Variant &item : Array(records)) {
        if (item.get_type() != Variant::DICTIONARY) {
            return "A callable definition is invalid.";
        }
        const Dictionary record = item;
        const int64_t id = read_id(record.get("id", Variant()));
        const Variant graph = record.get("graph", Variant());
        if (id <= 0 || programs.count(id) != 0 || graph.get_type() != Variant::DICTIONARY) {
            return "A callable definition has an invalid ID or missing graph.";
        }
        Dictionary body = Dictionary(graph).duplicate();
        body["definition"] = id;
        Program &program = programs[id];
        program.definition = id;
        program.pure = record.get("pure", false);
        if (const String failure = compile_program(root, body, base, members, program, custom_scripts); !failure.is_empty()) {
            return String(name_field(record, "name")) + ". " + failure;
        }
        if (program.pure) {
            for (const auto &[node_id, node] : program.nodes) {
                if (!node.pure && node.kind != StringName("function_entry") &&
                    node.kind != StringName("macro_entry") && node.kind != StringName("return")) {
                    return node_error(node_id, "A data-only graph cannot contain actions with execution pins.");
                }
            }
        }
    }
    return {};
}

/// Drop pure temporaries after the current evaluation so the next action sees writes.
void clear_pure_slots(Frame &frame) {
    for (size_t i = 0; i < frame.pure_slot_count; ++i) frame.slots[frame.pure_slots[i]].reset();
    for (const size_t slot : frame.extra_pure_slots) frame.slots[slot].reset();
    frame.pure_slot_count = 0;
    frame.extra_pure_slots.clear();
}

/// Access an execution slot for writing, retaining its prior value within a loop body.
ValueSlot &execution_slot_for_write(Frame &frame, size_t slot) {
    if (frame.loop_depth != 0) frame.loop_writes.push_back({slot, std::move(frame.slots[slot])});
    return frame.slots[slot];
}

/// Publish a validated value in its compiled slot, using the pin name only for diagnostics.
String publish_output(Frame &frame, const Instruction &node, const StringName &name, const Output &output, Variant value) {
    if (!output.type.accepts(value)) {
        return "Output '" + String(name) + "' is unavailable or does not match its type.";
    }
    if (node.pure) {
        frame.slots[output.slot].emplace(std::move(value));
        if (frame.pure_slot_count < frame.pure_slots.size()) {
            frame.pure_slots[frame.pure_slot_count++] = output.slot;
        } else {
            frame.extra_pure_slots.push_back(output.slot);
        }
    } else {
        execution_slot_for_write(frame, output.slot).emplace(std::move(value));
    }
    return {};
}

/// Publish all outputs of a node that supplies named values.
String write_outputs(Frame &frame, const Instruction &node, const Dictionary &values) {
    for (const auto &[name, output] : node.outputs) {
        if (!has_named_value(values, name)) {
            return "Output '" + String(name) + "' is unavailable or does not match its type.";
        }
        if (const String failure = publish_output(frame, node, name, output, named_value(values, name)); !failure.is_empty()) {
            return failure;
        }
    }
    return {};
}

/// Publish a one-output instruction without a temporary dictionary.
String write_single_output(Frame &frame, const Instruction &node, Variant value) {
    const auto &[name, output] = *node.outputs.begin();
    return publish_output(frame, node, name, output, std::move(value));
}

/// Execute one compiled collection operation and publish each stable output for data and debugger consumers.
String write_collection_outputs(Frame &frame, const Instruction &node, const ArgumentValues &inputs) {
    Array arguments;
    for (size_t index = 0; index < inputs.size(); ++index) arguments.append(inputs[index]);
    Dictionary outputs;
    const String failure = evaluate_collection_operation(node.collection, arguments, outputs,
            node.dictionary_template ? &*node.dictionary_template : nullptr);
    return failure.is_empty() ? write_outputs(frame, node, outputs) : failure;
}

/// Get the compiled local or instance variable slot, or null for an unresolved declaration.
Variant *variable_value(Frame &frame, const Instruction &node) {
    if (node.variable == no_slot) return nullptr;
    return &(node.local ? frame.locals : frame.runtime.variables.values)[node.variable];
}

/// Evaluate one pure node and cache its output for the duration of the current action.
String evaluate_data(Frame &frame, int64_t id, const Instruction &node, size_t slot, size_t depth);

/// Evaluate a data-only call in its own invocation frame.
String evaluate_pure_call(Frame &frame, const Instruction &node, const ArgumentValues &inputs,
        Dictionary &outputs, size_t depth);

/// Read wired, authored, default and implicit owner inputs in that priority order.
String evaluate_inputs(Frame &frame, [[maybe_unused]] int64_t id, const Instruction &node,
        ArgumentValues &inputs, size_t depth) {
    inputs.reserve(node.inputs.size());
    for (const Input &input : node.inputs) {
        const StringName &name = input.name;
        std::optional<Variant> temporary;
        const Variant *value = nullptr;
        switch (input.source) {
        case InputSource::WIRED: {
            if (const String failure = evaluate_data(frame, input.wire.node, *input.wire.target, input.slot, depth + 1);
                    !failure.is_empty()) {
#ifdef DEBUG_ENABLED
                if (frame.debug_recording) {
                    record_debug_pin(frame, id, false, name, input.pin, Variant(), debug_input_source(frame, input, Variant()), false);
                }
#endif
                return failure;
            }
            const auto &slot = frame.slots[input.slot];
            if (slot) value = &*slot;
            break;
        }
        case InputSource::VALUE:
            value = &input.literal;
            break;
        case InputSource::COPY:
            value = &temporary.emplace(fresh_value(input.literal));
            break;
        case InputSource::ASSET: {
            Variant loaded;
            if (resolve_literal(input.pin, input.literal, loaded)) {
                value = &temporary.emplace(std::move(loaded));
            }
            break;
        }
        case InputSource::OWNER:
            value = &temporary.emplace(ObjectDB::get_instance(frame.runtime.owner));
            break;
        case InputSource::MISSING:
            break;
        case InputSource::COLLECTION_PLACEHOLDER:
#ifdef DEBUG_ENABLED
            if (frame.debug_recording) {
                record_debug_pin(frame, id, false, name, input.pin, Variant(), "Unfilled dictionary pair", false);
            }
#endif
            inputs.push_back(Variant());
            continue;
        case InputSource::SCRIPT_DEFAULT:
            // Object callp evaluates true defaults once trailing omitted arguments are removed.
            [[fallthrough]];
        case InputSource::CODE_DEFAULT:
#ifdef DEBUG_ENABLED
            if (frame.debug_recording) {
                record_debug_pin(frame, id, false, name, input.pin, Variant(), input.debug_source, false);
            }
#endif
            // The mask distinguishes this placeholder from a supplied null.
            inputs.push_back(Variant());
            continue;
        }
        // A cast classifies a dynamically typed, null, or expired object at its own branch.
        const bool valid = value && (node.op == Op::CAST || !input.validate_value ||
                (input.validate_object_lifetime ? !is_expired(*value) : input.type.accepts(*value)));
#ifdef DEBUG_ENABLED
        if (frame.debug_recording) {
            const Variant missing;
            const Variant &observed = value ? *value : missing;
            record_debug_pin(frame, id, false, name, input.pin, observed, debug_input_source(frame, input, observed), valid);
            if (valid && input.wire.node != 0 && frame.debug_reads.size() < 512) {
                frame.debug_reads.emplace(input.wire.node, true, input.wire.pin);
            }
        }
#endif
        if (!valid) {
            return "Input '" + String(name) + "' is unavailable or incompatible. Connect a value or assign its default.";
        }
        if (temporary) inputs.push_back(std::move(*temporary));
        else inputs.push_back(*value);
    }
    return {};
}

/// Resolve a scene node path relative to the current instance's owner.
Node *scene_node(Frame &frame, const Dictionary &record) {
    auto *owner = Object::cast_to<Node>(ObjectDB::get_instance(frame.runtime.owner));
    const Variant source = record.get("source", NodePath());
    if (!owner || source.get_type() != Variant::NODE_PATH) {
        return nullptr;
    }
    const NodePath path = source;
    if (path.is_absolute()) {
        return nullptr;
    }
    return path.is_empty() ? owner : owner->get_node_or_null(path);
}

/// Reinterpret the wrapped two's-complement result without signed C++ overflow.
int64_t wrapped_int(uint64_t bits) {
    int64_t result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

/// Test the only nonzero signed division or remainder pair that can trap in C++.
bool is_division_wrap(int64_t left, int64_t right) {
    return left == std::numeric_limits<int64_t>::min() && right == -1;
}

/// Execute validated Int/Bool instructions without boxing intermediate results.
/// Failure leaves the action unexecuted and lets the normal evaluator supply its pin diagnostic.
bool evaluate_scalar(Frame &frame, const ScalarPlan &plan, int64_t &result) {
    if (plan.steps.empty() || plan.dispatch_cost > max_dispatch_steps - frame.runtime.steps) return false;
    std::array<int64_t, max_scalar_steps> values;
    for (size_t index = 0; index < plan.steps.size(); ++index) {
        const ScalarStep &step = plan.steps[index];
        const auto location = static_cast<size_t>(step.operand);
        switch (step.op) {
            case ScalarOp::CONSTANT: values[index] = step.operand; break;
            case ScalarOp::WIRE_INT:
            case ScalarOp::WIRE_BOOL:
            case ScalarOp::CHECKED_WIRE_INT:
            case ScalarOp::CHECKED_WIRE_BOOL: {
                const auto &slot = frame.slots[location];
                if (!slot) return false;
                const bool integer = step.op == ScalarOp::WIRE_INT || step.op == ScalarOp::CHECKED_WIRE_INT;
                if ((step.op == ScalarOp::CHECKED_WIRE_INT || step.op == ScalarOp::CHECKED_WIRE_BOOL) &&
                        slot->get_type() != (integer ? Variant::INT : Variant::BOOL)) return false;
                values[index] = integer ? *VariantInternal::get_int(&*slot) :
                        *VariantInternal::get_bool(&*slot);
                break;
            }
            case ScalarOp::LOCAL_INT: values[index] = *VariantInternal::get_int(&frame.locals[location]); break;
            case ScalarOp::LOCAL_BOOL: values[index] = *VariantInternal::get_bool(&frame.locals[location]); break;
            case ScalarOp::MEMBER_INT: values[index] = *VariantInternal::get_int(&frame.runtime.variables.values[location]); break;
            case ScalarOp::MEMBER_BOOL: values[index] = *VariantInternal::get_bool(&frame.runtime.variables.values[location]); break;
            case ScalarOp::ADD:
                values[index] = wrapped_int(uint64_t(values[step.left]) + uint64_t(values[step.right]));
                break;
            case ScalarOp::SUBTRACT:
                values[index] = wrapped_int(uint64_t(values[step.left]) - uint64_t(values[step.right]));
                break;
            case ScalarOp::MULTIPLY:
                values[index] = wrapped_int(uint64_t(values[step.left]) * uint64_t(values[step.right]));
                break;
            case ScalarOp::DIVIDE:
            case ScalarOp::REMAINDER: {
                const int64_t left = values[step.left];
                const int64_t right = values[step.right];
                if (right == 0) return false;
                if (is_division_wrap(left, right)) {
                    values[index] = step.op == ScalarOp::DIVIDE ? left : 0;
                } else {
                    values[index] = step.op == ScalarOp::DIVIDE ? left / right : left % right;
                }
                break;
            }
            case ScalarOp::EQUAL: values[index] = values[step.left] == values[step.right]; break;
            case ScalarOp::NOT_EQUAL: values[index] = values[step.left] != values[step.right]; break;
            case ScalarOp::LESS: values[index] = values[step.left] < values[step.right]; break;
            case ScalarOp::LESS_EQUAL: values[index] = values[step.left] <= values[step.right]; break;
            case ScalarOp::GREATER: values[index] = values[step.left] > values[step.right]; break;
            case ScalarOp::GREATER_EQUAL: values[index] = values[step.left] >= values[step.right]; break;
            case ScalarOp::AND: values[index] = values[step.left] && values[step.right]; break;
            case ScalarOp::OR: values[index] = values[step.left] || values[step.right]; break;
            case ScalarOp::NOT: values[index] = !values[step.left]; break;
            case ScalarOp::NEGATE: values[index] = wrapped_int(uint64_t{0} - uint64_t(values[step.left])); break;
        }
    }
    frame.runtime.steps += plan.dispatch_cost;
    result = values[plan.result];
    return true;
}

/// Apply a generic Godot operator to its checked arguments.
String evaluate_operator(const Instruction &node, const ArgumentValues &inputs, Variant &result) {
    if (node.variant_operator == Variant::OP_MAX || inputs.empty()) {
        return "Choose a valid operator.";
    }
    bool valid = false;
    if (inputs.size() > 1) {
        if ((node.variant_operator == Variant::OP_DIVIDE || node.variant_operator == Variant::OP_MODULE) &&
                inputs[0].get_type() == Variant::INT && inputs[1].get_type() == Variant::INT &&
                is_division_wrap(*VariantInternal::get_int(&inputs[0]), *VariantInternal::get_int(&inputs[1]))) {
            result = node.variant_operator == Variant::OP_DIVIDE ? std::numeric_limits<int64_t>::min() : int64_t{0};
            return {};
        }
        Variant::evaluate(node.variant_operator, inputs[0], inputs[1], result, valid);
    } else {
        Variant::evaluate(node.variant_operator, inputs[0], Variant(), result, valid);
    }
    return valid ? String() : String("Godot could not apply the operator to these values.");
}

/// Collect fixed argument pointers and protect trailing slot storage from reentrant calls.
String append_member_arguments(const ArgumentValues &inputs, size_t first, bool variadic,
        std::vector<const Variant *> &args, Array &tail) {
    if (inputs.size() < first + static_cast<size_t>(variadic)) return "The call is missing its argument list.";
    const size_t end = inputs.size() - static_cast<size_t>(variadic);
    if (variadic) tail = Array(inputs[end]).duplicate();
    const size_t count = end - first + static_cast<size_t>(tail.size());
    if (count > std::numeric_limits<int>::max()) return "Too many arguments for a Godot method call.";
    args.reserve(count);
    for (size_t index = first; index < end; ++index) args.push_back(&inputs[index]);
    for (int64_t index = 0; index < tail.size(); ++index) args.push_back(&static_cast<const Array &>(tail)[index]);
    return {};
}

/// Call a value method through Godot Variant dispatch, or its native global utility binding.
String invoke_builtin_method(const Instruction &node, ArgumentValues &inputs, Variant &result) {
    std::vector<const Variant *> args;
    Array tail;
    if (const String failure = append_member_arguments(inputs, node.native_static ? 0 : 1,
            node.native_variadic, args, tail); !failure.is_empty()) {
        return failure;
    }
    if (node.native_utility) return invoke_utility(*node.native_utility, node.native_name, args.data(), args.size(), result);
    GDExtensionCallError error{};
    if (node.native_static) {
        Variant::callp_static(node.builtin_receiver, node.native_name, args.data(), static_cast<int>(args.size()), result, error);
    } else {
        auto &target = inputs.front();
        if (target.get_type() != node.builtin_receiver) {
            if (node.builtin_receiver == Variant::STRING && target.get_type() == Variant::STRING_NAME) {
                target = String(target);
            } else if (node.builtin_receiver == Variant::STRING_NAME && target.get_type() == Variant::STRING) {
                target = StringName(String(target));
            } else {
                return "The Target does not match the method's receiver type.";
            }
        }
        target.callp(node.native_name, args.data(), static_cast<int>(args.size()), result, error);
    }
    return error.error == GDEXTENSION_CALL_OK ? String() :
            "Godot could not call the built-in method (call error " + String::num_int64(error.error) + ").";
}

/// Dispatch a reflected member against the currently live target.
String invoke_member(const Instruction &node, ArgumentValues &inputs, Variant &result) {
    if (node.builtin_receiver < Variant::VARIANT_MAX || node.native_utility) {
        return invoke_builtin_method(node, inputs, result);
    }
    if (node.native_static || node.native_variadic) {
        if (!node.native_static && (inputs.empty() || !is_live_method_receiver(inputs.front()))) {
            return "The Target is unavailable. Connect a live object.";
        }
        Array tail;
        std::vector<const Variant *> arguments;
        if (const String failure = append_member_arguments(inputs, node.native_static ? 0 : 1,
                node.native_variadic, arguments, tail); !failure.is_empty()) {
            return failure;
        }
        GDExtensionCallError error{};
        if (node.native_static) {
            if (arguments.size() > size_t(std::numeric_limits<int>::max() - 2)) {
                return "Too many arguments for a Godot static method call.";
            }
            const Variant owner_name = node.native_owner;
            const Variant method_name = node.native_name;
            arguments.insert(arguments.begin(), { &owner_name, &method_name });
            Variant class_db = ClassDBSingleton::get_singleton();
            class_db.callp("class_call_static", arguments.data(), static_cast<int>(arguments.size()), result, error);
        } else {
            inputs.front().callp(node.native_name, arguments.data(), static_cast<int>(arguments.size()), result, error);
        }
        return error.error == GDEXTENSION_CALL_OK ? String() :
                "Godot could not call the method (call error " + String::num_int64(error.error) + ").";
    }
    if (inputs.empty() || !is_live_method_receiver(inputs.front())) {
        return "The Target is unavailable. Connect a live object.";
    }
    const StringName &name = node.native_name;
    if (node.op == Op::GET_PROPERTY) {
        bool valid = false;
        result = inputs.front().get_named(name, valid);
        return valid ? String() : String("Godot could not read the property.");
    }
    if (node.op == Op::SET_PROPERTY) {
        if (!node.member_script_path.is_empty()) {
            bool valid = false;
            inputs.front().set_named(name, inputs[1], valid);
            return valid ? String() : String("Godot could not set the script property.");
        }
        Object *target = inputs.front().get_validated_object();
        if (!target) return "The Target is unavailable. Connect a live object.";
        const Error status = ClassDB::class_set_property(target, name, inputs[1]);
        return status == OK ? String() : "Godot could not set the property (error " + String::num_int64(status) + ").";
    }

    std::array<const Variant *, 8> inline_args;
    std::vector<const Variant *> overflow_args;
    size_t end = inputs.size();
    if (!node.member_script_path.is_empty()) {
        while (end > 1 && node.inputs[end - 1].source == InputSource::SCRIPT_DEFAULT) --end;
        for (size_t index = 1; index < end; ++index) {
            if (node.inputs[index].source == InputSource::SCRIPT_DEFAULT) {
                return "An earlier script argument uses a dynamic default. Set that argument before later inputs.";
            }
        }
    }
    const size_t count = end - 1;
    if (count > inline_args.size()) overflow_args.resize(count);
    const Variant **args = count <= inline_args.size() ? inline_args.data() : overflow_args.data();
    for (size_t index = 1; index < end; ++index) {
        args[index - 1] = &inputs[index];
    }
    GDExtensionCallError error{};
    inputs.front().callp(name, args, static_cast<int>(count), result, error);
    return error.error == GDEXTENSION_CALL_OK ? String() :
            "Godot could not call the method (call error " + String::num_int64(error.error) + ").";
}

/// Borrow a native call's stable operands when gathering them cannot execute graph code.
/// Leave unusual or invalid inputs to the general evaluator so diagnostics stay identical.
bool invoke_borrowed_method(Frame &frame, const Instruction &node, Variant &result, GDExtensionCallError &error) {
    if (!node.borrow_method_arguments) return false;
    std::array<const Variant *, 9> values;
    for (size_t index = 0; index < node.inputs.size(); ++index) {
        if (index == node.scalar_method_argument) continue;
        const auto &input = node.inputs[index];
        const Variant *value = &input.literal;
        if (input.source == InputSource::WIRED) {
            const auto &slot = frame.slots[input.slot];
            if (!slot) return false;
            value = &*slot;
        }
        if (input.validate_value &&
                (input.validate_object_lifetime ? is_expired(*value) : !input.type.accepts(*value))) return false;
        values[index] = value;
    }
    if (!is_live_method_receiver(*values[0])) return false;
    std::optional<Variant> scalar_argument;
    if (node.scalar_method_argument != no_slot) {
        int64_t value;
        if (!evaluate_scalar(frame, node.scalar, value)) return false;
        if (node.scalar.type == Variant::INT) scalar_argument.emplace(value);
        else scalar_argument.emplace(value != 0);
        values[node.scalar_method_argument] = &*scalar_argument;
    }
    // Own a mutable receiver for Godot's call API. The remaining Variant pointers are read-only.
    Variant target = *values[0];
    target.callp(node.native_name, values.data() + 1, static_cast<int>(node.inputs.size() - 1), result, error);
    return true;
}

String evaluate_data(Frame &frame, int64_t id, const Instruction &node, size_t slot, size_t depth) {
    if (!node.pure || frame.slots[slot]) {
        return {};
    }
    if (depth > max_dispatch_depth || ++frame.runtime.steps > max_dispatch_steps) {
        return node_error(id, "Data evaluation exceeded its limit. Check for a cycle or a long dependency chain.");
    }
    ArgumentValues inputs;
    if (const String failure = evaluate_inputs(frame, id, node, inputs, depth); !failure.is_empty()) {
        return node_error(id, failure);
    }

    if (node.op == Op::CALL_FUNCTION || node.op == Op::CALL_MACRO) {
        Dictionary outputs;
        if (const String failure = evaluate_pure_call(frame, node, inputs, outputs, depth); !failure.is_empty()) {
            return node_error(id, failure);
        }
        const String failure = write_outputs(frame, node, outputs);
#ifdef DEBUG_ENABLED
        if (frame.debug_recording && failure.is_empty()) record_debug_outputs(frame, id, node, "Returned by this call");
#endif
        return failure.is_empty() ? String() : node_error(id, failure);
    }

    Variant value;
    String failure;
    if (node.op == Op::COLLECTION) {
        failure = write_collection_outputs(frame, node, inputs);
#ifdef DEBUG_ENABLED
        if (frame.debug_recording && failure.is_empty()) record_debug_outputs(frame, id, node, "Read from this collection");
#endif
        return failure.is_empty() ? String() : node_error(id, failure);
    } else if (node.op == Op::PRELOAD_RESOURCE) {
        value = node.preloaded_resource;
    } else if (node.op == Op::MAKE_VALUE) {
        value = default_visual_value(node.value_type);
        if (value.get_type() != node.value_type || inputs.empty()) {
            return node_error(id, "Godot could not construct the selected value type.");
        }
        for (size_t i = 0; i < inputs.size(); ++i) {
            const StringName &field = node.inputs[i].name;
            if (!write_visual_value_component(value, field, inputs[i])) {
                return node_error(id, "Godot could not set the field '" + String(field) + "'.");
            }
        }
    } else if (node.op == Op::BREAK_VALUE) {
        if (inputs.size() != 1 || inputs[0].get_type() != node.value_type) {
            return node_error(id, "Connect a value of the selected type to Break.");
        }
        for (const auto &[name, output] : node.outputs) {
            bool valid = false;
            Variant component = inputs[0].get_named(name, valid);
            if (!valid) return node_error(id, "Godot could not read the field '" + String(name) + "'.");
            const String result = publish_output(frame, node, name, output, std::move(component));
            if (!result.is_empty()) return node_error(id, result);
        }
#ifdef DEBUG_ENABLED
        if (frame.debug_recording) record_debug_outputs(frame, id, node, "Read from this value");
#endif
        return {};
    } else if (node.op == Op::SELF) {
        value = ObjectDB::get_instance(frame.runtime.owner);
    } else if (node.op == Op::SINGLETON) {
        Object *singleton = resolve_visual_script_singleton(ObjectDB::get_instance(frame.runtime.owner), node.native_name,
                name_field(node.record, "source") == StringName("autoload"));
        if (!singleton || singleton->is_queued_for_deletion()) {
            failure = "Singleton '" + String(node.native_name) + "' is unavailable. Check the Autoload or register it before running this graph.";
        } else {
            value = singleton;
        }
    } else if (node.op == Op::SCENE_NODE) {
        Node *source = scene_node(frame, node.record);
        if (!source || source->is_queued_for_deletion()) {
            failure = "The scene node path does not point to a live node.";
        } else {
            value = source;
        }
    } else if (node.op == Op::GET_VARIABLE) {
        Variant *stored = variable_value(frame, node);
        if (!stored || node.outputs.empty()) {
            failure = "Choose a declared variable.";
        } else if (node.variable_asset && !node.outputs.begin()->second.type.accepts(*stored)) {
            if (!resolve_literal(node.outputs.begin()->second.pin, *stored, value)) {
                failure = "The variable's resource is unavailable or incompatible.";
            } else {
                *stored = value;
            }
        } else {
            value = *stored;
        }
    } else if (node.op == Op::GET_PROPERTY) {
        failure = invoke_member(node, inputs, value);
    } else if (node.op == Op::OPERATOR) {
        failure = evaluate_operator(node, inputs, value);
    } else if (node.op == Op::INPUT_POLL) {
        std::array<const Variant *, 5> arguments{};
        for (size_t index = 0; index < inputs.size(); ++index) arguments[index] = &inputs[index];
        value = read_input_poll(node.input_poll, arguments.data());
    } else if (node.op == Op::SELECT) {
        if (inputs.size() != 3) {
            failure = "Select needs its condition and both values.";
        } else {
            value = static_cast<bool>(inputs[0]) ? inputs[1] : inputs[2];
        }
    } else if (node.op == Op::LITERAL) {
        const Variant literal = inputs.empty() ? node.record.get("value", Variant()) : inputs.front();
        if (node.outputs.empty() || !resolve_literal(node.outputs.begin()->second.pin, literal, value)) {
            failure = "The literal does not match its output type.";
        }
    } else if (!inputs.empty()) {
        value = inputs.front();
    }

    if (failure.is_empty() && node.outputs.size() == 1) {
        failure = write_single_output(frame, node, std::move(value));
#ifdef DEBUG_ENABLED
        if (frame.debug_recording && failure.is_empty()) record_debug_outputs(frame, id, node, "Calculated for this step");
#endif
    }
    return failure.is_empty() ? String() : node_error(id, failure);
}

/// Attach a live detached child while honoring tree ownership and queued deletion.
String attach_child(const ArgumentValues &inputs) {
    auto *child = Object::cast_to<Node>(live_object(inputs[0]));
    auto *parent = Object::cast_to<Node>(live_object(inputs[1]));
    if (!child || !parent || child == parent || child->get_parent() || child->is_ancestor_of(parent)) {
        return "Add Child needs a live parent and a detached child.";
    }
    parent->add_child(child);
    child = Object::cast_to<Node>(live_object(inputs[0]));
    parent = Object::cast_to<Node>(live_object(inputs[1]));
    return child && parent && child->get_parent() == parent ? String() : String("Add Child could not attach the child.");
}

/// Track construction for the duration of a top-level graph dispatch.
struct Execution {
    /// Continue an immutable compiled path at its original nesting level.
    struct Path {
        Edge edge;
        size_t depth;
    };

    /// One signal to resume from once the synchronous dispatcher has unwound.
    struct Wait {
        Signal signal;
        /// Keep a timer alive only until it finishes or the wait is canceled.
        Ref<SceneTreeTimer> timer;
        int64_t node = 0;
        Path continuation;
    };

    /// Constructed nodes, without retaining freed objects.
    std::vector<ObjectID> created;
    /// Human-readable error from the innermost operation.
    String error;
    /// Set only when execution returns SUSPEND.
    std::optional<Wait> wait;
    /// Remaining Sequence outputs to visit after the current path resumes.
    std::vector<Path> following;

    Execution() = default;
    /// Move the cleanup list into a deferred invocation and leave its old owner empty.
    Execution(Execution &&other) noexcept : created(std::move(other.created)), error(std::move(other.error)),
            wait(std::move(other.wait)), following(std::move(other.following)) {
        other.created.clear();
    }

    /// Release constructed nodes that were never handed to the scene.
    ~Execution() {
        for (const ObjectID id : created) {
            auto *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
            if (node && !node->get_parent() && !node->is_queued_for_deletion()) {
                memdelete(node);
            }
        }
    }
};

/// Turn an action's ordered input list into named arguments for a nested graph.
Dictionary input_values(const Instruction &node, const ArgumentValues &values) {
    Dictionary result;
    for (size_t index = 0; index < node.inputs.size(); ++index) {
        result[node.inputs[index].name] = values[index];
    }
    return result;
}

/// Call GDScript's constructor and hand any new detached Node to the graph's creation cleanup.
String create_script_instance(const Instruction &node, const ArgumentValues &inputs, Variant &output, Execution &execution) {
    size_t count = inputs.size();
    while (count > 0 && node.inputs[count - 1].source == InputSource::SCRIPT_DEFAULT) --count;
    std::array<GDExtensionConstVariantPtr, 64> arguments;
    if (count > arguments.size()) return "This constructor has too many arguments.";
    for (size_t index = 0; index < count; ++index) {
        if (node.inputs[index].source == InputSource::SCRIPT_DEFAULT) {
            return "Set the earlier constructor argument before setting a later one.";
        }
        arguments[index] = inputs[index]._native_ptr();
    }
    // Use the engine constructor even if the script itself declares a static method called new.
    // Signature hash from the pinned Godot 4.7 GDScript.new binding.
    static const GDExtensionMethodBindPtr constructor = gdextension_interface::classdb_get_method_bind(
            StringName("GDScript")._native_ptr(), StringName("new")._native_ptr(), 1545262638);
    if (!constructor) return "The engine's GDScript constructor is unavailable.";
    GDExtensionCallError error{};
    alignas(Variant) std::byte result_storage[GODOT_CPP_VARIANT_SIZE];
    gdextension_interface::object_method_bind_call(constructor, node.preloaded_resource->_owner,
            arguments.data(), count, result_storage, &error);
    output = Variant(static_cast<GDExtensionConstVariantPtr>(result_storage));
    gdextension_interface::variant_destroy(result_storage);
    Object *instance = output.get_validated_object();
    if (auto *created = Object::cast_to<Node>(instance)) execution.created.emplace_back(created->get_instance_id());
    if (error.error != GDEXTENSION_CALL_OK || !instance || !node.outputs.begin()->second.type.accepts(output)) {
        return "Godot could not create this script instance. Check the constructor arguments and script.";
    }
    return {};
}

/// Run a path in one invocation and report structured control to its caller.
Flow execute_path(Frame &frame, const Edge &first, Execution &execution, size_t depth, const MacroExit *macro_exit = nullptr);

/// Validate a wait before publishing a continuation. The caller unwinds before it connects the signal.
Flow prepare_wait(Frame &frame, int64_t id, const Instruction &node, const ArgumentValues &inputs,
        Execution &execution, size_t depth, const MacroExit *macro_exit) {
    constexpr size_t max_pending_waits = 64;
    auto *owner = Object::cast_to<Node>(ObjectDB::get_instance(frame.runtime.owner));
    if (!frame.can_suspend || frame.loop_depth != 0 || macro_exit) {
        execution.error = node_error(id, "Delay and Await Signal need an Event Graph path outside a loop or a synchronous call.");
        return Flow::ERROR;
    }
    if (!owner || !owner->is_inside_tree() || owner->is_queued_for_deletion() || frame.runtime.exiting_tree) {
        execution.error = node_error(id, "The owner must be active in the scene tree to wait.");
        return Flow::ERROR;
    }
    if (frame.runtime.suspended.size() >= max_pending_waits) {
        execution.error = node_error(id, "Too many pending waits. Start fewer concurrent event paths.");
        return Flow::ERROR;
    }

    Signal signal;
    Ref<SceneTreeTimer> timer;
    if (node.op == Op::DELAY) {
        const double seconds = inputs.front();
        if (!std::isfinite(seconds) || seconds < 0.0) {
            execution.error = node_error(id, "Delay Seconds must be finite and nonnegative.");
            return Flow::ERROR;
        }
        timer = owner->get_tree()->create_timer(seconds, false);
        signal = Signal(timer.ptr(), "timeout");
    } else if (const StringName name = name_field(node.record, "signal"); name.is_empty()) {
        signal = inputs.front();
    } else {
        if (Object *target = live_object(inputs.front())) signal = Signal(target, name);
    }
    Object *source = signal.get_object();
    if (!source || source->is_queued_for_deletion() || !source->has_signal(signal.get_name())) {
        execution.error = node_error(id, "Connect a live object and an available signal before waiting.");
        return Flow::ERROR;
    }
    execution.wait.emplace(Execution::Wait{signal, std::move(timer), id, {node.then_edge, depth}});
    return Flow::SUSPEND;
}

/// Run the generated helper synchronously and publish its typed outputs on success.
Flow execute_custom_script(Frame &frame, int64_t id, const Instruction &node, const ArgumentValues &inputs,
        Execution &execution, size_t depth);

/// Set event, function, or macro arguments, then dispatch its selected execution output.
Flow execute_entry(Frame &frame, int64_t id, const Dictionary &values, const StringName &pin,
        Execution &execution, size_t depth, const MacroExit *macro_exit = nullptr) {
    if (id == 0) {
        return Flow::END;
    }
    const Instruction &entry = frame.program.nodes.at(id);
#ifdef DEBUG_ENABLED
    const VisualDebugScope debug_scope;
    frame.debug_node = id;
    if (auto *debugger = visual_debugger()) {
        frame.debug_recording = true;
        ++frame.debug_sample;
        record_debug_outputs(frame, id, entry, values, "Supplied to this graph");
        if (visual_debug_should_break(*debugger, frame.runtime.script->get_path(), visual_breakpoint_line(frame.program.definition, id))) {
            Dictionary pins;
            for (const auto &[key, descriptor] : entry.outputs) {
                pins[descriptor.pin.get("label", String(key))] = named_value(values, key);
            }
            if (!inspect_instruction(frame, pins, *debugger)) {
                return Flow::END;
            }
        }
        if (!frame.runtime.has_live_owner()) {
            return Flow::END;
        }
    }
#endif
    if (const String failure = write_outputs(frame, entry, values); !failure.is_empty()) {
        execution.error = node_error(id, failure);
        return Flow::ERROR;
    }
    refresh_signal_outputs(frame, id);
    return execute_path(frame, next_edge(entry, pin), execution, depth, macro_exit);
}

/// Link ordinary function calls by stable pin ID after all function programs have been created.
void link_function_calls(Runtime &runtime, Program &program) {
    for (auto &[id, node] : program.nodes) {
        if (node.op != Op::CALL_FUNCTION) continue;
        const auto callee = runtime.functions.find(node.declaration);
        if (callee == runtime.functions.end()) continue;
        node.callee = &callee->second;
        if (node.callee->entry == 0) continue;
        const auto &outputs = node.callee->nodes.at(node.callee->entry).outputs;
        if (outputs.size() != node.inputs.size()) continue;
        std::set<const Output *> assigned;
        for (const Input &input : node.inputs) {
            const auto output = outputs.find(input.name);
            if (output == outputs.end() || !assigned.insert(&output->second).second) {
                node.entry_outputs.clear();
                break;
            }
            EntryCheck check = EntryCheck::FULL;
            const auto &target = output->second.type;
            if (input.type.type == target.type && target.type != Variant::NIL && target.type != Variant::OBJECT &&
                    target.type != Variant::VARIANT_MAX) {
                check = EntryCheck::NONE;
            } else if (input.type.type == Variant::OBJECT && target.type == Variant::OBJECT &&
                    ClassDBSingleton::get_singleton()->class_get_api_type(target.object_class) == ClassDB::API_CORE &&
                    ClassDB::is_parent_class(input.type.object_class, target.object_class)) {
                check = EntryCheck::LIFETIME;
            }
            node.entry_outputs.push_back({&output->second, check});
        }
    }
}

/// Transfer checked call arguments to the callee's compiled slots without using named storage.
Flow execute_function_entry(Frame &child, const Instruction &call, const ArgumentValues &inputs,
        Execution &execution, size_t depth) {
    const int64_t id = child.program.entry;
    if (id == 0) return Flow::END;
    const auto &entry = child.program.nodes.at(id);
    if (call.entry_outputs.size() != entry.outputs.size() || inputs.size() != call.entry_outputs.size()
#ifdef DEBUG_ENABLED
            || visual_debugger()
#endif
    ) {
        return execute_entry(child, id, input_values(call, inputs), "then", execution, depth);
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto &transfer = call.entry_outputs[index];
        const auto &value = inputs[index];
        if ((transfer.check == EntryCheck::FULL && !transfer.output->type.accepts(value)) ||
                (transfer.check == EntryCheck::LIFETIME && is_expired(value))) {
            execution.error = node_error(id, "Output '" + String(call.inputs[index].name) + "' is unavailable or does not match its type.");
            return Flow::ERROR;
        }
        child.slots[transfer.output->slot].emplace(value);
    }
    return execute_path(child, entry.then_edge, execution, depth);
}

/// Evaluate one action's inputs from a fresh cache so loops observe writes.
String action_inputs(Frame &frame, int64_t id, const Instruction &node, ArgumentValues &inputs) {
    const String failure = evaluate_inputs(frame, id, node, inputs, 0);
    clear_pure_slots(frame);
    return failure;
}

/// Restore execution outputs only; local and instance variable writes are not rolled back.
struct LoopOutputs {
    /// The active call frame.
    Frame &frame;
    /// Journal position at entry, including any outer loop's writes.
    size_t checkpoint;

    /// Record writes only while a loop is active.
    explicit LoopOutputs(Frame &owner) : frame(owner), checkpoint(owner.loop_writes.size()) { ++frame.loop_depth; }
    /// Restore the enclosing scope before any exit from the loop.
    ~LoopOutputs() { restore(); --frame.loop_depth; }
    /// Return each changed output to its value at loop entry.
    void restore() const {
        while (frame.loop_writes.size() > checkpoint) {
            SlotWrite &write = frame.loop_writes.back();
            frame.slots[write.slot] = std::move(write.previous);
            frame.loop_writes.pop_back();
        }
    }
    /// A checkpoint has one owner.
    LoopOutputs(const LoopOutputs &) = delete;
    /// A checkpoint cannot be reassigned.
    LoopOutputs &operator=(const LoopOutputs &) = delete;
};

/// Execute a loop body with its current index and optional collection item and key.
Flow execute_loop_body(Frame &frame, int64_t id, const Instruction &node, int64_t index, const Variant *item, const Variant *key,
        Execution &execution, size_t depth, const MacroExit *macro_exit) {
    if (++frame.runtime.steps > max_dispatch_steps) {
        execution.error = node_error(id, "Execution exceeded its step limit. Check the loop condition.");
        return Flow::ERROR;
    }
    if (node.index_output) {
        // For and For Each declare Index as Int; publish it directly while keeping normal loop rollback.
        execution_slot_for_write(frame, node.index_output->slot).emplace(index);
    }
    if (node.item_output && item) {
        static const StringName item_pin("item");
        if (const String failure = publish_output(frame, node, item_pin, *node.item_output, *item); !failure.is_empty()) {
            execution.error = node_error(id, failure);
            return Flow::ERROR;
        }
    }
    if (node.key_output && key) {
        static const StringName key_pin("key");
        if (const String failure = publish_output(frame, node, key_pin, *node.key_output, *key); !failure.is_empty()) {
            execution.error = node_error(id, failure);
            return Flow::ERROR;
        }
    }
#ifdef DEBUG_ENABLED
    if (frame.debug_recording) record_debug_outputs(frame, id, node, "Current loop iteration");
#endif
    refresh_signal_outputs(frame, id);
    return execute_path(frame, node.body_edge, execution, depth + 1, macro_exit);
}

/// Execute a while, range or collection loop and consume its break/continue control.
Flow execute_loop(Frame &frame, int64_t id, const Instruction &node, const ArgumentValues &inputs,
        Execution &execution, size_t depth, const MacroExit *macro_exit) {
    const LoopOutputs loop_scope(frame);
    auto run_body = [&](int64_t index, const Variant *item = nullptr, const Variant *key = nullptr) {
        loop_scope.restore();
        return execute_loop_body(frame, id, node, index, item, key, execution, depth, macro_exit);
    };
    if (node.op == Op::WHILE) {
        bool condition = inputs.front();
        while (condition) {
            const Flow flow = run_body(0);
            if (flow == Flow::BREAK) {
                break;
            }
            if (flow == Flow::RETURN || flow == Flow::ERROR) {
                return flow;
            }
            ArgumentValues refreshed;
            if (const String failure = action_inputs(frame, id, node, refreshed); !failure.is_empty()) {
                execution.error = node_error(id, failure);
                return Flow::ERROR;
            }
            condition = refreshed.front();
        }
    } else if (node.op == Op::FOREACH) {
        const bool dictionary = inputs.front().get_type() == Variant::DICTIONARY;
        const Dictionary pairs = dictionary ? Dictionary(inputs.front()).duplicate() : Dictionary();
        const Array values = dictionary ? pairs.keys() : Array(inputs.front()).duplicate();
        for (int64_t index = 0; index < values.size(); ++index) {
            const Variant &slot = values[index];
            const Flow flow = dictionary ? run_body(index, &pairs[slot], &slot) : run_body(index, &slot);
            if (flow == Flow::BREAK) {
                break;
            }
            if (flow == Flow::RETURN || flow == Flow::ERROR) {
                return flow;
            }
        }
    } else {
        const int64_t first = inputs[0];
        const int64_t last = inputs[1];
        const int64_t step = inputs[2];
        if (step == 0) {
            execution.error = node_error(id, "For Step must be nonzero.");
            return Flow::ERROR;
        }
        for (int64_t index = first; step > 0 ? index <= last : index >= last; index += step) {
            const Flow flow = run_body(index);
            if (flow == Flow::BREAK) {
                break;
            }
            if (flow == Flow::RETURN || flow == Flow::ERROR) {
                return flow;
            }
            if ((step > 0 && index > std::numeric_limits<int64_t>::max() - step) ||
                (step < 0 && index < std::numeric_limits<int64_t>::min() - step)) {
                break;
            }
        }
    }
    return Flow::END;
}

/// Initialize a call frame with independent local collection defaults.
Frame::Frame(Runtime &owner, const Program &graph, [[maybe_unused]] const Frame *caller) :
        runtime(owner), program(graph), slots(graph.slot_count), can_suspend(caller == nullptr && graph.definition == 0) {
#ifdef DEBUG_ENABLED
    debug_caller = caller;
#endif
    locals.reserve(program.locals.values.size());
    for (const Variant &value : program.locals.values) {
        locals.push_back(fresh_value(value));
    }
}

/// Resolve a declared event to its visual entry node.
int64_t custom_event_entry(const Program &program, int64_t definition) {
    for (const auto &[id, node] : program.nodes) {
        if (node.kind == StringName("custom_event") && node.declaration == definition) {
            return id;
        }
    }
    return 0;
}

/// Run a graph function, macro or custom event with the caller's typed arguments.
Flow execute_call(Frame &frame, const Instruction &node,
        const ArgumentValues &inputs, std::optional<Dictionary> &values,
        Execution &execution, size_t depth) {
    const int64_t definition = node.declaration;
    const bool event = node.op == Op::EMIT_EVENT;
    if (!event && !node.callee) {
        execution.error = "Choose an available function or macro.";
        return Flow::ERROR;
    }
    Frame child(frame.runtime, event ? frame.runtime.events : *node.callee, &frame);
    const Flow flow = event ? execute_entry(child, custom_event_entry(child.program, definition), input_values(node, inputs),
            "then", execution, depth + 1) : execute_function_entry(child, node, inputs, execution, depth + 1);
    if (flow == Flow::BREAK || flow == Flow::CONTINUE) {
        execution.error = "Break and Continue must be inside a loop in the same graph.";
        return Flow::ERROR;
    }
    if (flow == Flow::ERROR) {
        return flow;
    }
    values = std::move(child.result);
    return Flow::END;
}

/// Run a macro path and execute each output tunnel against the original caller frame.
Flow execute_macro(Frame &frame, int64_t id, const Instruction &node, const StringName &exec_input,
        const ArgumentValues &inputs, Execution &execution, size_t depth, const MacroExit *outer_exit) {
    const auto program = frame.runtime.macros.find(node.declaration);
    if (program == frame.runtime.macros.end() || program->second.entry == 0) {
        execution.error = node_error(id, "Choose an available macro with an entry node.");
        return Flow::ERROR;
    }
    Frame child(frame.runtime, program->second, &frame);
    const Instruction &entry = child.program.nodes.at(child.program.entry);
    const StringName entry_pin = entry.next.count(exec_input) != 0 ? exec_input : StringName("then");
    Flow outer_flow = Flow::END;
    const MacroExit exit = [&](const StringName &port, const Dictionary &values) {
#ifdef DEBUG_ENABLED
        // The caller resumes at this tunnel even though the macro's C++ frame is still active.
        const VisualDebugScope debug_caller_scope(-1);
#endif
        if (const String failure = write_outputs(frame, node, values); !failure.is_empty()) {
            execution.error = node_error(id, failure);
            return Flow::ERROR;
        }
#ifdef DEBUG_ENABLED
        if (frame.debug_recording) record_debug_outputs(frame, id, node, "Returned by this macro");
#endif
        refresh_signal_outputs(frame, id);
        outer_flow = execute_path(frame, next_edge(node, port == StringName("in") ? StringName("then") : port),
                execution, depth + 1, outer_exit);
#ifdef DEBUG_ENABLED
        frame.debug_node = id;
#endif
        return outer_flow == Flow::END ? Flow::END : Flow::RETURN;
    };
    const Flow child_flow = execute_entry(child, child.program.entry, input_values(node, inputs), entry_pin,
            execution, depth + 1, &exit);
    if (child_flow == Flow::BREAK || child_flow == Flow::CONTINUE) {
        execution.error = node_error(id, "Break and Continue must be inside a loop in the same graph.");
        return Flow::ERROR;
    }
    return child_flow == Flow::ERROR ? Flow::ERROR : outer_flow;
}

/// Match flow nodes whose state persists across event and function calls.
bool stateful_flow(Op op) {
    return op == Op::DO_ONCE || op == Op::DO_N || op == Op::FLIP_FLOP || op == Op::GATE;
}

/// Apply a control pulse or choose an execution output for a stateful flow node.
StringName advance_stateful_flow(Frame &frame, int64_t id, const Instruction &node, const StringName &input_pin,
        const ArgumentValues &inputs, Variant &output) {
    const Op op = node.op;
    const auto key = std::make_pair(frame.program.definition, id);
    if (input_pin == StringName("reset")) {
        frame.runtime.flow_state[key] = 0;
        for (const auto &[name, pin] : node.outputs) execution_slot_for_write(frame, pin.slot).reset();
        return {};
    }
    const bool starts_closed = (op == Op::DO_ONCE || op == Op::GATE) &&
            !inputs.empty() && static_cast<bool>(inputs.front());
    auto &state = frame.runtime.flow_state.try_emplace(key, starts_closed ? 1 : 0).first->second;
    if (op == Op::GATE) {
        if (input_pin == StringName("open")) {
            state = 0;
        } else if (input_pin == StringName("close")) {
            state = 1;
        } else if (input_pin == StringName("toggle")) {
            state = 1 - state;
        } else if (state == 0) {
            return StringName("then");
        }
        return {};
    }
    if (op == Op::FLIP_FLOP) {
        const bool is_a = state == 0;
        state = 1 - state;
        output = is_a;
        return is_a ? StringName("a") : StringName("b");
    }
    const int64_t limit = op == Op::DO_N ? static_cast<int64_t>(inputs.front()) : 1;
    if (state < limit) {
        ++state;
        return StringName("then");
    }
    return {};
}

Flow execute_path(Frame &frame, const Edge &first, Execution &execution, size_t depth, const MacroExit *macro_exit) {
    if (depth > max_dispatch_depth) {
        execution.error = "Execution exceeded its nesting limit. Check for recursive calls.";
        return Flow::ERROR;
    }
    // All destinations belong to the immutable program or the shared terminal edge.
    const Edge *cursor = &first;
    while (cursor->node != 0) {
        const Edge &edge = *cursor;
        const int64_t id = edge.node;
        const Instruction &node = *edge.target;
        const Op op = node.op;
        if (++frame.runtime.steps > max_dispatch_steps) {
            execution.error = node_error(id, "Execution exceeded its step limit. Check for a cycle or a loop that never ends.");
            return Flow::ERROR;
        }
        if (!frame.runtime.has_live_owner()) {
            return Flow::END;
        }
#ifdef DEBUG_ENABLED
        auto *debugger = visual_debugger();
        if (!debugger)
#endif
        {
            int64_t scalar_result;
            if (op != Op::CALL_METHOD && evaluate_scalar(frame, node.scalar, scalar_result)) {
                if (op == Op::BRANCH) {
                    cursor = scalar_result ? &node.true_edge : &node.false_edge;
                } else {
                    auto *value = variable_value(frame, node);
                    // Scalar setters share the exact declared tag with the validated variable default.
                    if (node.scalar.type == Variant::INT) *VariantInternal::get_int(value) = scalar_result;
                    else *VariantInternal::get_bool(value) = scalar_result != 0;
                    cursor = &node.then_edge;
                }
                continue;
            }
            if (node.borrow_method_arguments) {
                Variant result;
                GDExtensionCallError error{};
                if (invoke_borrowed_method(frame, node, result, error)) {
                    if (error.error != GDEXTENSION_CALL_OK) {
                        execution.error = node_error(id, "Godot could not call the method (call error " +
                                String::num_int64(error.error) + ").");
                        return Flow::ERROR;
                    }
                    if (!node.outputs.empty()) {
                        if (const String failure = write_single_output(frame, node, std::move(result)); !failure.is_empty()) {
                            execution.error = node_error(id, failure);
                            return Flow::ERROR;
                        }
                        refresh_signal_outputs(frame, id);
                    }
                    cursor = &node.then_edge;
                    continue;
                }
            }
        }
        ArgumentValues inputs;
        String input_failure;
#ifdef DEBUG_ENABLED
        frame.debug_node = id;
        frame.debug_recording = debugger != nullptr;
        if (debugger) {
            ++frame.debug_sample;
            frame.debug_reads.clear();
        }
#endif
        const bool control = stateful_flow(op) && edge.pin != StringName("in");
        if (!control || (op == Op::GATE && edge.pin == StringName("toggle") &&
                frame.runtime.flow_state.count({frame.program.definition, id}) == 0)) {
            input_failure = action_inputs(frame, id, node, inputs);
        }
#ifdef DEBUG_ENABLED
        if (debugger) {
            const String path = frame.runtime.script->get_path();
            if (visual_debug_should_break(*debugger, path, visual_breakpoint_line(frame.program.definition, id))) {
                Dictionary pins;
                for (size_t index = 0; index < inputs.size(); ++index) {
                    const Dictionary &pin = node.inputs[index].pin;
                    pins[pin.get("label", pin.get("id", String("Input")))] = inputs[index];
                }
                if (!inspect_instruction(frame, pins, *debugger, input_failure)) {
                    return Flow::END;
                }
            }
            if (!frame.runtime.has_live_owner()) {
                return Flow::END;
            }
        }
#endif
        if (!input_failure.is_empty()) {
            execution.error = node_error(id, input_failure);
            return Flow::ERROR;
        }

        const Edge *outgoing = &node.then_edge;
        Variant single_output;
        String failure;
        if (op == Op::RETURN) {
            const Dictionary values = input_values(node, inputs);
            if (macro_exit) {
                return (*macro_exit)(edge.pin, values);
            }
            frame.result.emplace(values);
            return Flow::RETURN;
        }
        if (op == Op::BREAK) {
            return Flow::BREAK;
        }
        if (op == Op::CONTINUE) {
            return Flow::CONTINUE;
        }
        if (stateful_flow(op)) {
            const StringName output_pin = advance_stateful_flow(frame, id, node, edge.pin, inputs, single_output);
            if (output_pin.is_empty()) {
                return Flow::END;
            }
            outgoing = &next_edge(node, output_pin);
        } else if (op == Op::BRANCH) {
            outgoing = static_cast<bool>(inputs[0]) ? &node.true_edge : &node.false_edge;
        } else if (op == Op::CAST) {
            const Variant &candidate = inputs.front();
            const auto &output = node.outputs.begin()->second;
            const bool matches = is_live_method_receiver(candidate) && output.type.accepts(candidate);
            single_output = matches ? candidate : Variant();
            outgoing = &next_edge(node, matches ? StringName("success") : StringName("failure"));
        } else if (op == Op::LOAD_RESOURCE) {
            const Ref<Resource> resource = load_resource_path(inputs.front(), node.native_name);
            if (!frame.runtime.has_live_owner()) return Flow::END;
            if (resource.is_valid()) single_output = resource;
            outgoing = &next_edge(node, resource.is_valid() ? StringName("success") : StringName("failure"));
        } else if (op == Op::SWITCH) {
            outgoing = &next_edge(node, "default");
            for (const auto &[value, target] : node.cases) {
                if (inputs[0] == value) {
                    outgoing = &target;
                    break;
                }
            }
        } else if (op == Op::SWITCH_ENUM) {
            const auto match = node.enum_cases.find(static_cast<int64_t>(inputs[0]));
            outgoing = &next_edge(node, match == node.enum_cases.end() ? StringName("default") : match->second);
        } else if (op == Op::SEQUENCE) {
            for (auto target = node.sequence.begin(); target != node.sequence.end(); ++target) {
                const Flow flow = execute_path(frame, *target, execution, depth + 1, macro_exit);
                if (flow == Flow::SUSPEND) {
                    for (++target; target != node.sequence.end(); ++target) {
                        execution.following.push_back({*target, depth + 1});
                    }
                    return flow;
                }
                if (flow != Flow::END) {
                    return flow;
                }
            }
            return Flow::END;
        } else if (op == Op::WHILE || op == Op::FOR || op == Op::FOREACH) {
            const Flow flow = execute_loop(frame, id, node, inputs, execution, depth, macro_exit);
            if (flow != Flow::END) {
                return flow;
            }
            outgoing = &node.completed_edge;
        } else if (op == Op::DELAY || op == Op::AWAIT_SIGNAL) {
            return prepare_wait(frame, id, node, inputs, execution, depth, macro_exit);
        } else if (op == Op::PRINT) {
            UtilityFunctions::print(String(inputs[0]));
        } else if (op == Op::CREATE_NODE) {
            single_output = ClassDB::instantiate(node.native_name);
            auto *created = Object::cast_to<Node>(single_output.get_validated_object());
            if (created) {
                execution.created.emplace_back(created->get_instance_id());
            } else {
                failure = "Godot could not instantiate this node class.";
            }
        } else if (op == Op::CREATE_SCRIPT_INSTANCE) {
            failure = create_script_instance(node, inputs, single_output, execution);
            if (!frame.runtime.has_live_owner()) return Flow::END;
        } else if (op == Op::ADD_CHILD) {
            failure = attach_child(inputs);
        } else if (op == Op::SET_PROPERTY || op == Op::CALL_METHOD) {
            failure = invoke_member(node, inputs, single_output);
        } else if (op == Op::COLLECTION && collection_operation_edits(node.collection)) {
            failure = write_collection_outputs(frame, node, inputs);
        } else if (op == Op::SET_VARIABLE) {
            Variant *variable = variable_value(frame, node);
            if (!variable || inputs.empty()) {
                failure = "Choose a declared variable.";
            } else {
                *variable = inputs.front();
            }
        } else if (op == Op::CALL_MACRO) {
            return execute_macro(frame, id, node, edge.pin, inputs, execution, depth, macro_exit);
        } else if (op == Op::CALL_FUNCTION || op == Op::EMIT_EVENT) {
            std::optional<Dictionary> outputs;
            if (const Flow flow = execute_call(frame, node, inputs, outputs, execution, depth); flow != Flow::END) {
                return flow;
            }
            if (!node.outputs.empty()) {
                failure = outputs ? write_outputs(frame, node, *outputs) : write_outputs(frame, node, Dictionary());
            }
        } else if (op == Op::CUSTOM_SCRIPT) {
            if (const Flow flow = execute_custom_script(frame, id, node, inputs, execution, depth); flow != Flow::END) {
                return flow;
            }
            if (!frame.runtime.has_live_owner()) {
                return Flow::END;
            }
        } else if (op != Op::REROUTE) {
            failure = "This node cannot receive execution.";
        }

        if (failure.is_empty() && !node.outputs.empty() &&
                (op == Op::CREATE_NODE || op == Op::CREATE_SCRIPT_INSTANCE || op == Op::CALL_METHOD ||
                        op == Op::FLIP_FLOP || op == Op::CAST || op == Op::LOAD_RESOURCE)) {
            if (node.native_updated_target) {
                const auto &target = *node.outputs.find(StringName("updated_target"));
                failure = publish_output(frame, node, target.first, target.second, std::move(inputs.front()));
                const auto returned = node.outputs.find(StringName("result"));
                if (failure.is_empty() && returned != node.outputs.end()) {
                    failure = publish_output(frame, node, returned->first, returned->second, std::move(single_output));
                }
            } else {
                failure = write_single_output(frame, node, std::move(single_output));
            }
        }
        if (!failure.is_empty()) {
            execution.error = node_error(id, failure);
            return Flow::ERROR;
        }
        if (!node.outputs.empty()) {
#ifdef DEBUG_ENABLED
            if (frame.debug_recording) record_debug_outputs(frame, id, node, "Produced by this node");
#endif
            refresh_signal_outputs(frame, id);
        }
        cursor = outgoing;
    }
    return Flow::END;
}

String evaluate_pure_call(Frame &frame, const Instruction &node, const ArgumentValues &inputs,
        Dictionary &outputs, size_t depth) {
    if (frame.runtime.depth >= max_dispatch_depth || depth > max_dispatch_depth) {
        return "Too many nested data-only calls.";
    }
    const Program *program = node.callee;
    if (node.op == Op::CALL_MACRO) {
        const auto found = frame.runtime.macros.find(node.declaration);
        program = found == frame.runtime.macros.end() ? nullptr : &found->second;
    }
    if (!program || !program->pure || program->entry == 0) {
        return "The data-only definition is unavailable.";
    }
    ++frame.runtime.depth;
    Frame child(frame.runtime, *program, &frame);
    Execution execution;
    const Flow flow = node.op == Op::CALL_FUNCTION ? execute_function_entry(child, node, inputs, execution, depth + 1) :
            execute_entry(child, program->entry, input_values(node, inputs), "then", execution, depth + 1);
    --frame.runtime.depth;
    if (!execution.error.is_empty()) {
        return execution.error;
    }
    if (flow != Flow::RETURN) {
        return "The data-only definition ended without returning its outputs.";
    }
    if (child.result) outputs = std::move(*child.result);
    return {};
}



/// Read authored method/event input descriptors with a stable return order.
Array data_pins(const Variant &value) {
    Array pins;
    if (value.get_type() == Variant::ARRAY) {
        for (const Variant &entry : Array(value)) {
            if (entry.get_type() == Variant::DICTIONARY && !exec_pin(entry)) {
                pins.append(entry);
            }
        }
    }
    return pins;
}

/// Resolve and check a method name supplied by Godot or an external script.
bool find_method(const Dictionary &root, const StringName &base, const StringName &name, GraphMethod &method) {
    if (lifecycle(name)) {
        for (const Dictionary &event : lifecycle_events()) {
            if (name_field(event, "callback") != name) {
                continue;
            }
            method.inputs = data_pins(event.get("inputs", Array()));
            break;
        }
        for (const Variant &item : Array(root.get("nodes", Array()))) {
            if (item.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary node = item;
            const StringName kind = name_field(node, "kind");
            if ((kind == StringName("ready") && name == StringName("_ready")) ||
                (kind == StringName("lifecycle") && name_field(node, "callback") == name)) {
                method.entry = read_id(node.get("id", Variant()));
                break;
            }
        }
        return method.entry != 0 || name == StringName("_ready") || name == StringName("_enter_tree") ||
                name == StringName("_exit_tree");
    }
    if (name == StringName(signal_dispatch)) {
        return true;
    }
    if (name.is_empty() || String(name).begins_with("_") || !String(name).is_valid_identifier() ||
        ClassDB::class_has_method(base, name)) {
        return false;
    }
    for (const char *key : {"functions", "events"}) {
        const Variant records = root.get(key, Variant());
        if (records.get_type() != Variant::ARRAY) {
            continue;
        }
        for (const Variant &item : Array(records)) {
            if (item.get_type() != Variant::DICTIONARY || name_field(item, "name") != name) {
                continue;
            }
            const Dictionary definition = item;
            method.definition = read_id(definition.get("id", Variant()));
            method.function = String(key) == "functions";
            method.inputs = data_pins(definition.get("inputs", Array()));
            method.outputs = data_pins(definition.get("outputs", Array()));
            return method.definition > 0;
        }
    }
    return false;
}

/// Build the normal Godot method metadata for an authored callable.
MethodInfo method_info(const StringName &name, const GraphMethod &method) {
    MethodInfo info(name);
    for (const Dictionary &pin : method.inputs) {
        info.arguments.push_back(pin_info(pin));
        if (pin.has("default")) {
            info.default_arguments.push_back(pin["default"]);
        }
    }
    if (method.outputs.size() == 1) {
        info.return_val = pin_info(method.outputs[0]);
    } else if (method.outputs.size() > 1) {
        info.return_val = PropertyInfo(Variant::DICTIONARY, "");
    }
    return info;
}

/// Verify a Godot callback's arguments or resolve its authored trailing defaults.
bool bind_arguments(const Array &pins, const GDExtensionConstVariantPtr *args, GDExtensionInt count,
        Dictionary &values, GDExtensionCallError &error) {
    if (count > pins.size()) {
        error = {GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS, 0, static_cast<int32_t>(pins.size())};
        return false;
    }
    for (int64_t index = 0; index < pins.size(); ++index) {
        const Dictionary pin = pins[index];
        Variant value;
        if (index < count) {
            value = *static_cast<const Variant *>(args[index]);
        } else if (!pin.has("default")) {
            error = {GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS, 0, static_cast<int32_t>(index + 1)};
            return false;
        } else if (!resolve_literal(pin, pin["default"], value)) {
            error = {GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, static_cast<int32_t>(index),
                    static_cast<int32_t>(static_cast<int64_t>(pin.get("type", 0)))};
            return false;
        }
        if (!pin_accepts_value(pin, value)) {
            error = {GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, static_cast<int32_t>(index),
                    static_cast<int32_t>(static_cast<int64_t>(pin.get("type", 0)))};
            return false;
        }
        values[name_field(pin, "id")] = value;
    }
    return true;
}

/// The four operations exposed on the generated GDScript graph context.
enum class CustomOperation { READ, WRITE, CALL, EMIT };

/// State shared by the callbacks for one synchronous Custom Script invocation.
struct CustomInvocation {
    /// Caller frame, cleared before the invocation returns.
    Frame *frame;
    /// Execution outcome and cleanup shared with nested graph calls.
    Execution &execution;
    /// Depth of the action in its live graph.
    size_t depth;
    /// Stable node ID used to explain action-local errors.
    int64_t node;
    /// Report a bridge failure at the Custom Script node.
    void fail(const String &message) const { execution.error = node_error(node, message); }
};

/// A callback retained by the helper or by a script that stores it elsewhere.
struct CustomCallback {
    /// The request this callback validates and handles.
    CustomOperation operation;
    /// The live frame remains accessible only on the creating main thread.
    std::shared_ptr<CustomInvocation> invocation;
};

/// Invalidate all copies of the callbacks before leaving this action's C++ frame.
struct CustomInvocationScope {
    /// Heap lifetime lets an escaped Callable discover that the frame has ended.
    std::shared_ptr<CustomInvocation> invocation;

    /// Clear the frame only on the Godot main thread, which also runs all valid callbacks.
    ~CustomInvocationScope() { invocation->frame = nullptr; }
};

/// Read or write an existing variable slot without creating independent helper state.
Variant custom_variable(CustomInvocation &invocation, bool local, int64_t id, const Variant *replacement) {
    Frame &frame = *invocation.frame;
    const Variables &layout = local ? frame.program.locals : frame.runtime.variables;
    const auto entry = layout.indices.find(id);
    if (entry == layout.indices.end()) {
        invocation.fail("Custom Script could not find a graph variable.");
        return {};
    }
    const size_t slot = entry->second;
    const Dictionary &pin = layout.pins[slot];
    Variant &stored = (local ? frame.locals : frame.runtime.variables.values)[slot];
    if (replacement) {
        if (pin_has_collection_schema(pin)) {
            if (!resolve_collection_value(pin, *replacement, stored)) {
                invocation.fail("Custom Script assigned a collection that does not match the graph variable type.");
            }
        } else if (!pin_accepts_value(pin, *replacement)) {
            invocation.fail("Custom Script assigned a value that does not match the graph variable type.");
        } else {
            stored = *replacement;
        }
        return {};
    }
    if (asset_literal(pin, stored) || (resource_collection_pin(pin) && !pin_accepts_value(pin, stored))) {
        Variant loaded;
        if (!resolve_literal(pin, stored, loaded)) {
            invocation.fail("Custom Script could not load the graph variable's resource.");
            return {};
        }
        stored = std::move(loaded);
    }
    if (is_expired(stored)) {
        invocation.fail("Custom Script read a graph variable whose object has been freed.");
        return {};
    }
    return stored;
}

/// Invoke an existing graph function or event within the originating frame's debugger and execution budget.
Variant custom_call(CustomInvocation &invocation, bool function, int64_t definition, const Array &arguments) {
    Frame &frame = *invocation.frame;
    Execution &execution = invocation.execution;
    const auto method = frame.runtime.custom_methods.find({function, definition});
    if (method == frame.runtime.custom_methods.end()) {
        invocation.fail("Custom Script could not find the graph function or event.");
        return {};
    }
    if (invocation.depth >= max_dispatch_depth) {
        invocation.fail("Execution exceeded its nesting limit. Check for recursive calls.");
        return {};
    }
    const GraphMethod &signature = method->second;
    std::vector<GDExtensionConstVariantPtr> pointers;
    pointers.reserve(arguments.size());
    for (const Variant &argument : arguments) {
        pointers.push_back(&argument);
    }
    GDExtensionCallError error{};
    Dictionary values;
    if (!bind_arguments(signature.inputs, pointers.data(), arguments.size(), values, error)) {
        invocation.fail("Custom Script supplied arguments that do not match the graph function or event.");
        return {};
    }
    const auto found = frame.runtime.functions.find(definition);
    if (function && found == frame.runtime.functions.end()) {
        invocation.fail("Custom Script could not find the compiled graph function.");
        return {};
    }
    const Program &program = function ? found->second : frame.runtime.events;
    Frame child(frame.runtime, program, &frame);
    const int64_t entry = function ? program.entry : custom_event_entry(program, definition);
    const Flow flow = execute_entry(child, entry, values, "then", execution, invocation.depth + 1);
    if (flow == Flow::BREAK || flow == Flow::CONTINUE) {
        invocation.fail("Break and Continue must be inside a loop in the same graph.");
    }
    if (!execution.error.is_empty() || signature.outputs.is_empty() || !frame.runtime.has_live_owner()) {
        return {};
    }
    if (flow != Flow::RETURN || !child.result) {
        invocation.fail("This function ended without returning its declared outputs.");
        return {};
    }
    if (signature.outputs.size() == 1) {
        return named_value(*child.result, name_field(signature.outputs[0], "id"));
    }
    return *child.result;
}

/// Validate the generated bridge's ABI before using the active native frame.
Callable custom_callback(const std::shared_ptr<CustomInvocation> &invocation, CustomOperation operation) {
    GDExtensionCallableCustomInfo2 info{};
    info.callable_userdata = new CustomCallback{operation, invocation};
    info.token = gdextension_interface::token;
    info.is_valid_func = [](void *data) -> GDExtensionBool {
        if (!on_main_thread()) return false;
        const auto &active = *static_cast<const CustomCallback *>(data)->invocation;
        return active.frame && active.frame->runtime.has_live_owner() && active.execution.error.is_empty();
    };
    info.call_func = [](void *data, const GDExtensionConstVariantPtr *arguments, GDExtensionInt count,
                         GDExtensionVariantPtr result, GDExtensionCallError *error) {
        *error = {GDEXTENSION_CALL_ERROR_INVALID_METHOD, 0, 0};
        if (!on_main_thread()) return;
        const auto &callback = *static_cast<const CustomCallback *>(data);
        CustomInvocation &active = *callback.invocation;
        if (!active.frame || !active.frame->runtime.has_live_owner() || !active.execution.error.is_empty()) return;
        const bool variable = callback.operation == CustomOperation::READ || callback.operation == CustomOperation::WRITE;
        const bool write = callback.operation == CustomOperation::WRITE;
        if (count != (write ? 3 : 2)) return;
        const auto *first = static_cast<const Variant *>(arguments[0]);
        const auto *second = static_cast<const Variant *>(arguments[1]);
        const auto *third = write ? static_cast<const Variant *>(arguments[2]) : nullptr;
        if (variable && is_text(*first) && second->get_type() == Variant::INT) {
            *static_cast<Variant *>(result) = custom_variable(active, StringName(*first) == StringName("local"),
                    static_cast<int64_t>(*second), third);
        } else if (!variable && first->get_type() == Variant::INT && second->get_type() == Variant::ARRAY) {
            *static_cast<Variant *>(result) = custom_call(active, callback.operation == CustomOperation::CALL,
                    static_cast<int64_t>(*first), Array(*second));
        } else {
            return;
        }
        if (active.execution.error.is_empty()) *error = {GDEXTENSION_CALL_OK, 0, 0};
    };
    info.free_func = [](void *data) { delete static_cast<CustomCallback *>(data); };
    Callable callback;
    gdextension_interface::callable_custom_create2(callback._native_ptr(), &info);
    return callback;
}

Flow execute_custom_script(Frame &frame, int64_t id, const Instruction &node, const ArgumentValues &inputs,
        Execution &execution, size_t depth) {
    const CustomAction &action = *node.custom_script;
    Object *owner = ObjectDB::get_instance(frame.runtime.owner);
    if (!owner) return Flow::END;
    Variant helper = action.script->new_();
    if (!helper.get_validated_object()) {
        execution.error = node_error(id, "Custom Script could not instantiate its GDScript helper.");
        return Flow::ERROR;
    }
    const CustomInvocationScope scope{std::make_shared<CustomInvocation>(CustomInvocation{&frame, execution, depth, id})};
    const std::array<Variant, 4> callbacks{
        custom_callback(scope.invocation, CustomOperation::READ),
        custom_callback(scope.invocation, CustomOperation::WRITE),
        custom_callback(scope.invocation, CustomOperation::CALL),
        custom_callback(scope.invocation, CustomOperation::EMIT),
    };
    const Variant *configuration[]{&callbacks[0], &callbacks[1], &callbacks[2], &callbacks[3]};
    GDExtensionCallError error{};
    Variant completion_marker;
    helper.callp("__crimson_configure", configuration, static_cast<int>(callbacks.size()), completion_marker, error);
    if (error.error != GDEXTENSION_CALL_OK || !completion_marker.get_validated_object()) {
        execution.error = node_error(id, "Custom Script could not attach its graph context.");
        return Flow::ERROR;
    }
    const Variant owner_value = owner;
    const Variant missing_inputs = action.missing_inputs;
    std::vector<const Variant *> arguments{&owner_value};
    arguments.reserve(inputs.size() + (action.missing_inputs.is_empty() ? 1 : 2));
    if (!action.missing_inputs.is_empty()) {
        arguments.push_back(&missing_inputs);
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        arguments.push_back(&inputs[index]);
    }
    Variant returned;
    helper.callp("__crimson_run", arguments.data(), static_cast<int>(arguments.size()), returned, error);
    scope.invocation->frame = nullptr;
    if (!frame.runtime.has_live_owner()) return Flow::END;
    if (!execution.error.is_empty()) return Flow::ERROR;
    const Array values = returned.get_type() == Variant::ARRAY ? Array(returned) : Array();
    if (error.error != GDEXTENSION_CALL_OK || values.size() != static_cast<int64_t>(action.outputs.size()) + 1 ||
            values[0].get_type() != Variant::OBJECT ||
            values[0].operator ObjectID() != completion_marker.operator ObjectID()) {
        execution.error = node_error(id, "Custom Script did not finish. Check its GDScript errors.");
        return Flow::ERROR;
    }
    for (size_t index = 0; index < action.outputs.size(); ++index) {
        const Output &output = *action.outputs[index];
        if (const String failure = publish_output(frame, node, name_field(output.pin, "id"), output, values[index + 1]);
                !failure.is_empty()) {
            execution.error = node_error(id, failure);
            return Flow::ERROR;
        }
    }
    return Flow::END;
}

/// Remove a subscription from its still-live source.
void disconnect_signal(const Subscription &item) {
    if (item.connection) item.connection->active.store(false, std::memory_order_relaxed);
    Object *source = ObjectDB::get_instance(item.source);
    if (source && source->is_connected(item.signal, item.handler)) {
        source->disconnect(item.signal, item.handler);
    }
}

/// Execute a signal using the compiled entry kept alive by the active runtime.
void dispatch_signal_entry(Runtime &runtime, int64_t id, const Instruction &entry, ObjectID source_id,
        const GDExtensionConstVariantPtr *arguments, GDExtensionInt count, GDExtensionCallError &error);

/// Make a Godot connection that carries a compiled signal entry rather than bound Variant IDs.
Callable native_signal_handler(Runtime &runtime, int64_t id, const Instruction &node, ObjectID source,
        SignalConnection *&connection) {
    connection = new SignalConnection{runtime.self, runtime.owner, source, &node, id};
    GDExtensionCallableCustomInfo2 info{};
    info.callable_userdata = connection;
    info.token = gdextension_interface::token;
    info.object_id = runtime.owner;
    info.is_valid_func = [](void *data) -> GDExtensionBool {
        const auto &binding = *static_cast<const SignalConnection *>(data);
        return binding.active.load(std::memory_order_relaxed) &&
                gdextension_interface::object_get_instance_from_id(binding.owner);
    };
    info.call_func = [](void *data, const GDExtensionConstVariantPtr *arguments, GDExtensionInt count,
                         GDExtensionVariantPtr, GDExtensionCallError *error) {
        *error = {GDEXTENSION_CALL_OK, 0, 0};
        if (!on_main_thread()) {
            *error = {GDEXTENSION_CALL_ERROR_INVALID_METHOD, 0, 0};
            return;
        }
        const auto &binding = *static_cast<const SignalConnection *>(data);
        if (!binding.active.load(std::memory_order_relaxed)) return;
        const auto receiver = binding.runtime.lock();
        if (!receiver || receiver->detached) return;
        dispatch_signal_entry(*receiver, binding.node, *binding.entry, binding.source, arguments, count, *error);
    };
    info.free_func = [](void *data) { delete static_cast<SignalConnection *>(data); };
    info.to_string_func = [](void *, GDExtensionBool *valid, GDExtensionStringPtr output) {
        *static_cast<String *>(output) = "Crimson Visual Script signal";
        *valid = true;
    };
    Callable handler;
    gdextension_interface::callable_custom_create2(handler._native_ptr(), &info);
    return handler;
}

/// Remove all signal subscriptions from this instance.
void unbind_signals(Runtime &runtime) {
    runtime.signals_bound = false;
    for (const auto &[id, item] : runtime.subscriptions) {
        disconnect_signal(item);
    }
    runtime.subscriptions.clear();
}

/// Read the optional receiver wire from the signal entry's only data input.
Edge signal_target(const Instruction &node) {
    return node.inputs.empty() ? Edge() : node.inputs.front().wire;
}

/// Index execution producers referenced by each signal receiver's data expression.
void index_signal_producers(Runtime &runtime) {
    for (const auto &[id, node] : runtime.events.nodes) {
        if (node.kind != StringName("native_signal")) {
            continue;
        }
        std::set<int64_t> visited;
        std::vector<int64_t> pending{signal_target(node).node};
        while (!pending.empty()) {
            const int64_t producer = pending.back();
            pending.pop_back();
            if (producer == 0 || !visited.insert(producer).second) {
                continue;
            }
            const Instruction &source = runtime.events.nodes.at(producer);
        if (source.pure) {
                for (const Input &input : source.inputs) {
                    pending.push_back(input.wire.node);
                }
            } else if (producer != id) {
                runtime.signal_producers[producer].push_back(id);
            }
        }
    }
}

/// Bind or replace one receiver without retaining the emitting object.
void bind_signal(Runtime &runtime, int64_t id, const Instruction &node, Object *source, bool report_failure) {
    const ObjectID source_id = source ? ObjectID(source->get_instance_id()) : ObjectID();
    auto current = runtime.subscriptions.find(id);
    if (current != runtime.subscriptions.end()) {
        if (current->second.source == source_id) {
            return;
        }
        disconnect_signal(current->second);
        runtime.subscriptions.erase(current);
    }
    Object *owner = ObjectDB::get_instance(runtime.owner);
    const StringName signal = name_field(node.record, "signal");
    const StringName expected = name_field(node.record, "class");
    const StringName &target_class = node.inputs.empty() ? expected : node.inputs.front().type.object_class;
    const bool available = node.member_script_path.is_empty() ? ClassDB::class_has_signal(expected, signal) :
            object_has_script_type(source, node.member_script_path) && source->has_signal(signal);
    if (!source || !owner || source->is_queued_for_deletion() || expected.is_empty() ||
        target_class.is_empty() || !source->is_class(target_class) || !source->is_class(expected) ||
        !available) {
        if (report_failure) {
            UtilityFunctions::push_error("[Crimson] " + runtime.script->get_path() + ": " +
                    node_error(id, "The native signal target or signal is unavailable."));
        }
        return;
    }
    SignalConnection *connection = nullptr;
    const Callable handler = native_signal_handler(runtime, id, node, source_id, connection);
    const Error error = source->connect(signal, handler);
    if (error == OK) {
        runtime.subscriptions.emplace(id, Subscription{source_id, signal, handler, connection});
    } else if (report_failure) {
        UtilityFunctions::push_error("[Crimson] " + runtime.script->get_path() + ": " +
                node_error(id, "Godot could not connect the native signal."));
    }
}

/// Evaluate a receiver in the current frame without carrying its lifetime into a subscription.
void refresh_signal(Frame &frame, int64_t id, bool report_failure) {
    const Instruction &node = frame.runtime.events.nodes.at(id);
    if (node.signal_target_disabled) {
        return;
    }
    const Edge wire = signal_target(node);
    if (wire.node == 0) {
        bind_signal(frame.runtime, id, node, scene_node(frame, node.record), report_failure);
        return;
    }
    const size_t slot_index = node.inputs.front().slot;
    ValueSlot value;
    if (evaluate_data(frame, wire.node, *wire.target, slot_index, 0).is_empty()) {
        value = frame.slots[slot_index];
    }
    clear_pure_slots(frame);
    if (value && !is_expired(*value)) {
        bind_signal(frame.runtime, id, node, live_object(*value), false);
    }
}

void refresh_signal_outputs(Frame &frame, int64_t producer) {
    if (frame.program.definition != 0 || !frame.runtime.signals_bound || frame.runtime.detached) {
        return;
    }
    const auto found = frame.runtime.signal_producers.find(producer);
    if (found != frame.runtime.signal_producers.end()) {
        for (const int64_t id : found->second) {
            refresh_signal(frame, id, false);
        }
    }
}

/// Connect available scene and pure receivers; Ready retries targets unavailable during Enter Tree.
void bind_signals(Runtime &runtime, bool report_failure) {
    if (!runtime.error.is_empty()) {
        return;
    }
    runtime.signals_bound = true;
    if (runtime.depth == 0) {
        runtime.steps = 0;
    }
    Frame frame(runtime, runtime.events);
    for (const auto &[id, node] : runtime.events.nodes) {
        if (node.kind == StringName("native_signal")) {
            refresh_signal(frame, id, report_failure);
        }
    }
}

/// Open a top-level invocation or continue its shared budget on a reentrant callback.
bool begin_dispatch(Runtime &runtime) {
    if (!runtime.error.is_empty()) return false;
    if (runtime.depth >= max_dispatch_depth) {
        UtilityFunctions::push_error("[Crimson] " + runtime.script->get_path() + ": Too many nested visual script calls.");
        return false;
    }
    if (runtime.depth++ == 0) {
        runtime.steps = 0;
    }
    return true;
}

/// Close a dispatch and report failures using the recorded graph node.
bool end_dispatch(Runtime &runtime, Execution &execution, Flow flow, bool returns_value) {
    --runtime.depth;
    if (flow == Flow::BREAK || flow == Flow::CONTINUE) {
        execution.error = "Break and Continue must be inside a loop.";
    }
    if (execution.error.is_empty() && returns_value && flow != Flow::RETURN) {
        execution.error = "This function ended without returning its declared outputs.";
    }
    if (execution.error.is_empty()) return true;
    UtilityFunctions::push_error("[Crimson] " + runtime.script->get_path() + ": " + execution.error);
    return false;
}

/// Shared thread-safe lifecycle of a one-shot wait.
enum class WaitState { WAITING, EMITTED, CANCELED };

/// Own the execution values and connections for one event that yielded to Godot.
struct SuspendedDispatch {
    /// Retained event values, detached from the synchronous C++ call stack.
    Frame frame;
    /// Remaining paths and deferred cleanup of graph-created nodes.
    Execution execution;
    /// Retain the existing dispatch watchdog across each wait.
    size_t steps;
    /// Godot owns this connection until it emits or the graph cancels it.
    Callable handler;
    /// Optional connection to a Node emitter's departure notification.
    Callable source_exit;
    /// Set by the emitting thread before queuing a main-thread continuation.
    std::shared_ptr<std::atomic<WaitState>> state = std::make_shared<std::atomic<WaitState>>(WaitState::WAITING);

    /// Move the completed synchronous prefix off the C++ stack.
    SuspendedDispatch(Frame &&current_frame, Execution &&current_execution, size_t current_steps) :
            frame(std::move(current_frame)), execution(std::move(current_execution)), steps(current_steps) {}
};

/// How a native callback targets the owning script's suspended work.
enum class WaitAction { RESUME, CANCEL, MONITOR };

/// Only the engine connection owns these bindings. They never retain a graph instance.
struct WaitCallback {
    /// Stop delivering if the script instance has already been destroyed.
    std::weak_ptr<Runtime> runtime;
    /// Used by Godot to invalidate a callback when its incoming owner is gone.
    ObjectID owner;
    /// Identify one wait without retaining it in native callbacks.
    uint64_t token;
    /// Select the behavior of this engine connection.
    WaitAction action;
};

/// Disconnect owned callbacks before releasing the saved values and constructed nodes.
std::shared_ptr<SuspendedDispatch> remove_wait(Runtime &runtime, uint64_t token) {
    const auto found = runtime.suspended.find(token);
    if (found == runtime.suspended.end()) return {};
    auto suspended = std::move(found->second);
    runtime.suspended.erase(found);
    suspended->state->store(WaitState::CANCELED, std::memory_order_release);
    Signal &signal = suspended->execution.wait->signal;
    if (Object *source = signal.get_object()) {
        if (signal.is_connected(suspended->handler)) signal.disconnect(suspended->handler);
        constexpr char exiting[] = "tree_exiting";
        if (!suspended->source_exit.is_null() && source->is_connected(exiting, suspended->source_exit)) {
            source->disconnect(exiting, suspended->source_exit);
        }
    }
    if (runtime.suspended.empty()) {
        auto *tree = ObjectDB::get_instance(runtime.wait_tree);
        if (tree && tree->is_connected("process_frame", runtime.wait_monitor)) {
            tree->disconnect("process_frame", runtime.wait_monitor);
        }
        runtime.wait_monitor = Callable();
        runtime.wait_tree = ObjectID();
    }
    return suspended;
}

/// Stop every event waiting on this owner without running its Then path.
void cancel_waits(Runtime &runtime) {
    while (!runtime.suspended.empty()) remove_wait(runtime, runtime.suspended.begin()->first);
}

/// Cancel source cleanup only if a concurrent emission has not already claimed the wait.
void cancel_unemitted_wait(Runtime &runtime, uint64_t token) {
    const auto found = runtime.suspended.find(token);
    if (found == runtime.suspended.end()) return;
    WaitState waiting = WaitState::WAITING;
    if (found->second->state->compare_exchange_strong(waiting, WaitState::CANCELED, std::memory_order_acq_rel)) {
        remove_wait(runtime, token);
    }
}

/// Clear a wait whose non-Node emitter was destroyed without sending a lifecycle signal.
void monitor_waits(Runtime &runtime) {
    std::vector<uint64_t> abandoned;
    for (const auto &[token, suspended] : runtime.suspended) {
        if (suspended->state->load(std::memory_order_acquire) != WaitState::WAITING) continue;
        const Object *source = suspended->execution.wait->signal.get_object();
        if (!source || source->is_queued_for_deletion()) abandoned.push_back(token);
    }
    for (const uint64_t token : abandoned) cancel_unemitted_wait(runtime, token);
}

/// Resume the main-thread continuation with the emitted signal payload.
void resume_wait(Runtime &runtime, uint64_t token, const Array &arguments);

/// Make a unique callable for one wait. Godot defers result delivery to the main thread.
Callable wait_callback(Runtime &runtime, uint64_t token, WaitAction action) {
    GDExtensionCallableCustomInfo2 info{};
    info.callable_userdata = new WaitCallback{runtime.self, runtime.owner, token, action};
    info.object_id = runtime.owner;
    info.token = gdextension_interface::token;
    info.is_valid_func = [](void *data) -> GDExtensionBool {
        return gdextension_interface::object_get_instance_from_id(static_cast<const WaitCallback *>(data)->owner) != nullptr;
    };
    info.call_func = [](void *data, const GDExtensionConstVariantPtr *args, GDExtensionInt count,
                         GDExtensionVariantPtr, GDExtensionCallError *error) {
        *error = {GDEXTENSION_CALL_OK, 0, 0};
        if (!on_main_thread()) return;
        const auto &binding = *static_cast<const WaitCallback *>(data);
        const auto receiver = binding.runtime.lock();
        if (!receiver || receiver->detached) return;
        if (binding.action == WaitAction::MONITOR) {
            monitor_waits(*receiver);
        } else if (binding.action == WaitAction::RESUME && count == 1) {
            resume_wait(*receiver, binding.token, *static_cast<const Variant *>(args[0]));
        } else if (binding.action == WaitAction::CANCEL) {
            cancel_unemitted_wait(*receiver, binding.token);
        }
    };
    info.free_func = [](void *data) { delete static_cast<WaitCallback *>(data); };
    Callable callback;
    gdextension_interface::callable_custom_create2(callback._native_ptr(), &info);
    return callback;
}

/// Data owned by a one-shot signal connection. Capturing the emission permits delayed delivery even if its emitter exits.
struct WaitEmission {
    /// Claim the first emission without accessing mutable graph state off the main thread.
    std::shared_ptr<std::atomic<WaitState>> state;
    /// Delivery target that will check owner liveness and the saved wait ID.
    Callable resume;
};

/// Capture the first emission and defer graph execution until Godot leaves the signal call stack.
Callable wait_emitter(Runtime &runtime, uint64_t token, const std::shared_ptr<std::atomic<WaitState>> &state) {
    GDExtensionCallableCustomInfo2 info{};
    info.callable_userdata = new WaitEmission{state, wait_callback(runtime, token, WaitAction::RESUME)};
    info.object_id = runtime.owner;
    info.token = gdextension_interface::token;
    info.call_func = [](void *data, const GDExtensionConstVariantPtr *args, GDExtensionInt count,
                         GDExtensionVariantPtr, GDExtensionCallError *error) {
        *error = {GDEXTENSION_CALL_OK, 0, 0};
        const auto &binding = *static_cast<const WaitEmission *>(data);
        WaitState waiting = WaitState::WAITING;
        if (!binding.state->compare_exchange_strong(waiting, WaitState::EMITTED, std::memory_order_acq_rel)) return;
        Array payload;
        for (int64_t index = 0; index < count; ++index) payload.append(*static_cast<const Variant *>(args[index]));
        binding.resume.call_deferred(payload);
    };
    info.free_func = [](void *data) { delete static_cast<WaitEmission *>(data); };
    Callable callback;
    gdextension_interface::callable_custom_create2(callback._native_ptr(), &info);
    return callback;
}

/// Publish the moved frame only after all its native wait connections are ready.
bool store_wait(Frame &&frame, Execution &&execution) {
    Runtime &runtime = frame.runtime;
    const int64_t node = execution.wait->node;
    Object *source = execution.wait->signal.get_object();
    auto *owner = Object::cast_to<Node>(ObjectDB::get_instance(runtime.owner));
    if (!owner || !source || !owner->is_inside_tree() || !runtime.has_live_owner()) return true;
    SceneTree *tree = owner->get_tree();
    const uint64_t token = ++runtime.next_wait;
    auto suspended = std::make_shared<SuspendedDispatch>(std::move(frame), std::move(execution), runtime.steps);
    runtime.suspended.emplace(token, suspended);
    Signal &signal = suspended->execution.wait->signal;
    suspended->handler = wait_emitter(runtime, token, suspended->state);
    Error connected = static_cast<Error>(signal.connect(suspended->handler, Object::CONNECT_ONE_SHOT));
    if (connected == OK) {
        const StringName name = signal.get_name();
        if (auto *source_node = Object::cast_to<Node>(source); source_node && source_node->is_inside_tree() &&
                name != StringName("tree_exiting") && name != StringName("tree_exited")) {
            suspended->source_exit = wait_callback(runtime, token, WaitAction::CANCEL);
            connected = source_node->connect("tree_exiting", suspended->source_exit, Object::CONNECT_ONE_SHOT);
        }
    }
    if (connected == OK && runtime.wait_monitor.is_null()) {
        runtime.wait_monitor = wait_callback(runtime, 0, WaitAction::MONITOR);
        runtime.wait_tree = ObjectID(tree->get_instance_id());
        connected = tree->connect("process_frame", runtime.wait_monitor);
    }
    if (connected == OK) return true;
    remove_wait(runtime, token);
    execution.error = node_error(node, "Godot could not connect this wait. Check the signal source.");
    return false;
}

/// Make the emitted payload available on the wait card before resuming its Then path.
String publish_wait_payload(SuspendedDispatch &suspended, Object *source, const Array &arguments) {
    Frame &frame = suspended.frame;
    const int64_t id = suspended.execution.wait->node;
    const Instruction &node = frame.program.nodes.at(id);
    if (node.op == Op::DELAY) return {};
    Dictionary payload;
    if (node.signal_sender) {
        if (arguments.size() != static_cast<int64_t>(node.signal_arguments.size())) {
            return "The signal's argument count changed. Refresh the wait from its typed target.";
        }
        for (int64_t index = 0; index < arguments.size(); ++index) {
            payload[node.signal_arguments[index].name] = arguments[index];
        }
        payload["sender"] = source;
    } else {
        payload["arguments"] = arguments;
    }
    const String failure = write_outputs(frame, node, payload);
    if (!failure.is_empty()) return failure;
#ifdef DEBUG_ENABLED
    if (frame.debug_recording) record_debug_outputs(frame, id, node, "Emitted by the awaited signal");
#endif
    refresh_signal_outputs(frame, id);
    return {};
}

/// Resume at the exact Then edge and visit any remaining Sequence branches in original order.
void resume_wait(Runtime &runtime, uint64_t token, const Array &arguments) {
    auto suspended = remove_wait(runtime, token);
    if (!suspended || !runtime.has_live_owner() || runtime.exiting_tree) return;
    auto *owner = Object::cast_to<Node>(ObjectDB::get_instance(runtime.owner));
    if (!owner || !owner->is_inside_tree() || owner->is_queued_for_deletion() || !begin_dispatch(runtime)) return;
    Object *source = suspended->execution.wait->signal.get_object();
    runtime.steps = std::max(runtime.steps, suspended->steps);
    Execution &execution = suspended->execution;
    Frame &frame = suspended->frame;
    Flow flow = Flow::ERROR;
    if (const String failure = publish_wait_payload(*suspended, source, arguments); !failure.is_empty()) {
        execution.error = node_error(execution.wait->node, failure);
    } else {
        const auto first = execution.wait->continuation;
        execution.wait.reset();
        auto following = std::move(execution.following);
        execution.following.clear();
        flow = execute_path(frame, first.edge, execution, first.depth);
        auto path = following.begin();
        while (flow == Flow::END && path != following.end()) {
            flow = execute_path(frame, path->edge, execution, path->depth);
            ++path;
        }
        if (flow == Flow::SUSPEND) {
            execution.following.insert(execution.following.end(), path, following.end());
            if (!store_wait(std::move(frame), std::move(execution))) flow = Flow::ERROR;
        }
    }
    end_dispatch(runtime, execution, flow, false);
}

/// Run one top-level invocation, sharing its budget with reentrant callbacks.
Variant dispatch(Runtime &runtime, const Program &program, int64_t entry, const Dictionary &values,
        const Array &return_pins) {
    if (entry == 0 || !begin_dispatch(runtime)) return {};
    Frame frame(runtime, program);
    Execution execution;
    Flow flow = execute_entry(frame, entry, values, "then", execution, 0);
    if (flow == Flow::SUSPEND && !store_wait(std::move(frame), std::move(execution))) flow = Flow::ERROR;
    if (!end_dispatch(runtime, execution, flow, !return_pins.is_empty())) return {};
    for (const Dictionary &pin : return_pins) {
        const Variant returned = frame.result ? named_value(*frame.result, name_field(pin, "id")) : Variant();
        if (returned.get_type() == Variant::OBJECT) {
            const ObjectID object_id = returned.operator ObjectID();
            execution.created.erase(std::remove(execution.created.begin(), execution.created.end(), object_id), execution.created.end());
        }
    }
    if (return_pins.size() == 1) {
        return frame.result ? named_value(*frame.result, name_field(return_pins[0], "id")) : Variant();
    }
    return return_pins.size() > 1 && frame.result ? Variant(*frame.result) : Variant();
}

/// Data owned by Godot's ScriptInstance, retained separately during execution.
struct VisualScriptInstance {
    /// A native callback may delete the owner and instance reentrantly.
    std::shared_ptr<Runtime> runtime;
};

/// Build a method list backed by instance-owned storage for Godot's Object reflection.
void build_method_list(Runtime &runtime) {
    for (const auto &[name, method] : runtime.methods) {
        if (name != StringName(signal_dispatch) && (!lifecycle(name) || method.entry != 0)) {
            runtime.method_storage.push_back(method_info(name, method));
        }
    }
    runtime.method_arguments.resize(runtime.method_storage.size());
    runtime.method_defaults.resize(runtime.method_storage.size());
    for (size_t index = 0; index < runtime.method_storage.size(); ++index) {
        MethodInfo &source = runtime.method_storage[index];
        auto &arguments = runtime.method_arguments[index];
        for (const PropertyInfo &pin : source.arguments) {
            arguments.push_back(pin._to_gdextension());
        }
        auto &defaults = runtime.method_defaults[index];
        for (Variant &value : source.default_arguments) {
            defaults.push_back(value._native_ptr());
        }
        GDExtensionMethodInfo method{};
        method.name = source.name._native_ptr();
        method.return_value = source.return_val._to_gdextension();
        method.flags = source.flags;
        method.argument_count = static_cast<uint32_t>(arguments.size());
        method.arguments = arguments.data();
        method.default_argument_count = static_cast<uint32_t>(defaults.size());
        method.default_arguments = defaults.data();
        runtime.method_list.push_back(method);
    }
}

/// Create and check one cached runtime when attaching a script to an owner.
std::shared_ptr<Runtime> make_runtime(const Ref<VisualScript> &script, Object *owner, CustomScriptCache &custom_scripts,
        const std::shared_ptr<const VisualScriptExports> &exports) {
    auto runtime = std::make_shared<Runtime>();
    runtime->self = runtime;
    runtime->owner = ObjectID(owner->get_instance_id());
    runtime->script = script;
    const Dictionary root = script->get_graph();
    const StringName base = script->get_graph_base();
    runtime->error = read_variables(root.get("variables", Variant()), runtime->variables);
    if (runtime->error.is_empty()) {
        runtime->exports = exports;
        runtime->property_list.reserve(exports->properties.size());
        for (const PropertyInfo &property : exports->properties) {
            runtime->property_list.push_back(property._to_gdextension());
        }
        runtime->error = compile_program(root, root, base, runtime->variables, runtime->events, custom_scripts);
    }
    if (runtime->error.is_empty()) {
        runtime->error = compile_definitions(root, "functions", base, runtime->variables, runtime->functions, custom_scripts);
    }
    if (runtime->error.is_empty()) {
        runtime->error = compile_definitions(root, "macros", base, runtime->variables, runtime->macros, custom_scripts);
    }
    if (!runtime->error.is_empty()) {
        UtilityFunctions::push_error("[Crimson] Could not prepare Visual Script '" + script->get_path() + "'. " + runtime->error);
    } else {
        link_function_calls(*runtime, runtime->events);
        for (auto &[id, program] : runtime->functions) link_function_calls(*runtime, program);
        for (auto &[id, program] : runtime->macros) link_function_calls(*runtime, program);
        index_signal_producers(*runtime);
    }
    auto register_method = [&](const StringName &name) {
        GraphMethod info;
        if (find_method(root, base, name, info)) {
            runtime->methods.emplace(name, info);
        }
    };
    for (const Dictionary &event : lifecycle_events()) {
        register_method(name_field(event, "callback"));
    }
    register_method(signal_dispatch);
    bool has_custom_helpers;
    {
        std::lock_guard lock(custom_scripts.mutex);
        has_custom_helpers = !custom_scripts.entries.empty();
    }
    for (const char *key : {"functions", "events"}) {
        const Variant records = root.get(key, Variant());
        if (records.get_type() == Variant::ARRAY) {
            for (const Variant &item : Array(records)) {
                if (item.get_type() == Variant::DICTIONARY) {
                    const Dictionary declaration = item;
                    register_method(name_field(declaration, "name"));
                    const int64_t id = read_id(declaration.get("id", Variant()));
                    if (id > 0 && has_custom_helpers) {
                        const bool function = String(key) == "functions";
                        runtime->custom_methods.try_emplace({function, id}, GraphMethod{
                            id, 0, function, data_pins(declaration.get("inputs", Array())),
                            function ? data_pins(declaration.get("outputs", Array())) : Array(),
                        });
                    }
                }
            }
        }
    }
    build_method_list(*runtime);
    return runtime;
}

/// Return cached public or lifecycle method metadata for a Godot callback.
bool runtime_method(Runtime &runtime, const StringName &method, GraphMethod &info) {
    if (const auto match = runtime.methods.find(method); match != runtime.methods.end()) {
        info = match->second;
        return true;
    }
    return false;
}

/// Handle a native signal callback if it still matches the entry's active emitter.
void dispatch_signal(Runtime &runtime, const GDExtensionConstVariantPtr *arguments, GDExtensionInt count,
        GDExtensionCallError &error) {
    if (count < 2) {
        error = {GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS, 0, 2};
        return;
    }
    const int64_t id = read_id(*static_cast<const Variant *>(arguments[count - 2]));
    const ObjectID source_id = static_cast<const Variant *>(arguments[count - 1])->operator ObjectID();
    const auto subscription = runtime.subscriptions.find(id);
    if (subscription == runtime.subscriptions.end() || subscription->second.source != source_id) {
        return;
    }
    const auto node = runtime.events.nodes.find(id);
    static const StringName native_signal_kind("native_signal");
    if (node == runtime.events.nodes.end() || node->second.kind != native_signal_kind) {
        return;
    }
    dispatch_signal_entry(runtime, id, node->second, source_id, arguments, count - 2, error);
}

void dispatch_signal_entry(Runtime &runtime, int64_t id, const Instruction &entry, ObjectID source_id,
        const GDExtensionConstVariantPtr *arguments, GDExtensionInt count, GDExtensionCallError &error) {
    Object *source = entry.signal_sender ? ObjectDB::get_instance(source_id) : nullptr;
    if (entry.signal_sender ? !source : !gdextension_interface::object_get_instance_from_id(source_id)) return;
    const size_t supplied = static_cast<size_t>(count);
    const auto &pins = entry.signal_arguments;
    if (supplied > pins.size()) {
        error = {GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS, 0, static_cast<int32_t>(pins.size())};
        return;
    }

    // Resolving a default resource can reenter Godot. Bind all defaults before publishing any output.
    const bool direct = supplied == pins.size() && entry.signal_slots_match
#ifdef DEBUG_ENABLED
            && !visual_debugger()
#endif
            ;
    std::optional<Dictionary> named;
    std::optional<Frame> frame;
    if (direct) frame.emplace(runtime, runtime.events);
    else named.emplace();

    String output_error;
    for (size_t index = 0; index < pins.size(); ++index) {
        const auto &pin = pins[index];
        std::optional<Variant> default_value;
        const Variant *bound;
        if (index < supplied) {
            bound = static_cast<const Variant *>(arguments[index]);
        } else {
            if (!pin.has_default) {
                error = {GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS, 0, static_cast<int32_t>(index + 1)};
                return;
            }
            default_value.emplace();
            if (!resolve_literal(pin.pin, pin.pin["default"], *default_value)) {
                error = {GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, static_cast<int32_t>(index),
                        static_cast<int32_t>(pin.type.type)};
                return;
            }
            bound = &*default_value;
        }
        if (!pin.type.accepts(*bound)) {
            error = {GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, static_cast<int32_t>(index),
                    static_cast<int32_t>(pin.type.type)};
            return;
        }
        if (direct) {
            if (pin.validates_output) {
                // Fully supplied signals run no default readers between validation and entry publication.
                frame->slots[pin.output->slot].emplace(*bound);
            } else if (output_error.is_empty()) {
                output_error = publish_output(*frame, entry, pin.name, *pin.output, *bound);
            }
        } else {
            (*named)[pin.name] = *bound;
        }
    }

    if (!direct) {
        if (entry.signal_sender) (*named)["sender"] = source;
        dispatch(runtime, runtime.events, id, *named, {});
        return;
    }
    if (!begin_dispatch(runtime)) return;
    if (entry.signal_sender && output_error.is_empty()) {
        static const StringName sender_pin("sender");
        output_error = publish_output(*frame, entry, sender_pin, *entry.signal_sender, source);
    }
    Execution execution;
    Flow flow = Flow::ERROR;
    if (output_error.is_empty()) {
        refresh_signal_outputs(*frame, id);
        flow = execute_path(*frame, entry.then_edge, execution, 0);
        if (flow == Flow::SUSPEND && !store_wait(std::move(*frame), std::move(execution))) flow = Flow::ERROR;
    } else {
        execution.error = node_error(id, output_error);
    }
    end_dispatch(runtime, execution, flow, false);
}

/// Find an instance property backed by a compiled graph variable.
const VisualScriptExports::Variable *find_export(const VisualScriptExports &exports, const StringName &name) {
    const auto index = exports.indices.find(name);
    return index == exports.indices.end() ? nullptr : &exports.variables[index->second];
}

/// Fetch a public variable from the current instance slots, resolving lazy resource defaults.
bool get_export_value(Runtime &runtime, const VisualScriptExports::Variable &property, Variant &value) {
    Variant &slot = runtime.variables.values[runtime.variables.indices.at(property.id)];
    if (asset_literal(property.pin, slot) || (resource_collection_pin(property.pin) && !pin_accepts_value(property.pin, slot))) {
        const Variant initial = slot;
        Variant loaded;
        if (!resolve_literal(property.pin, initial, loaded)) return false;
        if (slot == initial) slot = std::move(loaded);
    }
    value = is_expired(slot) ? Variant() : slot;
    return true;
}

/// Expose instance variables and methods without allowing callbacks to retain graph stack state.
const GDExtensionScriptInstanceInfo3 &instance_interface() {
    static const GDExtensionScriptInstanceInfo3 callbacks = [] {
        GDExtensionScriptInstanceInfo3 result{};
        result.set_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name,
                                  GDExtensionConstVariantPtr value) -> GDExtensionBool {
            auto &runtime = *static_cast<VisualScriptInstance *>(data)->runtime;
            const auto *property = runtime.exports ? find_export(*runtime.exports, *static_cast<const StringName *>(name)) : nullptr;
            if (!property) return false;
            Variant &slot = runtime.variables.values[runtime.variables.indices.at(property->id)];
            return set_export_value(property->pin, slot, *static_cast<const Variant *>(value));
        };
        result.get_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name,
                                  GDExtensionVariantPtr value) -> GDExtensionBool {
            const auto runtime = static_cast<VisualScriptInstance *>(data)->runtime;
            const auto *property = runtime->exports ? find_export(*runtime->exports, *static_cast<const StringName *>(name)) : nullptr;
            return property && get_export_value(*runtime, *property, *static_cast<Variant *>(value));
        };
        result.get_property_list_func = [](GDExtensionScriptInstanceDataPtr data, uint32_t *count) -> const GDExtensionPropertyInfo * {
            const auto &properties = static_cast<VisualScriptInstance *>(data)->runtime->property_list;
            *count = static_cast<uint32_t>(properties.size());
            return properties.data();
        };
        result.get_property_type_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name,
                                               GDExtensionBool *valid) -> GDExtensionVariantType {
            const auto &runtime = *static_cast<VisualScriptInstance *>(data)->runtime;
            const auto *property = runtime.exports ? find_export(*runtime.exports, *static_cast<const StringName *>(name)) : nullptr;
            *valid = property != nullptr;
            return static_cast<GDExtensionVariantType>(property ? property->property.type : Variant::NIL);
        };
        result.property_can_revert_func = [](GDExtensionScriptInstanceDataPtr data,
                                                   GDExtensionConstStringNamePtr name) -> GDExtensionBool {
            const auto &runtime = *static_cast<VisualScriptInstance *>(data)->runtime;
            return runtime.exports && find_export(*runtime.exports, *static_cast<const StringName *>(name));
        };
        result.property_get_revert_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name,
                                                   GDExtensionVariantPtr value) -> GDExtensionBool {
            const auto exports = static_cast<VisualScriptInstance *>(data)->runtime->exports;
            const auto *property = exports ? find_export(*exports, *static_cast<const StringName *>(name)) : nullptr;
            if (!property) return false;
            *static_cast<Variant *>(value) = export_default(*property);
            return true;
        };
        result.get_owner_func = [](GDExtensionScriptInstanceDataPtr data) -> GDExtensionObjectPtr {
            const auto &runtime = *static_cast<VisualScriptInstance *>(data)->runtime;
            const Object *owner = ObjectDB::get_instance(runtime.owner);
            return owner ? owner->_owner : nullptr;
        };
        result.get_script_func = [](GDExtensionScriptInstanceDataPtr data) -> GDExtensionObjectPtr {
            return static_cast<VisualScriptInstance *>(data)->runtime->script->_owner;
        };
        result.get_language_func = [](GDExtensionScriptInstanceDataPtr) -> GDExtensionScriptLanguagePtr {
            const auto *language = visual_script_language();
            return language ? language->_owner : nullptr;
        };
        result.has_method_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name) -> GDExtensionBool {
            auto &runtime = *static_cast<VisualScriptInstance *>(data)->runtime;
            return runtime.methods.count(*static_cast<const StringName *>(name)) != 0;
        };
        result.get_method_list_func = [](GDExtensionScriptInstanceDataPtr data, uint32_t *count) -> const GDExtensionMethodInfo * {
            const auto &methods = static_cast<VisualScriptInstance *>(data)->runtime->method_list;
            *count = static_cast<uint32_t>(methods.size());
            return methods.data();
        };
        result.get_method_argument_count_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name,
                                                    GDExtensionBool *valid) -> GDExtensionInt {
            auto &runtime = *static_cast<VisualScriptInstance *>(data)->runtime;
            GraphMethod method;
            *valid = runtime_method(runtime, *static_cast<const StringName *>(name), method);
            return method.inputs.size();
        };
        result.call_func = [](GDExtensionScriptInstanceDataPtr data, GDExtensionConstStringNamePtr name,
                              const GDExtensionConstVariantPtr *arguments, GDExtensionInt count, GDExtensionVariantPtr output,
                              GDExtensionCallError *error) {
            const std::shared_ptr<Runtime> runtime = static_cast<VisualScriptInstance *>(data)->runtime;
            auto &value = *static_cast<Variant *>(output);
            value = Variant();
            const auto &method_name = *static_cast<const StringName *>(name);
            *error = {GDEXTENSION_CALL_OK, 0, 0};
            if (!on_main_thread()) {
                *error = {GDEXTENSION_CALL_ERROR_INVALID_METHOD, 0, 0};
                return;
            }
            static const StringName signal_method(signal_dispatch);
            if (method_name == signal_method) {
                dispatch_signal(*runtime, arguments, count, *error);
                return;
            }
            GraphMethod method;
            if (!runtime_method(*runtime, method_name, method)) {
                *error = {GDEXTENSION_CALL_ERROR_INVALID_METHOD, 0, 0};
                return;
            }
            Dictionary inputs;
            if (!bind_arguments(method.inputs, arguments, count, inputs, *error)) {
                return;
            }
            if (method_name == StringName("_ready")) {
                runtime->reached_ready = true;
                bind_signals(*runtime, true);
            } else if (method_name == StringName("_enter_tree")) {
                runtime->exiting_tree = false;
                bind_signals(*runtime, runtime->reached_ready);
            } else if (method_name == StringName("_exit_tree")) {
                runtime->exiting_tree = true;
                cancel_waits(*runtime);
            }
            if (method.function) {
                const auto program = runtime->functions.find(method.definition);
                if (program != runtime->functions.end()) {
                    value = dispatch(*runtime, program->second, program->second.entry, inputs, method.outputs);
                }
            } else {
                const int64_t entry = method.definition ? custom_event_entry(runtime->events, method.definition) : method.entry;
                value = dispatch(*runtime, runtime->events, entry, inputs, {});
            }
            if (method_name == StringName("_exit_tree")) {
                unbind_signals(*runtime);
            }
        };
        result.free_func = [](GDExtensionScriptInstanceDataPtr data) {
            auto *instance = static_cast<VisualScriptInstance *>(data);
            instance->runtime->detached = true;
            unbind_signals(*instance->runtime);
            cancel_waits(*instance->runtime);
            memdelete(instance);
        };
        return result;
    }();
    return callbacks;
}


} // namespace

bool can_export_variable(const StringName &base, const StringName &name, const Dictionary &pin) {
    return is_node_class(base) && String(name).is_valid_identifier() && exportable_type(pin) &&
            native_property_names(base).count(name) == 0;
}

bool has_numeric_inspector_options(const Dictionary &pin) {
    const int64_t type = pin.get("type", Variant::NIL);
    return type == Variant::FLOAT || (type == Variant::INT && !pin.has("enum_type"));
}

bool is_valid_inspector_range(const Dictionary &pin, const Dictionary &range) {
    if (!has_numeric_inspector_options(pin) || range.get("slider", true).get_type() != Variant::BOOL) return false;
    const bool integer = static_cast<int64_t>(pin.get("type", Variant::NIL)) == Variant::INT;
    for (const char *field : {"min", "max", "step"}) {
        const Variant value = range.get(field, Variant());
        if (value.get_type() == Variant::INT) continue;
        if (value.get_type() != Variant::FLOAT) return false;
        const double number = value;
        if (!std::isfinite(number)) return false;
        if (integer && (std::trunc(number) != number || number < -0x1p63 || number >= 0x1p63)) return false;
    }
    if (integer) {
        return static_cast<int64_t>(range["min"]) <= static_cast<int64_t>(range["max"]) &&
                static_cast<int64_t>(range["step"]) > 0;
    }
    const double minimum = range["min"];
    const double maximum = range["max"];
    return minimum <= maximum && std::isfinite(maximum - minimum) && static_cast<double>(range["step"]) > 0;
}

bool is_valid_inspector_suffix(const String &suffix) {
    return !suffix.contains(",") && !suffix.contains("\n") && !suffix.contains("\r");
}

bool is_valid_inspector_group(const String &group) {
    return !group.contains("/") && !group.contains("\n") && !group.contains("\r");
}

void VisualScript::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_graph_base"), &VisualScript::get_graph_base);
    ClassDB::bind_method(D_METHOD("set_graph_base", "base"), &VisualScript::set_graph_base);
    ClassDB::bind_method(D_METHOD("get_graph"), &VisualScript::get_graph);
    ClassDB::bind_method(D_METHOD("set_graph", "graph"), &VisualScript::set_graph);
}

VisualScript::VisualScript() : custom_scripts(std::make_shared<CustomScriptCache>()), exports(std::make_shared<VisualScriptExports>()) {
    graph["version"] = graph_version;
    graph["nodes"] = Array();
    graph["connections"] = Array();
}

void VisualScript::set_graph_base(const StringName &base) {
    if (graph_base != base) {
        graph_base = base;
        custom_scripts = std::make_shared<CustomScriptCache>();
        refresh_exports();
        emit_changed();
    }
}

StringName VisualScript::get_graph_base() const {
    return graph_base;
}

void VisualScript::set_graph(const Dictionary &value) {
    graph = value.duplicate(true);
    custom_scripts = std::make_shared<CustomScriptCache>();
    source_valid = true;
    refresh_exports();
    emit_changed();
}

Dictionary VisualScript::get_graph() const {
    return graph.duplicate(true);
}

bool VisualScript::can_export_for_owners(const StringName &name, const Dictionary &pin) const {
    if (!can_export_variable(graph_base, name, pin)) return false;
    for (const StringName &type : exports->owner_types) {
        if (type != graph_base && native_property_names(type).count(name) != 0) return false;
    }
    return true;
}

bool VisualScript::_editor_can_reload_from_file() { return false; }
bool VisualScript::_can_instantiate() const { return _is_valid() && !Engine::get_singleton()->is_editor_hint(); }
Ref<Script> VisualScript::_get_base_script() const { return {}; }
StringName VisualScript::_get_global_name() const { return {}; }
bool VisualScript::_inherits_script(const Ref<Script> &script) const { return script.ptr() == this; }
StringName VisualScript::_get_instance_base_type() const { return graph_base; }

void *VisualScript::_instance_create(Object *owner) const {
    if (!owner || !owner->is_class(graph_base)) {
        UtilityFunctions::push_error("[Crimson] Could not attach Visual Script '" + get_path() +
                                     "'. Choose a node that inherits " + String(graph_base) + ".");
        return nullptr;
    }
    const auto cache = custom_scripts;
    const auto published = exports;
    auto layout = published;
    const StringName type = owner->get_class();
    if (!published->variables.empty() && type != published->base) {
        const auto names = native_property_names(type);
        const auto collision = std::find_if(published->variables.begin(), published->variables.end(),
                [&](const auto &variable) { return names.count(variable.property.name) != 0; });
        if (collision != published->variables.end()) {
            auto filtered = std::make_shared<VisualScriptExports>();
            filtered->base = published->base;
            filtered->declarations = published->declarations;
            filtered->enums = published->enums;
            filtered->owner_types.insert(type);
            build_exports(*filtered);
            layout = filtered;
        }
    }
    auto *instance = memnew(VisualScriptInstance);
    instance->runtime = make_runtime(Ref<VisualScript>(const_cast<VisualScript *>(this)), owner, *cache, layout);
    return gdextension_interface::script_instance_create3(&instance_interface(), instance);
}

void *VisualScript::_placeholder_instance_create(Object *owner) const {
    const ScriptLanguage *language = _get_language();
    if (!owner || !language) {
        return nullptr;
    }
    if (!owner->is_class(graph_base)) {
        const Node *node = Object::cast_to<Node>(owner);
        const String owner_type = owner->get_class();
        const String target = node ? String("'") + node->get_name() + "'" : owner_type;
        WARN_PRINT_ED("[Crimson] Cannot attach: " + String(graph_base) + " required, got " + owner_type +
                ".\nVisual Script '" + get_path() + "' cannot attach to " + target + ". Drop it on a compatible node. "
                "Use Undo if the drop removed a previous script.");
        return nullptr;
    }
    void *placeholder = gdextension_interface::placeholder_script_instance_create(language->_owner, _owner, owner->_owner);
    if (placeholder) {
        const auto previous = exports;
        placeholders.emplace(placeholder, owner->get_class());
        const_cast<VisualScript *>(this)->refresh_exports();
        if (exports == previous) {
            const Array properties = export_properties(*previous);
            const Dictionary defaults = placeholder_defaults(*previous);
            if (exports == previous && placeholders.count(placeholder) != 0) {
                gdextension_interface::placeholder_script_instance_update(placeholder, properties._native_ptr(), defaults._native_ptr());
            }
        }
    }
    return placeholder;
}

void VisualScript::_placeholder_erased(void *placeholder) {
    if (placeholders.erase(placeholder) != 0) refresh_exports();
}

bool VisualScript::_has_source_code() const { return true; }

String VisualScript::_get_source_code() const {
    Dictionary source;
    source["base"] = graph_base;
    source["graph"] = graph;
    return UtilityFunctions::var_to_str(source);
}

void VisualScript::_set_source_code(const String &code) {
    source_valid = false;
    const Variant decoded = UtilityFunctions::str_to_var(code);
    if (decoded.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary source = decoded;
    const Variant base = source.get("base", Variant());
    const Variant content = source.get("graph", Variant());
    if (!is_text(base) || content.get_type() != Variant::DICTIONARY) {
        return;
    }
    graph_base = base;
    graph = content;
    custom_scripts = std::make_shared<CustomScriptCache>();
    source_valid = true;
    refresh_exports();
    emit_changed();
}

Error VisualScript::_reload(bool) { return _is_valid() ? OK : ERR_PARSE_ERROR; }
StringName VisualScript::_get_doc_class_name() const { return {}; }
TypedArray<Dictionary> VisualScript::_get_documentation() const { return {}; }

bool VisualScript::_has_method(const StringName &method) const {
    GraphMethod info;
    return has_graph_header(graph) && find_method(graph, graph_base, method, info);
}

bool VisualScript::_has_static_method(const StringName &) const { return false; }

Dictionary VisualScript::_get_method_info(const StringName &method) const {
    GraphMethod info;
    return has_graph_header(graph) && find_method(graph, graph_base, method, info) ? Dictionary(method_info(method, info)) : Dictionary();
}
bool VisualScript::_is_tool() const { return false; }
bool VisualScript::_is_valid() const { return source_valid && is_node_class(graph_base) && has_graph_header(graph); }
ScriptLanguage *VisualScript::_get_language() const { return visual_script_language(); }
bool VisualScript::_has_script_signal(const StringName &) const { return false; }
TypedArray<Dictionary> VisualScript::_get_script_signal_list() const { return {}; }
bool VisualScript::_has_property_default_value(const StringName &property) const {
    return find_export(*exports, property) != nullptr;
}

Variant VisualScript::_get_property_default_value(const StringName &property) const {
    const auto published = exports;
    const auto *variable = find_export(*published, property);
    return variable ? export_default(*variable) : Variant();
}

void VisualScript::_update_exports() { refresh_exports(); }

void VisualScript::refresh_exports() {
    const Array declarations = export_declarations(graph);
    const Variant raw_enums = graph.get("enums", Variant());
    const Array enums = declarations.is_empty() || raw_enums.get_type() != Variant::ARRAY ? Array() : Array(raw_enums);
    std::set<StringName> owner_types;
    for (const auto &[placeholder, type] : placeholders) owner_types.insert(type);
    if (exports->base == graph_base && exports->declarations == declarations && exports->enums == enums &&
            exports->owner_types == owner_types) return;

    auto next = std::make_shared<VisualScriptExports>();
    next->base = graph_base;
    next->declarations = declarations;
    next->enums = enums;
    next->owner_types = std::move(owner_types);
    build_exports(*next);
    exports = next;
    if (placeholders.empty()) return;
    const auto instances = placeholders;
    const Array properties = export_properties(*next);
    const Dictionary defaults = placeholder_defaults(*next);
    for (const auto &[placeholder, type] : instances) {
        if (exports != next) return;
        if (placeholders.count(placeholder) != 0) {
            gdextension_interface::placeholder_script_instance_update(placeholder, properties._native_ptr(), defaults._native_ptr());
        }
    }
}

TypedArray<Dictionary> VisualScript::_get_script_method_list() const {
    TypedArray<Dictionary> methods;
    for (const Dictionary &event : lifecycle_events()) {
        GraphMethod info;
        const StringName name = name_field(event, "callback");
        if (find_method(graph, graph_base, name, info) && info.entry != 0) {
            methods.append(Dictionary(method_info(name, info)));
        }
    }
    std::set<StringName> names;
    for (const char *key : {"functions", "events"}) {
        const Variant records = graph.get(key, Variant());
        if (records.get_type() != Variant::ARRAY) {
            continue;
        }
        for (const Variant &item : Array(records)) {
            if (item.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const StringName name = name_field(item, "name");
            GraphMethod info;
            if (names.insert(name).second && find_method(graph, graph_base, name, info)) {
                methods.append(Dictionary(method_info(name, info)));
            }
        }
    }
    return methods;
}
TypedArray<Dictionary> VisualScript::_get_script_property_list() const { return export_properties(*exports); }
int32_t VisualScript::_get_member_line(const StringName &) const { return -1; }
Dictionary VisualScript::_get_constants() const { return {}; }
TypedArray<StringName> VisualScript::_get_members() const { return {}; }
bool VisualScript::_is_placeholder_fallback_enabled() const { return false; }
Variant VisualScript::_get_rpc_config() const { return {}; }

bool VisualScript::_instance_has(Object *owner) const {
    return owner && static_cast<Object *>(owner->get_script()) == this;
}

} // namespace crimson::visual_scripting
