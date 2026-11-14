# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Delay and drop local test datagrams without changing the game or ENet configuration.

const LOOPBACK := "127.0.0.1"
const DELAY_MS := 75
const JITTER_MS := 7
const LOSS_CHANCE := 0.02
const BURST_MS := 150
const LONG_BURST_MS := 1400
const MAX_PENDING := 256
const MAX_PACKET_BYTES := 4096
const MAX_RUN_MS := 30000

class Datagram:
	var due: int
	var destination: int
	var payload: PackedByteArray

	func _init(time: int, port: int, bytes: PackedByteArray) -> void:
		due = time
		destination = port
		payload = bytes


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or not args[0].is_valid_int():
		_fail("Usage: movement_conditioner.gd -- AUTHORITY_PORT MARKER_DIRECTORY MODE")
		return
	var authority := args[0].to_int()
	if authority < 1 or authority > 65535:
		_fail("The authority port is outside the UDP port range.")
		return
	var markers := args[1]
	var burst_ms := LONG_BURST_MS if args[2] == "long-outage" else BURST_MS
	var udp := PacketPeerUDP.new()
	if udp.bind(0, LOOPBACK) != OK:
		_fail("Could not bind the movement conditioner on loopback.")
		return
	var rng := RandomNumberGenerator.new()
	rng.seed = 873510
	var pending: Array[Datagram] = []
	var guest := 0
	var burst_end := 0
	var burst_complete := false
	var clean_leave := false
	var burst_dropped := 0
	var random_dropped := 0
	Engine.max_fps = 240
	var deadline := Time.get_ticks_msec() + MAX_RUN_MS
	if not _publish(markers, "relay-ready", str(udp.get_local_port())):
		return
	while not FileAccess.file_exists(markers.path_join("relay-stop")):
		var now := Time.get_ticks_msec()
		if now >= deadline:
			_fail("The movement conditioner timed out.")
			return
		if burst_end == 0 and FileAccess.file_exists(markers.path_join("burst-request")):
			burst_end = now + burst_ms
			if not _publish(markers, "burst-active", ""):
				return
		elif burst_end != 0 and not burst_complete and now >= burst_end:
			burst_complete = true
			if not _publish(markers, "burst-complete", str(burst_dropped)):
				return
		var prepare_leave := not clean_leave and FileAccess.file_exists(markers.path_join("relay-clean-leave-request"))
		if prepare_leave:
			clean_leave = true

		for index in mini(udp.get_available_packet_count(), MAX_PENDING):
			var packet := udp.get_packet()
			var sender := udp.get_packet_port()
			if udp.get_packet_ip() != LOOPBACK or packet.size() > MAX_PACKET_BYTES:
				continue
			if guest == 0 and sender != authority:
				guest = sender
			if guest == 0 or (sender != guest and sender != authority):
				continue
			if not clean_leave and sender == guest and burst_end > now:
				burst_dropped += 1
				continue
			if not clean_leave and rng.randf() < LOSS_CHANCE:
				random_dropped += 1
				continue
			if pending.size() >= MAX_PENDING:
				_fail("The movement conditioner exceeded its bounded packet queue.")
				return
			var destination := guest if sender == authority else authority
			var delay := 0 if clean_leave else DELAY_MS + rng.randi_range(-JITTER_MS, JITTER_MS)
			pending.append(Datagram.new(now + delay, destination, packet))

		var index := 0
		while index < pending.size():
			var datagram := pending[index]
			if not clean_leave and datagram.due > now:
				index += 1
				continue
			if udp.set_dest_address(LOOPBACK, datagram.destination) != OK or udp.put_packet(datagram.payload) != OK:
				_fail("The movement conditioner could not forward a datagram.")
				return
			pending.remove_at(index)
		if prepare_leave and not _publish(markers, "relay-clean-leave-ready", ""):
			return
		await process_frame

	if FileAccess.file_exists(markers.path_join("burst-request")) and (not burst_complete or burst_dropped == 0):
		_fail("The requested burst did not drop any guest traffic.")
		return
	print("Configured RTT %d ms, jitter ±%d ms each way, burst length %d ms. Dropped %d random and %d burst datagrams." %
		[DELAY_MS * 2, JITTER_MS, burst_ms, random_dropped, burst_dropped])
	print("CRIMSON_MOVEMENT_CONDITIONER_PASSED")
	quit()


func _publish(markers: String, name: String, payload: String) -> bool:
	var path := markers.path_join(name)
	var staged := path + ".pending"
	var file := FileAccess.open(staged, FileAccess.WRITE)
	if file != null:
		file.store_string(payload)
		file.close()
		if DirAccess.rename_absolute(staged, path) == OK:
			return true
	_fail("Could not publish movement conditioner marker %s." % name)
	return false


func _fail(message: String) -> void:
	push_error(message)
	quit(1)
