// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/workshop/steam_workshop_publish_request.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>

namespace crimson::steam {

using namespace godot;

void SteamWorkshopPublishRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("named", "title"), &SteamWorkshopPublishRequest::named);
    ClassDB::bind_method(D_METHOD("described_as", "description"), &SteamWorkshopPublishRequest::described_as);
    ClassDB::bind_method(D_METHOD("with_content", "directory"), &SteamWorkshopPublishRequest::with_content);
    ClassDB::bind_method(D_METHOD("with_preview", "image_file"), &SteamWorkshopPublishRequest::with_preview);
    ClassDB::bind_method(D_METHOD("tagged", "tag"), &SteamWorkshopPublishRequest::tagged);
    ClassDB::bind_method(D_METHOD("visibility", "visibility"), &SteamWorkshopPublishRequest::visibility);
    ClassDB::bind_method(D_METHOD("submit", "change_notes"), &SteamWorkshopPublishRequest::submit, DEFVAL(String()));
    BIND_ENUM_CONSTANT(PRIVATE);
    BIND_ENUM_CONSTANT(FRIENDS_ONLY);
    BIND_ENUM_CONSTANT(PUBLIC);
    BIND_ENUM_CONSTANT(UNLISTED);
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::branch() const {
    Ref<SteamWorkshopPublishRequest> draft;
    draft.instantiate();
    draft->owner = owner;
    draft->editing = editing;
    draft->published_id = published_id;
    draft->title = title;
    draft->description = description;
    draft->directory = directory;
    draft->preview = preview;
    draft->tags = tags;
    draft->audience = audience;
    draft->valid = valid;
    return draft;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::named(const String &title) const {
    const auto draft = branch();
    draft->title = title;
    return draft;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::described_as(const String &description) const {
    const auto draft = branch();
    draft->description = description;
    return draft;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::with_content(const String &directory) const {
    const auto draft = branch();
    draft->directory = directory;
    return draft;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::with_preview(const String &image_file) const {
    const auto draft = branch();
    draft->preview = image_file;
    return draft;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::tagged(const String &tag) const {
    const auto draft = branch();
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        if (tags.size() < MAX_TAGS) {
            draft->tags.push_back(tag);
        } else {
            draft->valid = false;
        }
    }
    return draft;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopPublishRequest::visibility(Visibility visibility) const {
    const auto draft = branch();
    draft->audience = visibility;
    return draft;
}

} // namespace crimson::steam
