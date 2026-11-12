// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace crimson::steam {

struct SteamWorkshopScope;

// Branchable draft for creating or updating one Workshop item.
class SteamWorkshopPublishRequest : public godot::RefCounted {
    GDCLASS(SteamWorkshopPublishRequest, godot::RefCounted)

protected:
    // Bind the draft modifiers, visibility choices, and publish action.
    static void _bind_methods();

public:
    // Steam Workshop audiences available for an item update.
    enum Visibility {
        PRIVATE,
        FRIENDS_ONLY,
        PUBLIC,
        UNLISTED,
    };

    // Return a branch with the item's replacement title.
    godot::Ref<SteamWorkshopPublishRequest> named(const godot::String &title) const;
    // Return a branch with the item's replacement description.
    godot::Ref<SteamWorkshopPublishRequest> described_as(const godot::String &description) const;
    // Return a branch that uploads the supplied local content directory.
    godot::Ref<SteamWorkshopPublishRequest> with_content(const godot::String &directory) const;
    // Return a branch with a replacement local preview image.
    godot::Ref<SteamWorkshopPublishRequest> with_preview(const godot::String &image_file) const;
    // Return a branch adding a tag to the replacement tag set.
    godot::Ref<SteamWorkshopPublishRequest> tagged(const godot::String &tag) const;
    // Return a branch using the selected Steam visibility.
    godot::Ref<SteamWorkshopPublishRequest> visibility(Visibility visibility) const;
    // Return a publication result or the completion signal for the submitted draft.
    godot::Variant submit(const godot::String &change_notes = {}) const;

private:
    friend class SteamWorkshop;
    friend class SteamWorkshopItemRef;
    friend struct SteamWorkshopScope;

    // Bound the number of selected tags in one publication draft.
    static constexpr size_t MAX_TAGS = 16;

    // Copy the draft and keep its original settings and ownership unchanged.
    godot::Ref<SteamWorkshopPublishRequest> branch() const;

    // Weak lifetime of the originating Steam client scope.
    std::weak_ptr<SteamWorkshopScope> owner;
    // Whether this draft edits an existing published ID.
    bool editing = false;
    // Existing item ID supplied by item().edit().
    godot::String published_id;
    // Replacement title, when explicitly set.
    std::optional<godot::String> title;
    // Replacement description, when explicitly set.
    std::optional<godot::String> description;
    // Content directory selected for upload, when explicitly set.
    std::optional<godot::String> directory;
    // Preview image selected for upload, when explicitly set.
    std::optional<godot::String> preview;
    // Replacement tags. An empty list preserves existing tags for edits.
    std::vector<godot::String> tags;
    // Audience explicitly selected by the author.
    std::optional<Visibility> audience;
    // Invalid draft state deferred until submission.
    bool valid = true;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamWorkshopPublishRequest::Visibility);
