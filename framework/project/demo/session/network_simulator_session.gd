# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Boots the same Transfer Hall game and local relay for graphical and headless sessions.

const Emulator := preload("res://demo/session/network_emulator.gd")
const LEVEL_PATH: String = "res://demo/hub/starting_hub.tscn"
const PLAYER_PATH: String = "res://demo/player/stationary_player.tscn"
const DEFAULT_PORT: int = 24830
## The listen host and dedicated server mount Game beneath this same RPC path.
const ROOT_NAME: StringName = &"CrimsonDemo"

var game: Game
var relay: Emulator


## An override owns its provider. The standard demo config installs a local ENet relay.
func boot(owner: Node, port: int = 0, override: GameConfig = null) -> void:
	if override == null:
		boot_lan(owner, port)
	else:
		game = (override.on_port(port) if port != 0 else override).boot(owner)


## Install the local relay while using a supplied LAN configuration or the playable demo's defaults.
func boot_lan(owner: Node, port: int = 0, configuration: GameConfig = null) -> void:
	use_authority_routes(owner)
	relay = Emulator.new()
	owner.add_child(relay)
	var config: GameConfig = configuration
	if config == null:
		var level := load(LEVEL_PATH) as PackedScene
		var player := load(PLAYER_PATH) as PackedScene
		config = Game.configure(level, player).world_id(&"transfer_hall").lan(4).on_port(DEFAULT_PORT).player_controls()
	if port != 0:
		config = config.on_port(port)
	game = config.provider(relay.provider()).boot(owner)


## Use the demo's authoritative player roster and route all guest traffic through the host.
static func use_authority_routes(owner: Node) -> void:
	var scope := owner.get_multiplayer() as SceneMultiplayer
	if scope != null:
		# Godot can send relay notices to another ENet peer that retired in the same poll.
		scope.server_relay = false


## Temporarily restore direct forwarding so ENet can deliver its normal leave packet.
static func leave(active_game: Game, link_relay: Emulator) -> GameResult:
	if link_relay != null:
		link_relay.restore_link()
	var result: GameResult = await active_game.leave()
	if not result.ok and link_relay != null:
		link_relay.cancel_restore_link()
	return result


## Attach the authored controls for a headless guest to exercise the real movement input path.
static func boot_controls(active_game: Game, owner: Node) -> Controls:
	var body := active_game.get_local_player() as CharacterBody3D
	if body == null:
		return null
	var component := ControlsComponent.for_body(body)
	var view := owner.get_viewport().get_camera_3d()
	if component == null or component.input_layout == null or view == null or not body.is_ancestor_of(view):
		return null
	var controls := Controls.boot(owner, component.input_layout)
	if not controls.is_ready():
		return null
	controls.set_pointer_available(false)
	var player := component.boot(view, controls)
	if player == null or not player.is_ready():
		controls.close()
		return null
	return controls
