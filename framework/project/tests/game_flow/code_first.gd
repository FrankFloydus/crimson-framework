# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises code-first boot, typed actions, service isolation, and attachment retirement.

const LEVEL: PackedScene = preload("res://tests/game_flow/fixtures/start_world.tscn")
const PLAYER: PackedScene = preload("res://tests/game_flow/fixtures/start_player.tscn")


class CountingProvider extends GameProvider:
	## Number of times this provider supplied the game's transport.
	var created := 0

	## Supply the same direct transport through the typed factory seam.
	func _register(services: GameBindings) -> void:
		services.session_transport(_transport)

	## Resolve a fresh transport for each attachment.
	func _transport() -> SessionTransport:
		created += 1
		return ENetSessionTransport.new()


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var owner := Node.new()
	owner.name = "CodeFirst"
	root.add_child(owner)
	var menu := Node.new()
	owner.add_child(menu)
	var provider := CountingProvider.new()
	var setup := Game.configure(LEVEL, PLAYER).lan(2).on_port(24911).provider(provider)
	var game := setup.boot(owner)
	if not game.boot_result().ok or provider.created != 1 or game.get_port() != 24911:
		_fail("Code-first boot did not resolve the selected factory and settings.")
		return
	if Game.current() != game or Game.for_node(menu) != game or owner.get_child_count() != 1:
		_fail("The exact game facade or hidden engine owner could not be resolved.")
		return

	var rejected := setup.boot(owner)
	var rejected_play: GameResult = await rejected.play()
	if rejected.boot_result().ok or rejected_play.code != rejected.boot_result().code or Game.current() != game:
		_fail("Rejected boot published a second scope or lost its diagnostic.")
		return
	var invalid_owner := Node.new()
	root.add_child(invalid_owner)
	var invalid := Game.campaign(WorldDefinition.at(&"bad", "not-a-local-scene")).boot(invalid_owner)
	if invalid.boot_result().ok or Game.current() != game:
		_fail("An invalid world catalog published a scope.")
		return
	invalid_owner.queue_free()

	var started: GameResult = await game.play()
	var actor := game.get_local_player()
	if not started.ok or actor == null or Game.for_node(actor) != game:
		_fail("Solo did not settle with a typed success and an owned actor.")
		return
	if actor.get_node("Visuals").visible or not actor.get_node("Camera3D").current:
		_fail("The ordinary camera and local-visual convention was not applied.")
		return
	var left: GameResult = await game.leave()
	if not left.ok or game.get_world() != null:
		_fail("Leave did not settle after the world retired.")
		return

	var other_owner := Node.new()
	root.add_child(other_owner)
	var other := Game.configure(LEVEL, PLAYER).boot(other_owner)
	if not other.boot_result().ok or Game.current() != null or Game.set_default(game) != OK or Game.current() != game:
		_fail("Multiple game attachments were not isolated.")
		return
	Game.set_default(null)
	root.remove_child(owner)
	var expired: GameResult = await game.play()
	if expired.ok or Game.current() != other:
		_fail("Detaching the owner did not retire its original handle.")
		return
	root.add_child(owner)
	var reopened := setup.boot(owner)
	if not reopened.boot_result().ok or reopened == game or provider.created != 2:
		_fail("Reattaching the owner could not create a fresh code-first scope.")
		return

	print("CRIMSON_CODE_FIRST_PASSED")
	quit()


func _fail(message: String) -> void:
	push_error(message)
	quit(1)
