#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D video_texture;

layout(set = 3, binding = 0, std140) uniform EffectData {
    uint grayscale;
    vec3 padding;
    vec4 tint;
} effect_data;

void main() {
    vec4 color = texture(video_texture, v_uv);
    if (effect_data.grayscale != 0u) {
        float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        color = vec4(vec3(luminance), color.a);
    }
    out_color = color * effect_data.tint;
}
