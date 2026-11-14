# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Serializes the demo's detached diagnostics for headless network tests.

const NetworkEmulator := preload("res://demo/session/network_emulator.gd")


## Capture current game and relay data without exposing live nodes or peers.
static func snapshot(game: Game, relay: NetworkEmulator) -> Dictionary:
	var stats: GameNetworkDiagnostics = game.network_diagnostics()
	var world: Node = game.get_world()
	var player: Node3D = game.get_local_player()
	var players: Node = world.get_node_or_null(^"Players") if world != null else null
	return {
		"game": {
			"phase": phase_name(stats.phase),
			"role": _role_name(stats.role),
			"transport": String(stats.transport),
			"peer_id": stats.peer_id,
			"player_number": stats.player_number,
			"player_count": players.get_child_count() if players != null else 0,
			"connected_peers": stats.connected_peers,
			"round_trip_ms": stats.round_trip_ms,
			"state_age_ms": stats.state_age_ms,
			"movement_mode": _movement_name(stats.movement_mode),
			"simulation_tick": stats.simulation_tick,
			"acknowledged_input": stats.acknowledged_input,
			"pending_inputs": stats.pending_inputs,
			"replayed_inputs": stats.replayed_inputs,
			"corrections": stats.corrections,
			"correction_distance": stats.correction_distance,
			"authority_position": _position(stats.authority_position),
			"predicted_position": _position(stats.predicted_position),
			"local_position": _position(player.global_position) if player != null else null,
			"world_scene": world.scene_file_path if world != null else "",
			"player_scene": player.scene_file_path if player != null else "",
		},
		"relay": _relay_snapshot(relay.get_stats() if relay != null else NetworkEmulator.Stats.new()),
	}


## Compare a dotted scalar field. Missing fields and incompatible comparisons fail.
static func matches(data: Dictionary, field: String, op: String, value: Variant) -> Dictionary:
	var actual: Variant = data
	for key: String in field.split("."):
		if not actual is Dictionary or not actual.has(key):
			return {"ok": false, "found": false, "actual": null}
		actual = actual[key]
	return {"ok": _compare(actual, op, value), "found": true, "actual": actual}


## Name the affected datagram path using the plan vocabulary.
static func direction_name(direction: int) -> String:
	match direction:
		NetworkEmulator.Direction.BOTH: return "both"
		NetworkEmulator.Direction.UPLINK: return "uplink"
		NetworkEmulator.Direction.DOWNLINK: return "downlink"
	return "none"


## Encode the relay's effective settings, transient timers, and directional counters.
static func _relay_snapshot(stats: NetworkEmulator.Stats) -> Dictionary:
	return {
		"active": stats.active,
		"link_dropped": stats.link_dropped,
		"one_way_delay_ms": stats.one_way_delay_ms,
		"jitter_ms": stats.jitter_ms,
		"loss_percent": stats.loss_percent,
		"uplink_kbps": stats.uplink_kbps,
		"downlink_kbps": stats.downlink_kbps,
		"forwarded": stats.forwarded,
		"dropped": stats.dropped,
		"queued": stats.queued,
		"outage_direction": direction_name(stats.outage_direction),
		"outage_starts_in_ms": stats.outage_starts_in_ms,
		"outage_remaining_ms": stats.outage_remaining_ms,
		"spike_direction": direction_name(stats.spike_direction),
		"spike_delay_ms": stats.spike_delay_ms,
		"spike_starts_in_ms": stats.spike_starts_in_ms,
		"spike_remaining_ms": stats.spike_remaining_ms,
		"scenario_step": stats.scenario_step,
		"scenario_step_count": stats.scenario_step_count,
		"scenario_label": stats.scenario_label,
		"scenario_remaining_ms": stats.scenario_remaining_ms,
		"scenario_starts_in_ms": stats.scenario_starts_in_ms,
		"uplink": _traffic(stats.uplink),
		"downlink": _traffic(stats.downlink),
	}


## Keep the directional counter schema present even when no relay was installed.
static func _traffic(traffic: NetworkEmulator.Traffic) -> Dictionary:
	if traffic == null:
		traffic = NetworkEmulator.Traffic.new()
	return {
		"forwarded": traffic.forwarded,
		"dropped": traffic.dropped,
		"queued": traffic.queued,
		"forwarded_bytes": traffic.forwarded_bytes,
		"dropped_bytes": traffic.dropped_bytes,
		"queued_bytes": traffic.queued_bytes,
		"overflowed": traffic.overflowed,
	}


## Encode position coordinates as numbers so they can be selected by dotted paths.
static func _position(value: Vector3) -> Dictionary:
	return {"x": value.x, "y": value.y, "z": value.z}


## Keep machine-readable phase names independent of the graphical tool's labels.
static func phase_name(phase: int) -> String:
	match phase:
		Game.IDLE: return "idle"
		Game.LOBBY: return "lobby"
		Game.TRANSITIONING: return "transitioning"
		Game.RUNNING: return "running"
		Game.RECOVERING: return "recovering"
	return "unknown"


## Keep machine-readable role names independent of the graphical tool's labels.
static func _role_name(role: int) -> String:
	match role:
		GameNetworkDiagnostics.IDLE: return "idle"
		GameNetworkDiagnostics.OFFLINE: return "offline"
		GameNetworkDiagnostics.HOST: return "host"
		GameNetworkDiagnostics.CONNECTING: return "connecting"
		GameNetworkDiagnostics.GUEST: return "guest"
		GameNetworkDiagnostics.DEDICATED: return "dedicated"
	return "unknown"


## Preserve the distinction between the authoritative and guest movement paths.
static func _movement_name(mode: int) -> String:
	match mode:
		GameNetworkDiagnostics.MOVEMENT_DISABLED: return "disabled"
		GameNetworkDiagnostics.AUTHORITY_LIVE: return "authority_live"
		GameNetworkDiagnostics.AUTHORITY_PREDICTIVE: return "authority_predictive"
		GameNetworkDiagnostics.GUEST_INTERPOLATED: return "guest_interpolated"
		GameNetworkDiagnostics.GUEST_PREDICTED: return "guest_predicted"
	return "unknown"


## Compare numbers by value and check null explicitly for optional metrics.
static func _compare(left: Variant, op: String, right: Variant) -> bool:
	var numeric: bool = (left is int or left is float) and (right is int or right is float)
	if op in ["eq", "ne"]:
		if left == null or right == null:
			var both_null: bool = left == null and right == null
			return both_null if op == "eq" else not both_null
		if not (numeric or (left is String and right is String) or (left is bool and right is bool)):
			return false
		return left == right if op == "eq" else left != right
	if not numeric:
		return false
	match op:
		"gt": return left > right
		"gte": return left >= right
		"lt": return left < right
		"lte": return left <= right
	return false
