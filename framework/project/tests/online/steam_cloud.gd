# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies the standalone Steam Remote Storage client without changing sync settings.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480
## Time allowed for a locally cancelled read to finish inside Steam.
const DRAIN_TIMEOUT_MS := 3000
## Time allowed for Steam to drain a cancelled remote shared download.
const SHARE_DRAIN_TIMEOUT_MS := 8000

## Counts assertions that failed in this process.
var _failures := 0


## Run after the root can own Steam client nodes.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the live client and both unavailable modes through the public facade.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "app", "missing", "sdk-off"]:
		push_error("Choose the Steam Cloud fixture mode spacewar, app, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	var expected_app_id := SPACEWAR
	if mode == "app":
		expected_app_id = _authorized_app_id()
		if expected_app_id == 0:
			push_error("Cloud app mode requires CRIMSON_STEAM_CLOUD_APP_ID and CRIMSON_STEAM_CLOUD_ALLOW_SHARE=1.")
			quit(2)
			return
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.cloud() == null, "Cloud is detached before the client starts.")
	var detached := SteamCloud.new()
	_check(not detached.is_available() and detached.quota().error == ERR_UNAVAILABLE
		and detached.quota().value == null and detached.files().error == ERR_UNAVAILABLE,
		"A detached Cloud facade reports typed unavailable metadata results.")
	_check(detached.read_file("test").result().error == ERR_UNAVAILABLE
		and detached.delete_file("test") == ERR_UNAVAILABLE, "A detached Cloud facade does not submit SDK work.")
	var unavailable_batch := detached.begin_write_batch()
	var detached_handle := SteamCloudWriteBatch.new()
	_check(unavailable_batch.error == ERR_UNAVAILABLE and unavailable_batch.value == null
		and not detached_handle.is_open() and detached_handle.close() == OK,
		"Detached Cloud exposes typed unavailable batch hints and an idempotent empty handle.")
	var unavailable: SteamResult = await detached.write("test", PackedByteArray([1]))
	_check(unavailable.error == ERR_UNAVAILABLE,
		"A direct await returns the immediate typed error for an unavailable Cloud facade.")
	var shared: SteamStringResult = await detached.share("test")
	var downloaded: SteamSharedCloudFileResult = await detached.download_shared("1")
	_check(shared.error == ERR_UNAVAILABLE and shared.value.is_empty()
		and downloaded.error == ERR_UNAVAILABLE and downloaded.value == null,
		"Detached share and download return typed unavailable results.")

	var started := steam.start(expected_app_id)
	match mode:
		"spacewar":
			if _check(started == OK, "Spacewar initialization failed. %s" % steam.get_initialization_error()):
				if await _check_cloud_queries(steam.cloud()):
					_check_spacewar_write_hint(steam.cloud())
		"app":
			if _check(started == OK and steam.get_app_id() == expected_app_id,
					"The authorized Cloud app did not initialize with its expected AppID."):
				if await _check_cloud_queries(steam.cloud()):
					await _check_app(steam)
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.cloud() == null,
				"An SDK-enabled build keeps Cloud detached without its runtime.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.cloud() == null,
				"An SDK-free build registers Cloud and keeps the client service detached.")

	steam.stop()
	steam.queue_free()
	if _failures == 0:
		print("CRIMSON_STEAM_CLOUD_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Confirm explicit consent to a remote share on a canonical, non-Spacewar app ID.
func _authorized_app_id() -> int:
	if OS.get_environment("CRIMSON_STEAM_CLOUD_ALLOW_SHARE") != "1":
		return 0
	var configured := OS.get_environment("CRIMSON_STEAM_CLOUD_APP_ID")
	if not configured.is_valid_int():
		return 0
	var app_id := configured.to_int()
	return app_id if app_id > 0 and app_id <= 4294967295 and app_id != SPACEWAR and str(app_id) == configured else 0


## Check read-only metadata and immediate validation without submitting storage changes.
func _check_cloud_queries(cloud: SteamCloud) -> bool:
	if not _check(cloud != null and cloud.is_available(), "The started client exposes Remote Storage."):
		return false

	var capacity := cloud.quota()
	_check(capacity.ok and capacity.value.total_bytes >= capacity.value.available_bytes
		and capacity.value.available_bytes >= 0,
		"Steam returns a valid Cloud quota.")
	_check(cloud.files(-1).error == ERR_INVALID_PARAMETER
		and cloud.files(0, SteamCloud.MAX_PAGE_FILES + 1).error == ERR_INVALID_PARAMETER,
		"The file listing rejects out-of-range paging requests.")
	var page := cloud.files(0, 1)
	_check(page.ok and page.value.size() == mini(page.total_count, 1),
		"Steam lists a bounded page of typed file snapshots.")
	if page.ok:
		var empty := cloud.files(page.total_count, 1)
		_check(page.value.is_read_only() and empty.ok and empty.value.is_read_only() and empty.value.is_empty(),
			"File pages are read-only and paging past the end succeeds with no entries.")
	_check(cloud.read_file("../outside").result().error == ERR_INVALID_PARAMETER
		and cloud.delete_file("CON.txt") == ERR_INVALID_PARAMETER, "Reject unsafe or nonportable file names.")
	var invalid: SteamBytesResult = await cloud.read("../outside")
	_check(invalid.error == ERR_INVALID_PARAMETER and invalid.value.is_empty(),
		"A direct await returns the immediate typed error for an invalid file name.")
	_check(cloud.write_file("test", PackedByteArray()).result().error == ERR_INVALID_PARAMETER,
		"Reject a zero-byte write before any SDK submission.")
	var invalid_share: SteamStringResult = await cloud.share("../outside")
	var invalid_download: SteamSharedCloudFileResult = await cloud.download_shared("0001")
	_check(invalid_share.error == ERR_INVALID_PARAMETER and invalid_download.error == ERR_INVALID_PARAMETER
		and cloud.download_shared_file("1", SteamCloud.MAX_FILE_BYTES + 1).result().error == ERR_INVALID_PARAMETER,
		"Reject unsafe share names, noncanonical UGC handles, and oversized read limits before SDK submission.")
	return true


## Exercise an empty advisory hint on Spacewar while leaving its files and settings alone.
func _check_spacewar_write_hint(cloud: SteamCloud) -> void:
	var opened := cloud.begin_write_batch()
	if opened.error == ERR_UNAVAILABLE and opened.value == null:
		print("CRIMSON_STEAM_CLOUD_WRITE_BATCH_UNAVAILABLE spacewar")
		return
	if not _check(opened.ok and opened.value != null and opened.value.is_open(),
			"Steam opens an empty advisory batch for Spacewar. Error %d." % opened.error):
		return
	var nested := cloud.begin_write_batch()
	var closed := opened.value.close()
	if _check(nested.error == ERR_BUSY and nested.value == null and closed == OK
			and not opened.value.is_open() and opened.value.close() == OK,
			"Spacewar rejects a nested hint and ends the exact empty batch idempotently."):
		print("CRIMSON_STEAM_CLOUD_WRITE_BATCH_PASSED spacewar")


## Verify local persistence, sharing, and observer cleanup on the authorized test app.
func _check_app(steam: SteamClient) -> void:
	var cloud := steam.cloud()
	var filename := "crimson-cloud-test-%s.bin" % Crypto.new().generate_random_bytes(12).hex_encode()
	if not _check(cloud.file_info(filename).error == ERR_FILE_NOT_FOUND, "The randomly named throwaway file is unused."):
		return
	_check(cloud.read_file(filename).result().error == ERR_FILE_NOT_FOUND
		and cloud.delete_file(filename) == ERR_FILE_NOT_FOUND, "Report a missing Cloud file without creating it.")

	var payload := PackedByteArray([0, 255, 42, 0, 127, 1])
	var expected := payload.duplicate()
	var opened := cloud.begin_write_batch()
	if not _check(opened.ok and opened.value.is_open(), "Open a scoped write-batch hint for the authorized app."):
		return
	_check(cloud.begin_write_batch().error == ERR_BUSY, "A Cloud batch hint reserves the current app's native batch.")
	_check_direct_disk_batch(filename, expected)
	var submitted := cloud.write_file(filename, payload)
	payload[0] = 23
	_check(not submitted.is_finished() and submitted.result() == null and cloud.delete_file(filename.to_upper()) == ERR_BUSY,
		"An active write owns the filename, including case aliases.")
	var written: SteamResult = await submitted.wait()
	_check(opened.value.close() == OK and not opened.value.is_open() and opened.value.close() == OK,
		"Close the advisory hint after the enclosed Steam write settles.")
	if not _check(written == submitted.result() and written.ok,
			"Steam completes the local asynchronous write. Error %d. %s" % [written.error, written.message]):
		await _delete_after_drain(cloud, filename, false)
		return
	var dropped := cloud.begin_write_batch()
	if _check(dropped.ok and dropped.value.is_open(), "Open a disposable batch handle."):
		dropped = null
		var after_drop := cloud.begin_write_batch()
		if _check(after_drop.ok, "Dropping the last batch handle releases the exact native hint."):
			_check(after_drop.value.close() == OK, "Close the replacement batch hint.")

	var file := cloud.file_info(filename)
	_check(file.ok and file.value.name == filename and file.value.size_bytes == payload.size()
		and file.value.modified_at_unix > 0 and typeof(file.value.persisted) == TYPE_BOOL,
		"Steam reports the local file's size, modification time, and persistence state.")
	var read: SteamBytesResult = await cloud.read(filename)
	_check(read.ok and read.value == expected,
		"The direct await reads exactly the binary payload written.")
	var replacement := PackedByteArray([3, 0, 254])
	var replaced: SteamResult = await cloud.write(filename, replacement)
	var round_trip: SteamBytesResult = await cloud.read(filename)
	_check(replaced.ok and round_trip.ok and round_trip.value == replacement,
		"The direct await completes the overwrite and the next read sees its exact bytes.")
	await _check_shared(cloud, filename, steam.get_local_steam_id(), replacement)

	var abandoned := cloud.read_file(filename)
	if not _check(abandoned.result() == null and abandoned.cancel() == OK,
			"Cancel one local read observer before Steam reports completion."):
		await _delete_after_drain(cloud, filename, true)
		return
	var cancelled: SteamBytesResult = await abandoned.wait()
	_check(cancelled == abandoned.result() and cancelled.error == ERR_SKIP and cancelled.value.is_empty()
		and abandoned.cancel() == ERR_DOES_NOT_EXIST, "Cancellation settles exactly once.")
	_check(cloud.delete_file(filename.to_upper()) == ERR_BUSY,
		"Observation cancellation keeps the native filename reservation until SDK completion.")

	var second := SteamClient.new()
	root.add_child(second)
	if not _check(second.start() == OK, "A second scope holds the active Steam runtime."):
		await _delete_after_drain(cloud, filename, true)
		second.queue_free()
		return
	var stopped := await _read_after_drain(cloud, filename)
	var pending_after_drain := _check(stopped.result() == null,
		"A native read starts once Steam drains the cancelled predecessor.")
	var stopped_batch := cloud.begin_write_batch()
	if _check(stopped_batch.ok, "Open an advisory batch before stopping its owner."):
		_check(second.cloud().begin_write_batch().error == ERR_BUSY,
			"A batch in one scope also reserves the native hint in another scope.")
	steam.stop()
	_check(stopped.result() != null and stopped.result().error == ERR_SKIP
		and not cloud.is_available() and steam.cloud() == null, "Stopping the client detaches its pending observer and Cloud facade.")
	var other_cloud := second.cloud()
	_check(other_cloud != null and other_cloud.is_available(), "The other client keeps pumping submitted work.")
	if other_cloud != null:
		var new_batch := other_cloud.begin_write_batch()
		if _check(new_batch.ok, "Owner shutdown retires the advisory hint for other scopes."):
			if stopped_batch.ok:
				_check(not stopped_batch.value.is_open() and stopped_batch.value.close() == OK and new_batch.value.is_open(),
					"A retired handle cannot close another scope's replacement batch.")
			_check(new_batch.value.close() == OK, "Close the other scope's replacement batch.")
		if pending_after_drain:
			_check(other_cloud.delete_file(filename.to_upper()) == ERR_BUSY,
				"The native read keeps the filename reserved across client scopes after owner shutdown.")
		await _delete_after_drain(other_cloud, filename, true)
	second.queue_free()


## Exercise direct writes and deletes within the same hint using the isolated fixture directory.
func _check_direct_disk_batch(filename: String, expected: PackedByteArray) -> void:
	var path := "user://" + filename + ".auto-cloud-test"
	if not _check(not FileAccess.file_exists(path), "The direct-disk batch probe uses a new scratch file."):
		return
	var file := FileAccess.open(path, FileAccess.WRITE)
	if not _check(file != null, "Open the direct-disk scratch file during the hint."):
		return
	file.store_buffer(expected)
	file.close()
	_check(FileAccess.get_file_as_bytes(path) == expected, "The hint can enclose an ordinary local disk write.")
	_check(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK,
		"Delete the fixture's own local disk scratch file during the same hint.")


## Verify upload and bounded download by opaque handle using only the fixture's scratch bytes.
func _check_shared(cloud: SteamCloud, filename: String, owner: String, expected: PackedByteArray) -> void:
	var submitted := cloud.share_file(filename)
	if not _check(not submitted.is_finished() and cloud.delete_file(filename) == ERR_BUSY,
			"An active share reserves its source filename."):
		return
	var shared: SteamStringResult = await submitted.completed
	if not _check(submitted.is_finished() and submitted.result() == shared and shared.ok and not shared.value.is_empty(),
			"The share signal carries a successful typed handle. %s" % shared.message):
		return
	var mismatched: SteamSharedCloudFileResult = await cloud.download_shared(
		shared.value, SteamCloud.MAX_FILE_BYTES, filename + ".wrong")
	if not _check(mismatched.error == ERR_FILE_CANT_READ and mismatched.value == null,
			"A mismatched filename exposes no shared bytes and closes the SDK cache."):
		return
	var undersized: SteamSharedCloudFileResult = await cloud.download_shared(shared.value, 1, filename)
	if not _check(undersized.error == ERR_FILE_CANT_READ and undersized.value == null,
			"A shared file larger than the requested limit exposes no bytes and closes the SDK cache."):
		return
	var cancelled := cloud.download_shared_file(shared.value, expected.size(), filename)
	if not _check(not cancelled.is_finished() and cancelled.cancel() == OK and cancelled.result().error == ERR_SKIP,
			"Cancelling a shared download settles only its observer."):
		return
	var deadline := Time.get_ticks_msec() + SHARE_DRAIN_TIMEOUT_MS
	var download := cloud.download_shared_file(shared.value, expected.size(), filename)
	while download.is_finished() and download.result().error == ERR_BUSY and Time.get_ticks_msec() < deadline:
		await process_frame
		download = cloud.download_shared_file(shared.value, expected.size(), filename)
	var copied: SteamSharedCloudFileResult = await download.wait()
	_check(copied == download.result() and copied.ok and copied.value.handle == shared.value
		and copied.value.name == filename and copied.value.owner_steam_id == owner and copied.value.bytes == expected,
		"A fresh shared download returns exact metadata and bounded bytes after prior cache cleanup.")


## Poll only while Steam still holds the exact filename from a previous submission.
func _read_after_drain(cloud: SteamCloud, filename: String) -> SteamBytesOperation:
	var deadline := Time.get_ticks_msec() + DRAIN_TIMEOUT_MS
	var operation := cloud.read_file(filename)
	while operation.result() != null and operation.result().error == ERR_BUSY and Time.get_ticks_msec() < deadline:
		await process_frame
		operation = cloud.read_file(filename)
	return operation


## Remove only the test's random file once any submitted work has drained.
func _delete_after_drain(cloud: SteamCloud, filename: String, was_written: bool) -> void:
	var deadline := Time.get_ticks_msec() + DRAIN_TIMEOUT_MS
	var result := cloud.delete_file(filename)
	while result == ERR_BUSY and Time.get_ticks_msec() < deadline:
		await process_frame
		result = cloud.delete_file(filename)
	_check((result == OK or (result == ERR_FILE_NOT_FOUND and not was_written))
		and cloud.file_info(filename).error == ERR_FILE_NOT_FOUND
		and cloud.read_file(filename).result().error == ERR_FILE_NOT_FOUND,
		"Delete the throwaway file and verify local absence.")


## Report an assertion without disclosing Steam account data or stored bytes.
func _check(condition: bool, message: String) -> bool:
	if not condition:
		_failures += 1
		push_error(message)
	return condition
