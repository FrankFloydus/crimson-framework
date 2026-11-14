<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Rebinding

- Use the parent [Input module](../README.md) for raw capture, assigned devices, gameplay suspension, and atomic overlay changes. Keep UI and controller logic outside Rebinding.
- Retain one completion signal per capture. Release the exact capture token and clear state before notifying scripts. Handle cancellation during capture startup, modal retirement, and scope replacement.
- Persist only bounded, versioned binding values keyed by stable slot IDs. Validate complete files before changing the local overlay. Avoid resource loading or Variant object decoding from saved profiles.
- Verify raw capture, cancellation, conflict preservation, reset, and persistence through the [public workflow](README.md).
