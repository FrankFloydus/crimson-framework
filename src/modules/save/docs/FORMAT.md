<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Save format and recovery

## Files and limits

The default store resolves `user://godot-save-system` through the host project. A configured root uses the same layout.

```text
user://godot-save-system/
  p-player-1/
    s-manual-1/
      .writer.lock
      g-42-8b53fe2164a24d7f923ae8cb842c960b.save
```

Final filenames contain a positive decimal sequence without leading zeros and a 32-character lowercase hexadecimal generation ID. The slot head is the largest recognized sequence. A duplicate sequence is a conflict. Temporary files are named `t-<sequence>-<id>.tmp` and excluded from reads. A subsequent writer can clean up an abandoned temporary file while holding the slot lease.

The reference limits each framed file to 8 MiB, JSON nesting to 48 levels, the JSON tree to 200,000 values including containers, and each UTF-8 key or string to 1 MiB. New save metadata is limited to 8 KiB. A slot directory scan is capped at 4,096 entries. Use distinct slots or roots for independent checkpoint scopes. The scope ID is stored in the envelope.

## Reference wire format

The frame begins with this fixed 16-byte header. All integers are unsigned and little-endian.

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 4 | ASCII `GSAV` |
| 4 | 2 | Framing version `1` |
| 6 | 2 | Codec ID `1` for UTF-8 JSON |
| 8 | 8 | Payload length in bytes |

The JSON payload follows at offset 16. Append the raw 32-byte SHA-256 digest of the header and payload. The decoder validates size and checksum before reading the JSON. It rejects duplicate keys, malformed UTF-8, raw NULs, and a UTF-8 BOM at the start of the payload. Save payloads for Godot adapters also require strings without embedded NULs. A checksum-valid frame with an unsupported version or codec is incompatible.

```json
{
  "format_version": 1,
  "app_id": "example.notebook",
  "profile_id": "player-1",
  "slot_id": "manual-1",
  "scope_id": "notebook",
  "scope_version": 1,
  "generation": {
    "sequence": "42",
    "id": "8b53fe2164a24d7f923ae8cb842c960b"
  },
  "meta": {"kind": "manual", "capture_revision": "184", "label": "Notebook"},
  "participants": [
    {"id": "notebook.entries", "version": 1, "data": ["First note"]}
  ]
}
```

The app, profile, slot, and generation in the envelope must agree with the requested checkpoint and filename. Scope versions cover membership and cross-participant layout. Participant versions cover each DTO. Keep versions as positive JSON integers and represent larger counters as decimal strings. The coordinator supplies metadata `kind` and `capture_revision`. Labels and timestamps are host-supplied display fields.

A [native provider](../sdk/README.md) can change how the envelope is encoded and where generations are stored. The coordinator still validates decoded envelopes against the same schema. With a provider codec installed, reference GSAV frames still pass through the built-in decoder.

## Publication and recovery

1. The worker reads the current head and prepares a new generation. The Windows store acquires an exclusive handle for the slot and rechecks the expected head.
2. The store writes the temporary file in the slot directory, flushes and closes it, and verifies that readback matches the encoded bytes.
3. After the cancellation gate, the store moves the temporary file to its fresh final name without replacement. It verifies the published bytes again before reporting success.
4. Backup pruning runs after publication. The reference aims to retain the new generation and two earlier compatible, byte-verified generations. It preserves the selected restore source in the active coordinator and leaves unverified or incompatible entries available for inspection. Cleanup failures can appear as warnings on a successful save.

A process interruption before the move leaves the prior generation available. A process interruption after publication can leave a final generation even if the original process never emitted its result. Treat filesystem and power-loss qualification separately. The Windows backend flushes file contents before publication. Power-loss durability of the complete generation and directory entry depends on the storage stack and is outside this release contract.

If latest-load returns `Corrupt` or `Incompatible`, list the slot and offer an explicitly selected older generation. The selected data must still pass migration, staging, and gameplay validation. Continue in a fresh slot to preserve the damaged or newer checkpoint. A normal save protects an unreadable or incompatible head. To upgrade an older scope in place, the coordinator must successfully restore the exact head or migrate, stage, and validate its detached candidate through `validate_prepared_load_for_save`. The write checks the head again and preserves the verified generation as a backup.

When a save reports `CommitUnknown`, pass the attempted `sequence` and `id` to `request_inspect_generation` before retrying. An `Available` entry confirms the bytes. `NotFound` confirms the generation is absent. Other outcomes leave the commit unresolved.

Slot listings are paginated in descending sequence order, with up to 16 entries per page. `truncated` indicates that older entries remain and `next` points to the last sequence and ID on the page. Each page reports the slot head’s status. Slot listings and exact inspections work with app and scope configuration alone. They verify bytes and envelope structure, including participant record shape. With participant declarations they also check supported participant IDs and versions for the current scope. `Available` reports inspection checks. Gameplay validation takes place during restore. Entries whose metadata exceeds the display budget return an empty `meta` and `meta_truncated = true`.

SHA-256 detects accidental changes. Anyone who can edit the file can also recompute its checksum. Authentication, encryption, cloud synchronization, and multiplayer authority require separate implementations.
