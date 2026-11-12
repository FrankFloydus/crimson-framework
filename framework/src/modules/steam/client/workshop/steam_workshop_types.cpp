// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/workshop/steam_workshop_types.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

namespace crimson::steam {

using namespace godot;

void SteamWorkshopItem::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_published_id"), &SteamWorkshopItem::get_published_id);
    ClassDB::bind_method(D_METHOD("get_title"), &SteamWorkshopItem::get_title);
    ClassDB::bind_method(D_METHOD("get_description"), &SteamWorkshopItem::get_description);
    ClassDB::bind_method(D_METHOD("get_author_steam_id"), &SteamWorkshopItem::get_author_steam_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "published_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_published_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "title", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_title");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_description");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "author_steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_author_steam_id");
}

String SteamWorkshopItem::get_published_id() const {
    return published_id;
}

String SteamWorkshopItem::get_title() const {
    return title;
}

String SteamWorkshopItem::get_description() const {
    return description;
}

String SteamWorkshopItem::get_author_steam_id() const {
    return author_steam_id;
}

Ref<SteamWorkshopItem> SteamWorkshopItem::create(const String &published_id, const String &title,
        const String &description, const String &author_steam_id) {
    Ref<SteamWorkshopItem> item;
    item.instantiate();
    item->published_id = published_id;
    item->title = title;
    item->description = description;
    item->author_steam_id = author_steam_id;
    return item;
}

void SteamWorkshopPage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_items"), &SteamWorkshopPage::get_items);
    ClassDB::bind_method(D_METHOD("get_next_cursor"), &SteamWorkshopPage::get_next_cursor);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "items", PROPERTY_HINT_ARRAY_TYPE, "SteamWorkshopItem",
                         PROPERTY_USAGE_READ_ONLY), "", "get_items");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "next_cursor", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_next_cursor");
}

TypedArray<SteamWorkshopItem> SteamWorkshopPage::get_items() const {
    return items;
}

String SteamWorkshopPage::get_next_cursor() const {
    return next_cursor;
}

Ref<SteamWorkshopPage> SteamWorkshopPage::create(const TypedArray<SteamWorkshopItem> &items, const String &next_cursor) {
    Ref<SteamWorkshopPage> page;
    page.instantiate();
    page->items = items;
    page->items.make_read_only();
    page->next_cursor = next_cursor;
    return page;
}

void SteamWorkshopItemResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopItemResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopItem",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopItem> SteamWorkshopItemResult::get_value() const {
    return value;
}

Ref<SteamWorkshopItemResult> SteamWorkshopItemResult::create(Error error, const String &message,
        const Ref<SteamWorkshopItem> &item) {
    Ref<SteamWorkshopItemResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = item;
    return result;
}

void SteamWorkshopPageResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopPageResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopPage",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopPage> SteamWorkshopPageResult::get_value() const {
    return value;
}

Ref<SteamWorkshopPageResult> SteamWorkshopPageResult::create(Error error, const String &message,
        const Ref<SteamWorkshopPage> &page) {
    Ref<SteamWorkshopPageResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = page;
    return result;
}

void SteamWorkshopAction::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamResult")));
}

Variant SteamWorkshopAction::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamWorkshopAction::finish(const Ref<SteamResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamWorkshopAction> keep_alive(this);
    outcome = result;
    if (const auto next = std::exchange(continuation, {})) {
        next(outcome);
    }
    emit_signal("completed", outcome);
}

void SteamWorkshopAction::when_finished(std::function<void(const Ref<SteamResult> &)> next) {
    continuation = std::move(next);
}

} // namespace crimson::steam
