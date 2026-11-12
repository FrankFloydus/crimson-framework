// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "save_core.hpp"

namespace checkpoint {

// Storage and codec services supplied by one loaded native provider.
struct NativeProviderServices {
    // Optional storage backend supplied by the provider.
    std::shared_ptr<Store> store;
    // Optional codec backend supplied by the provider.
    std::shared_ptr<Codec> codec;
};

// Load the configured provider and validate its ABI and capabilities.
NativeProviderServices load_native_provider(const std::filesystem::path &path,
        const std::string &configuration = {});

} // namespace checkpoint
