# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends Node3D

## Route tint shared by the aperture, indicator lamps, and tunnel light.
@export var energy_color := Color(0.23, 0.88, 0.96):
	set(value):
		energy_color = value
		if is_node_ready():
			_apply_energy_color()


## Applies the authored color once the geometry and light are in the tree.
func _ready() -> void:
	_apply_energy_color()


## Updates per-instance parameters so changing one gate leaves the others intact.
func _apply_energy_color() -> void:
	$Aperture.set_instance_shader_parameter(&"energy_color", energy_color)
	$LeftStatus.set_instance_shader_parameter(&"energy_color", energy_color)
	$RightStatus.set_instance_shader_parameter(&"energy_color", energy_color)
	var terminal_light := find_child("TerminalLight") as OmniLight3D
	terminal_light.light_color = energy_color
