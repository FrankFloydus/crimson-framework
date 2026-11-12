// Copyright Francesco Di Blasi 2026 - All Rights Reserved
shader_type spatial;
render_mode diffuse_lambert, specular_disabled;

/** The same oxidized steel tile used on the tunnel's structural metal. */
uniform sampler2D steel : source_color, filter_nearest_mipmap, repeat_enable;
/** Value and warmth adjustments for inset and overlapping steel plates. */
uniform vec4 finish : source_color = vec4(1.0);

varying vec3 world_point;
varying vec3 world_normal;

// Keep the tile at the same scale across every plate, edge, and fastener.
void vertex() {
	world_point = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
}

// Project the steel every two meters with sharp texels and a soft corner blend.
void fragment() {
	vec3 point = world_point * 0.5;
	vec3 weight = pow(abs(normalize(world_normal)), vec3(8.0));
	weight /= weight.x + weight.y + weight.z;
	vec3 x_surface = texture(steel, vec2(point.z, -point.y)).rgb;
	vec3 y_surface = texture(steel, point.xz).rgb;
	vec3 z_surface = texture(steel, vec2(point.x, -point.y)).rgb;
	ALBEDO = (x_surface * weight.x + y_surface * weight.y + z_surface * weight.z) * finish.rgb;
	ROUGHNESS = 0.96;
}
