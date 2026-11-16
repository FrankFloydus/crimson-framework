# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends GameplayAbility


func on_activated(_activation: GameplayAbilityActivation) -> void:
	# Leave this run active so the fixture can verify travel and teardown cancellation.
	pass
