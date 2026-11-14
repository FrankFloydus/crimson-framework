# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises the public typed serializer and both standalone and game-scoped codec catalogs.

const LEVEL: PackedScene = preload("res://tests/game_flow/fixtures/start_world.tscn")
const PLAYER: PackedScene = preload("res://tests/game_flow/fixtures/start_player.tscn")
const AMMO_CODEC: StringName = &"weapon.ammo"
const CODEC_VERSION: int = 1

## Count failed expectations even when running with a Release addon.
var _failures: int = 0


## One game-owned message type, reconstructed by its codec.
class AmmoUpdate:
	## Bullets currently in the magazine.
	var rounds: int
	## Whether the gun is waiting for a reload.
	var reloading: bool

	## Store the fields used by the weapon state display.
	func _init(round_count: int, is_reloading: bool) -> void:
		rounds = round_count
		reloading = is_reloading


## Defer scoped game boot until the root can accept new owners.
func _initialize() -> void:
	_run.call_deferred()


## Check all codec operations and report the fixture outcome to the harness.
func _run() -> void:
	_check_fields()
	_check_invalid_fields()
	_check_registry()
	await _check_game_scope()
	if _failures == 0:
		print("CRIMSON_NETWORK_CODEC_TESTS_PASSED")
	quit(int(_failures != 0))


## Check exact bytes and all field widths against a round-trip decode.
func _check_fields() -> void:
	var writer: NetworkWriter = NetworkWriter.create()
	_expect(writer.write_bool(true) == OK, "Write a boolean")
	_expect(writer.write_u8(255) == OK and writer.write_u16(0x1234) == OK, "Write 8-bit and 16-bit unsigned fields")
	_expect(writer.write_u32(0xffffffff) == OK, "Write the largest unsigned 32-bit field")
	_expect(writer.write_i8(-128) == OK and writer.write_i16(-32768) == OK, "Write the smallest signed narrow fields")
	_expect(writer.write_i32(-2147483648) == OK and writer.write_i64(-0x123456789abcdef) == OK, "Write signed wide fields")
	_expect(writer.write_f32(1.5) == OK and writer.write_f64(-2.25) == OK, "Write finite floats")
	_expect(writer.write_string("caffè ☕") == OK and writer.write_string("") == OK, "Write Unicode and empty strings")
	var blob: PackedByteArray = PackedByteArray([0, 255, 17])
	_expect(writer.write_bytes(blob) == OK and writer.write_bytes(PackedByteArray()) == OK, "Write binary and empty fields")
	var bytes: PackedByteArray = writer.finish()
	_expect(bytes.slice(0, 8) == PackedByteArray([1, 255, 0x34, 0x12, 255, 255, 255, 255]), "Match canonical little-endian bytes")
	_expect(writer.get_size() == bytes.size() and writer.get_error() == OK, "Report the sealed byte count")
	_expect(writer.write_u8(9) == ERR_LOCKED and writer.finish() == bytes, "Keep sealed bytes stable")
	var reader: NetworkReader = NetworkReader.open(bytes)
	bytes[0] = 0
	_expect(reader.read_bool() and writer.finish()[0] == 1, "Preserve reader and sealed writer bytes after caller edits")
	_expect(reader.read_u8() == 255 and reader.read_u16() == 0x1234, "Round-trip narrow unsigned values")
	_expect(reader.read_u32() == 0xffffffff, "Round-trip the full unsigned 32-bit range")
	_expect(reader.read_i8() == -128 and reader.read_i16() == -32768, "Round-trip narrow signed values")
	_expect(reader.read_i32() == -2147483648 and reader.read_i64() == -0x123456789abcdef, "Round-trip wide signed values")
	_expect(reader.read_f32() == 1.5 and reader.read_f64() == -2.25, "Round-trip binary32 and binary64")
	_expect(reader.read_string(10) == "caffè ☕" and reader.read_string() == "", "Round-trip Unicode and empty strings")
	_expect(reader.read_bytes(3) == blob and reader.read_bytes(0).is_empty(), "Round-trip binary and empty fields")
	_expect(reader.get_remaining() == 0 and reader.finish() == OK, "Require exact payload consumption")


## Reject malformed wire values, invalid budgets, and writes past capacity.
func _check_invalid_fields() -> void:
	var overflow: NetworkWriter = NetworkWriter.create(1)
	_expect(overflow.write_u16(5) == ERR_OUT_OF_MEMORY, "Reject a write beyond the byte budget")
	_expect(overflow.get_size() == 0 and overflow.write_u8(5) == ERR_OUT_OF_MEMORY, "Keep the first error and the cursor")
	_expect(overflow.finish().is_empty() and overflow.get_error() == ERR_OUT_OF_MEMORY, "Never expose a partial failed payload")
	var narrow: NetworkWriter = NetworkWriter.create()
	_expect(narrow.write_i8(128) == ERR_PARAMETER_RANGE_ERROR, "Reject narrowing an out-of-range integer")
	_expect(NetworkWriter.create(-1).get_error() == ERR_PARAMETER_RANGE_ERROR, "Reject a negative budget")
	_expect(NetworkWriter.create().write_f32(1.0e100) == ERR_PARAMETER_RANGE_ERROR, "Reject binary32 overflow")
	_expect(NetworkWriter.create().write_f64(INF) == ERR_PARAMETER_RANGE_ERROR, "Reject non-finite writes")
	_expect(NetworkWriter.create().write_string(String.chr(0xfeff) + "abc") == ERR_INVALID_DATA, "Reject leading byte-order marks on write")
	_expect(NetworkReader.open(PackedByteArray([1]), 0).get_error() == ERR_INVALID_DATA, "Reject oversized input before reading")
	var trailing: NetworkReader = NetworkReader.open(PackedByteArray([1, 2]))
	_expect(trailing.read_bool() and trailing.get_remaining() == 1 and trailing.finish() == ERR_INVALID_DATA, "Reject trailing bytes")
	var invalid_bool: NetworkReader = NetworkReader.open(PackedByteArray([2]))
	_expect(not invalid_bool.read_bool() and invalid_bool.finish() == ERR_INVALID_DATA, "Reject noncanonical booleans")
	var invalid_float: NetworkReader = NetworkReader.open(PackedByteArray([0, 0, 0x80, 0x7f]))
	_expect(invalid_float.read_f32() == 0.0 and invalid_float.finish() == ERR_INVALID_DATA, "Reject non-finite wire values")
	for malformed: PackedByteArray in [
		PackedByteArray([4, 0, 0, 0, 1]),
		PackedByteArray([2, 0, 0, 0, 0xc0, 0x80]),
		PackedByteArray([3, 0, 0, 0, 0xef, 0xbb, 0xbf]),
		PackedByteArray([2, 0, 0, 0, 0x41, 0]),
	]:
		var reader: NetworkReader = NetworkReader.open(malformed)
		_expect(reader.read_string().is_empty() and reader.finish() == ERR_INVALID_DATA, "Reject malformed UTF-8 or truncated string")
	var oversized_field: NetworkReader = NetworkReader.open(PackedByteArray([2, 0, 0, 0, 33, 44]))
	_expect(oversized_field.read_bytes(1).is_empty() and oversized_field.finish() == ERR_INVALID_DATA, "Reject a field beyond its own limit")


## Verify custom callbacks, immutable encode results, version isolation, and explicit schema rejection.
func _check_registry() -> void:
	var codecs: NetworkCodecs = NetworkCodecs.new()
	_expect(codecs.register_codec(AMMO_CODEC, CODEC_VERSION, _encode_ammo, _decode_ammo) == OK, "Register typed game callbacks")
	_expect(codecs.register_codec(AMMO_CODEC, CODEC_VERSION, _encode_ammo, _decode_ammo) == ERR_ALREADY_EXISTS, "Reject duplicate schemas")
	_expect(codecs.register_codec(&"space invalid", CODEC_VERSION, _encode_ammo, _decode_ammo) == ERR_INVALID_PARAMETER, "Reject invalid codec names")
	_expect(codecs.register_codec(&"weapon.invalid", CODEC_VERSION, _decode_ammo, _decode_ammo) == ERR_INVALID_PARAMETER, "Reject callback arity mismatches")
	var result: NetworkEncodeResult = codecs.encode(AMMO_CODEC, CODEC_VERSION, AmmoUpdate.new(15, true))
	_expect(result.ok and result.error == OK and result.bytes == PackedByteArray([15, 0, 1]), "Use the custom encoder")
	var changed: PackedByteArray = result.bytes
	changed[0] = 20
	_expect(result.bytes[0] == 15, "Keep result bytes detached from caller edits")
	var decoded: NetworkDecodeResult = codecs.decode(AMMO_CODEC, CODEC_VERSION, result.bytes)
	var update: AmmoUpdate = decoded.value as AmmoUpdate
	_expect(decoded.ok and update != null and update.rounds == 15 and update.reloading, "Restore the registered game type")
	_expect(codecs.encode(AMMO_CODEC, CODEC_VERSION + 1, AmmoUpdate.new(0, false)).error == ERR_DOES_NOT_EXIST, "Resolve exact schema versions")
	_expect(codecs.encode(AMMO_CODEC, CODEC_VERSION, AmmoUpdate.new(15, true), 2).error == ERR_OUT_OF_MEMORY, "Propagate writer failure despite the callback ignoring it")
	_expect(codecs.decode(AMMO_CODEC, CODEC_VERSION, PackedByteArray([15, 0])).error == ERR_INVALID_DATA, "Reject a truncated custom payload")
	_expect(codecs.decode(AMMO_CODEC, CODEC_VERSION, PackedByteArray([15, 0, 1, 0])).error == ERR_INVALID_DATA, "Reject unread custom payload bytes")
	_expect(codecs.decode(AMMO_CODEC, CODEC_VERSION, PackedByteArray([255, 255, 1])).error == ERR_INVALID_DATA, "Respect schema validation by the callback")
	_expect(codecs.register_codec(&"weapon.bad_decoder", CODEC_VERSION, _encode_ammo, _decode_bad_type) == OK, "Resolve callable types when invoked")
	_expect(codecs.decode(&"weapon.bad_decoder", CODEC_VERSION, PackedByteArray()).error == ERR_INVALID_DECLARATION, "Distinguish callback invocation failure from successful null")
	_expect(codecs.register_codec(&"weapon.empty", CODEC_VERSION, _encode_empty, _decode_empty) == OK, "Register a nullable schema")
	var empty: NetworkEncodeResult = codecs.encode(&"weapon.empty", CODEC_VERSION, null, 0)
	_expect(empty.ok and empty.bytes.is_empty(), "Accept a successful empty payload")
	var nullable: NetworkDecodeResult = codecs.decode(&"weapon.empty", CODEC_VERSION, empty.bytes, 0)
	_expect(nullable.ok and nullable.value == null, "Distinguish successful null from failure")


## Retire a captured game codec service when its original scene attachment leaves.
func _check_game_scope() -> void:
	var owner: Node = Node.new()
	owner.name = "CodecFixture"
	root.add_child(owner)
	var game: Game = Game.configure(LEVEL, PLAYER).boot(owner)
	var codecs: NetworkCodecs = game.codecs()
	_expect(game.boot_result().ok and codecs != null and codecs == game.codecs(), "Resolve one registry from a live game")
	if codecs == null:
		owner.queue_free()
		return
	_expect(codecs.register_codec(AMMO_CODEC, CODEC_VERSION, _encode_ammo, _decode_ammo) == OK, "Register a codec in one game")
	var other: Node = Node.new()
	other.name = "OtherCodecFixture"
	root.add_child(other)
	var other_game: Game = Game.configure(LEVEL, PLAYER).boot(other)
	_expect(other_game.codecs() != null and other_game.codecs() != codecs, "Isolate catalogs across game scopes")
	_expect(other_game.codecs().decode(AMMO_CODEC, CODEC_VERSION, PackedByteArray()).error == ERR_DOES_NOT_EXIST, "Keep registrations out of other game scopes")
	var started: GameResult = await game.play()
	var ended: GameResult = await game.leave()
	_expect(started.ok and ended.ok and game.codecs() == codecs, "Retain the catalog across session leave")
	owner.queue_free()
	await process_frame
	_expect(game.codecs() == null, "Retire the game registry accessor when the owner leaves")
	_expect(codecs.encode(AMMO_CODEC, CODEC_VERSION, AmmoUpdate.new(0, false)).error == ERR_UNAVAILABLE, "Reject work on a retained retired catalog")
	other.queue_free()


## Encode a small weapon update in schema field order.
func _encode_ammo(writer: NetworkWriter, update: AmmoUpdate) -> Error:
	writer.write_u16(update.rounds)
	writer.write_bool(update.reloading)
	return OK


## Decode the weapon update and reject a magazine larger than the fixture permits.
func _decode_ammo(reader: NetworkReader) -> AmmoUpdate:
	var rounds: int = reader.read_u16()
	var reloading: bool = reader.read_bool()
	if rounds > 60:
		reader.fail()
	return AmmoUpdate.new(rounds, reloading)


## A message with no payload is a valid custom codec result.
func _encode_empty(_writer: NetworkWriter, _value: Variant) -> Error:
	return OK


## Demonstrate that null can be a legitimate decoded value.
func _decode_empty(_reader: NetworkReader) -> Variant:
	return null


## Deliberately use an incompatible parameter type to exercise engine call errors.
func _decode_bad_type(_reader: Node) -> Variant:
	return null


## Keep the diagnostic active even in Release mode.
func _expect(condition: bool, description: String) -> void:
	if not condition:
		_failures += 1
		push_error(description)
