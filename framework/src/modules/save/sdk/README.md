<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Native save provider SDK

A Windows x86-64 provider DLL can replace the bundled codec, store, or both without rebuilding the Godot addon. Providers run as native code inside the game process. Load a DLL you trust from a path the game controls. Use an on-disk file for an exported game.

## Build and use the example

Open an x64 Visual Studio Developer Command Prompt in the ZIP’s extracted root. The example uses the public C header and the Windows SDK. It has no dependency on Godot or godot-cpp.

```bat
cl /nologo /LD /std:c++17 /EHsc /MT /I sdk\include sdk\examples\provider.cpp bcrypt.lib /Fe:example-provider.dll
```

Call `load_native_provider` on the main thread after `configure` and before submitting requests. Place the example DLL at the given path, or pass its absolute path.

```gdscript
if not saves.load_native_provider("res://example-provider.dll", "codec"):
    push_error(saves.get_last_admission_error())
```

| Configuration | Capabilities |
| --- | --- |
| `"codec"` | Run-length codec with checksummed GSAV framing. The configured store is retained. |
| `"store"` | Bounded memory store. The configured codec is retained. |
| `"both"` or an empty string | Both example services. |

The memory store is a conformance example. Data survives only as long as its provider context. A durable store can use the configuration string for its own storage root or provider settings. The example codec uses ID `0x8001`. It can expand inputs with few repeated bytes and applies the same file-size cap.

## ABI v1

Include [godot_save_provider.h](include/godot_save_provider.h). Export exactly the symbol `godot_save_provider_v1` with C linkage and the declared calling convention. Use the header’s standard structure layout. The host initializes `struct_size` and `abi_version`. On success, set one or both capability bits, populate the complete function table for each capability, and return `GS_OK`. A provider context can be null for a stateless implementation. A non-null context requires a matching `destroy` callback. Clean up any partially initialized resources before returning failure.

Configuration is a provider-defined UTF-8 string of at most 64 KiB. Input pointers, errors, output sinks, and commit gates belong to the host and are valid for the current call. Copy data into your own storage if it must outlive that call. Invoke each sink and gate serially on the calling thread, and propagate the first nonzero sink result. Keep provider allocations inside the provider. Send UTF-8 diagnostics through the optional fixed-size error message. Catch exceptions inside every C callback.

The host keeps the module and its instance alive while its services are in use and calls `destroy` before releasing the module. Initialization is part of main-thread configuration. Codec and store callbacks run on a background worker. Treat simultaneous contexts as independent and synchronize access to any shared storage they use. The Windows loader resolves dependencies using the provider directory and System32. [Windows DLL loading flags](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)

### Codec

`encode` receives a bounded UTF-8 JSON envelope and emits opaque checkpoint bytes. `decode` reads your wire format and emits the same logical JSON envelope. The host validates decoded JSON and checks the envelope against the requested slot, generation, and schema. Honor the supplied limits for both the persisted frame and the JSON bridge.

A provider can use its own wire format or use common GSAV framing with a distinct codec ID. Reserve GSAV codec ID `1` for the built-in JSON codec. The host still reads legacy GSAV/JSON frames with the built-in decoder when a native codec is selected. Give provider formats explicit versions and integrity checks. Use `GS_CORRUPT` for damaged bytes and `GS_INCOMPATIBLE` for intact unsupported versions or codecs.

### Store

Use the profile and slot together as the storage key. Generation IDs are 32 lowercase ASCII hexadecimal bytes, with a positive sequence. The host validates emitted references and orders them by descending sequence. It accepts at most 4,096 entries per call.

| Callback | Contract |
| --- | --- |
| `list` | Emit committed generations only. An empty slot is a successful empty listing. |
| `read` | Emit exactly the bytes of the referenced generation up to the supplied byte limit. Return `GS_NOT_FOUND` when it is absent. |
| `publish` | Stage a complete immutable generation. Check the expected head under exclusive ownership or a transaction. A null expected head requires an empty slot. Call the gate exactly once immediately before publication, and publish only if it returns `GS_OK`. |
| `prune` | Retain every supplied reference. Recheck the current head under writer ownership and reject pruning if the head is absent from the keep set. |

After publication, return `GS_OK` when the generation is confirmed. Return `GS_COMMIT_UNKNOWN` if it may have become visible and verification fails. Other error statuses report definite failure. The host reconciles an uncertain save against the attempted generation before admitting another write to that slot. A durable provider owns atomic visibility, integrity verification, recovery, and the storage guarantees it advertises.

## Verify a provider

In the source checkout, run `scripts/Test-Native.ps1` for native provider and storage conformance. Run `scripts/Test-Release.ps1` to exercise the packaged addon in fresh Godot projects. For a new provider, cover initialization failure, codec round trips, incompatible and damaged inputs, store read limits, expected-head conflicts, cancellation at the publication gate, uncertain publication, and cleanup after the final context is released.
