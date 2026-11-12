// Copyright Francesco Di Blasi 2026 - All Rights Reserved
shader_type spatial;
render_mode diffuse_lambert, specular_disabled;

uniform sampler2D concrete : source_color, filter_nearest_mipmap, repeat_enable;

// Tunnel segments share the running distance at their mitered edges.
instance uniform vec2 run_axis : instance_index(0) = vec2(0.0);
instance uniform float run_offset : instance_index(1) = 0.0;

varying vec3 world_point;

void vertex() {
	world_point = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 face_normal = normalize(cross(dFdx(world_point), dFdy(world_point)));
	vec2 tile_uv = world_point.xz / 3.2;
	float seam = 0.0;
	if (abs(face_normal.y) < 0.5) {
		vec2 tangent = normalize(vec2(face_normal.z, -face_normal.x));
		bool follows_run = abs(dot(tangent, run_axis)) > 0.9;
		float major = abs(tangent.x) > abs(tangent.y) ? tangent.x : tangent.y;
		vec2 axis = follows_run ? run_axis : tangent * sign(major);
		float offset = follows_run ? run_offset : 0.0;
		float run = dot(world_point.xz, axis) + offset;
		tile_uv = vec2(run / 3.2, -world_point.y / 3.6);
		vec2 panel = vec2(run / 3.2, world_point.y / 2.4);
		vec2 edges = abs(fract(panel + 0.5) - 0.5);
		seam = 1.0 - smoothstep(0.0025, 0.008, min(edges.x, edges.y));
	}
	ALBEDO = texture(concrete, tile_uv).rgb * (1.0 - seam * 0.22);
	ROUGHNESS = 0.96;
}
