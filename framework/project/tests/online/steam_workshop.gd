# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises the signed-in Steam Workshop surface through the public client.
extends SceneTree

## Valve's shared development AppID. Its fixture performs read-only queries.
const SPACEWAR := 480
## Valve's bounded results per page.
const MAX_PAGE_ITEMS := 50

## Counts failed assertions.
var _failures := 0


## Run once the root can own the Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Select a Steam context, then verify the Workshop service through that context.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.is_empty() or arguments[0] not in ["spacewar", "app", "missing", "sdk-off"]:
		push_error("Choose Workshop fixture mode spacewar, app, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	var show_eula := mode == "app" and OS.get_environment("CRIMSON_STEAM_WORKSHOP_SHOW_EULA") == "1"
	var app_id := SPACEWAR
	var item_id := ""
	if mode == "app":
		app_id = _configured_app_id()
		item_id = OS.get_environment("CRIMSON_STEAM_WORKSHOP_ITEM_ID")
		var changes_item := OS.get_environment("CRIMSON_STEAM_WORKSHOP_ALLOW_MUTATIONS") == "1"
		if app_id == 0 or ((changes_item or not item_id.is_empty()) and not _is_published_id(item_id)):
			push_error("Workshop app mode needs a non-480 CRIMSON_STEAM_WORKSHOP_APP_ID and a canonical CRIMSON_STEAM_WORKSHOP_ITEM_ID for mutations.")
			quit(2)
			return
	if arguments.size() != 1 and (not show_eula or arguments.size() != 2
			or arguments[1] != "--crimson-steam-overlay=%d" % app_id):
		push_error("Supply only the Workshop mode and the exact app overlay bootstrap when requested.")
		quit(2)
		return

	var detached := SteamWorkshop.new()
	var detached_page: SteamWorkshopPageResult = await detached.query().fetch()
	var detached_item: SteamWorkshopItemResult = await detached.item("1").fetch()
	_check(detached_page.error == ERR_UNAVAILABLE and detached_page.value == null
		and detached_item.error == ERR_UNAVAILABLE and detached_item.value == null,
		"Detached Workshop methods return typed unavailable results.")
	var detached_subscribe: SteamWorkshopSubscriptionResult = await detached.item("1").subscribe()
	var detached_install: SteamWorkshopInstallResult = await detached.item("1").install()
	var detached_vote: SteamWorkshopVoteResult = await detached.item("1").vote(true)
	var detached_publish: SteamWorkshopPublicationResult = await detached.new_item().submit()
	_check(detached_subscribe.error == ERR_UNAVAILABLE and detached_install.error == ERR_UNAVAILABLE
		and detached_vote.error == ERR_UNAVAILABLE and detached_publish.error == ERR_UNAVAILABLE
		and detached.item("1").installed() == null,
		"Detached Workshop item actions settle without submitting SDK work.")
	var detached_subscriptions := detached.subscriptions()
	var detached_eula: SteamWorkshopLegalAgreementStatusResult = await detached.legal_agreement_status()
	_check(detached_subscriptions.error == ERR_UNAVAILABLE and detached_subscriptions.value == null
		and detached_eula.error == ERR_UNAVAILABLE and detached_eula.value == null
		and detached.show_legal_agreement() == ERR_UNAVAILABLE,
		"Detached Workshop account queries and overlay requests report unavailable.")

	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.workshop() == null, "Workshop is detached before startup.")
	var started := steam.start(app_id)
	match mode:
		"spacewar", "app":
			if _check(started == OK and steam.get_app_id() == app_id, "The expected Steam app started."):
				await _check_reads(steam, item_id, mode == "app", show_eula)
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.workshop() == null,
				"Workshop stays detached when the Steam runtime DLL is missing.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.workshop() == null,
				"An SDK-free build registers the Workshop API and keeps the service detached.")

	steam.stop()
	steam.queue_free()
	if _failures == 0:
		print("CRIMSON_STEAM_WORKSHOP_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Read pages and exact item details without downloading or changing account state.
func _check_reads(steam: SteamClient, known_id: String, app_mode: bool, show_eula: bool) -> void:
	var workshop := steam.workshop()
	if not _check(workshop != null, "The active client exposes the Workshop facade."):
		return
	_check_subscription_snapshot(workshop)
	var agreement: SteamWorkshopLegalAgreementStatusResult = await workshop.legal_agreement_status()
	if agreement.ok:
		_check_agreement(agreement, steam.get_app_id())
	elif app_mode or agreement.value != null or agreement.error not in [ERR_UNAVAILABLE, ERR_UNAUTHORIZED, ERR_INVALID_PARAMETER, FAILED]:
		_check(false, "Steam did not provide Workshop agreement status. Error %d." % agreement.error)
	else:
		# Steam rejects the argument-free EULA request with InvalidParam for Spacewar's configuration.
		print("CRIMSON_STEAM_WORKSHOP_EULA_STATUS_UNAVAILABLE spacewar error=%d" % agreement.error)
	if show_eula:
		await _check_legal_overlay(steam, workshop)
	var base := workshop.query()
	var invalid_tag: SteamWorkshopPageResult = await base.tagged("").fetch()
	var invalid_cursor: SteamWorkshopPageResult = await base.after("x".repeat(256)).fetch()
	var invalid_item: SteamWorkshopItemResult = await workshop.item("001").fetch()
	_check(invalid_tag.error == ERR_INVALID_PARAMETER and invalid_cursor.error == ERR_INVALID_PARAMETER
		and invalid_item.error == ERR_INVALID_PARAMETER, "Invalid tag, cursor, and item ID settle before SDK submission.")
	var invalid_subscription: SteamWorkshopSubscriptionResult = await workshop.item("001").subscribe()
	var invalid_install: SteamWorkshopInstallResult = await workshop.item("001").install()
	var invalid_vote: SteamWorkshopVoteResult = await workshop.item("001").vote(true)
	_check(invalid_subscription.error == ERR_INVALID_PARAMETER and invalid_install.error == ERR_INVALID_PARAMETER
		and invalid_vote.error == ERR_INVALID_PARAMETER and workshop.item("001").installed() == null,
		"Invalid item actions settle with typed validation errors before SDK submission.")

	var filtered := base.search("crimson-workshop-read-fixture").tagged("Map").sort_by(SteamWorkshopQuery.RELEVANCE)
	var first: SteamWorkshopPageResult = await base.fetch()
	if not _check(first.ok, "Steam returned the first Workshop page. %s" % first.message):
		return
	_check_page(first.value)
	if not first.value.next_cursor.is_empty():
		var next: SteamWorkshopPageResult = await base.after(first.value.next_cursor).fetch()
		if _check(next.ok, "The opaque continuation retrieves another Workshop page. %s" % next.message):
			_check_page(next.value)
	var filtered_page: SteamWorkshopPageResult = await filtered.fetch()
	if _check(filtered_page.ok, "Steam accepted a branch with tag, search, and sort. %s" % filtered_page.message):
		_check_page(filtered_page.value)

	var exact_id := known_id
	if exact_id.is_empty() and not first.value.items.is_empty():
		exact_id = first.value.items[0].published_id
	if not exact_id.is_empty():
		var exact: SteamWorkshopItemResult = await workshop.item(exact_id).fetch()
		_check(exact.ok and exact.value.published_id == exact_id and not exact.value.title.is_empty(),
			"The exact lookup returns typed metadata for the requested published file ID.")
	if not known_id.is_empty() and OS.get_environment("CRIMSON_STEAM_WORKSHOP_ALLOW_MUTATIONS") == "1":
		await _check_mutations(steam, known_id)
	if app_mode and OS.get_environment("CRIMSON_STEAM_WORKSHOP_ALLOW_PUBLISH") == "1":
		await _check_publish(workshop)

	var pending: Variant = base.fetch()
	if _check(typeof(pending) == TYPE_SIGNAL, "The live query exposes its pending completion signal."):
		var cancelled: Array[SteamWorkshopPageResult] = []
		pending.connect(func(result: SteamWorkshopPageResult) -> void: cancelled.append(result))
		steam.stop()
		_check(cancelled.size() == 1 and cancelled[0].error == ERR_SKIP and cancelled[0].value == null,
			"Scope stop settles a pending query exactly once with a typed cancellation.")
	else:
		steam.stop()
	var retired: SteamWorkshopPageResult = await base.fetch()
	_check(retired.error == ERR_UNAVAILABLE and steam.workshop() == null, "A retained query cannot use a stopped scope.")


## Read bounded current-app subscriptions with both local visibility settings.
func _check_subscription_snapshot(workshop: SteamWorkshop) -> void:
	_check(workshop.subscriptions(0).error == ERR_INVALID_PARAMETER
		and workshop.subscriptions(SteamWorkshop.MAX_SUBSCRIPTIONS + 1).error == ERR_INVALID_PARAMETER,
		"Subscription enumeration rejects limits outside its bounded range.")
	for include_disabled: bool in [false, true]:
		var result := workshop.subscriptions(1, include_disabled)
		if _check(result.ok and result.value != null, "Steam returns an app-scoped subscription snapshot."):
			var ids := result.value.published_ids
			_check(ids.size() <= 1 and result.value.total_count >= ids.size()
				and result.value.truncated == (result.value.total_count > ids.size()),
				"The subscription snapshot reports its limit and truncation consistently.")
			for id: String in ids:
				_check(_is_published_id(id), "Subscribed published IDs retain their canonical decimal form.")


## Check the exact app and independently reported agreement fields.
func _check_agreement(result: SteamWorkshopLegalAgreementStatusResult, app_id: int) -> bool:
	return _check(result.ok and result.value != null and result.value.app_id == app_id
		and result.value.version >= 0 and result.value.accepted_at_unix >= 0,
		"Steam returns a typed Workshop agreement status for the active app.")


## Open the agreement only for the explicit graphical app test and observe Steam's overlay.
func _check_legal_overlay(steam: SteamClient, workshop: SteamWorkshop) -> void:
	if not _check(DisplayServer.get_name() != "headless", "Run the opted-in Workshop EULA check in a rendered Steam session."):
		return
	var overlay := steam.overlay()
	if not _check(overlay != null, "The graphical client exposes its Steam overlay."):
		return
	var label := Label.new()
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.text = "Waiting for the Steam Workshop agreement overlay"
	root.add_child(label)
	label.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var ready := await _wait_for_workshop_overlay(func() -> bool: return overlay.is_ready() and not overlay.is_active(), 20000)
	if not _check(ready, "Steam made its overlay ready and closed for the Workshop agreement request."):
		label.queue_free()
		return
	label.text = "Review the Workshop agreement in Steam, then close the overlay to finish the check."
	if not _check(workshop.show_legal_agreement() == OK, "Steam accepted the explicit Workshop agreement overlay request."):
		label.queue_free()
		return
	var opened := await _wait_for_workshop_overlay(overlay.is_active, 20000)
	if not _check(opened, "Steam reported overlay activation after the Workshop agreement request."):
		label.queue_free()
		return
	print("CRIMSON_STEAM_WORKSHOP_EULA_OVERLAY_OPENED")
	var closed := await _wait_for_workshop_overlay(func() -> bool: return not overlay.is_active(), 120000)
	label.queue_free()
	if not _check(closed, "Close the Steam overlay within two minutes to finish the check."):
		return
	var updated: SteamWorkshopLegalAgreementStatusResult = await workshop.legal_agreement_status()
	if _check_agreement(updated, steam.get_app_id()):
		print("CRIMSON_STEAM_WORKSHOP_EULA_OVERLAY_CLOSED")


## Wait for the requested Steam overlay state without guessing the user's legal decision.
func _wait_for_workshop_overlay(predicate: Callable, timeout_ms: int) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if predicate.call():
			return true
		await process_frame
	return false


## Exercise subscriptions and local installation only for the designated private fixture item.
func _check_mutations(steam: SteamClient, item_id: String) -> void:
	var workshop := steam.workshop()
	var item := workshop.item(item_id)
	var subscribed: SteamWorkshopSubscriptionResult = await item.subscribe()
	if not _check(subscribed.ok and subscribed.value.published_id == item_id and subscribed.value.subscribed,
			"Steam acknowledged the designated test subscription. %s" % subscribed.message):
		return
	await _check_subscription_membership(workshop, item_id, true)
	var installed: SteamWorkshopInstallResult = await item.install()
	if _check(installed.ok and installed.value.published_id == item_id
			and installed.value.directory.is_absolute_path() and installed.value.size_bytes >= 0,
			"Steam returned an independent current installation receipt. %s" % installed.message):
		var snapshot := item.installed()
		_check(snapshot != null and snapshot.directory == installed.value.directory,
			"Installed inspection sees the same verified local content directory.")
	var unsubscribed: SteamWorkshopSubscriptionResult = await item.unsubscribe()
	if _check(unsubscribed.ok and unsubscribed.value.published_id == item_id and not unsubscribed.value.subscribed,
			"Steam removed the fixture's subscription. %s" % unsubscribed.message):
		await _check_subscription_membership(workshop, item_id, false)

	var vote_id := OS.get_environment("CRIMSON_STEAM_WORKSHOP_VOTE_ITEM_ID")
	if not vote_id.is_empty() and _check(_is_published_id(vote_id), "The designated vote item ID is canonical."):
		var vote_item := steam.workshop().item(vote_id)
		var vote_details: SteamWorkshopItemResult = await vote_item.fetch()
		if _check(vote_details.ok and vote_details.value.author_steam_id != steam.get_local_steam_id(),
				"The vote fixture belongs to a separate controlled author account."):
			var voted: SteamWorkshopVoteResult = await vote_item.vote(true)
			_check(voted.ok and voted.value.published_id == vote_id and voted.value.up,
				"Steam confirmed the opted-in vote on the controlled item. %s" % voted.message)


## Wait for the local subscription list to reflect a controlled change.
func _check_subscription_membership(workshop: SteamWorkshop, item_id: String, subscribed: bool) -> void:
	var deadline := Time.get_ticks_msec() + 3000
	while Time.get_ticks_msec() < deadline:
		var result := workshop.subscriptions(SteamWorkshop.MAX_SUBSCRIPTIONS, true)
		if not _check(result.ok and result.value != null, "Read subscriptions after the controlled change."):
			return
		var contains_item := result.value.published_ids.has(item_id)
		if contains_item == subscribed and (contains_item or not result.value.truncated):
			return
		if result.value.truncated and not contains_item:
			print("CRIMSON_STEAM_WORKSHOP_SUBSCRIPTION_MEMBERSHIP_TRUNCATED")
			return
		await process_frame
	_check(false, "The app subscription list reflected the controlled change within three seconds.")


## Publish and edit one private item using only the fixture's disposable content.
func _check_publish(workshop: SteamWorkshop) -> void:
	var invalid: SteamWorkshopPublicationResult = await workshop.new_item().named("Missing content").submit()
	if not _check(invalid.error == ERR_INVALID_PARAMETER and invalid.value == null,
			"Publication validates content before creating an item."):
		return

	var suffix := Crypto.new().generate_random_bytes(8).hex_encode()
	var directory := "user://steam-workshop-fixture-" + suffix
	var content := directory.path_join("content")
	var preview := directory.path_join("preview.png")
	var file_path := content.path_join("fixture.txt")
	if not _check(DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(content)) == OK,
			"Create a local folder for the private fixture content."):
		return
	var file := FileAccess.open(file_path, FileAccess.WRITE)
	if not _check(file != null, "Create the fixture's disposable content file."):
		_remove_publish_files(directory, content, file_path, preview)
		return
	file.store_string("Crimson Workshop integration fixture\n")
	file.close()
	var image := Image.create_empty(16, 16, false, Image.FORMAT_RGBA8)
	image.fill(Color("615d4b"))
	if not _check(image.save_png(preview) == OK, "Create a local preview for the private fixture."):
		_remove_publish_files(directory, content, file_path, preview)
		return

	var title := "Crimson Workshop Fixture " + suffix
	var description := "Crimson Workshop private fixture content."
	var draft := workshop.new_item().named(title).described_as(description)
	var published: SteamWorkshopPublicationResult = await draft.with_content(content).with_preview(preview).tagged("Map").submit()
	if published.value != null and _is_published_id(published.value.published_id):
		print("CRIMSON_STEAM_WORKSHOP_FIXTURE_ITEM %s" % published.value.published_id)
	if published.error not in [ERR_TIMEOUT, ERR_SKIP, ERR_CANT_CONNECT]:
		_remove_publish_files(directory, content, file_path, preview)
	if not _check(published.ok and published.value != null and _is_published_id(published.value.published_id),
			"Steam completed the private fixture publication. %s" % published.message):
		return

	var item := workshop.item(published.value.published_id)
	var updated_title := title + " Updated"
	var edited: SteamWorkshopPublicationResult = await item.edit().named(updated_title) \
		.visibility(SteamWorkshopPublishRequest.PRIVATE).submit("Verify a same-ID edit")
	if not _check(edited.ok and edited.value.published_id == published.value.published_id,
			"Steam updated the same private item. %s" % edited.message):
		return
	var readback: SteamWorkshopItemResult = await item.fetch()
	for attempt in 4:
		if readback.ok and readback.value.title == updated_title:
			break
		await create_timer(0.5).timeout
		readback = await item.fetch()
	_check(readback.ok and readback.value.title == updated_title and readback.value.description == description,
		"Exact readback sees the new title and the description the edit left unset.")


## Remove only the local files created by a settled fixture publication.
func _remove_publish_files(directory: String, content: String, file: String, preview: String) -> void:
	for path in [file, preview, content, directory]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


## Validate the page boundary without logging Workshop titles or author data.
func _check_page(page: SteamWorkshopPage) -> void:
	_check(page.items.is_read_only() and page.items.size() <= MAX_PAGE_ITEMS, "Steam returned a bounded read-only item page.")
	for item: SteamWorkshopItem in page.items:
		_check(item != null and _is_published_id(item.published_id), "Every row has a canonical published file ID.")


## Read the exact authorized non-Spacewar AppID.
func _configured_app_id() -> int:
	var configured := OS.get_environment("CRIMSON_STEAM_WORKSHOP_APP_ID")
	if not configured.is_valid_int():
		return 0
	var app_id := configured.to_int()
	return app_id if app_id > 0 and app_id <= 4294967295 and app_id != SPACEWAR and str(app_id) == configured else 0


## Check the canonical decimal form without narrowing a published file ID.
func _is_published_id(value: String) -> bool:
	if value.is_empty() or value.length() > 20 or value.begins_with("0"):
		return false
	for index in value.length():
		if value.unicode_at(index) < 48 or value.unicode_at(index) > 57:
			return false
	return value.length() < 20 or value <= "18446744073709551615"


## Record a failing assertion without disclosing user data.
func _check(condition: bool, message: String) -> bool:
	if not condition:
		_failures += 1
		push_error(message)
	return condition
