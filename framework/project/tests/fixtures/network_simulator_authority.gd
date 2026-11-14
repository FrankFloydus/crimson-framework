# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Supplies an independent listen or dedicated ENet authority for the simulator fixture.

const Fixture := preload("res://tests/fixtures/network_fixture.gd")
const SimulatorSession := preload("res://demo/session/network_simulator_session.gd")
const LOOPBACK: String = "127.0.0.1"
const PORT_OPTION: String = "--crimson-port="


func _initialize() -> void:
	_run.call_deferred()


## Keep the authority running until the parent test runner stops it.
func _run() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var port: int = 0
	for argument: String in args:
		if argument.begins_with(PORT_OPTION):
			port = argument.trim_prefix(PORT_OPTION).to_int()
	var dedicated: bool = args.has("--crimson-role=server")
	if port < 1 or port > 65535 or (not dedicated and not args.has("--crimson-role=listen")):
		push_error("Use --crimson-role=listen|server and a valid --crimson-port.")
		quit(1)
		return
	Engine.max_fps = 120
	var owner := Node.new()
	owner.name = SimulatorSession.ROOT_NAME
	root.add_child(owner)
	SimulatorSession.use_authority_routes(owner)
	var game := GameConfig.from_definition(Fixture.definition()).on_port(port).player_controls().boot(owner)
	if not game.boot_result().ok:
		push_error(game.boot_result().message)
		quit(1)
		return
	var result: GameResult = await (game.serve(LOOPBACK) if dedicated else game.host(LOOPBACK))
	if not result.ok:
		push_error(result.message)
		quit(1)
		return
	print("[Crimson] READY %s %d" % ["server" if dedicated else "listen", port])
