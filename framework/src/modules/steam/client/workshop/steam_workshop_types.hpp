// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>

namespace crimson::steam {

// Detached Workshop metadata from a successful Steam query.
class SteamWorkshopItem : public godot::RefCounted {
    GDCLASS(SteamWorkshopItem, godot::RefCounted)

protected:
    // Bind the read-only item fields.
    static void _bind_methods();

public:
    // Return the item's canonical unsigned decimal published file ID.
    godot::String get_published_id() const;
    // Return the Steam Workshop title.
    godot::String get_title() const;
    // Return the Steam Workshop description returned for this query.
    godot::String get_description() const;
    // Return the author's SteamID64, or an empty string if Steam did not supply it.
    godot::String get_author_steam_id() const;

private:
    friend struct SteamWorkshopQueryCall;

    // Create the detached metadata before the native query handle is released.
    static godot::Ref<SteamWorkshopItem> create(const godot::String &published_id, const godot::String &title,
            const godot::String &description, const godot::String &author_steam_id);

    // Canonical unsigned decimal published file ID.
    godot::String published_id;
    // Workshop title copied from Steam.
    godot::String title;
    // Workshop description copied from Steam.
    godot::String description;
    // SteamID64 supplied for the content's owner.
    godot::String author_steam_id;
};

// One bounded page of Workshop metadata and its continuation cursor.
class SteamWorkshopPage : public godot::RefCounted {
    GDCLASS(SteamWorkshopPage, godot::RefCounted)

protected:
    // Bind the read-only page and opaque cursor.
    static void _bind_methods();

public:
    // Return the read-only items detached from the Steam query handle.
    godot::TypedArray<SteamWorkshopItem> get_items() const;
    // Return the exact next cursor, or an empty string at the end.
    godot::String get_next_cursor() const;

private:
    friend struct SteamWorkshopQueryCall;

    // Create the page and make its item array read-only before publication.
    static godot::Ref<SteamWorkshopPage> create(const godot::TypedArray<SteamWorkshopItem> &items,
            const godot::String &next_cursor);

    // Read-only snapshots returned for this page.
    godot::TypedArray<SteamWorkshopItem> items;
    // Opaque Steam continuation cursor.
    godot::String next_cursor;
};

// Terminal outcome and one exact Workshop item lookup.
class SteamWorkshopItemResult : public SteamResult {
    GDCLASS(SteamWorkshopItemResult, SteamResult)

protected:
    // Bind the read-only item result.
    static void _bind_methods();

public:
    // Return the detached item on success, or null on failure.
    godot::Ref<SteamWorkshopItem> get_value() const;

private:
    friend class SteamWorkshopItemRef;
    friend struct SteamWorkshopQueryCall;

    // Construct a terminal item lookup result.
    static godot::Ref<SteamWorkshopItemResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamWorkshopItem> &item = {});

    // Detached metadata for the successful lookup.
    godot::Ref<SteamWorkshopItem> value;
};

// Terminal outcome and one bounded Workshop query page.
class SteamWorkshopPageResult : public SteamResult {
    GDCLASS(SteamWorkshopPageResult, SteamResult)

protected:
    // Bind the read-only page result.
    static void _bind_methods();

public:
    // Return the detached page on success, or null on failure.
    godot::Ref<SteamWorkshopPage> get_value() const;

private:
    friend class SteamWorkshopQuery;
    friend struct SteamWorkshopQueryCall;

    // Construct a terminal page result.
    static godot::Ref<SteamWorkshopPageResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamWorkshopPage> &page = {});

    // Detached items and cursor for the successful query.
    godot::Ref<SteamWorkshopPage> value;
};

// Own the signal behind a directly awaited Workshop action.
class SteamWorkshopAction : public godot::RefCounted {
    GDCLASS(SteamWorkshopAction, godot::RefCounted)

protected:
    // Register the internal completion signal carrying a concrete SteamResult subclass.
    static void _bind_methods();

public:
    // Return the completion signal, or the already-settled typed result.
    godot::Variant wait();
    // Settle local observation before resuming the waiting script.
    void finish(const godot::Ref<SteamResult> &result);
    // Continue an internal lookup before publishing a compound action's result.
    void when_finished(std::function<void(const godot::Ref<SteamResult> &)> next);

private:
    // One internal continuation consumed before the completion signal.
    std::function<void(const godot::Ref<SteamResult> &)> continuation;
    // Concrete terminal result retained through completion.
    godot::Ref<SteamResult> outcome;
};

} // namespace crimson::steam
