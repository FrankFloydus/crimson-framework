/* Copyright Francesco Di Blasi 2026 - All Rights Reserved */

#ifndef GODOT_SAVE_PROVIDER_H
#define GODOT_SAVE_PROVIDER_H

#include <stdint.h>

#ifdef _WIN32
#define GS_CALL __cdecl
#define GS_PROVIDER_EXPORT __declspec(dllexport)
#else
#define GS_CALL
#define GS_PROVIDER_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GS_PROVIDER_ENTRY "godot_save_provider_v1"
#define GS_PROVIDER_ABI_V1 UINT32_C(1)
#define GS_CAP_CODEC UINT32_C(1)
#define GS_CAP_STORE UINT32_C(2)
#define GS_GENERATION_ID_SIZE 32
#define GS_ERROR_MESSAGE_CAPACITY 256
#define GS_MAX_CONFIGURATION_BYTES UINT32_C(65536)

typedef int32_t gs_status_v1;

enum {
    GS_OK = 0,
    GS_CANCELLED = 1,
    GS_NOT_FOUND = 2,
    GS_BUSY = 3,
    GS_CONFLICT = 4,
    GS_CORRUPT = 5,
    GS_INCOMPATIBLE = 6,
    GS_LIMIT_EXCEEDED = 7,
    GS_INVALID_ARGUMENT = 8,
    GS_OUT_OF_SPACE = 9,
    GS_PERMISSION_DENIED = 10,
    GS_IO_ERROR = 11,
    GS_COMMIT_UNKNOWN = 12,
    GS_INVALID_DATA = 13,
    GS_PROVIDER_ERROR = 14
};

/* Diagnostic buffer that a provider may fill on failure. */
typedef struct gs_error_v1 {
    /* NUL-terminated UTF-8 diagnostic text. */
    char message[GS_ERROR_MESSAGE_CAPACITY];
} gs_error_v1;

/* Borrowed view of an encoded checkpoint or a byte chunk. */
typedef struct gs_bytes_v1 {
    /* First byte of the borrowed range. */
    const uint8_t *data;
    /* Number of bytes in the range. */
    uint64_t size;
} gs_bytes_v1;

/* Borrowed UTF-8 text that carries an explicit byte length. */
typedef struct gs_string_v1 {
    /* First byte of the borrowed text. */
    const char *data;
    /* Number of UTF-8 bytes in the view. */
    uint64_t size;
} gs_string_v1;

/* Profile and slot identifiers for one storage request. */
typedef struct gs_slot_v1 {
    /* Profile that owns the requested slot. */
    gs_string_v1 profile;
    /* Slot identifier within the profile. */
    gs_string_v1 slot;
} gs_slot_v1;

/* Identity of one committed generation in a slot. */
typedef struct gs_generation_v1 {
    /* Positive sequence used to order generations within the slot. */
    uint64_t sequence;
    /* Fixed-width hexadecimal identifier without a terminator. */
    char id[GS_GENERATION_ID_SIZE];
} gs_generation_v1;

/* Host resource limits for provider codec operations. */
typedef struct gs_limits_v1 {
    /* Maximum encoded checkpoint size in bytes. */
    uint64_t max_file_bytes;
    /* Maximum nesting depth of decoded JSON. */
    uint64_t max_depth;
    /* Maximum number of values in the decoded JSON tree. */
    uint64_t max_nodes;
    /* Maximum UTF-8 size of one JSON string or key. */
    uint64_t max_string_bytes;
} gs_limits_v1;

/* Invoke sinks and the gate serially on the calling thread. The host owns
 * their contexts and all input views for the duration of the call only. */
typedef struct gs_byte_sink_v1 {
    /* Host context passed back to the write callback. */
    void *context;
    /* Send one byte chunk to the host and check the returned status. */
    gs_status_v1 (GS_CALL *write)(void *context, gs_bytes_v1 chunk);
} gs_byte_sink_v1;

/* Host callback that receives generation references during listing. */
typedef struct gs_generation_sink_v1 {
    /* Host context passed back to the emit callback. */
    void *context;
    /* Emit a generation reference and check whether listing can continue. */
    gs_status_v1 (GS_CALL *emit)(void *context, const gs_generation_v1 *generation);
} gs_generation_sink_v1;

/* Host authorization gate for one generation publication. */
typedef struct gs_commit_gate_v1 {
    /* Host context passed back to the begin callback. */
    void *context;
    /* Request permission once immediately before the irreversible commit. */
    gs_status_v1 (GS_CALL *begin)(void *context);
} gs_commit_gate_v1;

/* Convert checkpoint bytes and stream the bounded result to the host sink. */
typedef gs_status_v1 (GS_CALL *gs_codec_operation_v1)(void *context, gs_bytes_v1 input,
        const gs_limits_v1 *limits, gs_byte_sink_v1 output, gs_error_v1 *error);

/* Paired encode and decode callbacks for a provider codec. */
typedef struct gs_codec_v1 {
    /* Encode UTF-8 JSON as opaque checkpoint bytes. */
    gs_codec_operation_v1 encode;
    /* Recover UTF-8 JSON from provider checkpoint bytes. */
    gs_codec_operation_v1 decode;
} gs_codec_v1;

/* Provider callbacks for listing, reading, publishing, and pruning generations. */
typedef struct gs_store_v1 {
    /* Emit committed generations from the specified slot. */
    gs_status_v1 (GS_CALL *list)(void *context, const gs_slot_v1 *slot,
            gs_generation_sink_v1 output, gs_error_v1 *error);
    /* Stream the selected generation's bytes within max_bytes. */
    gs_status_v1 (GS_CALL *read)(void *context, const gs_slot_v1 *slot,
            const gs_generation_v1 *generation, uint64_t max_bytes,
            gs_byte_sink_v1 output, gs_error_v1 *error);
    /* A null expected_head means the slot must have no committed generation. */
    gs_status_v1 (GS_CALL *publish)(void *context, const gs_slot_v1 *slot,
            const gs_generation_v1 *generation, const gs_generation_v1 *expected_head,
            gs_bytes_v1 bytes, gs_commit_gate_v1 gate, gs_error_v1 *error);
    /* Remove generations outside the retained set when its head still matches. */
    gs_status_v1 (GS_CALL *prune)(void *context, const gs_slot_v1 *slot,
            const gs_generation_v1 *keep, uint64_t keep_count, gs_error_v1 *error);
} gs_store_v1;

/* Versioned provider context and the callbacks enabled by its capabilities. */
typedef struct gs_provider_v1 {
    /* Size of the provider structure available to the ABI. */
    uint32_t struct_size;
    /* Version of the provider ABI implemented by these callbacks. */
    uint32_t abi_version;
    /* Capability flags indicating which callback tables are active. */
    uint32_t capabilities;
    /* Reserved ABI field initialized to zero. */
    uint32_t reserved;
    /* Provider-owned context passed to every operation. */
    void *context;
    /* Release provider-owned state before the host unloads the DLL. */
    void (GS_CALL *destroy)(void *context);
    /* Codec callbacks present when GS_CAP_CODEC is set. */
    gs_codec_v1 codec;
    /* Storage callbacks present when GS_CAP_STORE is set. */
    gs_store_v1 store;
} gs_provider_v1;

/*
 * Export godot_save_provider_v1 with this signature. Configuration is an
 * optional provider-defined UTF-8 string. The host initializes
 * struct_size and abi_version and zeros the remaining fields. On success the
 * provider transfers its context to the host. On failure it releases any
 * resources acquired during initialization.
 */
typedef gs_status_v1 (GS_CALL *gs_provider_entry_v1)(gs_string_v1 configuration,
        gs_provider_v1 *provider, gs_error_v1 *error);

#ifdef __cplusplus
}
#endif

#endif
