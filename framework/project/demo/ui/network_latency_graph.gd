# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Control
## Plots a fixed time window. Missing measurements leave visible gaps.

const SAMPLE_COUNT: int = 60
const GRID_COLOR := Color(0.22, 0.23, 0.26)
const BACKGROUND := Color(0.078, 0.082, 0.097)

## Colour of the data trace for this metric.
@export var trace_color := Color(0.34, 0.66, 1.0)
## Round the vertical scale to this increment, in the displayed unit.
@export var scale_step: float = 50.0
## Largest value or increment visible over the last 12 seconds.
var ceiling: float = 50.0
## One entry per monitor sample. Negative values represent missing measurements.
var _samples := PackedFloat32Array()


## Add one measurement at the monitor's fixed sampling cadence.
func add_sample(value: float) -> void:
	_samples.append(value)
	if _samples.size() > SAMPLE_COUNT:
		_samples.remove_at(0)
	var peak: float = scale_step
	for sample: float in _samples:
		peak = maxf(peak, sample)
	ceiling = ceilf(peak / scale_step) * scale_step
	queue_redraw()


## Clear the trace when the measured process's role or peer ID changes.
func clear() -> void:
	_samples.clear()
	ceiling = scale_step
	queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), BACKGROUND)
	for index: int in range(3):
		var y: float = (size.y - 1.0) * index * 0.5
		draw_line(Vector2(0.0, y), Vector2(size.x, y), GRID_COLOR)
	var spacing: float = (size.x - 2.0) / (SAMPLE_COUNT - 1)
	var points := PackedVector2Array()
	for index: int in range(_samples.size()):
		var value: float = _samples[index]
		if value < 0.0:
			_draw_segment(points)
			points.clear()
			continue
		var x: float = size.x - 1.0 - (_samples.size() - 1 - index) * spacing
		var y: float = size.y - 1.0 - value / ceiling * (size.y - 2.0)
		points.append(Vector2(x, y))
	_draw_segment(points)


## Draw each contiguous run of real samples.
func _draw_segment(points: PackedVector2Array) -> void:
	if points.size() > 1:
		draw_polyline(points, trace_color, 1.5, true)
	elif points.size() == 1:
		draw_circle(points[0], 1.5, trace_color)
