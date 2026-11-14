// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_wallet.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <vector>

namespace crimson::inventory {

using namespace godot;

namespace {

struct BalanceChange {
    // Actor whose wallet balance is affected.
    int64_t actor;
    // Currency whose balance is affected.
    StringName currency;
    // Expected balance before the change.
    int64_t before;
    // Proposed balance after the change.
    int64_t after;
};

// Validates and converts the wallet callback result to a Godot error code.
Error read_callback_error(const Variant &result, const char *callback) {
    if (result.get_type() != Variant::INT || int64_t(result) < OK || int64_t(result) > ERR_PRINTER_ON_FIRE) {
        UtilityFunctions::printerr(String("[Crimson] Inventory wallet ") + callback + " must return a Godot Error.");
        return ERR_INVALID_DATA;
    }
    return static_cast<Error>(int64_t(result));
}

} // namespace

int64_t SpatialInventoryWallet::get_balance(int64_t actor, const StringName &currency) const {
    if (GDVIRTUAL_IS_OVERRIDDEN(_get_balance)) {
        int64_t amount = 0;
        if (!GDVIRTUAL_CALL(_get_balance, actor, currency, amount) || amount < 0) {
            UtilityFunctions::printerr("[Crimson] Inventory wallet _get_balance could not provide a nonnegative balance.");
            return -1;
        }
        return amount;
    }
    const Dictionary currencies = balances.get(actor, Dictionary());
    return currencies.get(currency, 0);
}

Error SpatialInventoryWallet::set_balance(int64_t actor, const StringName &currency, int64_t amount) {
    const Variant current = call("get_balance", actor, currency);
    if (current.get_type() != Variant::INT || int64_t(current) < 0) {
        return ERR_INVALID_DATA;
    }
    Dictionary change;
    change["actor"] = actor;
    change["currency"] = currency;
    change["before"] = current;
    change["after"] = amount;
    TypedArray<Dictionary> changes;
    changes.push_back(change);
    return read_callback_error(call("apply_balance_changes", changes, Callable()), "apply_balance_changes");
}

Error SpatialInventoryWallet::apply_balance_changes(const Array &changes, const Callable &commit) {
    if (busy) {
        return ERR_BUSY;
    }
    if (!commit.is_null() && !commit.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    if (GDVIRTUAL_IS_OVERRIDDEN(_apply_balance_changes)) {
        TypedArray<Dictionary> rows;
        for (int64_t index = 0; index < changes.size(); ++index) {
            if (changes[index].get_type() != Variant::DICTIONARY) {
                return ERR_INVALID_PARAMETER;
            }
            rows.push_back(changes[index]);
        }
        busy = true;
        Error result = ERR_INVALID_DATA;
        const bool called = GDVIRTUAL_CALL(_apply_balance_changes, rows, commit, result);
        busy = false;
        if (!called) {
            UtilityFunctions::printerr("[Crimson] Inventory wallet _apply_balance_changes could not be called. Check the wallet script before retrying.");
            return ERR_INVALID_DATA;
        }
        return read_callback_error(static_cast<int64_t>(result), "_apply_balance_changes");
    }

    Dictionary seen;
    std::vector<BalanceChange> validated;
    validated.reserve(changes.size());
    for (int64_t index = 0; index < changes.size(); ++index) {
        if (changes[index].get_type() != Variant::DICTIONARY) {
            return ERR_INVALID_PARAMETER;
        }
        const Dictionary row = changes[index];
        const Variant actor = row.get("actor", Variant());
        const Variant currency = row.get("currency", Variant());
        const Variant before = row.get("before", Variant());
        const Variant after = row.get("after", Variant());
        if (actor.get_type() != Variant::INT || int64_t(actor) <= 0 ||
                currency.get_type() != Variant::STRING_NAME || StringName(currency).is_empty() ||
                before.get_type() != Variant::INT || int64_t(before) < 0 ||
                after.get_type() != Variant::INT || int64_t(after) < 0) {
            return ERR_INVALID_PARAMETER;
        }
        Dictionary currencies = seen.get(actor, Dictionary());
        if (currencies.has(currency)) {
            return ERR_INVALID_PARAMETER;
        }
        currencies[currency] = true;
        seen[actor] = currencies;
        const int64_t current = get_balance(actor, currency);
        if (current < 0) {
            return ERR_INVALID_DATA;
        }
        if (current != int64_t(before)) {
            return ERR_BUSY;
        }
        validated.push_back({actor, currency, before, after});
    }

    busy = true;
    for (const BalanceChange &change : validated) {
        write_balance(change.actor, change.currency, change.after);
    }
    const Error error = commit.is_valid() ? read_callback_error(commit.call(), "commit callback") : OK;
    if (error != OK) {
        for (const BalanceChange &change : validated) {
            write_balance(change.actor, change.currency, change.before);
        }
    }
    busy = false;
    return error;
}

void SpatialInventoryWallet::write_balance(int64_t actor, const StringName &currency, int64_t amount) {
    Dictionary currencies = balances.get(actor, Dictionary());
    if (amount == 0) {
        currencies.erase(currency);
        if (currencies.is_empty()) {
            balances.erase(actor);
        }
        return;
    }
    currencies[currency] = amount;
    balances[actor] = currencies;
}

void SpatialInventoryWallet::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_balance", "actor", "currency"), &SpatialInventoryWallet::get_balance);
    ClassDB::bind_method(D_METHOD("set_balance", "actor", "currency", "amount"), &SpatialInventoryWallet::set_balance);
    ClassDB::bind_method(D_METHOD("apply_balance_changes", "changes", "commit"),
            &SpatialInventoryWallet::apply_balance_changes, DEFVAL(Callable()));
    GDVIRTUAL_BIND(_get_balance, "actor", "currency");
    GDVIRTUAL_BIND(_apply_balance_changes, "changes", "commit");
}

} // namespace crimson::inventory
