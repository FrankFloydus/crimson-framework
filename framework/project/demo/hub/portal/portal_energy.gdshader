// Copyright Francesco Di Blasi 2026 - All Rights Reserved
shader_type spatial;
render_mode unshaded, cull_disabled;

/** Tint shared by the portal surface and its indicator strips. */
instance uniform vec4 energy_color : source_color = vec4(0.23, 0.88, 0.96, 1.0);
/** The narrow indicators use a steady version of the aperture color. */
uniform bool indicator = false;

varying vec2 surface_point;

// Store the portal's XY coordinates before the engine projects the mesh.
void vertex() {
	surface_point = VERTEX.xy;
}

// Hash the corners used by the interpolated field.
float hash_cell(vec2 cell) {
	return fract(52.9829189 * fract(dot(cell, vec2(0.06711056, 0.00583715))));
}

// Interpolate irregular clouds without a repeated texture.
float cloud(vec2 point) {
	vec2 cell = floor(point);
	vec2 fraction = fract(point);
	vec2 weight = fraction * fraction * (3.0 - 2.0 * fraction);
	float bottom = mix(hash_cell(cell), hash_cell(cell + vec2(1.0, 0.0)), weight.x);
	float top = mix(hash_cell(cell + vec2(0.0, 1.0)), hash_cell(cell + vec2(1.0)), weight.x);
	return mix(bottom, top, weight.y);
}

// Drift thin distortions through the aperture with a faint light at the recess.
void fragment() {
	float luminance = dot(energy_color.rgb, vec3(0.2126, 0.7152, 0.0722));
	vec3 tint = mix(vec3(luminance), energy_color.rgb, 0.56);
	if (indicator) {
		ALBEDO = tint * 0.47 + vec3(0.08);
	} else {
		vec2 pixel = floor(surface_point * 72.0);
		vec2 point = (pixel + vec2(0.5)) / 72.0 - vec2(0.0, 1.80);
		vec2 drift = vec2(0.19, -0.27) * TIME;
		float shear = cloud(point * vec2(1.8, 1.3) + drift);
		vec2 field = point + vec2(0.43, -0.28) * (shear - 0.5);
		float vapor = cloud(field * vec2(2.8, 1.25) + drift);
		float ripples = cloud(field * vec2(7.6, 2.1) - drift * 1.4);
		float mist = smoothstep(0.24, 0.77, vapor * 0.75 + ripples * 0.25);
		float filament = smoothstep(0.71, 0.95, ripples) * mist;
		float wall = min(1.62 - abs(point.x), min(point.y + 1.60, 1.60 - point.y));
		float top_corner = (3.18 - 1.34 * abs(point.x) - point.y) / 1.67;
		float floor_corner = (2.66 - 0.91 * abs(point.x) + point.y) / 1.35;
		float distance_to_frame = min(wall, min(top_corner, floor_corner));
		float rim = 1.0 - smoothstep(0.018, 0.16, distance_to_frame);
		float brightness = 0.048 + 0.185 * mist + 0.054 * filament + 0.010 * hash_cell(pixel);
		ALBEDO = vec3(0.009, 0.011, 0.014) + tint * (brightness + 0.17 * rim * (0.6 + 0.4 * vapor));
	}
}
