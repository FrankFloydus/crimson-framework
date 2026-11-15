# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

## Number of loop bodies executed by each implementation in each measured run.
const ITERATIONS := 1_000_000
## Both scripts carry the checksum across bounded calls to the same work unit.
const BATCH_SIZE := 250
## Both runners warm up before taking any measurements.
const WARMUP_ITERATIONS := 5_000
## Take an odd number of paired samples for the median.
const TRIALS := 5
## Fixed initial and expected checksums for the deterministic workload.
const SEED := 17
const WARMUP_CHECKSUM := 38_587
const EXPECTED_CHECKSUM := 85_457
## Authored Visual Script equivalent of the GDScript batch.
const BENCHMARK_GRAPH := "res://tests/visual_scripting/loop_benchmark.crimsonscript"


## Start after the scene tree can host the Visual Script owner.
func _initialize() -> void:
	_run.call_deferred()


## Run the same integer arithmetic, branch, and rolling checksum as the graph.
func _gdscript_batch(first_index: int, last_index: int, initial_checksum: int) -> int:
	var checksum := initial_checksum
	for index: int in range(first_index, last_index + 1):
		var sample := (index * 31 + checksum % 97) % 251
		if sample < 125:
			checksum = (checksum * 3 + sample * 7 + 3) % 104_729
		else:
			checksum = (checksum * 3 + sample * 3 + 11) % 104_729
	return checksum


## Call each implementation with the same ranges and verify its final checksum.
func _measure_batches(runner: Callable, iterations: int, expected: int) -> int:
	var checksum := SEED
	var started_usec := Time.get_ticks_usec()
	for first_index: int in range(0, iterations, BATCH_SIZE):
		var last_index := mini(first_index + BATCH_SIZE, iterations) - 1
		var result: Variant = runner.call(first_index, last_index, checksum)
		if typeof(result) != TYPE_INT:
			return -1
		checksum = result
	var elapsed_usec := Time.get_ticks_usec() - started_usec
	if checksum != expected:
		push_error("[Crimson] %s returned checksum %d, expected %d." % [runner.get_method(), checksum, expected])
		return -1
	return elapsed_usec


## Warm and time both runners in one process, alternating which goes first.
func _run() -> void:
	var graph_script := ResourceLoader.load(BENCHMARK_GRAPH, "Script") as Script
	if graph_script == null:
		push_error("[Crimson] Could not load the benchmark graph at %s. Check the Crimson GDExtension." % BENCHMARK_GRAPH)
		quit(1)
		return
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var runners: Array[Callable] = [_gdscript_batch, Callable(graph_owner, &"run_batch")]
	for runner: Callable in runners:
		if not runner.is_valid() or _measure_batches(runner, WARMUP_ITERATIONS, WARMUP_CHECKSUM) < 0:
			push_error("[Crimson] The Visual Script benchmark could not warm up %s." % runner.get_method())
			quit(1)
			return

	var samples: Array = [[], []]
	for trial: int in range(TRIALS):
		var timings: Array[int] = [0, 0]
		for offset: int in range(runners.size()):
			var index := (trial + offset) % runners.size()
			var elapsed_usec := _measure_batches(runners[index], ITERATIONS, EXPECTED_CHECKSUM)
			if elapsed_usec < 0:
				push_error("[Crimson] The Visual Script benchmark could not measure %s." % runners[index].get_method())
				quit(1)
				return
			timings[index] = elapsed_usec
			samples[index].append(elapsed_usec)
		print("Trial %d: GDScript %.3f ms, Visual Script %.3f ms" % [trial + 1, timings[0] / 1000.0, timings[1] / 1000.0])

	print("CRIMSON_VISUAL_SCRIPT_BENCHMARK_PASSED ", JSON.stringify({
		"iterations_per_trial": ITERATIONS,
		"iterations_per_call": BATCH_SIZE,
		"warmup_iterations": WARMUP_ITERATIONS,
		"checksum": EXPECTED_CHECKSUM,
		"gdscript_usec": samples[0],
		"visual_script_usec": samples[1],
	}))
	quit()
