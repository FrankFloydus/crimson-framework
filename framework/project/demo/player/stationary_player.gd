extends CharacterBody3D

@onready var abilities: AbilitySystemComponent = $AbilitySystemComponent

func _ready() -> void:
	var stats := abilities.get_attribute_set(&"main_set")
	if stats == null:
		push_error("Assign main_set to the AbilitySystemComponent.")
		return

	if stats.set_base(&"health", 100.0) != OK:
		return
	if stats.add_base(&"health", -25.0) != OK:
		return

	print(stats.get_attribute(&"health").get_current()) # 75.0
