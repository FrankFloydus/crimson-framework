# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises a game-owned online provider with a real listen host and independent guests.
extends SceneTree

const ADDRESS := "127.0.0.1"
const LISTING := "crimson-loopback-fixture"


## Test-only directory shared through an isolated harness folder.
class FixtureDirectory extends OnlineDirectory:
	var game: GameRuntime
	var listing_path: String
	var publication_mode := "normal"
	var held: OnlineDirectoryRequest
	var retired: Array[String] = []
	var left: Array[String] = []
	var published_while_closed := false

	func _publish(endpoint: SessionEndpoint, world_id: StringName, request: OnlineDirectoryRequest) -> void:
		var session := game.get_node(^"Session") as GameSession
		published_while_closed = game.get_world() != null and game.get_phase() == GameRuntime.TRANSITIONING \
				and not session.is_accepting_connections()
		if publication_mode == "hold":
			held = request
			return
		if publication_mode == "inline-cancel":
			if game.leave() == OK:
				request.publish("inline-retired-fixture")
			else:
				request.fail()
			return
		if publication_mode == "fail" or endpoint == null or world_id.is_empty():
			request.fail()
			return
		var file := FileAccess.open(listing_path, FileAccess.WRITE)
		if file == null:
			request.fail()
			return
		file.store_16(endpoint.get_port())
		file.close()
		request.publish(LISTING)

	func _resolve(listing_id: String, request: OnlineDirectoryRequest) -> void:
		if listing_id == "pending-fixture":
			held = request
			return
		var file := FileAccess.open(listing_path, FileAccess.READ) if listing_id == LISTING else null
		if file == null or file.get_length() != 2:
			request.fail()
			return
		request.resolve(SessionEndpoint.direct(ADDRESS, file.get_16()))

	func _unpublish(listing_id: String) -> void:
		retired.append(listing_id)
		if listing_id == LISTING:
			DirAccess.remove_absolute(listing_path)

	func _leave(listing_id: String) -> void:
		left.append(listing_id)


## Verifies a fixture-only proof with an ephemeral key provisioned by the isolated harness.
class FixtureIdentity extends SessionAuthenticator:
	var key: PackedByteArray
	var deny_local := false
	var denials := 0

	func _make_proof(_peer_id: int, authority: bool, challenge: PackedByteArray) -> PackedByteArray:
		var proof := _sign(authority, challenge)
		if deny_local and not proof.is_empty():
			proof[0] = proof[0] ^ 1
		return proof

	func _verify(request: SessionAuthRequest) -> void:
		var expected := _sign(not request.is_authority(), request.get_challenge())
		if not Crypto.new().constant_time_compare(expected, request.get_proof()):
			denials += 1
			request.reject()
			return
		var subject := "fixture-guest" if request.is_authority() else "fixture-host"
		request.accept(SessionIdentity.verified(&"fixture", subject))

	func _sign(authority: bool, challenge: PackedByteArray) -> PackedByteArray:
		var input := ("crimson-online-authority" if authority else "crimson-online-guest").to_utf8_buffer()
		input.append_array(challenge)
		return Crypto.new().hmac_digest(HashingContext.HASH_SHA256, key, input)


## Installs the two public service hooks from the game project.
class FixtureProvider extends GameServiceProvider:
	var directory := FixtureDirectory.new()
	var identity := FixtureIdentity.new()

	func _create_online_directory() -> OnlineDirectory:
		return directory

	func _create_session() -> GameSession:
		var session := GameSession.new()
		if session.set_authenticator(identity) != OK:
			session.free()
			return null
		return session


var _game: GameRuntime
var _session: GameSession
var _provider := FixtureProvider.new()
var _folder: String
var _port: int


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or args[0] not in ["host", "denied", "client"]:
		push_error("Expected online fixture role, port and harness folder.")
		quit(1)
		return
	Engine.max_fps = 120
	_folder = args[2]
	_port = args[1].to_int()
	_provider.identity.key = FileAccess.get_file_as_bytes(_folder.path_join("fixture-key"))
	if _provider.identity.key.size() != 32:
		push_error("The isolated fixture needs a 32-byte proof key.")
		quit(1)
		return
	_provider.identity.deny_local = args[0] == "denied"
	_provider.directory.listing_path = _folder.path_join("listing")
	var main := load(String(ProjectSettings.get_setting("application/run/main_scene"))) as PackedScene
	var demo := main.instantiate()
	_game = demo.get_node(^"Game") as GameRuntime
	_provider.directory.game = _game
	var definition := _game.definition
	# The guests intentionally author another port. Their directory route must win.
	var local_port := _port if args[0] == "host" else (_port % 65535) + 1
	_game.definition = definition.with_provider(_provider).with_multiplayer(definition.multiplayer_rules.on_port(local_port))
	root.add_child(demo)
	_session = _game.get_node(^"Session") as GameSession
	var passed := false
	match args[0]:
		"host": passed = await _run_host()
		"denied": passed = await _run_denied()
		"client": passed = await _run_client()
	if is_instance_valid(_game):
		_game.leave()
	if passed:
		print("CRIMSON_DEMO_ONLINE_PASSED %s" % args[0])
	quit(0 if passed else 1)


func _run_host() -> bool:
	var directory := _provider.directory
	directory.publication_mode = "fail"
	var failed: Dictionary = await _game.host_online(ADDRESS, ADDRESS).wait()
	if not _check(failed.code == "ListingPublishFailed" and _game.get_phase() == GameRuntime.IDLE
			and _game.get_world() == null and _session.get_state() == GameSession.IDLE,
			"Publication failure must roll back the mounted authority and release its port."):
		return false

	directory.publication_mode = "hold"
	var pending := _game.host_online(ADDRESS, ADDRESS)
	if not await _wait_until(func() -> bool: return directory.held != null, "held publication"):
		return false
	if not _check(_game.leave() == OK and pending.get_result().code == "Cancelled"
			and directory.held.is_cancelled() and directory.held.publish("retired-fixture") == OK
			and directory.retired == ["retired-fixture"] and _game.get_world() == null,
			"Cancellation must compensate exactly one late successful publication."):
		return false
	directory.held = null

	directory.publication_mode = "inline-cancel"
	var inline_cancelled: Dictionary = await _game.host_online(ADDRESS, ADDRESS).wait()
	# The cancelled result can be emitted inside the provider, before its stale result is compensated.
	await process_frame
	if not _check(inline_cancelled.code == "Cancelled" and _game.get_world() == null
			and directory.retired == ["retired-fixture", "inline-retired-fixture"],
			"A provider that cancels and completes inline must not resurrect the previous world or listing."):
		return false

	directory.publication_mode = "normal"
	var started: Dictionary = await _game.host_online(ADDRESS, ADDRESS).wait()
	if not _check(started.ok and started.listing_id == LISTING and _game.get_online_listing() == LISTING
			and directory.published_while_closed and _session.is_accepting_connections(),
			"Publish only after the scene mounts, then open authority admission."):
		return false
	var world := _game.get_world()
	var epoch := _game.get_world_epoch()
	if not _mark("host-ready") or not await _wait_until(_marked.bind("denied-done"), "denied guest"):
		return false
	if not _check(_provider.identity.denials == 1 and _session.get_connected_peer_ids().is_empty()
			and world.get_node(^"Players").get_child_count() == 1, "Denied identity must not enter the roster or scene."):
		return false
	if not _mark("denial-checked") or not await _wait_until(_marked.bind("client-ready"), "authenticated late guest"):
		return false
	var peers := _session.get_connected_peer_ids()
	if not _check(peers.size() == 1 and _game.get_world() == world and _game.get_world_epoch() == epoch,
			"Late join must preserve the mounted authority world and its epoch."):
		return false
	var identity := _session.get_peer_identity(peers[0])
	if not _check(identity != null and identity.get_provider() == &"fixture" and identity.get_subject() == "fixture-guest"
			and _game.get_player(peers[0]) != null, "Authority must bind the admitted actor to the verified connection."):
		return false
	if not _mark("client-checked") or not await _wait_until(_marked.bind("client-left"), "guest leave"):
		return false
	if not await _wait_until(func() -> bool: return _session.get_connected_peer_ids().is_empty(), "retired connection"):
		return false
	if not _check(_session.get_peer_identity(peers[0]) == null and _game.leave() == OK
			and _game.get_online_listing().is_empty()
			and directory.retired == ["retired-fixture", "inline-retired-fixture", LISTING]
			and not FileAccess.file_exists(directory.listing_path), "Teardown must retire identity and listing exactly once."):
		return false

	var restarted: Dictionary = await _game.host_online(ADDRESS, ADDRESS).wait()
	if not _check(restarted.ok and restarted.listing_id == LISTING, "The provider can reopen after leaving."):
		return false
	_game.free()
	await process_frame
	return _check(not is_instance_valid(_game) and directory.retired.count(LISTING) == 2
			and not FileAccess.file_exists(directory.listing_path),
			"Freeing the game from its completed startup must retire the published listing and survive the frame.")


func _run_denied() -> bool:
	if not await _wait_until(_marked.bind("host-ready"), "published host"):
		return false
	var denied: Dictionary = await _game.join_online(LISTING).wait()
	return _check(not denied.ok and _game.get_world() == null and _game.get_local_player() == null
			and _game.get_phase() == GameRuntime.IDLE and _provider.directory.left == [LISTING],
			"An incorrect proof must fail before world readiness and release membership.") \
			and _mark("denied-done")


func _run_client() -> bool:
	if not await _wait_until(_marked.bind("denial-checked"), "authority denial assertions"):
		return false
	var missing: Dictionary = await _game.join_online("missing").wait()
	if not _check(missing.code == "ListingUnavailable" and _session.get_state() == GameSession.IDLE,
			"Missing listing must fail before claiming the transport."):
		return false
	var pending := _game.join_online("pending-fixture")
	var held := _provider.directory.held
	if not _check(held != null and _game.leave() == OK and pending.get_result().code == "Cancelled"
			and held.resolve(SessionEndpoint.direct(ADDRESS, _port)) != OK,
			"Cancel a directory lookup and reject its stale result."):
		return false
	var changed := _game.join_online("pending-fixture")
	held = _provider.directory.held
	if not _check(_session.set_authenticator(null) == OK and held.resolve(SessionEndpoint.direct(ADDRESS, _port)) == OK,
			"Change the idle session authenticator while the lookup is pending."):
		return false
	var unverified: Dictionary = await changed.wait()
	if not _check(unverified.code == "OnlineAuthenticationRequired" and _session.get_state() == GameSession.IDLE
			and _session.set_authenticator(_provider.identity) == OK,
			"Do not open a resolved route after the selected authentication policy changes."):
		return false
	var joined: Dictionary = await _game.join_online(LISTING).wait()
	var identity := _session.get_peer_identity(1)
	if not _check(joined.ok and _game.get_world() != null and _game.get_local_player() != null
			and identity != null and identity.get_subject() == "fixture-host",
			"Join the resolved endpoint, verify the host, mount the world, and apply the player baseline."):
		return false
	return _mark("client-ready") and await _wait_until(_marked.bind("client-checked"), "authority sees the guest") \
			and _check(_game.leave() == OK
					and _provider.directory.left == ["missing", "pending-fixture", "pending-fixture", LISTING],
					"Guest releases each cancelled, failed, and completed directory membership once.") \
			and _mark("client-left")


func _wait_until(predicate: Callable, description: String) -> bool:
	var deadline := Time.get_ticks_msec() + 12000
	while not predicate.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return _check(predicate.call(), "Wait for %s." % description)


func _mark(event: String) -> bool:
	return _check(FileAccess.open(_folder.path_join(event), FileAccess.WRITE) != null, "Write %s." % event)


func _marked(event: String) -> bool:
	return FileAccess.file_exists(_folder.path_join(event))


func _check(condition: bool, description: String) -> bool:
	if not condition:
		push_error(description)
	return condition
