# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends Node

const ABILITY: Script = preload("res://tests/gameplay_abilities/network_ability.gd")
const ADDRESS: String = "127.0.0.1"
const AMBER: StringName = &"abilities.amber"
const COBALT: StringName = &"abilities.cobalt"
const HOST: String = "host"
const GUEST: String = "guest"
const HERO: String = "hero"
const ALTERNATE: String = "alternate"
const COMMAND: String = "fixture.activate"
const ABILITY_ID: String = "fixture.pulse"
const PLAYERS: String = "fixture.players"
const CHARACTERS: String = "fixture.characters"
const OUTCOMES: String = "fixture.activations"
const TIMEOUT_MSEC: int = 10000

var _game: Game
var _runtime: GameRuntime
var _actors: Dictionary[String, AbilitySystemComponent] = {}
var _grants: Dictionary[String, int] = {}
var _role: String
var _guest_peer: int
var _observed_stage: int
var _world: StringName
var _epoch: int
var _can_request: bool
var _can_leave: bool
var _canceled_before_transition: bool

@onready var _hero_abilities: AbilitySystemComponent = $HeroAbilities
@onready var _alternate_abilities: AbilitySystemComponent = $AlternateAbilities


func _ready() -> void:
	_run.call_deferred()


func _run() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() != 2 or args[0] not in ["host", "client"] or not args[1].is_valid_int():
		_expect(false, "Pass a host or client role and a UDP port.")
		get_tree().quit(1)
		return
	_role = args[0]
	Engine.max_fps = 120
	var active_tree: SceneTree = get_tree()
	active_tree.create_timer(32.0).timeout.connect(func() -> void:
		_expect(false, "The ability networking fixture exceeded its deadline.")
		active_tree.quit(1)
	)
	var amber: WorldDefinition = WorldDefinition.at(AMBER, "res://tests/game_flow/fixtures/amber.tscn")
	var cobalt: WorldDefinition = WorldDefinition.at(COBALT, "res://tests/game_flow/fixtures/cobalt.tscn")
	_game = Game.campaign(amber).with_world(cobalt).lan(2).on_port(args[1].to_int()).boot(self)
	var boot: GameResult = _game.boot_result()
	if not _expect(boot.ok, boot.message):
		active_tree.quit(1)
		return
	_runtime = get_node("Game") as GameRuntime
	_actors[HERO] = _hero_abilities
	_actors[ALTERNATE] = _alternate_abilities
	_game.world_ready.connect(_on_world_ready)
	_game.phase_changed.connect(_on_phase_changed)
	_game.peer_joined.connect(_on_peer_joined)
	if not _bind_actors():
		active_tree.quit(1)
		return
	var passed: bool = await _run_host(cobalt) if _role == "host" else await _run_client()
	var left: GameResult = await _game.leave()
	passed = _expect(left.ok and _hero_abilities.give_ability(ABILITY) == 0
		and _hero_abilities.try_activate_ability(1) == ERR_UNAUTHORIZED and not _hero_abilities.is_ability_active(1),
		"An actor from the retired game stays denied through its old ability scope.") and passed
	if passed:
		print("CRIMSON_ABILITIES_NETWORK_OK ", _role)
	active_tree.quit(0 if passed else 1)


func _bind_actors() -> bool:
	var detached := AbilitySystemComponent.new()
	var detached_status: Error = _game.bind_ability_system(detached)
	detached.free()
	return _expect(_game.attributes() == null and detached_status == ERR_INVALID_PARAMETER
		and _game.bind_ability_system(_hero_abilities) == OK and _game.bind_ability_system(_alternate_abilities) == OK
		and _game.bind_ability_system(_hero_abilities) == ERR_ALREADY_IN_USE and _hero_abilities.give_ability(ABILITY) == 0,
		"An ability-only game scopes mounted actors once and denies mutations before world readiness.")


func _run_host(cobalt: WorldDefinition) -> bool:
	var transactions: TransactionCoordinator = _game.transactions()
	var possession: PlayerPossession = _runtime.get_possession()
	if not _expect(transactions.register_store(PLAYERS, {HOST: {}, GUEST: {}}, _read_only_store) == OK
			and transactions.register_store(CHARACTERS, {HERO: {}, ALTERNATE: {}}, _read_only_store) == OK
			and transactions.register_store(OUTCOMES, {"count": 0}, _prepare_outcomes) == OK
			and possession.configure(transactions, PLAYERS, CHARACTERS) == OK
			and _runtime.set_shared_stores(PackedStringArray([OUTCOMES])) == OK
			and _game.register_command(COMMAND, _handle_activation, true) == OK,
			"Configure the authority command, committed possession, and visible outcome."):
		return false
	var opened: GameResult = _game.open_lobby(ADDRESS)
	if not _expect(opened.ok, opened.message) or not _expect(_game.bind_player(1, HOST) == OK,
			"Bind the listen authority's local player."):
		return false
	print("CRIMSON_ABILITIES_NETWORK_READY host")
	if not await _wait_for(func() -> bool: return _guest_peer > 1, "the guest to be admitted") \
			or not _expect(_game.bind_player(_guest_peer, GUEST) == OK
			and possession.try_set_primary(GUEST, HERO, 0).get("ok", false), "Bind the admitted player and assign its current character."):
		return false
	var started: GameResult = await _game.start_match()
	if not _expect(started.ok, started.message):
		return false
	_grants[HERO] = _hero_abilities.give_ability(ABILITY)
	_grants[ALTERNATE] = _alternate_abilities.give_ability(ABILITY)
	if not _expect(_grants[HERO] > 0 and _grants[ALTERNATE] > 0 and rpc_id(_guest_peer, &"_allow_request") == OK,
			"Grant the ability on the authority after the world is ready.") \
			or not await _wait_for(func() -> bool: return _observed_stage == 1, "the guest to observe the confirmed activation"):
		return false
	if not _expect(_hero_abilities.is_ability_active(_grants[HERO]) and not _alternate_abilities.is_ability_active(_grants[ALTERNATE])
			and transactions.read_store(OUTCOMES).get("count", 0) == 1, "Only the actual possessed hero ran and published an activation."):
		return false
	var traveled: GameResult = await _game.scenes().load(cobalt)
	if not _expect(traveled.ok and _canceled_before_transition and not _hero_abilities.is_ability_active(_grants[HERO]),
			"Travel retires the previous world's active ability before the public phase callback.") \
			or not await _wait_for(func() -> bool: return _observed_stage == 2, "the guest to verify the next world's baseline") \
			or not _expect(_hero_abilities.try_activate_ability(_grants[HERO]) == OK
			and _hero_abilities.is_ability_active(_grants[HERO]), "The persistent actor can start a fresh run in the new world.") \
			or not _expect(rpc_id(_guest_peer, &"_allow_leave") == OK, "Release the verified guest."):
		return false
	return await _wait_for(func() -> bool: return multiplayer.get_peers().is_empty(), "the verified guest to leave")


func _run_client() -> bool:
	if not _expect(_runtime.declare_command(COMMAND, true) == OK, "Declare the same primary-character command on the guest."):
		return false
	var joined: GameResult = _game.join_lobby(ADDRESS)
	if not _expect(joined.ok, joined.message) or not await _wait_for(func() -> bool: return _world == AMBER and _can_request,
			"the authority grant and initial world baseline"):
		return false
	var first_epoch: int = _epoch
	if not _expect(_runtime.get_local_character_id() == HERO and _runtime.get_projection()[OUTCOMES]["count"] == 0
			and _hero_abilities.give_ability(ABILITY) == 0 and _hero_abilities.try_activate_ability(1) == ERR_UNAUTHORIZED,
			"The ready guest reads its confirmed possession and cannot grant or activate directly."):
		return false
	var forged: GameResult = await _game.perform(COMMAND, {"ability": ABILITY_ID, "character": ALTERNATE})
	var unknown: GameResult = await _game.perform(COMMAND, {"ability": "unknown"})
	if not _expect(not forged.ok and forged.code == "InvalidAbilityIntent" and not unknown.ok and unknown.code == "UnknownAbility",
			"The authority rejects a client-selected actor and an unknown ability."):
		return false
	var accepted: GameResult = await _game.perform(COMMAND, {"ability": ABILITY_ID})
	var repeated: GameResult = await _game.perform(COMMAND, {"ability": ABILITY_ID})
	if not _expect(accepted.ok and not repeated.ok and repeated.code == "ActivationRejected", "Only the first valid request starts a run.") \
			or not await _wait_for(_has_one_outcome, "the committed ability outcome") \
			or not _expect(rpc_id(1, &"_report_stage", 1) == OK, "Report the verified activation to the authority."):
		return false
	if not await _wait_for(func() -> bool: return _world == COBALT and _epoch > first_epoch and _has_one_outcome(),
			"the next world and its existing committed outcome") \
			or not _expect(rpc_id(1, &"_report_stage", 2) == OK, "Report the verified new world baseline."):
		return false
	return await _wait_for(func() -> bool: return _can_leave, "the authority to confirm completion")


func _handle_activation(player_id: String, intent: Dictionary) -> Dictionary:
	var payload: Dictionary = intent.get("payload", {})
	if payload.size() != 1 or not payload.get("ability") is String:
		return _rejected("InvalidAbilityIntent", "Send only the ability ID.")
	if payload["ability"] != ABILITY_ID:
		return _rejected("UnknownAbility", "The requested ability is unknown.")
	var character: String = intent["character_id"]
	if player_id != GUEST or not _actors.has(character) or not _grants.has(character):
		return _rejected("AbilityUnavailable", "The possessed actor has no available grant.")
	var actor: AbilitySystemComponent = _actors[character]
	if actor.try_activate_ability(_grants[character]) != OK:
		return _rejected("ActivationRejected", "The ability cannot start in its current state.")
	var transactions: TransactionCoordinator = _game.transactions()
	var transaction: GameTransaction = transactions.begin()
	var queued: Error = transaction.add_change(OUTCOMES, {"activated": true}, transactions.get_store_revision(OUTCOMES))
	if queued != OK:
		actor.cancel_ability(_grants[character])
		return _rejected("CommitFailed", "Could not stage the visible activation result.")
	var result: Dictionary = transactions.commit(transaction)
	if not result.get("ok", false):
		actor.cancel_ability(_grants[character])
	return result


func _prepare_outcomes(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
	if operations.size() != 1 or operations[0] != {"activated": true}:
		return {"ok": false, "message": "Expected one confirmation from the authority command."}
	return {"ok": true, "state": {"count": int(current["count"]) + 1}}


func _read_only_store(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
	return {"ok": operations.is_empty(), "state": current}


func _rejected(code: String, message: String) -> Dictionary:
	return {"ok": false, "code": code, "message": message}


func _has_one_outcome() -> bool:
	return _runtime.get_projection().get(OUTCOMES, {}).get("count", 0) == 1


func _on_peer_joined(peer: int) -> void:
	if _role == "host" and _guest_peer == 0:
		_guest_peer = peer


func _on_world_ready(world: StringName, epoch: int) -> void:
	_world = world
	_epoch = epoch


func _on_phase_changed(new_phase: int) -> void:
	if _role == "host" and new_phase == Game.TRANSITIONING and _grants.has(HERO):
		_canceled_before_transition = not _hero_abilities.is_ability_active(_grants[HERO])


@rpc("authority", "call_remote", "reliable")
func _allow_request() -> void:
	_can_request = true


@rpc("authority", "call_remote", "reliable")
func _allow_leave() -> void:
	_can_leave = true


@rpc("any_peer", "call_remote", "reliable")
func _report_stage(stage: int) -> void:
	if multiplayer.is_server() and multiplayer.get_remote_sender_id() == _guest_peer and stage == _observed_stage + 1:
		_observed_stage = stage


func _wait_for(condition: Callable, description: String) -> bool:
	var deadline: int = Time.get_ticks_msec() + TIMEOUT_MSEC
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	return _expect(bool(condition.call()), "Wait for %s." % description)


func _expect(condition: bool, detail: String) -> bool:
	if not condition:
		push_error("Ability network fixture %s: %s" % [_role, detail])
	return condition
