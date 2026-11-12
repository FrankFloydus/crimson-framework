# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies the Steam warning hook's scope and teardown without provoking SDK failures.
extends SceneTree

## Spacewar is Valve's shared development AppID.
const SPACEWAR := 480

## Counts failed assertions in this process.
var _failures := 0


## Run after the root can own both Steam client nodes.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the public diagnostics API against the selected SDK/runtime configuration.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	var debug_option := arguments.find("-debug_steamapi")
	if debug_option >= 0:
		arguments.remove_at(debug_option)
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose the Steam diagnostics fixture mode spacewar, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	var detached := SteamDiagnostics.new()
	detached.stop()
	_check(detached.start() == ERR_UNAVAILABLE and _is_empty(detached.drain()),
		"Detached diagnostics returns unavailable on start and an empty read-only batch on drain.")
	var steam := SteamClient.new()
	root.add_child(steam)
	var started := steam.start(SPACEWAR)
	match mode:
		"spacewar":
			if started == OK:
				await _check_spacewar(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.diagnostics() == null,
				"A missing Steam runtime leaves the client and diagnostics detached.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.diagnostics() == null,
				"An SDK-free build keeps the diagnostics methods available on the detached type.")

	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_DIAGNOSTICS_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Verify exclusivity, live callback output if present, and automatic unhook on client stop.
func _check_spacewar(steam: SteamClient) -> void:
	var second := SteamClient.new()
	root.add_child(second)
	if second.start() != OK:
		_check(false, "A second Steam scope could not start.")
		second.free()
		return
	var owner := steam.diagnostics()
	var contender := second.diagnostics()
	if owner == null or contender == null:
		_check(false, "Both Steam scopes expose diagnostics.")
		second.free()
		return
	_check(_is_empty(owner.drain()), "Diagnostics starts disabled.")
	_check(owner.start() == OK, "The first scope registers the live Steam warning hook.")
	_check(owner.start() == OK, "Starting capture twice on the same service is idempotent.")
	_check(contender.start() == ERR_BUSY, "A competing client receives a busy error.")
	contender.stop()
	_check(contender.start() == ERR_BUSY and _is_empty(contender.drain()),
		"The first scope retains exclusive capture after the contender stops and drains.")

	for _frame in range(3):
		await process_frame
		steam.utils().ui_language()
		steam.apps().current_language()
	var batch := owner.drain()
	_check_batch(batch)

	owner.stop()
	owner.stop()
	_check(_is_empty(owner.drain()), "Repeated stop releases and clears the warning hook.")
	_check(contender.start() == OK, "The second scope can claim the released warning hook.")
	second.stop()
	_check(contender.start() == ERR_UNAVAILABLE and _is_empty(contender.drain()),
		"Client stop unhooks and retires its retained facade.")
	_check(owner.start() == OK,
		"The first scope can reclaim the hook while its shared Steam runtime remains active.")
	_check(second.start() == OK, "The second client can start a fresh scope.")
	var restarted := second.diagnostics()
	_check(restarted != null and restarted != contender and restarted.start() == ERR_BUSY,
		"The second client's fresh scope gets a distinct service and observes the existing owner.")
	owner.stop()
	if restarted != null:
		_check(restarted.start() == OK, "The fresh scope can take the released hook.")
		contender.stop()
		_check(owner.start() == ERR_BUSY and contender.start() == ERR_UNAVAILABLE,
			"The retired service cannot release the hook on its former client.")
	second.stop()
	_check(owner.start() == OK, "The first scope can capture after the second scope stops again.")
	steam.stop()
	_check(owner.start() == ERR_UNAVAILABLE and _is_empty(owner.drain()),
		"Stopping the last client clears its hook before releasing the runtime.")
	_check(batch.messages.is_read_only(), "A captured batch remains readable after both scopes stop.")
	second.free()


## Check any observed Steam messages and log only the count, including zero.
func _check_batch(batch: SteamDiagnosticBatch) -> void:
	var messages := batch.messages
	_check(messages.is_read_only() and messages.size() <= 32 and batch.dropped_count >= 0,
		"The drained diagnostics batch is read-only and bounded.")
	for message in messages:
		var summary := "Steam SDK diagnostic."
		match message.severity:
			0: summary = "Steam SDK information."
			1: summary = "Steam SDK warning."
		_check(message.text == summary, "The typed warning record contains a fixed description.")
	print("CRIMSON_STEAM_DIAGNOSTICS_MESSAGES count=%d dropped=%d" % [messages.size(), batch.dropped_count])


## Match the normal empty response for an inactive or non-owner scope.
func _is_empty(batch: SteamDiagnosticBatch) -> bool:
	return batch.messages.is_read_only() and batch.messages.is_empty() and batch.dropped_count == 0


## Report a test failure without writing warning content or account data to the log.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
