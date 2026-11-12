# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies Steam transport and route bindings without opening a Steam session.
extends SceneTree

const EXAMPLE_ACCOUNT := 76561197960265729

var _failures := 0


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var transport := CrimsonSteamSessionTransport.new()
	var endpoint := CrimsonSteamSessionTransport.route(EXAMPLE_ACCOUNT)
	var upper_port := CrimsonSteamSessionTransport.route(EXAMPLE_ACCOUNT, 65535)
	_check(endpoint != null and upper_port != null and endpoint.get_transport() == &"steam"
		and endpoint.get_address().is_empty() and endpoint.get_port() == 0,
		"Steam routes use an opaque account and virtual port rather than an ENet address.")
	if endpoint != null and upper_port != null:
		var route := endpoint.get_route()
		var upper := upper_port.get_route()
		_check(route.size() == 11 and route[0] == 1 and upper[9] == 255 and upper[10] == 255
			and route != upper, "Steam routes encode exact versioned virtual ports.")
		route[0] = 0
		_check(endpoint.get_route()[0] == 1, "Endpoint route bytes remain immutable.")
		_check(transport.open_client(CrimsonSessionEndpoint.opaque(&"steam", route)).get_error() == ERR_INVALID_PARAMETER,
			"An unknown route version is rejected before Steam setup.")
		_check(transport.open_client(endpoint).get_error() == ERR_UNCONFIGURED,
			"A valid route requires an explicitly configured Steam runtime.")
	_check(CrimsonSteamSessionTransport.route(0) == null
		and CrimsonSteamSessionTransport.route(EXAMPLE_ACCOUNT, -1) == null
		and CrimsonSteamSessionTransport.route(EXAMPLE_ACCOUNT, 65536) == null,
		"Steam rejects invalid account IDs and virtual ports.")
	_check(transport.open_client(CrimsonSessionEndpoint.direct("127.0.0.1", 24830)).get_error() == ERR_INVALID_PARAMETER
		and transport.open_server(24830, 65, "*").get_error() == ERR_INVALID_PARAMETER
		and transport.open_server(24830, 3, "127.0.0.1").get_error() == ERR_INVALID_PARAMETER,
		"Steam declines incompatible endpoints, oversized seat counts and UDP bind addresses.")
	_check(transport.get_host_endpoint() == null and transport.get_host_remote_limit() == 0
		and transport.get_remote_steam_id(1) == 0,
		"Disconnected Steam objects expose no host route or verified identity.")
	_check(transport.open_server(24830, 3, "*").get_error() == ERR_UNCONFIGURED,
		"An unconfigured adapter cannot create a Steam listener.")
	var provider := CrimsonSteamProvider.new()
	_check(provider.invite_player("invalid") == ERR_INVALID_PARAMETER
		and provider.invite_player(str(EXAMPLE_ACCOUNT)) == ERR_UNCONFIGURED,
		"A detached provider rejects malformed recipients and lacks a private lobby.")
	if _failures == 0:
		print("CRIMSON_STEAM_TRANSPORT_PASSED")
	quit(0 if _failures == 0 else 1)


func _check(condition: bool, description: String) -> void:
	if not condition:
		_failures += 1
		push_error(description)
