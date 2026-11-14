# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Shared link profiles and scenario templates for the demo's graphical and headless simulators.

const NetworkEmulator := preload("res://demo/session/network_emulator.gd")

const PROFILE_STORE_ENV: String = "CRIMSON_NETWORK_PROFILE_STORE"
const PROFILE_STORE_DEFAULT: String = "user://network_profiles"
const PROFILE_PREFIX: String = "profile-"
const PROFILE_SUFFIX: String = ".cfg"
const PROFILE_SECTION: String = "network"
const MAX_PROFILE_NAME: int = 48
const MAX_PROFILE_FILE_BYTES: int = 4096

## Identifies built-in entries referenced by the starter scenarios.
enum Preset {
	## Adds no delay, jitter, packet loss, or bandwidth limit.
	CLEAR,
	## Models a faster 4G link with 30 ms delay per direction.
	FAST_4G,
	## Models a slower 4G link with 75 ms delay per direction.
	SLOW_4G,
	## Models a slow 3G link with 200 ms delay per direction.
	SLOW_3G,
	## Caps bandwidth at 32 kbit/s up and 64 kbit/s down, with delay and jitter.
	CONSTRAINED,
	## Adds 150 ms nominal round-trip delay with jitter.
	JITTER_150,
	## Adds 300 ms nominal round-trip delay with jitter.
	JITTER_300,
	## Adds 150 ms nominal round-trip delay, jitter, and 10 percent packet loss.
	LOSS_150,
}
## Identifies the reusable scenario starters.
enum Template {
	## Cycles through clear, slow 4G, and constrained conditions before returning to clear.
	PROFILE_CYCLE,
	## Interrupts client-to-server traffic briefly, then restores the link.
	UPLINK_INTERRUPTION,
}

const _PRESET_IDS: Array[StringName] = [
	&"clear", &"fast-4g", &"slow-4g", &"slow-3g", &"constrained",
	&"150-ms-jitter", &"300-ms-jitter", &"150-ms-loss",
]
const _TEMPLATE_IDS: Array[StringName] = [&"profile-cycle", &"uplink-interruption"]

## Editable link settings or a detached preset.
class Profile extends NetworkEmulator.Profile:
	var label: String

	func _init(name: String, delay: int, jitter: int, loss: float, up: int, down: int) -> void:
		label = name
		delay_ms = delay
		jitter_ms = jitter
		loss_percent = loss
		uplink_kbps = up
		downlink_kbps = down

	## Compare impairment values independently of a preset's display name.
	func matches(other: NetworkEmulator.Profile) -> bool:
		return delay_ms == other.delay_ms and jitter_ms == other.jitter_ms \
			and is_equal_approx(loss_percent, other.loss_percent) and uplink_kbps == other.uplink_kbps \
			and downlink_kbps == other.downlink_kbps

	## Copy this profile's link settings into one independently timed stage.
	func stage(duration_ms: int, outage_direction: int = -1) -> NetworkEmulator.ScenarioStep:
		var result := NetworkEmulator.ScenarioStep.new()
		result.label = label
		result.duration_ms = duration_ms
		result.delay_ms = delay_ms
		result.jitter_ms = jitter_ms
		result.loss_percent = loss_percent
		result.uplink_kbps = uplink_kbps
		result.downlink_kbps = downlink_kbps
		result.outage_direction = outage_direction
		return result


## Return built-in conditions in display order. Mobile rates are nominal decimal kbit/s.
static func presets() -> Array[Profile]:
	return [
		Profile.new(TranslationServer.translate("Clear"), 0, 0, 0.0, 0, 0),
		Profile.new(TranslationServer.translate("Fast 4G"), 30, 0, 0.0, 1500, 9000),
		Profile.new(TranslationServer.translate("Slow 4G"), 75, 0, 0.0, 750, 1600),
		Profile.new(TranslationServer.translate("Slow 3G"), 200, 0, 0.0, 500, 500),
		Profile.new(TranslationServer.translate("Constrained game link"), 35, 5, 0.0, 32, 64),
		Profile.new(TranslationServer.translate("150 ms RTT + jitter"), 75, 15, 0.0, 0, 0),
		Profile.new(TranslationServer.translate("300 ms RTT + jitter"), 150, 25, 0.0, 0, 0),
		Profile.new(TranslationServer.translate("150 ms RTT + 10% loss"), 75, 15, 10.0, 0, 0),
	]


## Return the stable command-line name at the given preset index.
static func preset_id(index: int) -> StringName:
	return _PRESET_IDS[index] if index >= 0 and index < _PRESET_IDS.size() else &""


## Resolve a command-line preset name. An unknown name returns -1.
static func preset_index(id: StringName) -> int:
	return _PRESET_IDS.find(id)


## Return the stable command-line name at the given template index.
static func template_id(index: int) -> StringName:
	return _TEMPLATE_IDS[index] if index >= 0 and index < _TEMPLATE_IDS.size() else &""


## Resolve a command-line template name. An unknown name returns -1.
static func template_index(id: StringName) -> int:
	return _TEMPLATE_IDS.find(id)


## Return the translated name of a reusable scenario template.
static func template_label(index: int) -> String:
	match index:
		Template.PROFILE_CYCLE: return TranslationServer.translate("Profile cycle")
		Template.UPLINK_INTERRUPTION: return TranslationServer.translate("Uplink interruption")
	return ""


## Build a fresh editable sequence with independent copies of its profiles.
static func scenario_template(index: Template) -> Array[NetworkEmulator.ScenarioStep]:
	var profiles: Array[Profile] = presets()
	var clear: Profile = profiles[Preset.CLEAR]
	var slow: Profile = profiles[Preset.SLOW_4G]
	match index:
		Template.PROFILE_CYCLE:
			return [clear.stage(3000), slow.stage(4000), profiles[Preset.CONSTRAINED].stage(4000), clear.stage(3000)]
		Template.UPLINK_INTERRUPTION:
			return [clear.stage(3000), slow.stage(1400, NetworkEmulator.Direction.UPLINK), slow.stage(3000), clear.stage(3000)]
	return []


## Read named profiles saved by this demo, in stable display order.
static func saved_profiles() -> Array[Profile]:
	var folder: String = _profile_store()
	var results: Array[Profile] = []
	if not DirAccess.dir_exists_absolute(folder):
		return results
	for file: String in DirAccess.get_files_at(folder):
		if not file.begins_with(PROFILE_PREFIX) or not file.ends_with(PROFILE_SUFFIX):
			continue
		var path: String = folder.path_join(file)
		if FileAccess.get_size(path) > MAX_PROFILE_FILE_BYTES:
			continue
		var config := ConfigFile.new()
		if config.load(path) != OK:
			continue
		var name: Variant = config.get_value(PROFILE_SECTION, "name", "")
		var delay: Variant = config.get_value(PROFILE_SECTION, "delay_ms", -1)
		var jitter: Variant = config.get_value(PROFILE_SECTION, "jitter_ms", -1)
		var loss: Variant = config.get_value(PROFILE_SECTION, "loss_percent", -1.0)
		var up: Variant = config.get_value(PROFILE_SECTION, "uplink_kbps", -1)
		var down: Variant = config.get_value(PROFILE_SECTION, "downlink_kbps", -1)
		if name is String and delay is int and jitter is int and (loss is float or loss is int) \
				and up is int and down is int:
			var profile := Profile.new(name, delay, jitter, float(loss), up, down)
			if _valid_profile(profile) and file == _profile_filename(profile.label):
				results.append(profile)
	results.sort_custom(func(left: Profile, right: Profile) -> bool:
		return left.label.naturalnocasecmp_to(right.label) < 0)
	return results


## Save the draft as a named preset. Saving under the same name replaces that profile.
static func save_profile(profile: Profile) -> Error:
	if not _valid_profile(profile):
		return ERR_INVALID_PARAMETER
	var folder: String = _profile_store()
	var made: Error = DirAccess.make_dir_recursive_absolute(folder)
	if made != OK:
		return made
	var config := ConfigFile.new()
	config.set_value(PROFILE_SECTION, "name", profile.label)
	config.set_value(PROFILE_SECTION, "delay_ms", profile.delay_ms)
	config.set_value(PROFILE_SECTION, "jitter_ms", profile.jitter_ms)
	config.set_value(PROFILE_SECTION, "loss_percent", profile.loss_percent)
	config.set_value(PROFILE_SECTION, "uplink_kbps", profile.uplink_kbps)
	config.set_value(PROFILE_SECTION, "downlink_kbps", profile.downlink_kbps)
	var destination: String = folder.path_join(_profile_filename(profile.label))
	var temporary: String = "%s.%s.tmp" % [destination, Crypto.new().generate_random_bytes(8).hex_encode()]
	var result: Error = config.save(temporary)
	if result == OK:
		result = DirAccess.rename_absolute(temporary, destination)
	if result != OK and FileAccess.file_exists(temporary):
		DirAccess.remove_absolute(temporary)
	return result


## Use one local tool directory without changing the demo's isolated game data.
static func _profile_store() -> String:
	var configured: String = OS.get_environment(PROFILE_STORE_ENV)
	return configured if configured.is_absolute_path() else ProjectSettings.globalize_path(PROFILE_STORE_DEFAULT)


## Restrict filenames to tool-owned prefixes and a stable, case-insensitive name hash.
static func _profile_filename(name: String) -> String:
	return PROFILE_PREFIX + name.to_lower().sha256_text() + PROFILE_SUFFIX


## Reject malformed files and drafts outside the transport's supported range.
static func _valid_profile(profile: Profile) -> bool:
	return not profile.label.is_empty() and profile.label == profile.label.strip_edges() \
		and profile.label.length() <= MAX_PROFILE_NAME and not profile.label.contains("\n") \
		and not profile.label.contains("\r") and profile.delay_ms >= 0 and profile.delay_ms <= NetworkEmulator.MAX_DELAY_MS \
		and profile.jitter_ms >= 0 and profile.jitter_ms <= NetworkEmulator.MAX_DELAY_MS \
		and is_finite(profile.loss_percent) and profile.loss_percent >= 0.0 and profile.loss_percent <= 100.0 \
		and profile.uplink_kbps >= 0 and profile.uplink_kbps <= NetworkEmulator.MAX_BANDWIDTH_KBPS \
		and profile.downlink_kbps >= 0 and profile.downlink_kbps <= NetworkEmulator.MAX_BANDWIDTH_KBPS
