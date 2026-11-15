# Copyright Francesco Di Blasi 2026 - All Rights Reserved
class_name CrimsonTestForeignController
extends Node

## Reports the updated charge to the graph attached to the parent.
signal recharged(new_charge: int)

## Deliberately use a saved script property with a typed, nonexported value.
var charge: int = 10


## A typed collection default is evaluated by GDScript when the caller omits it.
func recharge(amount: int, bonuses: Array[int] = [3, 4]) -> int:
	charge += amount
	for bonus: int in bonuses:
		charge += bonus
	recharged.emit(charge)
	return charge
