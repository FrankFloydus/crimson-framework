# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends Node

const ADDRESS: String = "127.0.0.1"
const REGISTRY: String = "crimson/gameplay_attributes/sets"
const AMBER: StringName = &"attributes.amber"
const COBALT: StringName = &"attributes.cobalt"
const ALPHA: String = "alpha"
const VITALS: StringName = &"vitals"
const HEALTH: StringName = &"health"
const STAMINA: StringName = &"stamina"
const SECRET: StringName = &"secret"
const HEALTH_ID: StringName = &"vitals.health"
const STAMINA_ID: StringName = &"vitals.stamina"
const SECRET_ID: StringName = &"vitals.secret"
const STRIKE: String = "fixture.strike"
const SCHEMA: String = "crimson.attributes"
const ALPHA_RECORD: String = "crimson.attributes/alpha"
const TIMEOUT_MS: int = 10000
const BASELINE_MISMATCH_TIMEOUT_MS: int = 3000

var _role: String
var _game: Game
var _runtime: GameRuntime
var _service: MatchAttributes
@onready var _alpha_component: AbilitySystemComponent = $AlphaAbilitySystem
var _alpha: GameplayAttributeSet
var _health_watch: GameplayAttributeWatch
var _peers: Dictionary[String, int] = {}
var _stages: Dictionary[String, int] = {}
var _world: StringName
var _epoch: int
var _strike_ready: bool
var _finish_ready: bool
var _authorized_commands: int
var _monitor_reads_match: bool = true
var _noticed_scope_removal: bool
var _noticed_transition: bool
var _changes: Array[float] = []


func _ready() -> void:
	_run.call_deferred()


func _run() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() != 2 or args[0] not in ["host", "dedicated", "owner", "observer", "mismatch-host", "mismatch-client"] \
			or not args[1].is_valid_int():
		_expect(false, "Pass a supported fixture role and a UDP port.")
		get_tree().quit(1)
		return
	_role = args[0]
	Engine.max_fps = 120
	var tree: SceneTree = get_tree()
	tree.create_timer(32.0).timeout.connect(func() -> void:
		_expect(false, "The Attributes network fixture exceeded its deadline.")
		tree.quit(1)
	)
	var authority: bool = _role in ["host", "dedicated", "mismatch-host"]
	ProjectSettings.set_setting(REGISTRY, _definitions(authority))
	var amber: WorldDefinition = WorldDefinition.at(AMBER, "res://tests/game_flow/fixtures/amber.tscn")
	var cobalt: WorldDefinition = WorldDefinition.at(COBALT, "res://tests/game_flow/fixtures/cobalt.tscn")
	_game = Game.campaign(amber).with_world(cobalt).lan(3).on_port(args[1].to_int()).attributes().boot(self)
	var boot: GameResult = _game.boot_result()
	if not _expect(boot.ok, boot.message):
		tree.quit(1)
		return
	_runtime = get_node("Game") as GameRuntime
	_service = _game.attributes()
	if not _expect(_service.bind_component(_alpha_component, ALPHA) == OK
			and _service.bind_component(_alpha_component, ALPHA) == OK
			and _service.bind_component(_alpha_component, "another_actor") == ERR_ALREADY_IN_USE,
			"Bind the scene component to one match character before admission."):
		tree.quit(1)
		return
	_alpha = _alpha_component.get_attribute_set(VITALS)
	if not _expect(_alpha != null and _alpha.get_attribute(HEALTH) == null and _service.for_character(ALPHA) == null,
			"The configured match set is available before admission and its values wait for committed state."):
		tree.quit(1)
		return
	_game.world_ready.connect(_on_world_ready)
	_game.phase_changed.connect(_on_phase_changed)
	var passed: bool
	match _role:
		"mismatch-host": passed = await _run_mismatch_authority()
		"mismatch-client": passed = await _run_mismatch_client()
		_: passed = await _run_authority(cobalt) if authority else await _run_client()
	var left: GameResult = await _game.leave()
	passed = _expect(left.ok, left.message) and passed
	if _alpha != null:
		passed = _expect(_alpha.get_attribute(HEALTH) == null, "A retired character handle must not read an old match.") and passed
	if passed and authority:
		passed = await _confirm_retired_component()
	if passed:
		print("CRIMSON_ATTRIBUTES_NETWORK_OK ", _role)
	tree.quit(0 if passed else 1)


# Compatible peers may use different labels and display order for the same gameplay schema.
func _definitions(authority: bool) -> Dictionary:
	var rows: Array = [
		{"id": "health", "label": "Health" if authority else "HP",
			"visibility": "owner" if _role == "mismatch-client" else "observers"},
		{"id": "stamina", "visibility": "owner"},
		{"id": "secret"},
	]
	if not authority:
		rows.reverse()
	return {"version": 1, "sets": [{"id": "vitals", "attributes": rows}]}


func _confirm_retired_component() -> bool:
	var played: GameResult = await _game.play()
	if not _expect(played.ok, played.message):
		return false
	var transactions: TransactionCoordinator = _game.transactions()
	var transaction: GameTransaction = transactions.begin()
	var published: bool = _service.queue_add_component(transaction, _alpha_component, ALPHA, "local") == OK \
		and _service.queue_base(transaction, ALPHA, HEALTH_ID, 80.0) == OK \
		and transactions.commit(transaction).get("ok", false)
	var newest: GameplayAttributes = _service.for_character(ALPHA)
	if not _expect(published and newest != null and newest.get_attribute(HEALTH_ID).get_base() == 80.0
		and _alpha.get_attribute(HEALTH) == null and _service.bind_component(_alpha_component, ALPHA) == ERR_ALREADY_IN_USE,
		"A retired component stays unavailable after an authority recreates its character ID."):
		return false
	var active_component := AbilitySystemComponent.new()
	active_component.attribute_source = AbilitySystemComponent.MATCH
	active_component.attribute_sets = _alpha_component.attribute_sets
	add_child(active_component)
	if not _expect(_service.bind_component(active_component, ALPHA) == OK, "Bind a fresh component to the current match."):
		return false
	var retained_set: GameplayAttributeSet = active_component.get_attribute_set(VITALS)
	if not _expect(retained_set != null and _reads(retained_set, HEALTH, 80.0, true), "Read the live source before destroying its game."):
		return false
	var retained_revision: int = transactions.get_revision()
	var snapshot: Dictionary = transactions.capture_snapshot().get("stores", {})
	var pending: GameTransaction = transactions.begin()
	if not _expect(snapshot.has(SCHEMA) and _service.queue_base(pending, ALPHA, HEALTH_ID, 1.0) == OK,
			"Retain a committed snapshot and a pending authority edit across teardown."):
		return false
	var published_service: WeakRef = weakref(_service)
	_service = null
	_runtime.queue_free()
	if not await _wait_for(func() -> bool: return published_service.get_ref() == null, "the match service to be destroyed"):
		return false
	if not _expect(newest.get_attribute(HEALTH_ID) == null and newest.set_base(HEALTH_ID, 1.0) == ERR_UNAUTHORIZED
		and retained_set.get_attribute(HEALTH) == null and retained_set.set_base(HEALTH, 1.0) == ERR_UNAUTHORIZED,
		"The retained character and actor set safely reject reads and writes after their match service is freed."):
		return false
	var rejected_edit: Dictionary = transactions.commit(pending)
	var rejected_restore: Dictionary = transactions.prepare_restore(snapshot)
	return _expect(rejected_edit.code == "prepare_failed" and rejected_restore.code == "restore_validation_failed"
		and "retired authority" in rejected_edit.message and "retired authority" in rejected_restore.message
		and transactions.get_revision() == retained_revision,
		"A retained coordinator rejects a pending edit and checkpoint restore after the attribute authority is freed.")


func _run_mismatch_authority() -> bool:
	var loader: SceneLoadManager = _runtime.get_node("SceneLoader") as SceneLoadManager
	if not _expect(loader.set_load_timeout_msec(BASELINE_MISMATCH_TIMEOUT_MS) == OK, "Set the fixture's baseline timeout."):
		return false
	var opened: GameResult = _game.open_lobby(ADDRESS)
	if not _expect(opened.ok, opened.message):
		return false
	var transactions: TransactionCoordinator = _game.transactions()
	var transaction: GameTransaction = transactions.begin()
	if not _expect(_service.queue_add_component(transaction, _alpha_component, ALPHA, "mismatch-client") == OK
			and transactions.commit(transaction).get("ok", false)
			and _reads(_alpha, HEALTH, 0.0, true) and _reads(_alpha, STAMINA, 0.0, true) and _reads(_alpha, SECRET, 0.0, true),
			"Create zero attributes the guest could observe if its registry matched."):
		return false
	print("CRIMSON_ATTRIBUTES_NETWORK_READY ", _role)
	if not await _wait_for(func() -> bool: return _peers.has("mismatch-client"), "the incompatible peer to bind"):
		return false
	var started: GameResult = await _game.start_match()
	if not _expect(not started.ok and started.code == "BaselineTimeout" and _game.get_phase() == Game.RECOVERING,
			"The authority never receives an acknowledgment for the incompatible baseline.") \
			or not await _wait_for(func() -> bool: return _stages.get("mismatch-client", 0) == 1, "the guest to verify its unavailable projection") \
			or not _expect(rpc_id(_peers["mismatch-client"], &"_release_clients") == OK, "Confirm the incompatible peer's final assertion."):
		return false
	return await _wait_for(func() -> bool: return multiplayer.get_peers().is_empty(), "the verified incompatible peer to leave")


func _run_mismatch_client() -> bool:
	var joined: GameResult = _game.join_lobby(ADDRESS)
	if not _expect(joined.ok, joined.message) or not await _wait_for(func() -> bool: return _game.get_phase() == Game.LOBBY, "the incompatible guest to enter the lobby") \
			or not _expect(rpc_id(1, &"_announce", _role) == OK, "Report the incompatible guest's fixture role.") \
			or not await _wait_for(func() -> bool: return _game.get_phase() == Game.RECOVERING, "recovery after the incompatible baseline") \
			or not _expect(_world.is_empty() and _runtime.get_projection().is_empty() and _alpha.get_attribute(HEALTH) == null
				and _service.for_character(ALPHA) == null,
				"No world becomes ready and the incompatible baseline never becomes observable.") \
			or not _report_stage(1):
		return false
	return await _wait_for(func() -> bool: return _finish_ready, "the authority to acknowledge recovery")


func _run_authority(cobalt: WorldDefinition) -> bool:
	if not _expect(_game.register_command(STRIKE, _perform_strike) == OK, "Register the server-owned command.") \
			or not _expect(_runtime.set_shared_stores(PackedStringArray([SCHEMA])) == ERR_INVALID_PARAMETER,
				"The private Attributes store cannot be configured as a shared store.") \
			or not _expect(_runtime.set_projector(_project_game_record) == OK, "Install a separate game projection."):
		return false
	var opened: GameResult = _game.open_lobby(ADDRESS)
	if not _expect(opened.ok, opened.message):
		return false
	if _role == "host" and not _expect(_game.bind_player(1, "local_host") == OK, "Bind the listen host's local player."):
		return false
	if not _expect(_runtime.get_local_player_id() == ("local_host" if _role == "host" else ""), "Verify whether authority has a local player.") \
			or not _initialize_characters():
		return false
	if not _expect(_reads(_alpha, HEALTH, 75.0, true)
			and _reads(_alpha, STAMINA, 20.0, true) and _reads(_alpha, SECRET, 42.0, true), "Read the authoritative base and current values before the baseline."):
		return false
	if not _expect(_alpha.add_base(SECRET, -2.0) == OK and _reads(_alpha, SECRET, 40.0, true)
			and _alpha.set_base(SECRET, 42.0) == OK, "Commit direct base edits through the mounted authority component."):
		return false
	_watch_alpha()
	print("CRIMSON_ATTRIBUTES_NETWORK_READY ", _role)
	if not await _wait_for(func() -> bool: return _peers.size() == 2, "both admitted test players to bind"):
		return false
	var started: GameResult = await _game.start_match()
	if not _expect(started.ok, started.message) or not await _wait_for(_both_at.bind(1), "both clients to verify the baseline and the observer to attempt a denied command"):
		return false
	if not _expect(_authorized_commands == 0 and _changes.is_empty() and _reads(_alpha, HEALTH, 75.0, true), "The denied command did not edit or publish attributes.") \
			or not _exercise_rollback():
		return false
	if not _expect(rpc_id(_peers["owner"], &"_open_strike") == OK, "Allow the owner to send the strike command.") \
			or not await _wait_for(_both_at.bind(2), "both clients to read the committed health delta") \
			or not _expect(_authorized_commands == 1 and _changes == [65.0] and _monitor_reads_match,
				"The authority published one committed health update after the owned strike."):
		return false
	var transactions: TransactionCoordinator = _game.transactions()
	var scope_change: GameTransaction = transactions.begin()
	if not _expect(_service.queue_visibility(scope_change, ALPHA, "owner", PackedStringArray()) == OK
			and transactions.commit(scope_change).get("ok", false), "Commit removal of the observer's visibility."):
		return false
	if not await _wait_for(_both_at.bind(3), "the revoked observer to lose Alpha and the owner to retain Alpha"):
		return false
	var traveled: GameResult = await _game.scenes().load(cobalt)
	if not _expect(traveled.ok, traveled.message) or not await _wait_for(_both_at.bind(4), "both clients to verify the next world baseline"):
		return false
	if not _expect(_service.remove_character(ALPHA) == OK and _alpha.get_attribute(HEALTH) == null,
			"Remove the character and retire its committed Attributes rows.") \
			or not await _wait_for(_both_at.bind(5), "clients to verify the final visibility"):
		return false
	for role: String in ["observer", "owner"]:
		var peer: int = _peers[role]
		if not _expect(rpc_id(peer, &"_release_clients") == OK, "Confirm the final assertion before the client disconnects."):
			return false
		if not await _wait_for(func() -> bool: return not multiplayer.get_peers().has(peer), "the verified %s to close its session" % role):
			return false
	return true


func _initialize_characters() -> bool:
	var transactions: TransactionCoordinator = _game.transactions()
	var transaction: GameTransaction = transactions.begin()
	var hidden_component := AbilitySystemComponent.new()
	hidden_component.attribute_source = AbilitySystemComponent.MATCH
	hidden_component.attribute_sets = _alpha_component.attribute_sets
	var detached_binding: Error = _service.bind_component(hidden_component, "hidden")
	var hidden_queued: Error = _service.queue_add_component(transaction, hidden_component, "hidden", "absent")
	hidden_component.free()
	if not _expect(detached_binding == ERR_INVALID_PARAMETER and hidden_queued == OK,
			"A detached component supplies configuration for an authority row and binds only once mounted."):
		return false
	return _expect(_service.queue_add_component(transaction, _alpha_component, ALPHA, "owner", PackedStringArray(["observer"])) == OK
		and _service.queue_base(transaction, ALPHA, HEALTH_ID, 125.0) == OK
		and _service.queue_base(transaction, ALPHA, HEALTH_ID, -50.0, true) == OK
		and _service.queue_base(transaction, ALPHA, STAMINA_ID, 20.0) == OK
		and _service.queue_base(transaction, ALPHA, SECRET_ID, 42.0) == OK
		and transactions.commit(transaction).get("ok", false), "Create Alpha and initialize all gameplay values in one commit.")


func _run_client() -> bool:
	if not _expect(_runtime.declare_command(STRIKE) == OK, "Declare the same command envelope on the guest."):
		return false
	_watch_alpha()
	var joined: GameResult = _game.join_lobby(ADDRESS)
	if not _expect(joined.ok, joined.message) or not await _wait_for(func() -> bool: return _game.get_phase() == Game.LOBBY, "the guest to enter the admitted lobby") \
			or not _expect(_alpha.get_attribute(HEALTH) == null, "An admitted guest waits for its first confirmed attribute baseline.") \
			or not _expect(rpc_id(1, &"_announce", _role) == OK, "Report test role over the admitted connection.") \
			or not await _wait_for(func() -> bool: return _world == AMBER and _epoch > 0, "the first acknowledged world baseline"):
		return false
	var first_epoch: int = _epoch
	var owned: bool = _role == "owner"
	var view: Dictionary = _runtime.get_projection()
	if not _expect(_reads(_alpha, HEALTH, 75.0, false) and _alpha.get_attribute(SECRET) == null
			and _service.for_character("hidden") == null and _alpha.set_base(HEALTH, 99.0) == ERR_UNAUTHORIZED
			and _service.remove_character(ALPHA) == ERR_UNAUTHORIZED
			and _service.queue_add_component(null, _alpha_component, "forged") == ERR_UNAUTHORIZED,
			"Replicated current values are read-only and scoped.") \
			or not _expect(_changes == [75.0] and _monitor_reads_match, "The component watch receives the confirmed first value.") \
			or not _expect(_has_allowed_records(view, owned), "The wire projection exposes only authorized current values alongside game records."):
		return false
	if not owned:
		var denied: GameResult = await _game.perform(STRIKE)
		if not _expect(not denied.ok and denied.code == "Denied" and _reads(_alpha, HEALTH, 75.0, false), "The authority rejects the observer's strike without editing state."):
			return false
	if not _report_stage(1):
		return false
	if owned:
		if not await _wait_for(func() -> bool: return _strike_ready, "the owner command window"):
			return false
		var forged: GameResult = await _game.perform(STRIKE, {"damage": 999.0, "character": "hidden"})
		if not _expect(not forged.ok and forged.code == "Denied", "The authority rejects client-selected attribute operands and targets."):
			return false
		var accepted: GameResult = await _game.perform(STRIKE)
		if not _expect(accepted.ok, accepted.message):
			return false
	if not await _wait_for(func() -> bool: return _reads(_alpha, HEALTH, 65.0, false), "the committed health delta") \
			or not _expect(_changes == [75.0, 65.0] and _monitor_reads_match, "The component watch observes one committed update after the baseline.") \
			or not _report_stage(2):
		return false
	if owned:
		if not await _wait_for(func() -> bool: return _runtime.get_projection().get("fixture", {}).get("revision", 0) > int(view["fixture"]["revision"]) + 1,
				"the later committed visibility revision") or not _expect(_reads(_alpha, HEALTH, 65.0, false), "The owner retains access after observer revocation."):
			return false
	elif not await _wait_for(func() -> bool: return _noticed_scope_removal and not _runtime.get_projection().has(ALPHA_RECORD), "Alpha's record removal") \
			or not _expect(_alpha.get_attribute(HEALTH) == null and _service.for_character(ALPHA) == null, "The retained and newly acquired handles cannot read a revoked record."):
		return false
	if not _report_stage(3) or not await _wait_for(func() -> bool: return _world == COBALT and _epoch > first_epoch, "the new world and its acknowledged baseline") \
			or not _expect(_noticed_transition, "Previous-epoch remote readings become unavailable during travel."):
		return false
	if not _expect(_reads(_alpha, HEALTH, 65.0, false) if owned else _alpha.get_attribute(HEALTH) == null,
			"The confirmed new epoch retains the committed owner and observer policy.") or not _report_stage(4):
		return false
	if not await _wait_for(func() -> bool: return _runtime.get_projection().get("fixture", {}).get("revision", 0) > int(view["fixture"]["revision"]) + 2,
			"the final committed removal revision") \
			or not _expect(_alpha.get_attribute(HEALTH) == null and not _runtime.get_projection().has(ALPHA_RECORD), "Character removal reaches the client projection.") \
			or not _report_stage(5):
		return false
	return await _wait_for(func() -> bool: return _finish_ready, "the authority to acknowledge the final assertions")


func _exercise_rollback() -> bool:
	var transactions: TransactionCoordinator = _game.transactions()
	var before: int = transactions.get_revision()
	var transaction: GameTransaction = transactions.begin()
	if not _expect(_service.queue_base(transaction, ALPHA, HEALTH_ID, NAN) == ERR_INVALID_PARAMETER
		and _service.queue_base(transaction, ALPHA, HEALTH_ID, 6.0) == OK
		and _service.queue_base(transaction, "absent", HEALTH_ID, 9.0) == OK
		and not transactions.commit(transaction).get("ok", true)
		and transactions.get_revision() == before and _changes.is_empty() and _reads(_alpha, HEALTH, 75.0, true),
		"Reject nonfinite operands and roll back a multi-edit transaction without observer notifications."):
		return false
	var overflow: GameTransaction = transactions.begin()
	var large: float = 1.7e308
	return _expect(_service.queue_base(overflow, ALPHA, HEALTH_ID, large) == OK
		and _service.queue_base(overflow, ALPHA, HEALTH_ID, large, true) == OK
		and not transactions.commit(overflow).get("ok", true)
		and transactions.get_revision() == before and _changes.is_empty() and _reads(_alpha, HEALTH, 75.0, true),
		"Reject a nonfinite result from finite operands without publishing any staged value.")


func _perform_strike(player_id: String, intent: Dictionary) -> Dictionary:
	if player_id != "owner" or not Dictionary(intent.get("payload", {})).is_empty():
		return {"ok": false, "code": "Denied", "message": "This action is reserved for Alpha's owner with an empty intent."}
	var transaction: GameTransaction = _game.transactions().begin()
	if _service.queue_base(transaction, ALPHA, HEALTH_ID, -10.0, true) != OK:
		return {"ok": false, "code": "CommitFailed", "message": "Could not stage the authority damage."}
	var result: Dictionary = _game.transactions().commit(transaction)
	if result.get("ok", false):
		_authorized_commands += 1
	return result


func _project_game_record(_peer_id: int, _player_id: String, epoch: int, revision: int) -> Dictionary:
	return {"ok": true, "view": {"fixture": {"epoch": epoch, "revision": revision}}}


func _has_allowed_records(view: Dictionary, owned: bool) -> bool:
	if not view.has("fixture") or not view.get(SCHEMA) is String or not view.has(ALPHA_RECORD) \
			or view.has("crimson.attributes/hidden"):
		return false
	var alpha_row: Dictionary = view[ALPHA_RECORD]
	return alpha_row.size() == (2 if owned else 1) and alpha_row.has(String(HEALTH_ID)) \
		and alpha_row.has(String(STAMINA_ID)) == owned and not alpha_row.has(String(SECRET_ID)) \
		and (_reads(_alpha, STAMINA, 20.0, false) if owned else _alpha.get_attribute(STAMINA) == null)


func _watch_alpha() -> void:
	_health_watch = _alpha.watch_attribute(HEALTH)
	_health_watch.changed.connect(_record_health)


func _record_health(previous: GameplayAttributeValue, current: GameplayAttributeValue) -> void:
	if current != null:
		_changes.append(current.get_current())
		_monitor_reads_match = _monitor_reads_match and _reads(_alpha, HEALTH, current.get_current(), current.has_base())
	elif previous != null and _game.get_phase() == Game.RUNNING:
		_noticed_scope_removal = true


func _reads(handle: GameplayAttributeSet, attribute: StringName, number: float, base_visible: bool) -> bool:
	var value: GameplayAttributeValue = handle.get_attribute(attribute)
	return value != null and value.get_current() == number and value.has_base() == base_visible \
		and (not base_visible or value.get_base() == number)


func _on_phase_changed(phase: int) -> void:
	if phase == Game.TRANSITIONING and _alpha != null and (_role == "owner" or _role == "observer"):
		_noticed_transition = _alpha.get_attribute(HEALTH) == null


func _on_world_ready(world: StringName, epoch: int) -> void:
	_world = world
	_epoch = epoch


# These test-only signals coordinate assertions and use the actual admitted sender.
@rpc("any_peer", "call_remote", "reliable")
func _announce(role: String) -> void:
	var sender: int = multiplayer.get_remote_sender_id()
	var allowed: bool = role == "mismatch-client" if _role == "mismatch-host" else role in ["owner", "observer"]
	if multiplayer.is_server() and sender > 1 and allowed and not _peers.has(role) \
			and not _peers.values().has(sender) and _game.bind_player(sender, role) == OK:
		_peers[role] = sender
		_stages[role] = 0


@rpc("any_peer", "call_remote", "reliable")
func _milestone(role: String, stage: int) -> void:
	if multiplayer.is_server() and _peers.get(role, 0) == multiplayer.get_remote_sender_id() \
			and _stages.has(role) and stage == _stages[role] + 1:
		_stages[role] = stage


@rpc("authority", "call_remote", "reliable")
func _open_strike() -> void:
	_strike_ready = true


@rpc("authority", "call_remote", "reliable")
func _release_clients() -> void:
	_finish_ready = true


func _both_at(stage: int) -> bool:
	return _stages.get("owner", 0) >= stage and _stages.get("observer", 0) >= stage


func _report_stage(stage: int) -> bool:
	return _expect(rpc_id(1, &"_milestone", _role, stage) == OK, "Report the verified client stage %s." % stage)


func _wait_for(condition: Callable, detail: String) -> bool:
	var deadline: int = Time.get_ticks_msec() + TIMEOUT_MS
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	return _expect(bool(condition.call()), "Wait for %s." % detail)


func _expect(condition: bool, detail: String) -> bool:
	if not condition:
		push_error("Attributes network fixture %s: %s" % [_role, detail])
	return condition
