// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "save_core.hpp"

namespace checkpoint {

// Serialize validated JSON to bounded bytes without a frame.
Bytes encode_json_payload(const Json &value, const Limits &limits);
// Parse raw payload bytes with the checkpoint JSON constraints.
Json decode_json_payload(const Bytes &bytes, const Limits &limits);

} // namespace checkpoint
