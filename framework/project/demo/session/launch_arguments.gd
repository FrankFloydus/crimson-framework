# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Parses the demo's command-line launch options before the game boots.


## Resolve an optional graphical listen or join request from the demo's arguments.
static func parse(arguments: PackedStringArray) -> Dictionary:
	var options := {"role": "", "address": "", "port": 0, "bind": "*", "error": ""}
	var supplied: Dictionary[String, String] = {}
	for argument in arguments:
		if not argument.begins_with("--crimson-"):
			continue
		var pair := argument.trim_prefix("--crimson-").split("=", true, 1)
		if pair.size() != 2 or pair[1].strip_edges().is_empty():
			options.error = "Crimson launch options require --crimson-name=value."
			break
		var name := pair[0]
		if name == "run-dir":
			continue
		if name not in ["role", "address", "port", "bind"]:
			options.error = "Unknown Crimson launch option: --crimson-%s." % name
			break
		if supplied.has(name):
			options.error = "Duplicate Crimson launch option: --crimson-%s." % name
			break
		supplied[name] = pair[1].strip_edges()

	options.role = supplied.get("role", "")
	if not options.error.is_empty():
		return options
	if options.role.is_empty() and supplied.is_empty():
		return options
	if options.role not in ["listen", "join"]:
		options.error = "Choose --crimson-role=listen or --crimson-role=join."
	elif options.role == "join" and not supplied.has("address"):
		options.error = "Join requires --crimson-address with a host name or IP address."
	elif options.role == "join" and supplied["address"].length() > 253:
		options.error = "Choose a host name or IP address of up to 253 characters."
	elif options.role == "join" and supplied.has("bind"):
		options.error = "Only servers can set --crimson-bind."
	elif options.role != "join" and supplied.has("address"):
		options.error = "Only joining clients can set --crimson-address."
	else:
		options.address = supplied.get("address", "")
		options.bind = supplied.get("bind", "*")
		if options.bind != "*" and not options.bind.is_valid_ip_address():
			options.error = "Choose * or a literal IP address for --crimson-bind."
		elif supplied.has("port"):
			var value: String = supplied["port"]
			var port := value.to_int()
			if not value.is_valid_int() or port < 1 or port > 65535:
				options.error = "Choose a UDP port from 1 to 65535."
			else:
				options.port = port
	return options
