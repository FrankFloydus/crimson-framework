// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "match_attributes.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/game/game.hpp"
#include "integration/game/game_services.hpp"
#include "modules/attributes/ability_system_component.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <cmath>
#include <locale>
#include <optional>
#include <set>
#include <sstream>

namespace crimson::integration {

using namespace godot;
using attributes::AbilitySystemComponent;
using attributes::AttributeDefinition;
using attributes::AttributeRegistry;
using attributes::AttributeVisibility;
using attributes::GameplayAttributes;
using attributes::GameplayAttributeValue;
using match_runtime::GameTransaction;
using match_runtime::TransactionCoordinator;

namespace {

// Reserved store and recipient schema record. Rows use the same prefix followed by '/'.
constexpr const char *store_id = "crimson.attributes";
// Persistent store metadata distinct from every accepted character ID.
constexpr const char *store_schema_key = "$schema";
// Recipient record prefix followed by one character ID.
constexpr const char *row_prefix = "crimson.attributes/";
// Existing StateReplication record identity limit, including its namespace.
constexpr int64_t max_record_length = 128;

// Restrict only the characters that can be represented by the existing record format.
bool valid_character_id(const String &id) {
    return id != String(store_schema_key) && !id.strip_edges().is_empty() &&
            id.length() <= max_record_length - String(row_prefix).length();
}

// Build an operation envelope using the already-validated coordinator DTO types.
Dictionary operation(const char *kind, const String &character_id) {
    Dictionary change;
    change["kind"] = String(kind);
    change["character"] = character_id;
    return change;
}

// Copy typed Godot strings into the plain arrays supported by match stores.
Array dto_list(const PackedStringArray &values) {
    Array result;
    for (int64_t index = 0; index < values.size(); ++index) {
        result.append(values[index]);
    }
    return result;
}

// Decode unique, nonblank policy or set IDs without accepting objects or coerced values.
bool decode_list(const Variant &raw, PackedStringArray &output) {
    if (raw.get_type() != Variant::ARRAY) {
        return false;
    }
    std::set<String> seen;
    const Array values = raw;
    for (int64_t index = 0; index < values.size(); ++index) {
        const Variant &entry = values[index];
        if (entry.get_type() != Variant::STRING) {
            return false;
        }
        const String id = entry;
        if (id.strip_edges().is_empty() || !seen.insert(id).second) {
            return false;
        }
        output.append(id);
    }
    return true;
}

// Read a finite attribute number, accepting an exact integer DTO as well.
bool decode_number(const Variant &raw, double &number) {
    if (raw.get_type() != Variant::FLOAT && raw.get_type() != Variant::INT) {
        return false;
    }
    number = static_cast<double>(raw);
    return std::isfinite(number);
}

// Resolve the frozen native definition using the exact UTF-8 identifier.
const AttributeDefinition *lookup(const AttributeRegistry &registry, const String &id) {
    const CharString encoded = id.utf8();
    return registry.find(std::string_view(encoded.get_data(), static_cast<std::size_t>(encoded.length())));
}

// Hash canonical gameplay data while excluding labels and all authoring order.
String schema_digest(const AttributeRegistry &registry) {
    std::map<std::string, const attributes::AttributeSetDefinition *> sets;
    for (const auto &set : registry.sets) {
        sets.emplace(set.id, &set);
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "crimson-attributes-2\n";
    for (const auto &[id, set] : sets) {
        stream << "set\n" << id << '\n';
        std::map<std::string, const AttributeDefinition *> definitions;
        for (const auto &definition : set->attributes) {
            definitions.emplace(definition.id, &definition);
        }
        for (const auto &[name, definition] : definitions) {
            stream << "attribute\n" << name << '\n' << static_cast<int>(definition->visibility) << '\n';
        }
    }
    return String(stream.str().c_str()).sha256_text();
}

// Return the transaction coordinator's normal preparation result.
Dictionary prepared(const Dictionary &candidate) {
    Dictionary result;
    result["ok"] = true;
    result["state"] = candidate;
    return result;
}

// Report a local schema or mutation problem through the owning transaction.
Dictionary invalid(const String &message) {
    Dictionary result;
    result["ok"] = false;
    result["message"] = message;
    return result;
}

// Read a detached character row or an empty row when unavailable.
Dictionary row_at(const Dictionary &state, const String &character_id) {
    const Variant raw = state.get(character_id, Variant());
    return raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
}

// Select only the values from one already-validated character row.
Dictionary row_values(const Dictionary &row) {
    const Variant raw = row.get("values", Variant());
    return raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
}

// Convert an authority store into the values used for typed notifications.
Dictionary authority_snapshot(const Dictionary &store) {
    Dictionary result;
    const Array keys = store.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const String id = keys[index];
        if (id != String(store_schema_key)) {
            result[id] = row_values(row_at(store, id));
        }
    }
    return result;
}

// Decode the authority's explicit per-character visibility data.
bool decode_visibility(const Dictionary &row, String &owner, PackedStringArray &observers) {
    const Variant raw_owner = row.get("owner", Variant());
    if (raw_owner.get_type() != Variant::STRING) {
        return false;
    }
    owner = raw_owner;
    return (owner.is_empty() || !owner.strip_edges().is_empty()) &&
            decode_list(row.get("observers", Variant()), observers);
}

// Return a new detached reading, leaving hidden or unknown values unavailable.
Ref<GameplayAttributeValue> make_reading(const Variant &raw, bool authority) {
    double number = 0.0;
    return decode_number(raw, number) ?
            GameplayAttributeValue::make(number, authority ? std::optional<double>(number) : std::nullopt) :
            Ref<GameplayAttributeValue>();
}

} // namespace

void MatchAttributes::_bind_methods() {
    ClassDB::bind_method(D_METHOD("for_character", "character_id"), &MatchAttributes::for_character);
    ClassDB::bind_method(D_METHOD("bind_component", "component", "character_id"), &MatchAttributes::bind_component);
    ClassDB::bind_method(D_METHOD("add_character", "character_id", "sets", "owner", "observers"),
            &MatchAttributes::add_character, DEFVAL(String()), DEFVAL(PackedStringArray()));
    ClassDB::bind_method(D_METHOD("remove_character", "character_id"), &MatchAttributes::remove_character);
    ClassDB::bind_method(D_METHOD("set_visibility", "character_id", "owner", "observers"), &MatchAttributes::set_visibility);
    ClassDB::bind_method(D_METHOD("queue_add_character", "transaction", "character_id", "sets", "owner", "observers"),
            &MatchAttributes::queue_add_character, DEFVAL(String()), DEFVAL(PackedStringArray()));
    ClassDB::bind_method(D_METHOD("queue_add_component", "transaction", "component", "character_id", "owner", "observers"),
            &MatchAttributes::queue_add_component, DEFVAL(String()), DEFVAL(PackedStringArray()));
    ClassDB::bind_method(D_METHOD("queue_remove_character", "transaction", "character_id"), &MatchAttributes::queue_remove_character);
    ClassDB::bind_method(D_METHOD("queue_visibility", "transaction", "character_id", "owner", "observers"),
            &MatchAttributes::queue_visibility);
    ClassDB::bind_method(D_METHOD("queue_base", "transaction", "character_id", "attribute_id", "operand", "add"),
            &MatchAttributes::queue_base, DEFVAL(false));
}

Ref<MatchAttributes> MatchAttributes::attach(GameRuntime *runtime) {
    if (!runtime || !runtime->replication_node) {
        return {};
    }
    const Ref<SceneService> attachment = GameServiceContainer::scenes_for(runtime);
    if (attachment.is_null()) {
        return {};
    }
    Ref<MatchAttributes> service;
    service.instantiate();
    String error;
    if (!attributes::load_project_attribute_registry(service->registry, error)) {
        return {};
    }
    service->runtime_id = ObjectID(runtime->get_instance_id());
    service->scope = attachment;
    service->schema = schema_digest(service->registry);
    if (runtime->connect("phase_changed", callable_mp(service.ptr(), &MatchAttributes::on_phase_changed)) != OK ||
            runtime->connect("projection_updated", callable_mp(service.ptr(), &MatchAttributes::on_projection_updated)) != OK ||
            runtime->replication_node->connect("projection_cleared", callable_mp(service.ptr(), &MatchAttributes::on_projection_cleared)) != OK) {
        return {};
    }
    return service;
}

GameRuntime *MatchAttributes::resolve_game() const {
    if (std::this_thread::get_id() != owner_thread) {
        return nullptr;
    }
    auto *runtime = Object::cast_to<GameRuntime>(ObjectDB::get_instance(runtime_id));
    return runtime && GameServiceContainer::scenes_for(runtime) == scope ? runtime : nullptr;
}

bool MatchAttributes::owns_component(AbilitySystemComponent *component) const {
    if (!component || !component->is_inside_tree() || component->is_queued_for_deletion()) {
        return false;
    }
    const Ref<Game> game = Game::for_node(component);
    return game.is_valid() && game->attributes().ptr() == this;
}

Error MatchAttributes::prepare() {
    const GameRuntime *runtime = resolve_game();
    if (!runtime) {
        return ERR_UNAVAILABLE;
    }
    const Ref<TransactionCoordinator> stores = runtime->get_transactions();
    if (stores.is_null()) {
        return ERR_UNAVAILABLE;
    }
    const ObjectID coordinator_id(stores->get_instance_id());
    if (coordinator_id == prepared_id) {
        return OK;
    }
    if (runtime->get_phase() != GameRuntime::IDLE) {
        return ERR_UNAVAILABLE;
    }
    Dictionary initial;
    initial[store_schema_key] = schema;
    const int64_t service_id = get_instance_id();
    const int64_t store_owner_id = stores->get_instance_id();
    const Error status = stores->register_store(store_id, initial,
            callable_mp_static(&MatchAttributes::prepare_store).bind(service_id, store_owner_id),
            callable_mp_static(&MatchAttributes::restore_store).bind(service_id, store_owner_id));
    if (status != OK) {
        return status;
    }
    const Error connected = stores->connect("committed",
            callable_mp_static(&MatchAttributes::on_committed).bind(service_id, store_owner_id));
    if (connected != OK) {
        return connected;
    }
    prepared_id = coordinator_id;
    return OK;
}

Ref<TransactionCoordinator> MatchAttributes::authority_store(bool write) const {
    const GameRuntime *runtime = resolve_game();
    if (!runtime || !runtime->is_authority() || runtime->cleaning) {
        return {};
    }
    const auto phase = runtime->get_phase();
    if (phase == GameRuntime::IDLE || phase == GameRuntime::RECOVERING ||
            (write && phase != GameRuntime::LOBBY && phase != GameRuntime::RUNNING)) {
        return {};
    }
    const Ref<TransactionCoordinator> stores = runtime->get_transactions();
    return stores.is_valid() && ObjectID(stores->get_instance_id()) == prepared_id ? stores : Ref<TransactionCoordinator>();
}

Ref<MatchAttributes> MatchAttributes::resolve_store_owner(int64_t service_id, int64_t coordinator_id, bool write) {
    auto *service = Object::cast_to<MatchAttributes>(ObjectDB::get_instance(ObjectID(service_id)));
    const Ref<TransactionCoordinator> stores = service ? service->authority_store(write) : Ref<TransactionCoordinator>();
    return stores.is_valid() && int64_t(stores->get_instance_id()) == coordinator_id ?
            Ref<MatchAttributes>(service) : Ref<MatchAttributes>();
}

Error MatchAttributes::queue_change(const Ref<GameTransaction> &transaction, const Dictionary &change) {
    const Ref<TransactionCoordinator> stores = authority_store(true);
    if (stores.is_null()) {
        return ERR_UNAUTHORIZED;
    }
    if (!stores->owns_transaction(transaction) || !valid_character_id(change.get("character", String()))) {
        return ERR_INVALID_PARAMETER;
    }
    return transaction->add_change(store_id, change, stores->get_store_revision(store_id));
}

Error MatchAttributes::submit_change(const Dictionary &change) {
    const Ref<TransactionCoordinator> stores = authority_store(true);
    if (stores.is_null()) {
        return ERR_UNAUTHORIZED;
    }
    const Ref<GameTransaction> transaction = stores->begin();
    const Error queued = queue_change(transaction, change);
    if (queued != OK) {
        return queued;
    }
    const Dictionary result = stores->commit(transaction);
    return result.get("ok", false) ? OK : ERR_INVALID_DATA;
}

Error MatchAttributes::add_character(const String &character_id, const PackedStringArray &sets,
        const String &owner, const PackedStringArray &observers) {
    Dictionary change = operation("add", character_id);
    change["sets"] = dto_list(sets);
    change["owner"] = owner;
    change["observers"] = dto_list(observers);
    return submit_change(change);
}

Error MatchAttributes::remove_character(const String &character_id) {
    return submit_change(operation("remove", character_id));
}

Error MatchAttributes::set_visibility(const String &character_id, const String &owner, const PackedStringArray &observers) {
    Dictionary change = operation("visibility", character_id);
    change["owner"] = owner;
    change["observers"] = dto_list(observers);
    return submit_change(change);
}

Error MatchAttributes::queue_add_character(const Ref<GameTransaction> &transaction, const String &character_id,
        const PackedStringArray &sets, const String &owner, const PackedStringArray &observers) {
    Dictionary change = operation("add", character_id);
    change["sets"] = dto_list(sets);
    change["owner"] = owner;
    change["observers"] = dto_list(observers);
    return queue_change(transaction, change);
}

Error MatchAttributes::queue_add_component(const Ref<GameTransaction> &transaction, AbilitySystemComponent *component,
        const String &character_id, const String &owner, const PackedStringArray &observers) {
    if (authority_store(true).is_null()) {
        return ERR_UNAUTHORIZED;
    }
    if (!component || component->is_queued_for_deletion() || component->get_attribute_source() != AbilitySystemComponent::MATCH ||
            (component->is_inside_tree() && !owns_component(component))) {
        return ERR_INVALID_PARAMETER;
    }
    return queue_add_character(transaction, character_id, component->get_attribute_sets(), owner, observers);
}

Error MatchAttributes::queue_remove_character(const Ref<GameTransaction> &transaction, const String &character_id) {
    return queue_change(transaction, operation("remove", character_id));
}

Error MatchAttributes::queue_visibility(const Ref<GameTransaction> &transaction, const String &character_id,
        const String &owner, const PackedStringArray &observers) {
    Dictionary change = operation("visibility", character_id);
    change["owner"] = owner;
    change["observers"] = dto_list(observers);
    return queue_change(transaction, change);
}

Error MatchAttributes::queue_base(const Ref<GameTransaction> &transaction, const String &character_id,
        const StringName &attribute_id, double operand, bool add) {
    if (!std::isfinite(operand) || !lookup(registry, String(attribute_id))) {
        return ERR_INVALID_PARAMETER;
    }
    Dictionary change = operation(add ? "add_base" : "set_base", character_id);
    change["attribute"] = String(attribute_id);
    change["operand"] = operand;
    return queue_change(transaction, change);
}

Dictionary MatchAttributes::prepare_store(const Dictionary &current, const Array &operations, const Dictionary &context,
        int64_t service_id, int64_t coordinator_id) {
    const Ref<MatchAttributes> service = resolve_store_owner(service_id, coordinator_id, true);
    return service.is_valid() ? service->prepare_changes(current, operations, context) :
            invalid("The Attributes store belongs to a retired authority or is closed for changes.");
}

Dictionary MatchAttributes::prepare_changes(const Dictionary &current, const Array &operations, const Dictionary &) const {
    Dictionary candidate = current;
    for (int64_t index = 0; index < operations.size(); ++index) {
        const Variant &raw = operations[index];
        if (raw.get_type() != Variant::DICTIONARY) {
            return invalid("Attribute edits must be dictionaries.");
        }
        const Dictionary change = raw;
        const Variant raw_character = change.get("character", Variant());
        const Variant raw_kind = change.get("kind", Variant());
        if (raw_character.get_type() != Variant::STRING || raw_kind.get_type() != Variant::STRING ||
                !valid_character_id(raw_character)) {
            return invalid("The attribute edit needs a valid character ID and operation.");
        }
        const String character = raw_character;
        const String kind = raw_kind;
        Dictionary row = row_at(candidate, character);
        if (kind == "add") {
            PackedStringArray sets;
            PackedStringArray observers;
            String owner;
            String error;
            Dictionary values;
            if (!row.is_empty() || change.size() != 5 || !decode_list(change.get("sets", Variant()), sets) ||
                    !decode_visibility(change, owner, observers) ||
                    !attributes::make_attribute_values(registry, sets, values, error)) {
                return invalid(error.is_empty() ? String("The character or its attribute set and visibility are invalid.") : error);
            }
            row["sets"] = dto_list(sets);
            row["values"] = values;
            row["owner"] = owner;
            row["observers"] = dto_list(observers);
        } else if (row.is_empty()) {
            return invalid("The character has no registered attributes.");
        } else if (kind == "remove" && change.size() == 2) {
            candidate.erase(character);
            continue;
        } else if (kind == "visibility" && change.size() == 4) {
            String owner;
            PackedStringArray observers;
            if (!decode_visibility(change, owner, observers)) {
                return invalid("The character's attribute visibility is invalid.");
            }
            row["owner"] = owner;
            row["observers"] = dto_list(observers);
        } else if ((kind == "set_base" || kind == "add_base") && change.size() == 4) {
            const Variant raw_id = change.get("attribute", Variant());
            if (raw_id.get_type() != Variant::STRING) {
                return invalid("An attribute edit needs a registered attribute ID.");
            }
            const String id = raw_id;
            Dictionary values = row_values(row);
            double previous = 0.0;
            double operand = 0.0;
            double result = 0.0;
            if (!lookup(registry, id) || !decode_number(values.get(id, Variant()), previous) ||
                    !decode_number(change.get("operand", Variant()), operand) ||
                    attributes::evaluate_attribute_base(previous, operand, kind == "add_base", result) != OK) {
                return invalid("The attribute is absent or its numeric edit is invalid.");
            }
            values[id] = result;
            row["values"] = values;
        } else {
            return invalid("Unknown attribute edit operation.");
        }
        candidate[character] = row;
    }
    return prepared(candidate);
}

Dictionary MatchAttributes::restore_store(const Dictionary &candidate, const Dictionary &context,
        int64_t service_id, int64_t coordinator_id) {
    const Ref<MatchAttributes> service = resolve_store_owner(service_id, coordinator_id, false);
    return service.is_valid() ? service->validate_restore(candidate, context) :
            invalid("The Attributes store belongs to a retired authority or is closed for restore.");
}

Dictionary MatchAttributes::validate_restore(const Dictionary &candidate, const Dictionary &) const {
    if (candidate.get(store_schema_key, Variant()) != Variant(schema)) {
        return invalid("The checkpoint attribute schema does not match this project registry.");
    }
    const Array keys = candidate.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const String id = keys[index];
        if (id == String(store_schema_key)) {
            continue;
        }
        const Dictionary row = row_at(candidate, id);
        const Dictionary values = row_values(row);
        String owner;
        String error;
        PackedStringArray observers;
        PackedStringArray sets;
        Dictionary expected;
        if (!valid_character_id(id) || row.size() != 4 || !decode_visibility(row, owner, observers) ||
                !decode_list(row.get("sets", Variant()), sets) ||
                !attributes::make_attribute_values(registry, sets, expected, error) || expected.size() != values.size() ||
                row.get("values", Variant()).get_type() != Variant::DICTIONARY) {
            return invalid("The checkpoint contains an invalid character attribute row.");
        }
        const Array ids = expected.keys();
        for (int64_t value = 0; value < ids.size(); ++value) {
            double number = 0.0;
            if (!decode_number(values.get(ids[value], Variant()), number)) {
                return invalid("The checkpoint is missing a known attribute or contains a nonfinite value.");
            }
        }
    }
    return prepared(candidate);
}

bool MatchAttributes::reserved_projection_key(const String &key) {
    return key == String(store_id) || key.begins_with(row_prefix);
}

Error MatchAttributes::append_projection(const String &player_id, Dictionary &view) const {
    const Ref<TransactionCoordinator> stores = authority_store(false);
    if (stores.is_null()) {
        return ERR_UNAVAILABLE;
    }
    const Dictionary state = stores->read_store(store_id);
    view[store_id] = schema;
    const Array characters = state.keys();
    for (int64_t index = 0; index < characters.size(); ++index) {
        const String id = characters[index];
        if (id == String(store_schema_key)) {
            continue;
        }
        const Dictionary row = row_at(state, id);
        const bool owner = !player_id.is_empty() && row.get("owner", String()) == Variant(player_id);
        const Array observers = row["observers"];
        const bool observes = owner || observers.has(player_id);
        const Dictionary values = row_values(row);
        const Array names = values.keys();
        Dictionary visible;
        for (int64_t attribute = 0; attribute < names.size(); ++attribute) {
            const String key = names[attribute];
            const AttributeDefinition *definition = lookup(registry, key);
            if (!definition) {
                return ERR_INVALID_DATA;
            }
            if ((definition->visibility == AttributeVisibility::OWNER && owner) ||
                    (definition->visibility == AttributeVisibility::OBSERVERS && observes)) {
                visible[key] = values[key];
            }
        }
        if (!visible.is_empty()) {
            view[String(row_prefix) + id] = visible;
        }
    }
    return OK;
}

bool MatchAttributes::validate_projection(const Dictionary &view) const {
    if (view.get(store_id, Variant()) != Variant(schema)) {
        return false;
    }
    const Array keys = view.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const String key = keys[index];
        if (!key.begins_with(row_prefix)) {
            continue;
        }
        if (!valid_character_id(key.substr(String(row_prefix).length())) || view[key].get_type() != Variant::DICTIONARY) {
            return false;
        }
        const Dictionary row = view[key];
        const Array ids = row.keys();
        if (ids.is_empty()) {
            return false;
        }
        for (int64_t attribute = 0; attribute < ids.size(); ++attribute) {
            const String id = ids[attribute];
            const AttributeDefinition *definition = lookup(registry, id);
            double number = 0.0;
            if (!definition || definition->visibility == AttributeVisibility::AUTHORITY ||
                    !decode_number(row[ids[attribute]], number)) {
                return false;
            }
        }
    }
    return true;
}

Dictionary MatchAttributes::read_character(const String &character_id) const {
    const GameRuntime *runtime = resolve_game();
    if (!runtime || runtime->cleaning) {
        return {};
    }
    if (runtime->is_authority()) {
        const Ref<TransactionCoordinator> stores = authority_store(false);
        if (stores.is_valid()) {
            const Variant row = stores->read_store_value(store_id, character_id);
            return row.get_type() == Variant::DICTIONARY ? row_values(Dictionary(row)) : Dictionary();
        }
        return {};
    }
    return runtime->get_phase() == GameRuntime::RUNNING && runtime->replication_node->is_client_ready() ?
            row_at(client_values, character_id) : Dictionary();
}

Ref<GameplayAttributeValue> MatchAttributes::read_value(const StringName &attribute_id, const String &character_id,
        int64_t generation) const {
    const GameRuntime *runtime = resolve_game();
    return runtime && generation == match_generation ?
            make_reading(read_character(character_id).get(String(attribute_id), Variant()), runtime->is_authority()) :
            Ref<GameplayAttributeValue>();
}

Error MatchAttributes::edit_value(const StringName &attribute_id, double operand, bool add,
        const String &character_id, int64_t generation) {
    if (!resolve_game() || generation != match_generation || authority_store(true).is_null()) {
        return ERR_UNAUTHORIZED;
    }
    const String id = attribute_id;
    double previous = 0.0;
    if (!lookup(registry, id) || !decode_number(read_character(character_id).get(id, Variant()), previous)) {
        return ERR_DOES_NOT_EXIST;
    }
    double result = 0.0;
    const Error evaluated = attributes::evaluate_attribute_base(previous, operand, add, result);
    if (evaluated != OK || previous == result) {
        return evaluated;
    }
    Dictionary change = operation("set_base", character_id);
    change["attribute"] = id;
    change["operand"] = result;
    return submit_change(change);
}

Ref<GameplayAttributes> MatchAttributes::for_character(const String &character_id) {
    if (!resolve_game() || !valid_character_id(character_id) || read_character(character_id).is_empty()) {
        return {};
    }
    return character_bridge(character_id);
}

Error MatchAttributes::bind_component(AbilitySystemComponent *component, const String &character_id) {
    const GameRuntime *runtime = resolve_game();
    if (!runtime || runtime->cleaning) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_character_id(character_id) || !owns_component(component) ||
            component->get_attribute_source() != AbilitySystemComponent::MATCH) {
        return ERR_INVALID_PARAMETER;
    }
    return component->bind_match_attributes(character_bridge(character_id));
}

Ref<GameplayAttributes> MatchAttributes::character_bridge(const String &character_id) {
    for (auto found = handles.begin(); found != handles.end();) {
        if (!ObjectDB::get_instance(found->second)) {
            found = handles.erase(found);
        } else {
            ++found;
        }
    }
    const auto existing = handles.find(character_id);
    if (existing != handles.end()) {
        return Ref<GameplayAttributes>(Object::cast_to<GameplayAttributes>(ObjectDB::get_instance(existing->second)));
    }
    const Ref<GameplayAttributes> view = GameplayAttributes::create_bridge(
            ObjectID(get_instance_id()),
            callable_mp(this, &MatchAttributes::read_value).bind(character_id, match_generation),
            callable_mp(this, &MatchAttributes::edit_value).bind(character_id, match_generation));
    if (view.is_valid()) {
        handles.emplace(character_id, ObjectID(view->get_instance_id()));
    }
    return view;
}

void MatchAttributes::on_committed(int64_t, const PackedStringArray &changed_stores, int64_t service_id,
        int64_t coordinator_id) {
    if (!changed_stores.has(store_id)) {
        return;
    }
    const Ref<MatchAttributes> service = resolve_store_owner(service_id, coordinator_id, false);
    if (service.is_valid()) {
        const Ref<TransactionCoordinator> stores = service->authority_store(false);
        service->publish_values(service->authority_values, authority_snapshot(stores->read_store(store_id)), true);
    }
}

void MatchAttributes::on_projection_updated(int64_t, bool) {
    const GameRuntime *runtime = resolve_game();
    if (!runtime || runtime->is_authority() || runtime->get_phase() != GameRuntime::RUNNING) {
        return;
    }
    const Dictionary view = runtime->get_projection();
    Dictionary next;
    const Array keys = view.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const String key = keys[index];
        if (key.begins_with(row_prefix)) {
            next[key.substr(String(row_prefix).length())] = view[key];
        }
    }
    publish_values(client_values, next, false);
}

void MatchAttributes::on_projection_cleared() {
    publish_values(client_values, Dictionary(), false);
}

void MatchAttributes::on_phase_changed(int64_t phase) {
    if (phase == GameRuntime::IDLE) {
        ++match_generation;
        ++publication;
        handles.clear();
        authority_values.clear();
        client_values.clear();
    } else if (phase == GameRuntime::TRANSITIONING || phase == GameRuntime::RECOVERING) {
        on_projection_cleared();
    } else if (phase == GameRuntime::RUNNING) {
        on_projection_updated(0, true);
    }
}

void MatchAttributes::publish_values(Dictionary &values, Dictionary next, bool authority) {
    if (values == next) {
        return;
    }
    const Ref<MatchAttributes> keep_alive(this);
    const Dictionary before = values;
    values = next;
    const uint64_t published = ++publication;
    const auto observers = handles;
    for (const auto &[character, handle] : observers) {
        const Ref<GameplayAttributes> view(Object::cast_to<GameplayAttributes>(ObjectDB::get_instance(handle)));
        if (view.is_null()) {
            continue;
        }
        const Dictionary old_row = row_at(before, character);
        const Dictionary new_row = row_at(next, character);
        if (old_row == new_row) {
            continue;
        }
        std::set<String> names;
        for (const Variant &key : old_row.keys()) {
            names.insert(String(key));
        }
        for (const Variant &key : new_row.keys()) {
            names.insert(String(key));
        }
        for (const String &name : names) {
            const Variant old_value = old_row.get(name, Variant());
            const Variant new_value = new_row.get(name, Variant());
            if (old_value != new_value) {
                view->notify_changed(StringName(name), make_reading(old_value, authority), make_reading(new_value, authority));
            }
            if (published != publication || !resolve_game()) {
                return;
            }
        }
    }
}

void register_attributes_integration() {
    GDREGISTER_CLASS(MatchAttributes);
}

} // namespace crimson::integration
