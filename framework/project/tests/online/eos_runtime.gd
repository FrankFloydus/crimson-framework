# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree


## Verify lazy initialization, the real vendor version, and repeated call behavior.
func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var mode := args[0] if args.size() == 1 else ""
	if mode not in ["available", "missing", "disabled"]:
		push_error("Expected one EOS fixture mode: available, missing, or disabled.")
		quit(1)
		return
	if CrimsonEOS.is_initialized() or not CrimsonEOS.get_runtime_version().is_empty():
		push_error("EOS initialized before the fixture explicitly selected it.")
		quit(1)
		return

	var expected := OK if mode == "available" else ERR_CANT_OPEN if mode == "missing" else ERR_UNAVAILABLE
	var first := CrimsonEOS.initialize()
	var second := CrimsonEOS.initialize()
	if first != expected or second != expected or CrimsonEOS.is_initialized() != (expected == OK):
		push_error("EOS %s initialization returned %s, %s instead of %s." % [mode, first, second, expected])
		quit(1)
		return

	var version := CrimsonEOS.get_runtime_version()
	if (expected == OK and not version.begins_with("1.19.2.1")) or (expected != OK and not version.is_empty()):
		push_error("Unexpected EOS %s runtime version: %s." % [mode, version])
		quit(1)
		return
	if not version.is_empty():
		print("EOS runtime version %s" % version)
	print("CRIMSON_EOS_RUNTIME_PASSED %s" % mode)
	quit(0)
