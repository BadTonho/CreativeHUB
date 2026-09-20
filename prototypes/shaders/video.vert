#version 450

layout(location = 0) out vec2 v_uv;

layout(set = 1, binding = 0, std140) uniform ViewData {
    vec4 rect;
} view_data;

const vec2 POSITIONS[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0)
);

const vec2 UVS[4] = vec2[](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 0.0)
);

void main() {
    vec2 normalized = POSITIONS[gl_VertexIndex] * 0.5 + 0.5;
    vec2 position = view_data.rect.xy + normalized * view_data.rect.zw;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
    v_uv = UVS[gl_VertexIndex];
}
