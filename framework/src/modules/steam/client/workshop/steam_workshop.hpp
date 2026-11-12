// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/workshop/steam_workshop_mutation_types.hpp"
#include "modules/steam/client/workshop/steam_workshop_publish_request.hpp"
#include "modules/steam/client/workshop/steam_workshop_types.hpp"

#include <godot_cpp/core/binder_common.hpp>

#include <memory>
#include <vector>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
struct SteamWorkshopScope;

// Branchable filters and paging for the active app's ready-to-use Workshop items.
class SteamWorkshopQuery : public godot::RefCounted {
    GDCLASS(SteamWorkshopQuery, godot::RefCounted)

protected:
    // Bind the query modifiers, sort choices, and direct await action.
    static void _bind_methods();

public:
    // Ways to order matching Workshop items.
    enum Sort {
        RELEVANCE,
        RATING,
        NEWEST,
    };

    // Return a branch that matches text in the title or description.
    godot::Ref<SteamWorkshopQuery> search(const godot::String &text) const;
    // Return a branch that also requires this Workshop tag.
    godot::Ref<SteamWorkshopQuery> tagged(const godot::String &tag) const;
    // Return a branch with explicit sorting.
    godot::Ref<SteamWorkshopQuery> sort_by(Sort sort) const;
    // Return a branch that starts after a cursor from the same query.
    godot::Ref<SteamWorkshopQuery> after(const godot::String &cursor) const;
    // Return a typed page result or the signal that resolves it.
    godot::Variant fetch() const;

private:
    friend class SteamWorkshop;
    friend struct SteamWorkshopScope;

    // Copy this query so chained modifiers preserve their source branch.
    godot::Ref<SteamWorkshopQuery> branch() const;

    // Weak lifetime of the client scope that created this query.
    std::weak_ptr<SteamWorkshopScope> owner;
    // Optional title or description search text.
    godot::String text;
    // All tags required by this branch.
    std::vector<godot::String> tags;
    // Optional opaque continuation cursor.
    godot::String cursor;
    // Explicit sort chosen by the developer.
    Sort sorting = RATING;
    // Whether the developer overrode the search-dependent default order.
    bool has_sort = false;
    // Invalid filter state deferred until the branch is submitted.
    bool valid = true;
};

// Scoped reference to one Workshop item by its unsigned decimal published file ID.
class SteamWorkshopItemRef : public godot::RefCounted {
    GDCLASS(SteamWorkshopItemRef, godot::RefCounted)

protected:
    // Bind the exact item lookup, subscription, vote, and install actions.
    static void _bind_methods();

public:
    // Return the exact app-owned item result or the signal that resolves it.
    godot::Variant fetch() const;
    // Return the typed result of subscribing, or the signal that resolves it.
    godot::Variant subscribe() const;
    // Return the typed result of unsubscribing, or the signal that resolves it.
    godot::Variant unsubscribe() const;
    // Return the typed result of downloading an up-to-date copy, or the signal that resolves it.
    godot::Variant install(bool high_priority = false) const;
    // Return the verified current local install, or null when unavailable.
    godot::Ref<SteamWorkshopInstall> installed() const;
    // Return the typed outcome of voting, or the signal that resolves it.
    godot::Variant vote(bool up) const;
    // Start a draft that updates this existing Workshop item.
    godot::Ref<SteamWorkshopPublishRequest> edit() const;

private:
    friend class SteamWorkshop;

    // Share the same scope verification and result handling for either subscription action.
    godot::Variant change_subscription(bool subscribed) const;

    // Weak lifetime of the client scope that created this reference.
    std::weak_ptr<SteamWorkshopScope> owner;
    // Published file ID supplied by the caller and checked before SDK submission.
    godot::String published_id;
};

// Client-scoped access to Steam Workshop.
class SteamWorkshop : public godot::RefCounted {
    GDCLASS(SteamWorkshop, godot::RefCounted)

protected:
    // Bind the scoped query and item factories and install notification.
    static void _bind_methods();

public:
    // Maximum number of published IDs copied in one subscription enumeration.
    enum { MAX_SUBSCRIPTIONS = 512 };

    // Start a fresh Workshop query at its first page.
    godot::Ref<SteamWorkshopQuery> query() const;
    // Reference an item by canonical unsigned decimal published file ID.
    godot::Ref<SteamWorkshopItemRef> item(const godot::String &published_id) const;
    // Start a fresh publication draft with private visibility by default.
    godot::Ref<SteamWorkshopPublishRequest> new_item() const;
    // Return a bounded app-scoped snapshot of the current user's subscribed IDs.
    godot::Ref<SteamWorkshopSubscriptionsResult> subscriptions(int64_t limit = 128,
            bool include_locally_disabled = false) const;
    // Return the typed agreement status or the signal that resolves it.
    godot::Variant legal_agreement_status() const;
    // Explicitly ask Steam to show this app's Workshop legal agreement in the overlay.
    godot::Error show_legal_agreement() const;

private:
    friend class SteamClient;

    // Attach a new Workshop scope to the initialized Steam client.
    void configure(const std::shared_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &client_scope);
    // Close local observation before the client releases its runtime.
    void close();
    // Process local observation deadlines on the client frame.
    void tick();

    // Native scope owned by this facade while the Steam client is active.
    std::shared_ptr<SteamWorkshopScope> scope;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamWorkshopQuery::Sort);
