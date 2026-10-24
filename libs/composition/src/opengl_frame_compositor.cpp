#include <creative_suite/composition/opengl_frame_compositor.h>
#include "windows_video_interop.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLContextGroup>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector3D>
#include <QVector4D>
#include <QThread>
#include <QMatrix3x3>
#include <QSysInfo>
#include <unordered_map>
#include <unordered_set>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <mutex>
#include <stdexcept>

namespace creative_suite::composition {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t maximum_texture_bytes = 256ULL * 1024 * 1024;
constexpr std::uint64_t maximum_effect_scratch_bytes = 64ULL * 1024 * 1024;
std::uint64_t elapsed(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}
OpenGlCompositionResult result(OpenGlCompositionStatus status,
    const char* operation = "", std::string cause = {}, std::int64_t code = 0,
    int layer_index = -1) {
    return {status, {}, operation, std::move(cause), code, layer_index};
}
bool unpremultiplyRgba(media::RgbaFrame& frame,
                      const GpuFrameCompositor::CancellationPredicate& cancel) {
    for (int y = 0; y < frame.height; ++y) {
        if (cancel && cancel()) return false;
        for (int x = 0; x < frame.width; ++x) {
            const auto offset = static_cast<std::size_t>(y) * frame.stride +
                static_cast<std::size_t>(x) * 4U;
            const auto alpha = frame.rgba_pixels[offset + 3];
            if (alpha == 0) {
                frame.rgba_pixels[offset] = 0;
                frame.rgba_pixels[offset + 1] = 0;
                frame.rgba_pixels[offset + 2] = 0;
                continue;
            }
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto premultiplied = frame.rgba_pixels[offset + channel];
                frame.rgba_pixels[offset + channel] = static_cast<std::uint8_t>(
                    std::min(255U, (unsigned(premultiplied) * 255U + alpha / 2U) / alpha));
            }
        }
    }
    return true;
}
bool usable(const CompositionLayer& layer) {
    if (!layer.frame || !animation::validTransform(layer.transform) || (layer.source_to_canvas && !validAffine(*layer.source_to_canvas))) return false;
    const auto& f = *layer.frame;
    if (layer.native_frame) {
#ifdef CS_NATIVE_VIDEO_INTEROP
        return f.width == layer.native_frame->width() && f.height == layer.native_frame->height();
#else
        return false;
#endif
    }
    if (layer.texture_frame) return layer.texture_frame->backend() == GraphicsBackend::OpenGl && layer.texture_frame->valid() &&
        f.width == layer.texture_frame->width() && f.height == layer.texture_frame->height();
    return f.width > 0 && f.height > 0 &&
        static_cast<std::int64_t>(f.stride) >= static_cast<std::int64_t>(f.width) * 4 &&
        static_cast<std::uint64_t>(f.stride) * f.height <= f.rgba_pixels.size();
}
bool bounded(int width, int height, int limit) {
    return width > 0 && height > 0 && width <= limit && height <= limit &&
        static_cast<std::uint64_t>(width) * height * 4 <= maximum_texture_bytes;
}
bool requiresCpuMatteRefinement(const effects::ColorAdjustmentParameters& value) {
    return value.video_curves && !effects::tonalQualifierRefinementIsNeutral(
        value.video_curves->qualifier);
}
constexpr char vertex_shader[] = R"GLSL(#version 150 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
constexpr char fragment_shader[] = R"GLSL(
uniform sampler2D source_image;
uniform sampler2D backdrop_image;
uniform sampler2D curve_lookup;
uniform int blend_mode;
uniform bool source_bottom_left;
uniform float canvas_height;
uniform int color_adjustment_count;
uniform GEOMETRY_VEC2 center;
uniform GEOMETRY_VEC2 displayed_size;
uniform GEOMETRY_VEC2 rotation_cs;
uniform bool use_affine;
uniform GEOMETRY_VEC2 affine_x;
uniform GEOMETRY_VEC2 affine_y;
uniform GEOMETRY_VEC2 affine_translation;
uniform float opacity;
uniform bool axis_aligned;
layout(std140) uniform SourceLookupX { ivec4 lookup_x[1024]; };
layout(std140) uniform SourceLookupY { ivec4 lookup_y[1024]; };
layout(std140) uniform ColorAdjustments { vec4 color_adjustments[1024]; };
layout(std140) uniform LogAdjustments { vec4 log_adjustments[768]; };
layout(std140) uniform CurveQualifiers { vec4 curve_qualifiers[768]; };
out vec4 color;
float curveValue(int index, int curve, float input_value) {
    int x = int(floor(clamp(input_value, 0.0, 1.0) * 255.0 + 0.50001));
    return floor(texelFetch(curve_lookup, ivec2(x, index * 9 + curve), 0).r * 255.0 + 0.5) / 255.0;
}
float curveOffset(int index, int curve, float input_value) {
    return (curveValue(index, curve, input_value) * 255.0 - 128.0) / 127.0;
}
float rangeWeight(float value, float minimum, float maximum, float softness) {
    float lo = softness <= 0.0 ? float(value >= minimum) : smoothstep(minimum - softness, minimum + softness, value);
    float hi = softness <= 0.0 ? float(value <= maximum) : 1.0 - smoothstep(maximum - softness, maximum + softness, value);
    return min(lo, hi);
}
void toHsv(vec3 rgb, out float h, out float s, out float v) {
    PRECISE float mx = max(max(rgb.r, rgb.g), rgb.b), mn = min(min(rgb.r, rgb.g), rgb.b), d = mx - mn;
    PRECISE float hue_value = 0.0, saturation_value = mx <= 0.0 ? 0.0 : d / mx;
    if (d > 0.0) {
        if (mx == rgb.r) hue_value = 60.0 * mod((rgb.g - rgb.b) / d, 6.0);
        else if (mx == rgb.g) hue_value = 60.0 * ((rgb.b - rgb.r) / d + 2.0);
        else hue_value = 60.0 * ((rgb.r - rgb.g) / d + 4.0);
        if (hue_value < 0.0) hue_value += 360.0;
    }
    h = hue_value; s = saturation_value; v = mx;
}
vec3 fromHsv(float h, float s, float v) {
    PRECISE float hue = mod(h, 360.0); if (hue < 0.0) hue += 360.0;
    PRECISE float saturation = clamp(s, 0.0, 1.0), value = clamp(v, 0.0, 1.0);
    PRECISE float c = value * saturation, x = c * (1.0 - abs(mod(hue / 60.0, 2.0) - 1.0)), m = value - c;
    PRECISE vec3 result;
    if (hue < 60.0) result = vec3(c + m, x + m, m);
    else if (hue < 120.0) result = vec3(x + m, c + m, m);
    else if (hue < 180.0) result = vec3(m, c + m, x + m);
    else if (hue < 240.0) result = vec3(m, x + m, c + m);
    else if (hue < 300.0) result = vec3(x + m, m, c + m);
    else result = vec3(c + m, m, x + m);
    return result;
}
float curveMatte(vec3 rgb, int index) {
    vec4 r = curve_qualifiers[index * 3], sft = curve_qualifiers[index * 3 + 1], f = curve_qualifiers[index * 3 + 2];
    if (f.y < 0.5) return 1.0;
    PRECISE float h, s, v; toHsv(rgb, h, s, v);
    float end = r.y, sample_h = h; bool all_h = r.x == 0.0 && r.y == 360.0;
    if (!all_h && end < r.x) { end += 360.0; if (sample_h < r.x) sample_h += 360.0; }
    float wh = all_h ? 1.0 : rangeWeight(sample_h, r.x, end, sft.z);
    float ws = rangeWeight(s * 100.0, r.z, r.w, sft.w);
    float y = clamp(rgb.r * 0.2126 + rgb.g * 0.7152 + rgb.b * 0.0722, 0.0, 1.0) * 100.0;
    return wh * ws * rangeWeight(y, sft.x, sft.y, f.x);
}
vec3 applyVideoCurves(vec3 src, int index) {
    PRECISE float y = clamp(src.r * 0.2126 + src.g * 0.7152 + src.b * 0.0722, 0.0, 1.0);
    float mapped = curveValue(index, 0, y);
    vec3 rgb = y > 1e-12 ? clamp(src * mapped / y, 0.0, 1.0) : vec3(mapped);
    rgb = vec3(curveValue(index, 1, rgb.r), curveValue(index, 2, rgb.g), curveValue(index, 3, rgb.b));
    PRECISE float old_y = clamp(rgb.r * 0.2126 + rgb.g * 0.7152 + rgb.b * 0.0722, 0.0, 1.0);
    PRECISE float h, s, v; toHsv(rgb, h, s, v);
    PRECISE float hue_offset = curveOffset(index, 4, h / 360.0);
    h += hue_offset * (360.0 / 255.0);
    s += curveOffset(index, 5, h / 360.0); PRECISE float dy = curveOffset(index, 6, h / 360.0);
    s += curveOffset(index, 7, old_y); s += curveOffset(index, 8, s);
    PRECISE vec3 adj = fromHsv(h, s, v); PRECISE float target_y = clamp(old_y + dy, 0.0, 1.0);
    if (dy != 0.0) {
        PRECISE float adj_y = adj.r * 0.2126 + adj.g * 0.7152 + adj.b * 0.0722;
        if (adj_y > 1e-12) adj = clamp(adj * target_y / adj_y, 0.0, 1.0);
    }
    return floor(clamp(mix(src, adj, curveMatte(src, index)), 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
int mappedX(int i) { return lookup_x[i / 4][i % 4]; }
int mappedY(int i) { return lookup_y[i / 4][i % 4]; }
vec3 adjustColor(vec3 rgb, int index) {
    if (curve_qualifiers[index * 3 + 2].z > 0.5) return applyVideoCurves(rgb, index);
    int base = index * 4;
    vec4 affine = color_adjustments[base];
    vec4 lift_saturation = color_adjustments[base + 1];
    vec4 gamma_gain_red = color_adjustments[base + 2];
    vec4 gain_green_blue = color_adjustments[base + 3];
    rgb = rgb * affine.x + affine.yzw;
    float shadow_luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float shadow_amount = clamp(1.0 - shadow_luma, 0.0, 1.0);
    float shadow_weight = shadow_amount * shadow_amount;
    rgb += lift_saturation.rgb * shadow_weight;
    if (gamma_gain_red.x != 1.0) rgb.r = pow(max(rgb.r, 0.0), gamma_gain_red.x);
    if (gamma_gain_red.y != 1.0) rgb.g = pow(max(rgb.g, 0.0), gamma_gain_red.y);
    if (gamma_gain_red.z != 1.0) rgb.b = pow(max(rgb.b, 0.0), gamma_gain_red.z);
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = (vec3(luma) + (rgb - vec3(luma)) * lift_saturation.w) *
        vec3(gamma_gain_red.w, gain_green_blue.xy);
    float log_luma = clamp(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    float shadows = 1.0 - smoothstep(0.20, 0.45, log_luma);
    float highlights = smoothstep(0.55, 0.80, log_luma);
    float midtones = clamp(1.0 - shadows - highlights, 0.0, 1.0);
    int log_base = index * 3;
    rgb += log_adjustments[log_base].rgb * shadows +
        log_adjustments[log_base + 1].rgb * midtones +
        log_adjustments[log_base + 2].rgb * highlights;
    return floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
void main() {
    ivec2 size = textureSize(source_image, 0);
    ivec2 pixel;
    if (use_affine) {
        PRECISE GEOMETRY_VEC2 point=GEOMETRY_VEC2(gl_FragCoord.x,canvas_height-gl_FragCoord.y);
        PRECISE GEOMETRY_VEC2 source_point=affine_x*point.x+affine_y*point.y+affine_translation;
        if(any(isnan(source_point)) || any(isinf(source_point)) || any(lessThan(source_point,GEOMETRY_VEC2(0))) || any(greaterThanEqual(source_point,GEOMETRY_VEC2(size))))discard;
        pixel=ivec2(floor(source_point));
    } else if (axis_aligned) {
        pixel = ivec2(mappedX(int(gl_FragCoord.x)),
            mappedY(int(canvas_height - gl_FragCoord.y)));
        if (any(lessThan(pixel, ivec2(0)))) discard;
    } else {
        PRECISE GEOMETRY_VEC2 d = GEOMETRY_VEC2(gl_FragCoord.x, canvas_height - gl_FragCoord.y) - center;
        PRECISE GEOMETRY_VEC2 local = GEOMETRY_VEC2(rotation_cs.x * d.x + rotation_cs.y * d.y,
                     -rotation_cs.y * d.x + rotation_cs.x * d.y);
        if (any(greaterThan(abs(local), displayed_size * 0.5))) discard;
        PRECISE GEOMETRY_VEC2 sample_position = (local / displayed_size + 0.5) * GEOMETRY_VEC2(size);
        pixel = clamp(ivec2(floor(sample_position)),
                       ivec2(0), size - ivec2(1));
    }
    if (source_bottom_left) pixel.y = size.y - 1 - pixel.y;
    vec4 source = texelFetch(source_image, pixel, 0);
    for (int i = 0; i < color_adjustment_count; ++i) {
        source.rgb = adjustColor(source.rgb, i);
        source.rgb = floor(clamp(source.rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
    }
    color = vec4(source.rgb, source.a * opacity);
    if(blend_mode!=0) {
        vec4 b=texelFetch(backdrop_image,ivec2(gl_FragCoord.xy),0);
        vec3 cb=b.a>0.0 ? clamp(b.rgb/b.a,0.0,1.0) : vec3(0);
        vec3 mixed=source.rgb;
        if(blend_mode==1)mixed=cb*source.rgb;
        else if(blend_mode==2)mixed=1.0-(1.0-cb)*(1.0-source.rgb);
        else if(blend_mode==3)mixed=mix(1.0-2.0*(1.0-cb)*(1.0-source.rgb),2.0*cb*source.rgb,lessThanEqual(cb,vec3(0.5)));
        else if(blend_mode==4)mixed=min(vec3(1),cb+source.rgb);
        else if(blend_mode==5)mixed=abs(cb-source.rgb);
        float a=color.a;
        color=vec4((1.0-a)*b.rgb+a*(1.0-b.a)*source.rgb+a*b.a*mixed,a+b.a*(1.0-a));
    }
}
)GLSL";
constexpr char image_fragment_shader_prefix[] = R"GLSL(
uniform sampler2D background_image;
uniform sampler2D foreground_image;
uniform sampler2D mask_image;
uniform sampler2D curve_lookup;
uniform bool mask_bottom_left;
uniform bool has_track_matte;
uniform int track_matte_mode;
uniform bool background_bottom_left;
uniform bool foreground_bottom_left;
uniform int operation;
uniform int canvas_height;
uniform GEOMETRY_VEC2 center;
uniform GEOMETRY_VEC2 rotation_cs;
uniform bool use_affine;
uniform GEOMETRY_VEC2 affine_x;
uniform GEOMETRY_VEC2 affine_y;
uniform GEOMETRY_VEC2 affine_translation;
uniform GEOMETRY_VEC2 scale_opacity;
uniform GEOMETRY_VEC2 merge_scale;
uniform ivec2 merge_origin;
uniform ivec2 merge_size;
uniform ivec2 shadow_origin;
uniform GEOMETRY_VEC2 shadow_delta;
uniform ivec3 shadow_rgb;
uniform GEOMETRY_VEC2 shadow_strength;
uniform ivec2 glow_origin;
uniform ivec2 glow_blur_origin;
uniform ivec3 glow_rgb;
uniform GEOMETRY_VEC2 glow_strength;
uniform int glow_mode;
uniform int color_count;
uniform bool use_refined_matte;
uniform vec4 matte_parameters;
uniform int matte_radius;
uniform int matte_horizontal;
layout(std140) uniform ColorAdjustments { vec4 color_adjustments[1024]; };
layout(std140) uniform LogAdjustments { vec4 log_adjustments[768]; };
layout(std140) uniform CurveQualifiers { vec4 curve_qualifiers[768]; };
out vec4 color;
float curveValue(int index, int curve, float input_value) {
    int x = int(floor(clamp(input_value, 0.0, 1.0) * 255.0 + 0.50001));
    return floor(texelFetch(curve_lookup, ivec2(x, index * 9 + curve), 0).r * 255.0 + 0.5) / 255.0;
}
float curveOffset(int index, int curve, float input_value) {
    return (curveValue(index, curve, input_value) * 255.0 - 128.0) / 127.0;
}
float rangeWeight(float value, float minimum, float maximum, float softness) {
    float lower = softness <= 0.0 ? float(value >= minimum) : smoothstep(minimum - softness, minimum + softness, value);
    float upper = softness <= 0.0 ? float(value <= maximum) : 1.0 - smoothstep(maximum - softness, maximum + softness, value);
    return min(lower, upper);
}
vec3 toHsv(vec3 rgb, out float hue, out float saturation, out float value) {
    float maximum = max(max(rgb.r, rgb.g), rgb.b);
    float minimum = min(min(rgb.r, rgb.g), rgb.b);
    float delta = maximum - minimum;
    hue = 0.0; saturation = maximum <= 0.0 ? 0.0 : delta / maximum; value = maximum;
    if (delta > 0.0) {
        if (maximum == rgb.r) hue = 60.0 * mod((rgb.g - rgb.b) / delta, 6.0);
        else if (maximum == rgb.g) hue = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
        else hue = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
        if (hue < 0.0) hue += 360.0;
    }
    return vec3(hue, saturation, value);
}
vec3 fromHsv(float hue, float saturation, float value) {
    hue = mod(hue, 360.0); if (hue < 0.0) hue += 360.0;
    saturation = clamp(saturation, 0.0, 1.0); value = clamp(value, 0.0, 1.0);
    float chroma = value * saturation;
    float x = chroma * (1.0 - abs(mod(hue / 60.0, 2.0) - 1.0));
    float m = value - chroma;
    if (hue < 60.0) return vec3(chroma + m, x + m, m);
    if (hue < 120.0) return vec3(x + m, chroma + m, m);
    if (hue < 180.0) return vec3(m, chroma + m, x + m);
    if (hue < 240.0) return vec3(m, x + m, chroma + m);
    if (hue < 300.0) return vec3(x + m, m, chroma + m);
    return vec3(chroma + m, m, x + m);
}
float curveMatte(vec3 rgb, int index) {
    vec4 ranges = curve_qualifiers[index * 3];
    vec4 softness = curve_qualifiers[index * 3 + 1];
    vec4 flags = curve_qualifiers[index * 3 + 2];
    if (flags.y < 0.5) return 1.0;
    float hue, saturation, value;
    toHsv(rgb, hue, saturation, value);
    float end = ranges.y, sample_hue = hue;
    bool all_hues = ranges.x == 0.0 && ranges.y == 360.0;
    if (!all_hues && end < ranges.x) { end += 360.0; if (sample_hue < ranges.x) sample_hue += 360.0; }
    float wh = all_hues ? 1.0 : rangeWeight(sample_hue, ranges.x, end, softness.z);
    float ws = rangeWeight(saturation * 100.0, ranges.z, ranges.w, softness.w);
    float y = clamp(rgb.r * 0.2126 + rgb.g * 0.7152 + rgb.b * 0.0722, 0.0, 1.0) * 100.0;
    return wh * ws * rangeWeight(y, softness.x, softness.y, flags.x);
}
vec3 applyVideoCurves(vec3 source_rgb, int index, float external_matte) {
    float y = clamp(source_rgb.r * 0.2126 + source_rgb.g * 0.7152 + source_rgb.b * 0.0722, 0.0, 1.0);
    float mapped = curveValue(index, 0, y);
    vec3 rgb = y > 1e-12 ? clamp(source_rgb * (mapped / y), 0.0, 1.0) : vec3(mapped);
    rgb = vec3(curveValue(index, 1, rgb.r), curveValue(index, 2, rgb.g), curveValue(index, 3, rgb.b));
    float old_y = clamp(rgb.r * 0.2126 + rgb.g * 0.7152 + rgb.b * 0.0722, 0.0, 1.0);
    float hue, saturation, value; toHsv(rgb, hue, saturation, value);
    float hue_offset = curveOffset(index, 4, hue / 360.0);
    hue += hue_offset * (360.0 / 255.0);
    saturation += curveOffset(index, 5, hue / 360.0);
    float luminance_delta = curveOffset(index, 6, hue / 360.0);
    saturation += curveOffset(index, 7, old_y);
    saturation += curveOffset(index, 8, saturation);
    vec3 adjusted = fromHsv(hue, saturation, value);
    float target_y = clamp(old_y + luminance_delta, 0.0, 1.0);
    if (luminance_delta != 0.0) {
        float adjusted_y = adjusted.r * 0.2126 + adjusted.g * 0.7152 + adjusted.b * 0.0722;
        if (adjusted_y > 1e-12) adjusted = clamp(adjusted * (target_y / adjusted_y), 0.0, 1.0);
    }
    float matte = external_matte >= 0.0 ? external_matte : curveMatte(source_rgb, index);
    return floor(clamp(mix(source_rgb, adjusted, matte), 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
vec3 adjustColor(vec3 rgb, int index, float external_matte) {
    if (curve_qualifiers[index * 3 + 2].z > 0.5) return applyVideoCurves(rgb, index, external_matte);
    int base = index * 4;
    vec4 affine = color_adjustments[base];
    vec4 lift_saturation = color_adjustments[base + 1];
    vec4 gamma_gain_red = color_adjustments[base + 2];
    vec4 gain_green_blue = color_adjustments[base + 3];
    rgb = rgb * affine.x + affine.yzw;
    float shadow_luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float shadow_amount = clamp(1.0 - shadow_luma, 0.0, 1.0);
    float shadow_weight = shadow_amount * shadow_amount;
    rgb += lift_saturation.rgb * shadow_weight;
    if (gamma_gain_red.x != 1.0) rgb.r = pow(max(rgb.r, 0.0), gamma_gain_red.x);
    if (gamma_gain_red.y != 1.0) rgb.g = pow(max(rgb.g, 0.0), gamma_gain_red.y);
    if (gamma_gain_red.z != 1.0) rgb.b = pow(max(rgb.b, 0.0), gamma_gain_red.z);
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = (vec3(luma) + (rgb - vec3(luma)) * lift_saturation.w) *
        vec3(gamma_gain_red.w, gain_green_blue.xy);
    float log_luma = clamp(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    float shadows = 1.0 - smoothstep(0.20, 0.45, log_luma);
    float highlights = smoothstep(0.55, 0.80, log_luma);
    float midtones = clamp(1.0 - shadows - highlights, 0.0, 1.0);
    int log_base = index * 3;
    rgb += log_adjustments[log_base].rgb * shadows +
        log_adjustments[log_base + 1].rgb * midtones +
        log_adjustments[log_base + 2].rgb * highlights;
    return floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
)GLSL";
constexpr char image_fragment_shader[] = R"GLSL(
vec4 background(ivec2 pixel) {
    if (background_bottom_left) pixel.y = textureSize(background_image, 0).y - 1 - pixel.y;
    return texelFetch(background_image, pixel, 0);
}
vec4 foreground(ivec2 pixel) {
    if (foreground_bottom_left) pixel.y = textureSize(foreground_image, 0).y - 1 - pixel.y;
    return texelFetch(foreground_image, pixel, 0);
}
int shadowAlphaByte(ivec2 p) {
    ivec2 size=textureSize(foreground_image,0);
    if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,size)))return 0;
    return int(floor(foreground(p).a*255.0+0.5));
}
int glowAlphaByte(ivec2 p) {
    ivec2 size=textureSize(foreground_image,0);
    if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,size)))return 0;
    return int(floor(foreground(p).a*255.0+0.5));
}
// Eight-bit long division keeps the exact weighted product below INT_MAX.
// GLSL 1.50 needs neither int64 nor FP64 for RGBA8 half-up rounding.
int matteAlpha(int alpha,int numerator) {
    int quotient=0,remainder=0;
    for(int bit=7;bit>=0;--bit) {
        remainder=2*remainder+((alpha>>bit)&1)*numerator;
        quotient=2*quotient+remainder/650250000;
        remainder=remainder%650250000;
    }
    return quotient+(remainder>=325125000?1:0);
}
void main() {
    ivec2 pixel = ivec2(int(gl_FragCoord.x), canvas_height - 1 - int(gl_FragCoord.y));
    ivec2 size = textureSize(background_image, 0);
    if (operation == 11) {
        PRECISE GEOMETRY_VEC2 d = GEOMETRY_VEC2(pixel) + 0.5 - center;
        PRECISE GEOMETRY_VEC2 local = GEOMETRY_VEC2(rotation_cs.x * d.x + rotation_cs.y * d.y,
            -rotation_cs.y * d.x + rotation_cs.x * d.y);
        PRECISE GEOMETRY_VEC2 mapped = local / scale_opacity.x + GEOMETRY_VEC2(size) * 0.5;
        ivec2 source_pixel = ivec2(floor(mapped));
        ivec4 adjusted = ivec4(0, 0, 0, 255);
        if (all(greaterThanEqual(source_pixel, ivec2(0))) &&
            all(lessThan(source_pixel, textureSize(foreground_image, 0)))) {
            adjusted = ivec4(floor(foreground(source_pixel) * 255.0 + 0.5));
            vec3 rgb = vec3(adjusted.rgb) / 255.0;
            for (int i = 0; i < color_count; ++i) {
                rgb = adjustColor(rgb, i, -1.0);
                rgb = floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
            }
            adjusted.rgb = ivec3(floor(rgb * 255.0 + 0.5));
        }
        ivec4 original = ivec4(floor(background(pixel) * 255.0 + 0.5));
        float adjustment_opacity = float(scale_opacity.y);
        vec3 mixed = vec3(original.rgb) * (1.0 - adjustment_opacity) +
            vec3(adjusted.rgb) * adjustment_opacity;
        color = vec4(floor(mixed + 0.5) / 255.0, 1.0);
        return;
    }
    if (operation == 1) {
        PRECISE GEOMETRY_VEC2 d = GEOMETRY_VEC2(pixel) + 0.5 - center;
        PRECISE GEOMETRY_VEC2 local = GEOMETRY_VEC2(rotation_cs.x * d.x + rotation_cs.y * d.y,
            -rotation_cs.y * d.x + rotation_cs.x * d.y);
        if(use_affine) {
            PRECISE GEOMETRY_VEC2 point=GEOMETRY_VEC2(pixel)+0.5;
            PRECISE GEOMETRY_VEC2 source_point=affine_x*point.x+affine_y*point.y+affine_translation;
            if(any(isnan(source_point)) || any(isinf(source_point)) || any(lessThan(source_point,GEOMETRY_VEC2(0))) || any(greaterThanEqual(source_point,GEOMETRY_VEC2(size)))) {color=vec4(0);return;}
            pixel=ivec2(floor(source_point));
        } else pixel = ivec2(floor(local / scale_opacity.x + GEOMETRY_VEC2(size) * 0.5));
        if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) { color = vec4(0); return; }
        color = background(pixel);
        color.a = float(floor(GEOMETRY_VEC2(floor(color.a * 255.0 + 0.5), 0).x * scale_opacity.y + 0.5)) / 255.0;
        return;
    }
    if(operation==8) {
        ivec2 source_pixel=pixel-shadow_origin;
        ivec4 original=any(lessThan(source_pixel,ivec2(0)))||any(greaterThanEqual(source_pixel,size))?ivec4(0):ivec4(floor(background(source_pixel)*255.0+0.5));
        PRECISE GEOMETRY_VEC2 point=GEOMETRY_VEC2(pixel)-shadow_delta;
        ivec2 p=ivec2(floor(point));
        PRECISE GEOMETRY_VEC2 f=point-GEOMETRY_VEC2(p);
        PRECISE GEOMETRY_VEC2 top=GEOMETRY_VEC2(shadowAlphaByte(p),shadowAlphaByte(p+ivec2(1,0)))/255.0;
        PRECISE GEOMETRY_VEC2 bottom=GEOMETRY_VEC2(shadowAlphaByte(p+ivec2(0,1)),shadowAlphaByte(p+ivec2(1,1)))/255.0;
        PRECISE GEOMETRY_VEC2 rows=GEOMETRY_VEC2((1.0-f.x)*top.x+f.x*top.y,(1.0-f.x)*bottom.x+f.x*bottom.y);
        PRECISE GEOMETRY_VEC2 alpha=GEOMETRY_VEC2(((1.0-f.y)*rows.x+f.y*rows.y)*shadow_strength.x/100.0,GEOMETRY_VEC2(original.a).x/255.0);
        PRECISE GEOMETRY_VEC2 ao=GEOMETRY_VEC2(alpha.y+alpha.x*(1.0-alpha.y));
        color.a=float(floor(ao.x*255.0+0.5)/255.0);
        for(int channel=0;channel<3;++channel) {
            PRECISE GEOMETRY_VEC2 numerator=GEOMETRY_VEC2(original[channel])*alpha.y+GEOMETRY_VEC2(shadow_rgb[channel])*alpha.x*(1.0-alpha.y);
            color[channel]=ao.x>0.0?float(floor(numerator.x/ao.x+0.5)/255.0):0.0;
        }
        return;
    }
    if(operation==9) {
        ivec2 source_pixel=pixel-glow_origin;
        ivec4 original=any(lessThan(source_pixel,ivec2(0)))||any(greaterThanEqual(source_pixel,size))
            ? ivec4(0) : ivec4(floor(background(source_pixel)*255.0+0.5));
        PRECISE GEOMETRY_VEC2 blurred=GEOMETRY_VEC2(glowAlphaByte(pixel+glow_blur_origin)/255.0,0);
        PRECISE GEOMETRY_VEC2 source_alpha=GEOMETRY_VEC2(original.a/255.0,0);
        PRECISE GEOMETRY_VEC2 outer_alpha=(glow_mode==1?GEOMETRY_VEC2(0):blurred)*glow_strength.x/100.0;
        PRECISE GEOMETRY_VEC2 inner_mix=(glow_mode==0?GEOMETRY_VEC2(0):
            max(GEOMETRY_VEC2(0),GEOMETRY_VEC2(1)-blurred))*glow_strength.x/100.0;
        PRECISE GEOMETRY_VEC2 base_alpha=source_alpha+outer_alpha*(1.0-source_alpha);
        PRECISE GEOMETRY_VEC2 output_alpha=base_alpha;
        color.a=float(floor(output_alpha.x*255.0+0.5)/255.0);
        for(int channel=0;channel<3;++channel) {
            PRECISE GEOMETRY_VEC2 source_value=GEOMETRY_VEC2(original[channel]/255.0,0);
            PRECISE GEOMETRY_VEC2 glow_value=GEOMETRY_VEC2(glow_rgb[channel]/255.0,0);
            PRECISE GEOMETRY_VEC2 inner_value=source_value*source_alpha*(1.0-inner_mix)+
                glow_value*source_alpha*inner_mix;
            PRECISE GEOMETRY_VEC2 premultiplied=inner_value+
                glow_value*outer_alpha*(1.0-source_alpha);
            color[channel]=output_alpha.x>0.0
                ? float(floor(premultiplied.x/output_alpha.x*255.0+0.5)/255.0) : 0.0;
        }
        return;
    }
    color = background(pixel);
    ivec2 matte_pixel = ivec2(gl_FragCoord.xy);
    if (operation == 12) {
        float coverage = clamp(curveMatte(color.rgb, 0), 0.0, 1.0);
        color = vec4(floor(coverage * 255.0 + 0.5) / 255.0, 0.0, 0.0, 1.0);
        return;
    }
    if (operation == 13) {
        int values[9]; int count = 0;
        ivec2 matte_size = textureSize(mask_image, 0);
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            ivec2 pos = clamp(matte_pixel + ivec2(x, y), ivec2(0), matte_size - 1);
            values[count++] = int(floor(texelFetch(mask_image, pos, 0).r * 255.0 + 0.5));
        }
        for (int i = 1; i < 9; ++i) {
            int value = values[i], j = i - 1;
            while (j >= 0 && values[j] > value) { values[j + 1] = values[j]; --j; }
            values[j + 1] = value;
        }
        float current = floor(texelFetch(mask_image, matte_pixel, 0).r * 255.0 + 0.5);
        float amount = clamp(matte_parameters.x / 100.0, 0.0, 1.0);
        float filtered = floor(clamp(mix(current, float(values[4]), amount), 0.0, 255.0) + 0.5);
        color = vec4(filtered / 255.0, 0.0, 0.0, 1.0);
        return;
    }
    if (operation == 14) {
        float value = texelFetch(mask_image, matte_pixel, 0).r;
        float black = clamp(matte_parameters.x / 100.0, 0.0, 1.0);
        float white = clamp(matte_parameters.y / 100.0, 0.0, 1.0);
        float shift = clamp(matte_parameters.z / 100.0, -1.0, 1.0);
        float normalized = clamp((value - black) / (white - black), 0.0, 1.0);
        float adjusted = floor(clamp(normalized * 255.0 + shift * 255.0, 0.0, 255.0) + 0.5);
        color = vec4(adjusted / 255.0, 0.0, 0.0, 1.0);
        return;
    }
    if (operation == 15) {
        int radius = clamp(matte_radius, 0, 8);
        float sigma = max(0.5, float(radius) / 2.0), total = 0.0, sum = 0.0;
        ivec2 matte_size = textureSize(mask_image, 0);
        for (int offset = -radius; offset <= radius; ++offset) {
            float weight = exp(-float(offset * offset) / (2.0 * sigma * sigma));
            ivec2 step = matte_horizontal != 0 ? ivec2(offset, 0) : ivec2(0, offset);
            ivec2 pos = clamp(matte_pixel + step, ivec2(0), matte_size - 1);
            sum += floor(texelFetch(mask_image, pos, 0).r * 255.0 + 0.5) * weight;
            total += weight;
        }
        float blurred = floor(clamp(sum / max(total, 1e-12), 0.0, 255.0) + 0.5);
        color = vec4(blurred / 255.0, 0.0, 0.0, 1.0);
        return;
    }
    if (operation == 2) {
        for (int i = 0; i < color_count; ++i) {
            float matte = use_refined_matte
                ? texelFetch(mask_image, matte_pixel, 0).r : -1.0;
            color.rgb = adjustColor(color.rgb, i, matte);
            color.rgb = floor(clamp(color.rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
        }
        return;
    }
    if (operation == 7) {
        ivec4 matte=ivec4(0);
        if(has_track_matte) {
            ivec2 mp=pixel;
            if(mask_bottom_left)mp.y=textureSize(mask_image,0).y-1-mp.y;
            matte=ivec4(floor(texelFetch(mask_image,mp,0)*255.0+0.5));
        }
        int weighted=2550000;
        if(track_matte_mode>=2)weighted=matte.r*2126+matte.g*7152+matte.b*722;
        int numerator=matte.a*weighted;
        if(track_matte_mode==1 || track_matte_mode==3)numerator=650250000-numerator;
        color.a=float(matteAlpha(int(floor(color.a*255.0+0.5)),numerator))/255.0;
        return;
    }
    if (operation == 4 || operation == 5) {
        ivec2 mp = pixel;
        if (mask_bottom_left) mp.y = textureSize(mask_image,0).y-1-mp.y;
        ivec3 mask_bytes=ivec3(floor(texelFetch(mask_image,mp,0).rgb*255.0+0.5));
        int coverage=(mask_bytes.r*2126+mask_bytes.g*7152+mask_bytes.b*722+5000)/10000;
        ivec4 bg=ivec4(floor(color*255.0+0.5));
        if (operation == 4) { color.a=float((bg.a*coverage+127)/255)/255.0; return; }
        if (coverage==0) return;
        ivec4 fg=ivec4(floor(foreground(pixel)*255.0+0.5));
        if (coverage==255) { color=vec4(fg)/255.0; return; }
        int ba=bg.a*(255-coverage), fa=fg.a*coverage, alpha=ba+fa;
        ivec3 numerator=bg.rgb*ba+fg.rgb*fa;
        ivec3 rgb=alpha==0 ? ivec3(0) : (numerator+ivec3(alpha/2))/alpha;
        color=vec4(vec3(rgb),float((alpha+127)/255))/255.0;
        return;
    }
    if (operation == 3) {
        ivec2 local = pixel - merge_origin;
        if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, merge_size))) return;
        ivec2 position = clamp(ivec2(GEOMETRY_VEC2(local) / merge_scale.x), ivec2(0),
            textureSize(foreground_image, 0) - ivec2(1));
        ivec4 bg = ivec4(floor(color * 255.0 + 0.5));
        ivec4 fg = ivec4(floor(foreground(position) * 255.0 + 0.5));
        int alpha = fg.a * 255 + bg.a * (255 - fg.a);
        ivec3 numerator = fg.rgb * fg.a * 255 + bg.rgb * bg.a * (255 - fg.a);
        ivec3 rgb = alpha == 0 ? ivec3(0) : (numerator + ivec3(alpha / 2)) / alpha;
        color = vec4(vec3(rgb), float((alpha + 127) / 255)) / 255.0;
    }
}
)GLSL";
constexpr char effect_fragment_shader[] = R"GLSL(#version 150 core
uniform sampler2D source_image;
uniform int effect_kind;
uniform vec4 color_affine;
uniform vec4 lift_saturation;
uniform vec4 gamma_gain_red;
uniform vec4 gain_green_blue;
uniform vec3 log_shadows;
uniform vec3 log_midtones;
uniform vec3 log_highlights;
uniform int blur_radius;
uniform bool horizontal_pass;
uniform bool premultiply_alpha;
uniform bool unpremultiply_alpha;
out vec4 color;
vec3 adjustColor(vec3 rgb) {
    rgb = rgb * color_affine.x + color_affine.yzw;
    float shadow_luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float shadow_amount = clamp(1.0 - shadow_luma, 0.0, 1.0);
    rgb += lift_saturation.rgb * shadow_amount * shadow_amount;
    if (gamma_gain_red.x != 1.0) rgb.r = pow(max(rgb.r, 0.0), gamma_gain_red.x);
    if (gamma_gain_red.y != 1.0) rgb.g = pow(max(rgb.g, 0.0), gamma_gain_red.y);
    if (gamma_gain_red.z != 1.0) rgb.b = pow(max(rgb.b, 0.0), gamma_gain_red.z);
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = (vec3(luma) + (rgb - vec3(luma)) * lift_saturation.w) *
        vec3(gamma_gain_red.w, gain_green_blue.xy);
    float log_luma = clamp(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    float shadows = 1.0 - smoothstep(0.20, 0.45, log_luma);
    float highlights = smoothstep(0.55, 0.80, log_luma);
    float midtones = clamp(1.0 - shadows - highlights, 0.0, 1.0);
    rgb += log_shadows * shadows + log_midtones * midtones +
        log_highlights * highlights;
    return floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
void main() {
    ivec2 size = textureSize(source_image, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (effect_kind == 0) {
        vec4 source = texelFetch(source_image, pixel, 0);
        source.rgb = adjustColor(source.rgb);
        color = source;
        return;
    }
    if (premultiply_alpha) {
        vec4 source = texelFetch(source_image, pixel, 0);
        ivec3 bytes = ivec3(floor(source.rgb * 255.0 + 0.5));
        int alpha = int(floor(source.a * 255.0 + 0.5));
        bytes = (bytes * alpha + ivec3(127)) / 255;
        color = vec4(vec3(bytes) / 255.0, source.a);
        return;
    }
    if (unpremultiply_alpha) {
        vec4 source = texelFetch(source_image, pixel, 0);
        int alpha = int(floor(source.a * 255.0 + 0.5));
        ivec3 bytes = ivec3(floor(source.rgb * 255.0 + 0.5));
        if (alpha == 0) bytes = ivec3(0);
        else bytes = min(ivec3(255), (bytes * 255 + ivec3(alpha / 2)) / alpha);
        color = vec4(vec3(bytes) / 255.0, source.a);
        return;
    }
    ivec2 step = horizontal_pass ? ivec2(1, 0) : ivec2(0, 1);
    ivec4 sum = ivec4(0);
    for (int offset = -blur_radius; offset <= blur_radius; ++offset) {
        ivec2 sample_pixel = clamp(pixel + step * offset, ivec2(0), size - ivec2(1));
        sum += ivec4(floor(texelFetch(source_image, sample_pixel, 0) * 255.0 + 0.5));
    }
    int divisor = blur_radius * 2 + 1;
    ivec4 averaged = (sum + ivec4(divisor / 2)) / divisor;
    color = vec4(averaged) / 255.0;
}
)GLSL";
} // namespace

struct OpenGlTexturePoolBudget::Impl {
    mutable std::mutex mutex;
    std::uint64_t bytes = 0;
    unsigned targets = 0;
    std::uint64_t byte_limit = maximum_bytes;
    unsigned target_limit = maximum_targets;
};
std::uint64_t OpenGlTexturePoolBudget::byteLimit() const noexcept { return impl_->byte_limit; }
unsigned OpenGlTexturePoolBudget::targetLimit() const noexcept { return impl_->target_limit; }
bool OpenGlTexturePoolBudget::tryReserve(std::uint64_t bytes) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->targets >= impl_->target_limit || bytes > impl_->byte_limit - impl_->bytes) return false;
    ++impl_->targets; impl_->bytes += bytes; return true;
}
void OpenGlTexturePoolBudget::releaseReservation(std::uint64_t bytes) noexcept {
    std::lock_guard lock(impl_->mutex); --impl_->targets; impl_->bytes -= bytes;
}
OpenGlTexturePoolBudget::OpenGlTexturePoolBudget(std::uint64_t byte_limit, unsigned target_limit)
    : impl_(std::make_unique<Impl>()) {
    if (!byte_limit || byte_limit > 256ULL * 1024 * 1024 || !target_limit || target_limit > 8)
        throw std::invalid_argument("The texture pool limit must be within 256 MiB and eight targets.");
    impl_->byte_limit = byte_limit;
    impl_->target_limit = target_limit;
}
OpenGlTexturePoolBudget::~OpenGlTexturePoolBudget() = default;
std::uint64_t OpenGlTexturePoolBudget::bytes() const noexcept {
    std::lock_guard lock(impl_->mutex); return impl_->bytes;
}
unsigned OpenGlTexturePoolBudget::targets() const noexcept {
    std::lock_guard lock(impl_->mutex); return impl_->targets;
}

struct OpenGlTextureFrame::State {
    int width = 0, height = 0;
    GLuint texture = 0;
    std::uint64_t session = 0;
    QOpenGLContextGroup* group = nullptr;
    mutable std::mutex mutex;
    std::atomic_bool valid{true};
    GLsync producer = nullptr;
    std::vector<GLsync> consumers;
    bool unsafe = false;
};
OpenGlTextureFrame::OpenGlTextureFrame(std::shared_ptr<State> state) : state_(std::move(state)) {}
OpenGlTextureFrame::~OpenGlTextureFrame() = default;
OpenGlTextureFrame::OpenGlTextureFrame(std::shared_ptr<VulkanImage> image) : vulkan_(std::move(image)) {}
bool OpenGlTextureFrame::bottomLeftOrigin() const noexcept { return !vulkan_; }
GraphicsBackend OpenGlTextureFrame::backend() const noexcept { return vulkan_ ? GraphicsBackend::Vulkan : GraphicsBackend::OpenGl; }
int OpenGlTextureFrame::width() const noexcept { return vulkan_ ? vulkan_->width : state_->width; }
int OpenGlTextureFrame::height() const noexcept { return vulkan_ ? vulkan_->height : state_->height; }
unsigned OpenGlTextureFrame::texture() const noexcept { return vulkan_ ? 0 : state_->texture; }
std::uint64_t OpenGlTextureFrame::session() const noexcept { return vulkan_ ? reinterpret_cast<std::uintptr_t>(vulkan_->device.get()) : state_->session; }
bool OpenGlTextureFrame::valid() const noexcept { return vulkan_ ? vulkan_->device->valid() : state_->valid.load(); }
bool OpenGlTextureFrame::beginUse(QOpenGLContext* context, std::string& cause, std::int64_t& code) const {
    code = 0;
    if (vulkan_) { cause = "A Vulkan lease cannot be used by an OpenGL consumer."; return false; }
    std::lock_guard lock(state_->mutex);
    if (!valid() || !context || QOpenGLContext::currentContext() != context ||
        context->shareGroup() != state_->group || state_->unsafe || state_->consumers.size() >= 128) {
        cause = "The texture lease or consumer sharing context is unavailable."; return false;
    }
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context);
    if (!gl || !gl->initializeOpenGLFunctions()) {
        cause = "Consumer OpenGL functions are unavailable."; return false;
    }
    gl->glWaitSync(state_->producer, 0, GL_TIMEOUT_IGNORED);
    code = gl->glGetError();
    if (code) cause = "OpenGL failed to submit the producer fence wait.";
    return code == 0;
}
bool OpenGlTextureFrame::endUse(QOpenGLContext* context, std::string& cause, std::int64_t& code) const {
    code = 0;
    if (vulkan_) { cause = "A Vulkan lease cannot be used by an OpenGL consumer."; return false; }
    std::lock_guard lock(state_->mutex);
    if (!valid() || !context || QOpenGLContext::currentContext() != context ||
        context->shareGroup() != state_->group) {
        cause = "The texture consumer context was lost."; state_->unsafe = true; return false;
    }
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context);
    if (!gl || !gl->initializeOpenGLFunctions()) {
        cause = "Consumer OpenGL functions are unavailable."; state_->unsafe = true; return false;
    }
    if (state_->consumers.size() >= 128) {
        gl->glFinish();
        cause = "The bounded consumer fence registry is full.";
        state_->unsafe = true;
        return false;
    }
    const auto fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (fence) state_->consumers.push_back(fence);
    gl->glFlush();
    code = gl->glGetError();
    if (!fence || code) {
        // Exceptional recovery only: without a consumption fence, complete
        // this consumer's commands before its lease can be returned.
        gl->glFinish();
        cause = "OpenGL failed to publish the consumer fence."; state_->unsafe = true; return false;
    }
    return true;
}

struct OpenGlFrameCompositor::Impl {
    QOffscreenSurface* surface;
    std::unique_ptr<QOpenGLContext> context;
    QOpenGLFunctions_3_2_Core* gl = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> program;
    std::unique_ptr<QOpenGLShaderProgram> effect_program;
    std::unique_ptr<QOpenGLShaderProgram> image_program;
    const OpenGlImageOperation* image_operation = nullptr;
    const OpenGlRasterRequest* raster_request = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> raster_program;
    struct RasterTexture { GLuint texture = 0; qint64 key = 0; QSize size; std::uint64_t used = 0; unsigned pixel_bytes = 4; };
    std::unordered_map<std::string, RasterTexture> raster_textures;
    std::unique_ptr<QOpenGLFramebufferObject> raster_scratch[2];
    std::uint64_t raster_reserved = 0, raster_clock = 0;
    GLuint image_textures[3]{};
    std::uint64_t image_texture_bytes[3]{};
    std::uint64_t matte_upload_reserved[3]{};
    std::unique_ptr<detail::WindowsVideoInterop> video_interop;
    std::unique_ptr<QOpenGLFramebufferObject> output;
    std::unique_ptr<QOpenGLFramebufferObject> effect_scratch;
    std::unique_ptr<QOpenGLFramebufferObject> blend_backdrop;
    std::uint64_t blend_reserved = 0;
    GLuint texture = 0;
    GLuint effect_source_fbo = 0;
    GLuint matte_textures[2]{};
    int matte_width = 0, matte_height = 0;
    std::uint64_t matte_scratch_reserved = 0;
    GLuint vao = 0;
    GLuint curve_lut_texture = 0;
    std::uint64_t curve_lut_bytes = 0;
    GLuint lookup_buffers[5]{};
    std::uint64_t peak_known_bytes = 0;
    std::vector<GLint> source_lookup;
    int source_width = 0;
    int source_height = 0;
    int texture_limit = 0;
    bool precise_geometry = false;
    using Uniform2dv = void (QOPENGLF_APIENTRYP)(GLint, GLsizei, const GLdouble*);
    Uniform2dv uniform2dv = nullptr;
    OpenGlPrecisionPolicy precision;
    QOpenGLContext* share_context = nullptr;
    std::shared_ptr<OpenGlTexturePoolBudget> budget;
    struct Target {
        std::unique_ptr<QOpenGLFramebufferObject> output;
        std::shared_ptr<OpenGlTextureFrame::State> state;
        std::uint64_t bytes = 0;
    };
    std::vector<Target> targets;
    QOpenGLFramebufferObject* selected_output = nullptr;
    std::uint64_t session = 0;
    bool retiring = false;
    struct ReadbackSlot {
        GLuint buffer = 0;
        GLsync fence = nullptr;
        std::uint64_t ticket = 0;
        std::uint64_t bytes = 0;
        int width = 0;
        int height = 0;
        bool busy = false;
        bool preserve_alpha = false;
    };
    std::vector<ReadbackSlot> readback_slots;
    std::uint64_t next_readback_ticket = 0;

    explicit Impl(QOffscreenSurface* s, OpenGlPrecisionPolicy policy, QOpenGLContext* sharing,
        std::shared_ptr<OpenGlTexturePoolBudget> pool_budget)
        : surface(s), precision(policy), share_context(sharing),
          budget(pool_budget ? std::move(pool_budget) : std::make_shared<OpenGlTexturePoolBudget>()) {
        static std::atomic<std::uint64_t> next_session{0}; session = ++next_session;
    }
    ~Impl() {
        if (context && context->makeCurrent(surface)) {
            video_interop.reset();
            raster_program.reset();
            for (auto& pair : raster_textures) gl->glDeleteTextures(1, &pair.second.texture);
            raster_textures.clear();
            raster_scratch[0].reset(); raster_scratch[1].reset();
            for (auto& target : targets) {
                if (target.state) {
                    std::lock_guard lock(target.state->mutex);
                    target.state->valid.store(false);
                    if (target.state->unsafe) gl->glFinish(); // Exceptional teardown only.
                    if (target.state->producer) {
                        gl->glWaitSync(target.state->producer, 0, GL_TIMEOUT_IGNORED);
                        gl->glDeleteSync(target.state->producer);
                        target.state->producer = nullptr;
                    }
                    for (auto fence : target.state->consumers) {
                        gl->glWaitSync(fence, 0, GL_TIMEOUT_IGNORED); gl->glDeleteSync(fence);
                    }
                    target.state->consumers.clear();
                }
                target.output.reset();
            }
            output.reset();
            effect_scratch.reset();
            blend_backdrop.reset();
            discardMatteScratch();
            program.reset();
            effect_program.reset();
            image_program.reset();
            gl->glDeleteTextures(3, image_textures);
            if (curve_lut_texture) gl->glDeleteTextures(1, &curve_lut_texture);
            curve_lut_bytes = 0;
            if (effect_source_fbo) gl->glDeleteFramebuffers(1, &effect_source_fbo);
            if (texture) gl->glDeleteTextures(1, &texture);
            if (vao) gl->glDeleteVertexArrays(1, &vao);
            gl->glDeleteBuffers(5, lookup_buffers);
            for (auto& slot : readback_slots) {
                if (slot.fence) gl->glDeleteSync(slot.fence);
                if (slot.buffer) gl->glDeleteBuffers(1, &slot.buffer);
            }
            readback_slots.clear();
            context->doneCurrent();
        }
        {
            std::lock_guard lock(budget->impl_->mutex);
            budget->impl_->bytes -= raster_reserved;
        }
        for (auto& target : targets) {
            if (target.state) target.state->valid.store(false);
            std::lock_guard lock(budget->impl_->mutex);
            budget->impl_->bytes -= target.bytes; --budget->impl_->targets;
        }
    }
    OpenGlCompositionResult initialize() {
        if (!surface || !surface->isValid() || !QOpenGLContext::supportsThreadedOpenGL())
            return result(OpenGlCompositionStatus::Failed, "create-context",
                "A valid offscreen surface and threaded OpenGL support are required.");
        context = std::make_unique<QOpenGLContext>();
        auto requested = surface->format();
        requested.setRenderableType(QSurfaceFormat::OpenGL);
        requested.setVersion(3, 2);
        requested.setProfile(QSurfaceFormat::CoreProfile);
        context->setFormat(requested);
        if (share_context) context->setShareContext(share_context);
        if (!context->create() || !context->makeCurrent(surface))
            return result(OpenGlCompositionStatus::Failed, "create-context",
                "Cannot create or activate the worker OpenGL context.");
        const auto format = context->format();
        if (format.renderableType() != QSurfaceFormat::OpenGL ||
            format.profile() != QSurfaceFormat::CoreProfile ||
            format.majorVersion() < 3 ||
            (format.majorVersion() == 3 && format.minorVersion() < 2))
            return result(OpenGlCompositionStatus::Failed, "check-context",
                "OpenGL 3.2 Core is unavailable.");
        gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context.get());
        if (!gl || !gl->initializeOpenGLFunctions())
            return result(OpenGlCompositionStatus::Failed, "load-functions",
                "Cannot initialize OpenGL 3.2 Core functions.");
        const bool precise_shader_math = context->hasExtension("GL_ARB_gpu_shader5");
        precise_geometry = precision == OpenGlPrecisionPolicy::Automatic &&
            context->hasExtension("GL_ARB_gpu_shader_fp64") && precise_shader_math;
        if (precise_geometry) {
            uniform2dv = reinterpret_cast<Uniform2dv>(context->getProcAddress("glUniform2dv"));
            if (!uniform2dv) return result(OpenGlCompositionStatus::Failed, "load-functions",
                "The driver advertises precise geometry but cannot load glUniform2dv.");
        }
        const QByteArray fragment = QByteArray("#version 150 core\n") +
            (precise_geometry ?
                "#extension GL_ARB_gpu_shader_fp64 : require\n#extension GL_ARB_gpu_shader5 : require\n"
                "#define GEOMETRY_VEC2 dvec2\n#define PRECISE precise\n" :
                (precise_shader_math ?
                    "#extension GL_ARB_gpu_shader5 : require\n"
                    "#define GEOMETRY_VEC2 vec2\n#define PRECISE precise\n" :
                    "#define GEOMETRY_VEC2 vec2\n#define PRECISE\n")) + fragment_shader;
        program = std::make_unique<QOpenGLShaderProgram>();
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
            !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) ||
            !program->link())
            return result(OpenGlCompositionStatus::Failed, "compile-shaders",
                program->log().toStdString());
        effect_program = std::make_unique<QOpenGLShaderProgram>();
        if (!effect_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
            !effect_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                effect_fragment_shader) || !effect_program->link())
            return result(OpenGlCompositionStatus::Failed, "compile-effect-shaders",
                effect_program->log().toStdString());
        image_program = std::make_unique<QOpenGLShaderProgram>();
        const auto image_fragment = fragment.left(fragment.indexOf("uniform sampler2D")) +
            image_fragment_shader_prefix + image_fragment_shader;
        if (!image_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
            !image_program->addShaderFromSourceCode(QOpenGLShader::Fragment, image_fragment) || !image_program->link())
            return result(OpenGlCompositionStatus::Failed, "compile-image-shaders", image_program->log().toStdString());
        gl->glGenTextures(3, image_textures);
        gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
        gl->glGenTextures(1, &texture);
        gl->glGenFramebuffers(1, &effect_source_fbo);
        gl->glGenVertexArrays(1, &vao);
        GLint block_size = 0, fragment_blocks = 0;
        gl->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &block_size);
        gl->glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &fragment_blocks);
        if (block_size < 16384 || fragment_blocks < 5)
            return result(OpenGlCompositionStatus::Failed, "check-uniform-limits",
                "Geometry, color, Log, and Curve qualifier shader blocks are required.");
        gl->glGenBuffers(5, lookup_buffers);
        for (unsigned axis = 0; axis < 2; ++axis) {
            gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[axis]);
            gl->glBufferData(GL_UNIFORM_BUFFER, 16384, nullptr, GL_DYNAMIC_DRAW);
            const auto block = gl->glGetUniformBlockIndex(program->programId(),
                axis == 0 ? "SourceLookupX" : "SourceLookupY");
            gl->glUniformBlockBinding(program->programId(), block, axis);
        }
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[2]);
        gl->glBufferData(GL_UNIFORM_BUFFER, 1024 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        const auto adjustments_block = gl->glGetUniformBlockIndex(
            program->programId(), "ColorAdjustments");
        if (adjustments_block == GL_INVALID_INDEX)
            return result(OpenGlCompositionStatus::Failed, "compile-shaders",
                "The color adjustment shader block is unavailable.");
        gl->glUniformBlockBinding(program->programId(), adjustments_block, 2);
        const auto log_adjustments_block = gl->glGetUniformBlockIndex(
            program->programId(), "LogAdjustments");
        if (log_adjustments_block == GL_INVALID_INDEX)
            return result(OpenGlCompositionStatus::Failed, "compile-shaders",
                "The Log adjustment shader block is unavailable.");
        gl->glUniformBlockBinding(program->programId(), log_adjustments_block, 3);
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[3]);
        gl->glBufferData(GL_UNIFORM_BUFFER, 768 * 4 * sizeof(float), nullptr,
            GL_DYNAMIC_DRAW);
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[4]);
        gl->glBufferData(GL_UNIFORM_BUFFER, 768 * 4 * sizeof(float), nullptr,
            GL_DYNAMIC_DRAW);
        const auto qualifiers_block = gl->glGetUniformBlockIndex(
            program->programId(), "CurveQualifiers");
        if (qualifiers_block == GL_INVALID_INDEX)
            return result(OpenGlCompositionStatus::Failed, "compile-shaders",
                "The Color Curves qualifier shader block is unavailable.");
        gl->glUniformBlockBinding(program->programId(), qualifiers_block, 4);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 4, lookup_buffers[4]);
        gl->glGenTextures(1, &curve_lut_texture);
        gl->glBindTexture(GL_TEXTURE_2D, curve_lut_texture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const std::array<std::uint8_t, 256 * 9> identity_lut{};
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 9, 0, GL_RED,
            GL_UNSIGNED_BYTE, identity_lut.data());
        curve_lut_bytes = identity_lut.size();
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        const auto image_qualifiers_block = gl->glGetUniformBlockIndex(
            image_program->programId(), "CurveQualifiers");
        if (image_qualifiers_block == GL_INVALID_INDEX)
            return result(OpenGlCompositionStatus::Failed, "compile-image-shaders",
                "The Color Curves qualifier shader block is unavailable.");
        gl->glUniformBlockBinding(image_program->programId(), image_qualifiers_block, 4);
        rememberResourcePeak();
        return result(OpenGlCompositionStatus::Complete);
    }
    OpenGlCompositionResult activate() {
        if (!program || !program->isLinked()) {
            if (context) return result(OpenGlCompositionStatus::Failed, "initialize",
                "The previous context initialization failed.");
            return initialize();
        }
        if (QOpenGLContext::currentContext() != context.get() && !context->makeCurrent(surface))
            return result(OpenGlCompositionStatus::Failed, "make-current",
                "Cannot reactivate the worker OpenGL context.");
        return result(OpenGlCompositionStatus::Complete);
    }

    bool reserveRaster(std::uint64_t bytes) {
        std::lock_guard lock(budget->impl_->mutex);
        if (bytes > budget->impl_->byte_limit || budget->impl_->bytes > budget->impl_->byte_limit - bytes) return false;
        budget->impl_->bytes += bytes; raster_reserved += bytes; return true;
    }
    void releaseRaster(std::uint64_t bytes) {
        std::lock_guard lock(budget->impl_->mutex);
        budget->impl_->bytes -= bytes; raster_reserved -= bytes;
    }
    void discardMatteScratch() {
        if (matte_textures[0] || matte_textures[1])
            gl->glDeleteTextures(2, matte_textures);
        matte_textures[0] = matte_textures[1] = 0;
        matte_width = matte_height = 0;
        if (matte_scratch_reserved) {
            releaseRaster(matte_scratch_reserved);
            matte_scratch_reserved = 0;
        }
    }
    bool ensureMatteScratch(int width, int height) {
        if (matte_width == width && matte_height == height &&
            matte_textures[0] && matte_textures[1]) return true;
        discardMatteScratch();
        const auto per_texture = static_cast<std::uint64_t>(width) * height;
        const auto bytes = per_texture * 2;
        if (!reserveRaster(bytes)) return false;
        matte_scratch_reserved = bytes;
        gl->glGenTextures(2, matte_textures);
        for (unsigned index = 0; index < 2; ++index) {
            gl->glBindTexture(GL_TEXTURE_2D, matte_textures[index]);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0,
                GL_RED, GL_UNSIGNED_BYTE, nullptr);
            gl->glBindFramebuffer(GL_FRAMEBUFFER, effect_source_fbo);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, matte_textures[index], 0);
            if (!matte_textures[index] ||
                gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                discardMatteScratch();
                return false;
            }
        }
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, 0, 0);
        matte_width = width;
        matte_height = height;
        rememberResourcePeak();
        if (gl->glGetError() != GL_NO_ERROR) {
            discardMatteScratch();
            return false;
        }
        return true;
    }
    void discardMatteUploads() {
        for(unsigned i=0;i<3;++i)if(matte_upload_reserved[i]) {
            gl->glDeleteTextures(1,&image_textures[i]);gl->glGenTextures(1,&image_textures[i]);
            image_texture_bytes[i]=0;releaseRaster(matte_upload_reserved[i]);matte_upload_reserved[i]=0;
        }
    }
    void discardBlendBackdrop() {
        if(!blend_backdrop && !blend_reserved)return;
        blend_backdrop.reset();releaseRaster(blend_reserved);blend_reserved=0;
    }
    OpenGlCompositionResult renderRaster(const OpenGlRasterRequest& request,
        const OpenGlFrameCompositor::CancellationPredicate& cancel, OpenGlCompositionTimings& measured) {
        if (!selected_output) return result(OpenGlCompositionStatus::Failed, "raster-target", "No raster output target.");
        const int width = request.size.width(), height = request.size.height();
        bool blending = false;
        const bool adjustments=std::any_of(request.nodes.begin(),request.nodes.end(),[](const auto& node){return std::any_of(node.layers.begin(),node.layers.end(),[](const auto& layer){return layer.adjustment && (!effects::identityAdjustment(*layer.adjustment) || layer.blend_mode!=BlendMode::Normal);});});
        for (const auto& node : request.nodes) {
            if (!validBlendMode(node.blend_mode)) return result(OpenGlCompositionStatus::Unsupported,"raster-blend-mode","Invalid group blend mode.");
            blending |= node.blend_mode != BlendMode::Normal;
            for (const auto& layer : node.layers) {
                if (!validBlendMode(layer.blend_mode)) return result(OpenGlCompositionStatus::Unsupported,"raster-blend-mode","Invalid layer blend mode.");
                blending |= layer.adjustment.has_value() || layer.blend_mode != BlendMode::Normal;
            }
        }
        if (!blending) discardBlendBackdrop();
        std::unordered_set<std::string> needed;
        for (const auto& node : request.nodes) {
            if (!node.group && (!node.transforms.empty() || node.opacity != 100 || node.blend_mode != BlendMode::Normal))
                return result(OpenGlCompositionStatus::Unsupported, "raster-node", "Only groups accept transforms or node opacity.");
            for (const auto& transform : node.transforms) {
                if (!transform.matrix.isAffine() || !transform.matrix.isInvertible() ||
                    !std::isfinite(transform.matrix.m11()) || !std::isfinite(transform.matrix.m12()) ||
                    !std::isfinite(transform.matrix.m21()) || !std::isfinite(transform.matrix.m22()) ||
                    !std::isfinite(transform.matrix.dx()) || !std::isfinite(transform.matrix.dy()))
                    return result(OpenGlCompositionStatus::Unsupported, "raster-transform", "Raster transforms must be finite invertible affine matrices.");
            }
        }
        for (const auto& node : request.nodes) for (const auto& layer : node.layers) {
            for (const auto* input : {&layer.content, &layer.mask}) {
                if (input->pixels.isNull()) continue;
                if (input->identity.empty() || input->pixels.size() != request.size ||
                    (input->pixels.format() != QImage::Format_ARGB32_Premultiplied &&
                     (input != &layer.mask || input->pixels.format() != QImage::Format_Grayscale8)))
                    return result(OpenGlCompositionStatus::Unsupported, "raster-input", "Raster inputs must be canvas-sized premultiplied ARGB32 images with identities.");
                needed.insert(input->identity);
            }
            if(layer.adjustment && (!effects::validAdjustment(*layer.adjustment) || !layer.content.pixels.isNull()))
                return result(OpenGlCompositionStatus::Unsupported,"raster-adjustment","Invalid raster adjustment.");
            if (layer.opacity < 0 || layer.opacity > 100)
                return result(OpenGlCompositionStatus::Unsupported, "raster-opacity", "Invalid layer opacity.");
        }
        const auto canvas_bytes = std::uint64_t(width) * height * 4;
        // Evict unused sources in LRU order before allocating current resources.
        for (;;) {
            auto victim = raster_textures.end();
            for (auto it = raster_textures.begin(); it != raster_textures.end(); ++it)
                if (!needed.contains(it->first) && (victim == raster_textures.end() || it->second.used < victim->second.used)) victim = it;
            if (victim == raster_textures.end()) break;
            gl->glDeleteTextures(1, &victim->second.texture);
            releaseRaster(std::uint64_t(victim->second.size.width()) * victim->second.size.height() * victim->second.pixel_bytes);
            raster_textures.erase(victim);
        }
        if (!raster_program) {
            constexpr char fragment[] = R"GLSL(#version 150 core
uniform sampler2D source_image;
uniform sampler2D mask_image;
uniform sampler2D backdrop_image;
uniform int blend_mode;
uniform float blend_opacity;
uniform int gradient_count;
uniform bool quantize_blend_opacity;
uniform vec4 gradient_stops[32];
uniform int adjustment_kind;
uniform vec2 brightness_contrast;
uniform vec3 exposure_offset_gamma;
uniform vec3 hue_saturation;
uniform bool hue_colorize_enabled;
uniform vec3 hue_colorize;
uniform vec3 levels_input[4];
uniform vec2 levels_output[4];
uniform bool levels_identity;
uniform vec4 curve_table[192];
uniform bool curve_identity;
uniform bool has_mask;
uniform bool bottom_left;
uniform int canvas_height;
uniform int opacity256;
uniform mat3 inverse_matrix;
uniform ivec4 clip_rect;
out vec4 color;
float adjustHsl(float value, float amount) {
    return amount < 0.0 ? value * (1.0 + amount) : value + (1.0 - value) * amount;
}
vec3 adjustHueSaturation(vec3 rgb) {
    float lo = min(rgb.r, min(rgb.g, rgb.b)), hi = max(rgb.r, max(rgb.g, rgb.b));
    float delta = hi - lo, original_lightness = (hi + lo) * .5;
    float lightness = adjustHsl(original_lightness, hue_colorize_enabled ? hue_colorize.z : hue_saturation.z);
    float hue = 0.0, saturation = 0.0;
    if (hue_colorize_enabled) {
        hue = mod(hue_colorize.x, 360.0) / 60.0;
        saturation = hue_colorize.y;
    } else if (delta > 0.0) {
        saturation = adjustHsl(clamp(delta / (1.0 - abs(2.0 * original_lightness - 1.0)), 0.0, 1.0), hue_saturation.y);
        hue = hi == rgb.r ? (rgb.g-rgb.b)/delta : hi == rgb.g ? (rgb.b-rgb.r)/delta+2.0 : (rgb.r-rgb.g)/delta+4.0;
        hue = mod(hue + hue_saturation.x, 6.0);
    }
    float chroma = (1.0 - abs(2.0 * lightness - 1.0)) * saturation;
    float x = chroma * (1.0 - abs(mod(hue,2.0)-1.0));
    vec3 result = hue < 1.0 ? vec3(chroma,x,0) : hue < 2.0 ? vec3(x,chroma,0) :
        hue < 3.0 ? vec3(0,chroma,x) : hue < 4.0 ? vec3(0,x,chroma) : hue < 5.0 ? vec3(x,0,chroma) : vec3(chroma,0,x);
    return clamp(result + lightness - chroma * .5, 0.0, 1.0);
}
vec3 exposeSrgb(vec3 rgb) {
    vec3 linear = mix(pow((rgb + .055) / 1.055, vec3(2.4)), rgb / 12.92, lessThanEqual(rgb, vec3(.04045)));
    vec3 corrected = pow(clamp(linear * exp2(exposure_offset_gamma.x) + exposure_offset_gamma.y, 0.0, 1.0), vec3(1.0 / exposure_offset_gamma.z));
    return clamp(mix(1.055 * pow(corrected, vec3(1.0 / 2.4)) - .055, corrected * 12.92, lessThanEqual(corrected, vec3(.0031308))), 0.0, 1.0);
}
float levelsValue(float value, int channel_set) {
    vec3 input = levels_input[channel_set];
    vec2 output = levels_output[channel_set];
    float normalized = clamp((value * 255.0 - input.x) /
        (input.y - input.x), 0.0, 1.0);
    return (output.x + (output.y - output.x) *
        pow(normalized, 1.0 / input.z)) / 255.0;
}
vec3 adjustLevels(vec3 rgb) {
    vec3 master = vec3(levelsValue(rgb.r, 0), levelsValue(rgb.g, 0), levelsValue(rgb.b, 0));
    master = floor(master * 255.0 + 0.5) / 255.0;
    return vec3(levelsValue(master.r, 1), levelsValue(master.g, 2), levelsValue(master.b, 3));
}
float curveValue(float value, int channel) {
    int index = int(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5));
    return curve_table[channel * 64 + index / 4][index % 4];
}
void main() {
    vec2 point = vec2(gl_FragCoord.x, float(canvas_height) - gl_FragCoord.y);
    if(adjustment_kind>0) {
        vec4 b=texelFetch(backdrop_image,ivec2(gl_FragCoord.xy),0);
        vec3 cb=b.a>0.0?clamp(b.rgb/b.a,0.0,1.0):vec3(0);
        float t=dot(cb,vec3(0.2126,0.7152,0.0722));
        if(((adjustment_kind==2 && brightness_contrast==vec2(0)) || (adjustment_kind==3 && exposure_offset_gamma==vec3(0,0,1)) || (adjustment_kind==4 && hue_saturation==vec3(0) && !hue_colorize_enabled) || (adjustment_kind==5 && levels_identity) || (adjustment_kind==6 && curve_identity)) && blend_mode==0){color=b;return;}
        vec3 mapped=adjustment_kind==2 ? clamp((cb+brightness_contrast.x-.5)*(1.0+brightness_contrast.y)+.5,0.0,1.0) : adjustment_kind==3 ? exposeSrgb(cb) : adjustment_kind==4 ? adjustHueSaturation(cb) : adjustment_kind==5 ? adjustLevels(cb) : adjustment_kind==6 ? vec3(curveValue(cb.r,0),curveValue(cb.g,1),curveValue(cb.b,2)) : gradient_stops[gradient_count-1].rgb;
        for(int i=1;adjustment_kind==1 && i<gradient_count;++i) {
            if(t<=gradient_stops[i].a){vec4 l=gradient_stops[i-1],r=gradient_stops[i];mapped=mix(l.rgb,r.rgb,clamp((t-l.a)/(r.a-l.a),0.0,1.0));break;}
        }
        vec3 cs=floor(mapped*255.0+0.5)/255.0;
        vec3 mixed=cs;
        if(blend_mode==1)mixed=cb*cs;
        else if(blend_mode==2)mixed=1.0-(1.0-cb)*(1.0-cs);
        else if(blend_mode==3)mixed=mix(1.0-2.0*(1.0-cb)*(1.0-cs),2.0*cb*cs,lessThanEqual(cb,vec3(0.5)));
        else if(blend_mode==4)mixed=min(vec3(1),cb+cs);
        else if(blend_mode==5)mixed=abs(cb-cs);
        float coverage=1.0;
        if(has_mask){ivec4 m=ivec4(floor(texelFetch(mask_image,ivec2(floor(point)),0)*255.0+0.5));int luma=(m.r*2126+m.g*7152+m.b*722+5000)/10000;coverage=float(luma)/255.0;}
        color=vec4(mix(b.rgb,mixed*b.a,blend_opacity*coverage),b.a);return;
    }
    ivec2 pixel = ivec2(floor((inverse_matrix * vec3(point, 1.0)).xy));
    ivec2 size = textureSize(source_image, 0);
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= size.x || pixel.y >= size.y ||
        pixel.x < clip_rect.x || pixel.y < clip_rect.y || pixel.x >= clip_rect.z || pixel.y >= clip_rect.w) { if(blend_mode!=0) discard; color = vec4(0); return; }
    ivec2 source_pixel = bottom_left ? ivec2(pixel.x, size.y - 1 - pixel.y) : pixel;
    ivec4 bytes = ivec4(floor(texelFetch(source_image, source_pixel, 0) * 255.0 + 0.5));
    if (has_mask) {
        ivec3 mask_bytes = ivec3(floor(texelFetch(mask_image, pixel, 0).rgb * 255.0 + 0.5));
        int coverage = (mask_bytes.r * 2126 + mask_bytes.g * 7152 + mask_bytes.b * 722 + 5000) / 10000;
        bytes = (bytes * coverage + ivec4(127)) / 255;
    }
    if(blend_mode!=0) {
        vec4 b=texelFetch(backdrop_image,ivec2(gl_FragCoord.xy),0);
        if(quantize_blend_opacity)bytes=(bytes*opacity256)/256;
        vec3 cs=bytes.a>0 ? clamp(vec3(bytes.rgb)/float(bytes.a),0.0,1.0) : vec3(0);
        float as=float(bytes.a)/255.0*blend_opacity;
        vec3 cb=b.a>0.0 ? clamp(b.rgb/b.a,0.0,1.0) : vec3(0);
        vec3 mixed=cs;
        if(blend_mode==1)mixed=cb*cs;
        else if(blend_mode==2)mixed=1.0-(1.0-cb)*(1.0-cs);
        else if(blend_mode==3)mixed=mix(1.0-2.0*(1.0-cb)*(1.0-cs),2.0*cb*cs,lessThanEqual(cb,vec3(0.5)));
        else if(blend_mode==4)mixed=min(vec3(1),cb+cs);
        else if(blend_mode==5)mixed=abs(cb-cs);
        color=vec4((1.0-as)*b.rgb+as*(1.0-b.a)*cs+as*b.a*mixed,as+b.a*(1.0-as));
        return;
    }
    // Match Qt's 0..256 constant-alpha scale before source-over blending.
    bytes = (bytes * opacity256) / 256;
    color = vec4(bytes) / 255.0;
})GLSL";
            raster_program = std::make_unique<QOpenGLShaderProgram>();
            if (!raster_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
                !raster_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !raster_program->link())
                return result(OpenGlCompositionStatus::Failed, "raster-shader", raster_program->log().toStdString());
        }
        bool groups = false, transforms = false;
        for (const auto& node : request.nodes) { groups |= node.group; transforms |= !node.transforms.empty();
            if (node.opacity < 0 || node.opacity > 100) return result(OpenGlCompositionStatus::Unsupported, "raster-opacity", "Invalid group opacity."); }
        for (int i = 0; i < 2; ++i) {
            const bool required = i == 0 ? groups : transforms;
            if (raster_scratch[i] && (!required || raster_scratch[i]->size() != request.size)) {
                releaseRaster(std::uint64_t(raster_scratch[i]->width()) * raster_scratch[i]->height() * 4);
                raster_scratch[i].reset();
            }
            if (required && !raster_scratch[i]) {
                if (!reserveRaster(canvas_bytes)) return result(OpenGlCompositionStatus::Unsupported, "raster-budget", "Group intermediates exceed the raster budget.");
                QOpenGLFramebufferObjectFormat format; format.setInternalTextureFormat(GL_RGBA8);
                raster_scratch[i] = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
                if (!raster_scratch[i]->isValid()) return result(OpenGlCompositionStatus::Failed, "raster-group", "Cannot allocate a group intermediate.");
            }
        }
        if (blending && (!blend_backdrop || blend_backdrop->size() != request.size)) {
            discardBlendBackdrop();
            if (!reserveRaster(canvas_bytes)) return result(OpenGlCompositionStatus::Unsupported,"raster-blend-budget","Blend backdrop exceeds the shared texture budget.");
            blend_reserved=canvas_bytes;
            QOpenGLFramebufferObjectFormat format; format.setInternalTextureFormat(GL_RGBA8);
            blend_backdrop=std::make_unique<QOpenGLFramebufferObject>(width,height,format);
            if (!blend_backdrop->isValid()) { discardBlendBackdrop(); return result(OpenGlCompositionStatus::Failed,"raster-blend-target","Cannot allocate the raster blend backdrop."); }
        }
        const auto upload = [&](const OpenGlRasterInput& input, unsigned slot) -> OpenGlCompositionResult {
            gl->glActiveTexture(GL_TEXTURE0 + slot);
            auto& entry = raster_textures[input.identity];
            entry.used = ++raster_clock;
            if (entry.texture && (entry.size != request.size || entry.pixel_bytes != (input.pixels.format()==QImage::Format_Grayscale8?1U:4U))) {
                gl->glDeleteTextures(1, &entry.texture); entry.texture = 0;
                releaseRaster(std::uint64_t(entry.size.width()) * entry.size.height() * entry.pixel_bytes);
            }
            const bool gray=input.pixels.format()==QImage::Format_Grayscale8;
            const unsigned pixel_bytes=gray?1:4;
            bool fresh = entry.texture == 0;
            if (fresh) {
                if (!reserveRaster(canvas_bytes/4*pixel_bytes)) { raster_textures.erase(input.identity); return result(OpenGlCompositionStatus::Unsupported, "raster-budget", "Source textures exceed the raster budget."); }
                gl->glGenTextures(1, &entry.texture); entry.size = request.size; entry.pixel_bytes=pixel_bytes;
            }
            gl->glBindTexture(GL_TEXTURE_2D, entry.texture);
            if (fresh) {
                gl->glTexImage2D(GL_TEXTURE_2D, 0, gray?GL_R8:GL_RGBA8, width, height, 0, gray?GL_RED:GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            const qint64 revision = input.revision ? input.revision : input.pixels.cacheKey();
            if (fresh || entry.key != revision) {
                const auto started = Clock::now();
                const QRect dirty = !fresh && entry.key == input.previous_key && !input.dirty.isEmpty()
                    ? input.dirty.intersected(QRect(QPoint(), request.size)) : QRect(QPoint(), request.size);
                gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, input.pixels.bytesPerLine() / pixel_bytes);
                // ARGB32 is BGRA on little-endian platforms. The big-endian path
                // returns Unsupported below, instead of misinterpreting channels.
                gl->glTexSubImage2D(GL_TEXTURE_2D, 0, dirty.x(), dirty.y(), dirty.width(), dirty.height(), gray?GL_RED:GL_BGRA, GL_UNSIGNED_BYTE,
                    input.pixels.constScanLine(dirty.y()) + dirty.x() * pixel_bytes);
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                measured.uploaded_bytes += std::uint64_t(dirty.width()) * dirty.height() * pixel_bytes;
                ++measured.uploaded_layers; measured.upload_nanoseconds += elapsed(started);
                entry.key = revision;
            }
            return result(OpenGlCompositionStatus::Complete);
        };
        if constexpr (QSysInfo::ByteOrder != QSysInfo::LittleEndian)
            return result(OpenGlCompositionStatus::Unsupported, "raster-layout", "Native ARGB raster upload requires little-endian storage.");
        raster_program->bind(); gl->glBindVertexArray(vao);
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_DEPTH_TEST); gl->glDisable(GL_STENCIL_TEST); gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_CULL_FACE); gl->glDisable(GL_DITHER); gl->glDisable(GL_FRAMEBUFFER_SRGB); gl->glDisable(GL_MULTISAMPLE);
        gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); gl->glBlendEquation(GL_FUNC_ADD);
        raster_program->setUniformValue("source_image", 0); raster_program->setUniformValue("mask_image", 1);
        raster_program->setUniformValue("canvas_height", height);
        raster_program->setUniformValue("backdrop_image", 2);
        const auto draw = [&](GLuint source, bool bottom, GLuint mask, int opacity, const OpenGlRasterTransform& transform, BlendMode mode = BlendMode::Normal,const effects::AdjustmentParameters* adjustment=nullptr,bool quantize_group_opacity=false) -> bool {
            bool invertible = false; const auto inverse = transform.matrix.inverted(&invertible);
            if (!invertible || !transform.matrix.isAffine()) return false;
            QMatrix3x3 matrix;
            matrix(0,0)=float(inverse.m11()); matrix(0,1)=float(inverse.m21()); matrix(0,2)=float(inverse.dx());
            matrix(1,0)=float(inverse.m12()); matrix(1,1)=float(inverse.m22()); matrix(1,2)=float(inverse.dy());
            matrix(2,0)=0; matrix(2,1)=0; matrix(2,2)=1;
            raster_program->setUniformValue("inverse_matrix", matrix);
            const QRect clip = transform.clip.isValid() ? transform.clip : QRect(QPoint(), request.size);
            gl->glUniform4i(raster_program->uniformLocation("clip_rect"), clip.x(), clip.y(), clip.x()+clip.width(), clip.y()+clip.height());
            raster_program->setUniformValue("bottom_left", bottom); raster_program->setUniformValue("has_mask", mask != 0);
            raster_program->setUniformValue("opacity256", opacity * 256 / 100);
            raster_program->setUniformValue("blend_mode",static_cast<int>(mode));
            raster_program->setUniformValue("blend_opacity",quantize_group_opacity?1.f:float(opacity)/100.f);
            raster_program->setUniformValue("quantize_blend_opacity",quantize_group_opacity);
            const auto* gradient=adjustment?std::get_if<effects::GradientMapParameters>(adjustment):nullptr;
            const auto* bc=adjustment?std::get_if<effects::BrightnessContrastParameters>(adjustment):nullptr;
            const auto* exposure = adjustment ? std::get_if<effects::ExposureOffsetGammaParameters>(adjustment) : nullptr;
            const auto* hue = adjustment ? std::get_if<effects::HueSaturationParameters>(adjustment) : nullptr;
            const auto* levels = adjustment ? std::get_if<effects::LevelsParameters>(adjustment) : nullptr;
            const auto* curves = adjustment ? std::get_if<effects::CurvesParameters>(adjustment) : nullptr;
            raster_program->setUniformValue("adjustment_kind",gradient?1:bc?2:exposure?3:hue?4:levels?5:curves?6:0);
            raster_program->setUniformValue("curve_identity",curves && effects::identityCurves(*curves));
            if (curves) {
                const auto tables = effects::curvesLuts(*curves);
                std::array<QVector4D,192> packed;
                for (int channel=0;channel<3;++channel)
                    for (int i=0;i<64;++i) packed[channel*64+i] = QVector4D(
                        tables[channel][i*4]/255.f,tables[channel][i*4+1]/255.f,
                        tables[channel][i*4+2]/255.f,tables[channel][i*4+3]/255.f);
                raster_program->setUniformValueArray("curve_table",packed.data(),192);
            }
            std::array<QVector3D, 4> levels_input{};
            std::array<QVector2D, 4> levels_output{};
            for (int channel = 0; channel < 4; ++channel) {
                const auto parameters = levels ? effects::levelsChannel(*levels, channel)
                    : effects::LevelsChannelParameters{};
                levels_input[channel] = QVector3D(float(parameters.input_black),
                    float(parameters.input_white), float(parameters.gamma));
                levels_output[channel] = QVector2D(float(parameters.output_black),
                    float(parameters.output_white));
            }
            raster_program->setUniformValueArray("levels_input", levels_input.data(), 4);
            raster_program->setUniformValueArray("levels_output", levels_output.data(), 4);
            raster_program->setUniformValue("levels_identity", !levels || effects::identityLevels(*levels));
            raster_program->setUniformValue("hue_saturation", hue ?
                QVector3D(hue->hue / 60.f, hue->saturation / 100.f, hue->lightness / 100.f) : QVector3D{});
            raster_program->setUniformValue("hue_colorize_enabled", hue && hue->colorize_enabled);
            raster_program->setUniformValue("hue_colorize", hue && hue->colorize_enabled ?
                QVector3D(float(hue->colorize.hue), hue->colorize.saturation / 100.f, hue->colorize.lightness / 100.f) : QVector3D{});
            raster_program->setUniformValue("exposure_offset_gamma", exposure ?
                QVector3D(float(exposure->exposure), float(exposure->offset), float(exposure->gamma)) : QVector3D(0,0,1));
            raster_program->setUniformValue("brightness_contrast",bc?QVector2D(bc->brightness/100.f,bc->contrast/100.f):QVector2D{});
            raster_program->setUniformValue("gradient_count",gradient?int(gradient->stops.size()):0);
            if(gradient){std::array<QVector4D,32> points;int count=0;for(const auto& stop:gradient->stops)points[count++]=QVector4D(stop.color[0]/255.f,stop.color[1]/255.f,stop.color[2]/255.f,float(stop.position));raster_program->setUniformValueArray("gradient_stops",points.data(),count);}
            const bool was_blending=gl->glIsEnabled(GL_BLEND);
            if (adjustment || mode!=BlendMode::Normal) {
                gl->glActiveTexture(GL_TEXTURE2); gl->glBindTexture(GL_TEXTURE_2D,blend_backdrop->texture());
                gl->glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
                gl->glDisable(GL_BLEND);
            }
            gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, source);
            gl->glActiveTexture(GL_TEXTURE1); gl->glBindTexture(GL_TEXTURE_2D, mask);
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            if ((adjustment || mode!=BlendMode::Normal) && was_blending) gl->glEnable(GL_BLEND);
            return true;
        };
        const auto clear = [&](QOpenGLFramebufferObject* target) { target->bind(); gl->glClearColor(0,0,0,0); gl->glClear(GL_COLOR_BUFFER_BIT); };
        clear(selected_output);
        const auto started = Clock::now();
        for (const auto& node : request.nodes) {
            if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
            auto* target = node.group ? raster_scratch[0].get() : selected_output;
            if (node.group) clear(target); else target->bind();
            gl->glEnable(GL_BLEND);
            for (const auto& layer : node.layers) {
                if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
                if (layer.content.pixels.isNull() && !layer.adjustment) continue;
                auto loaded=result(OpenGlCompositionStatus::Complete);
                if(!layer.adjustment){loaded=upload(layer.content,0);if(loaded.status!=OpenGlCompositionStatus::Complete)return loaded;}
                GLuint mask = 0;
                if (!layer.mask.pixels.isNull()) {
                    loaded = upload(layer.mask, 1); if (loaded.status != OpenGlCompositionStatus::Complete) return loaded;
                    mask = raster_textures.at(layer.mask.identity).texture;
                }
                if (!draw(layer.adjustment?0:raster_textures.at(layer.content.identity).texture, false, mask, layer.opacity, {}, layer.blend_mode,layer.adjustment?&*layer.adjustment:nullptr))
                    return result(OpenGlCompositionStatus::Unsupported, "raster-transform", "Invalid raster transform.");
            }
            if (!node.group) continue;
            gl->glDisable(GL_BLEND);
            for (const auto& transform : node.transforms) {
                auto* next = target == raster_scratch[0].get() ? raster_scratch[1].get() : raster_scratch[0].get();
                clear(next);
                if (!draw(target->texture(), true, 0, 100, transform))
                    return result(OpenGlCompositionStatus::Unsupported, "raster-transform", "Invalid group transform.");
                target = next;
            }
            selected_output->bind(); gl->glEnable(GL_BLEND);
            draw(target->texture(), true, 0, node.opacity, {}, node.blend_mode,nullptr,adjustments && node.blend_mode!=BlendMode::Normal);
        }
        measured.draw_submission_nanoseconds += elapsed(started);
        rememberResourcePeak();
        if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "raster-compose", "OpenGL raster composition failed.", code);
        return result(OpenGlCompositionStatus::Complete);
    }

    OpenGlCompositionResult renderImage(const OpenGlImageOperation& operation,
        const OpenGlFrameCompositor::CancellationPredicate& cancel, OpenGlCompositionTimings& measured) {
        if (!selected_output || !selected_output->bind() || !image_program->bind())
            return result(OpenGlCompositionStatus::Failed, "bind-image-output", "Cannot bind the image operation target.");
        const auto width = operation.output_width > 0 ? operation.output_width : operation.background.width();
        const auto height = operation.output_height > 0 ? operation.output_height : operation.background.height();
        if (!bounded(width, height, texture_limit) || operation.colors.size() > 256 ||
            !animation::validTransform(operation.transform))
            return result(OpenGlCompositionStatus::Unsupported, "check-image-operation", "Image geometry or effects exceed the supported contract.");
        if ((operation.kind == OpenGlImageOperationKind::Transform ||
             operation.kind == OpenGlImageOperationKind::Adjustment) &&
            operation.transform.rotation_degrees != 0 && !precise_geometry)
            return result(OpenGlCompositionStatus::Unsupported, "check-precision", "Rotated intermediates require precise GPU geometry.");
        if(operation.source_to_canvas && (operation.kind!=OpenGlImageOperationKind::Transform || !precise_geometry || !validAffine(*operation.source_to_canvas)))
            return result(OpenGlCompositionStatus::Unsupported,"affine-image-transform","Affine Transform requires finite inverse geometry and precise GPU sampling.");
        struct Uses {
            QOpenGLContext* context;
            std::vector<OpenGlTextureFramePtr> frames;
            ~Uses() {
                for (const auto& frame : frames) {
                    std::string cause; std::int64_t code = 0;
                    (void)frame->endUse(context, cause, code);
                }
            }
        } uses{context.get()};
        const auto bind = [&](const OpenGlImageInput& input, unsigned slot) -> OpenGlCompositionResult {
            gl->glActiveTexture(GL_TEXTURE0 + slot);
            if (input.native) {
                if (slot != 0) return result(OpenGlCompositionStatus::Unsupported, "native-image-input",
                    "Import native sources before merging them.");
                if (!video_interop) video_interop = std::make_unique<detail::WindowsVideoInterop>(context.get(), gl);
                const auto started = Clock::now();
                auto imported = video_interop->begin(input.native);
                if (imported.status != OpenGlCompositionStatus::Complete) return imported;
                gl->glBindTexture(GL_TEXTURE_2D, video_interop->texture());
                ++measured.native_video_imports;
                measured.native_video_conversion_nanoseconds += elapsed(started);
                rememberResourcePeak();
            } else if (input.texture) {
                if (std::find(uses.frames.begin(), uses.frames.end(), input.texture) == uses.frames.end()) {
                    std::string cause; std::int64_t code = 0;
                    if (!input.texture->beginUse(context.get(), cause, code))
                        return result(OpenGlCompositionStatus::Failed, "consume-image-texture", cause, code);
                    uses.frames.push_back(input.texture);
                }
                gl->glBindTexture(GL_TEXTURE_2D, input.texture->texture());
            } else {
                if (!input.rgba || !usable(CompositionLayer{input.rgba}) ||
                    !bounded(input.width(), input.height(), texture_limit))
                    return result(OpenGlCompositionStatus::Unsupported, "check-image-input", "Image operation requires a valid source.");
                const auto bytes=static_cast<std::uint64_t>(input.width())*input.height()*4;
                if (slot==2 && bytes>budget->impl_->byte_limit)
                    return result(OpenGlCompositionStatus::Unsupported,"check-mask-budget","Coverage exceeds the bounded mask upload budget.");
                if(operation.kind==OpenGlImageOperationKind::ApplyTrackMatte) {
                    if(bytes>matte_upload_reserved[slot] && !reserveRaster(bytes-matte_upload_reserved[slot]))
                        return result(OpenGlCompositionStatus::Unsupported,"track-matte-upload-budget","Track matte inputs exceed the shared texture budget.");
                    if(bytes<matte_upload_reserved[slot])releaseRaster(matte_upload_reserved[slot]-bytes);
                    matte_upload_reserved[slot]=bytes;
                } else if(matte_upload_reserved[slot]){releaseRaster(matte_upload_reserved[slot]);matte_upload_reserved[slot]=0;}
                const auto started = Clock::now();
                gl->glBindTexture(GL_TEXTURE_2D, image_textures[slot]);
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, input.width(), input.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                image_texture_bytes[slot]=bytes;
                rememberResourcePeak();
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                for (int y = 0; y < input.height(); ++y) {
                    if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
                    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, input.width(), 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        input.rgba->rgba_pixels.data() + static_cast<std::size_t>(y) * input.rgba->stride);
                }
                measured.uploaded_bytes += image_texture_bytes[slot];
                ++measured.uploaded_layers;
                measured.upload_nanoseconds += elapsed(started);
                rememberResourcePeak();
            }
            return result(OpenGlCompositionStatus::Complete);
        };
        auto bound = bind(operation.background, 0);
        struct InteropGuard {
            detail::WindowsVideoInterop* adapter;
            ~InteropGuard() { if (adapter) (void)adapter->end(); }
        } interop_guard{operation.background.native ? video_interop.get() : nullptr};
        if (bound.status != OpenGlCompositionStatus::Complete) return bound;
        for (const auto* input : {&operation.background, &operation.foreground, &operation.mask})
        if (input->texture && input->texture->backend() != GraphicsBackend::OpenGl)
            return {OpenGlCompositionStatus::Unsupported, {}, "check-image-backend", "An OpenGL operation cannot consume a Vulkan image."};
    if(operation.kind==OpenGlImageOperationKind::GaussianBlur) {
            const auto bytes=static_cast<std::uint64_t>(width)*height*4;
            if(budget->bytes()+bytes>budget->impl_->byte_limit)
                return result(OpenGlCompositionStatus::Unsupported,"image-blur-budget","Blur scratch exceeds the bounded texture budget.");
            if(!effect_scratch || effect_scratch->width()!=width || effect_scratch->height()!=height) {
                QOpenGLFramebufferObjectFormat format;format.setInternalTextureFormat(GL_RGBA8);
                effect_scratch=std::make_unique<QOpenGLFramebufferObject>(width,height,format);
            }
            if(!effect_scratch->isValid()||!effect_program->bind())
                return result(OpenGlCompositionStatus::Failed,"image-blur-target","Cannot prepare the existing blur shader and scratch target.");
            const auto started=Clock::now();
            gl->glDisable(GL_BLEND);gl->glDisable(GL_DITHER);gl->glDisable(GL_FRAMEBUFFER_SRGB);
            gl->glDisable(GL_DEPTH_TEST);gl->glDisable(GL_SCISSOR_TEST);gl->glDisable(GL_STENCIL_TEST);
            gl->glViewport(0,0,width,height);gl->glBindVertexArray(vao);
            effect_program->setUniformValue("source_image",0);
            effect_program->setUniformValue("effect_kind",1);
            effect_program->setUniformValue("blur_radius",int(std::lround(operation.blur_radius_pixels)));
            GLuint current=operation.background.texture ? operation.background.texture->texture() : video_interop->texture();
            for(int pass=0;pass<8;++pass) {
                if(cancel && cancel())return result(OpenGlCompositionStatus::Cancelled);
                auto* destination=pass%2==0?effect_scratch.get():selected_output;
                if(!destination->bind())return result(OpenGlCompositionStatus::Failed,"image-blur-bind","Cannot bind a blur pass target.");
                effect_program->setUniformValue("premultiply_alpha",pass==0);
                effect_program->setUniformValue("unpremultiply_alpha",pass==7);
                effect_program->setUniformValue("horizontal_pass",pass%2==1);
                gl->glActiveTexture(GL_TEXTURE0);gl->glBindTexture(GL_TEXTURE_2D,current);
                gl->glDrawArrays(GL_TRIANGLES,0,3);current=destination->texture();
                if(const auto code=gl->glGetError();code!=GL_NO_ERROR)return result(OpenGlCompositionStatus::Failed,"image-blur-pass","The existing Gaussian Blur pass failed.",code);
            }
            ++measured.gaussian_blur_count;
            measured.gaussian_blur_submission_nanoseconds+=elapsed(started);
            measured.draw_submission_nanoseconds+=elapsed(started);
            rememberResourcePeak();return result(OpenGlCompositionStatus::Complete);
        }
        if (operation.kind == OpenGlImageOperationKind::Merge || operation.kind == OpenGlImageOperationKind::MaskedBlend || operation.kind==OpenGlImageOperationKind::DropShadow || operation.kind==OpenGlImageOperationKind::Glow || operation.kind==OpenGlImageOperationKind::Adjustment) {
            bound = bind(operation.foreground, 1);
            if (bound.status != OpenGlCompositionStatus::Complete) return bound;
        }
        if (operation.kind == OpenGlImageOperationKind::Adjustment &&
            (operation.background.width() != width || operation.background.height() != height ||
             operation.foreground.width() != width || operation.foreground.height() != height))
            return result(OpenGlCompositionStatus::Unsupported, "adjustment-image-geometry",
                "Adjustment inputs must match the output canvas.");
        if (operation.kind == OpenGlImageOperationKind::ApplyMask || operation.kind == OpenGlImageOperationKind::MaskedBlend) {
            if (operation.mask.width()!=width || operation.mask.height()!=height ||
                (operation.kind==OpenGlImageOperationKind::MaskedBlend &&
                 (operation.foreground.width()!=width || operation.foreground.height()!=height)))
                return result(OpenGlCompositionStatus::Unsupported,"check-mask-geometry","Mask dimensions must match the receiver.");
            bound=bind(operation.mask,2);
            if (bound.status!=OpenGlCompositionStatus::Complete) return bound;
        }
        if(operation.kind==OpenGlImageOperationKind::ApplyTrackMatte && (operation.mask.rgba || operation.mask.texture || operation.mask.native)) {
            if(operation.mask.width()!=width || operation.mask.height()!=height)
                return result(OpenGlCompositionStatus::Unsupported,"track-matte-geometry","Track matte dimensions must match the receiver.");
            bound=bind(operation.mask,2);if(bound.status!=OpenGlCompositionStatus::Complete)return bound;
        }
        const auto started = Clock::now();
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_BLEND); gl->glDisable(GL_DEPTH_TEST); gl->glDisable(GL_STENCIL_TEST);
        gl->glDisable(GL_SCISSOR_TEST); gl->glDisable(GL_CULL_FACE); gl->glDisable(GL_DITHER);
        gl->glDisable(GL_FRAMEBUFFER_SRGB); gl->glDisable(GL_MULTISAMPLE);
        gl->glBindVertexArray(vao);
        image_program->setUniformValue("background_image", 0);
        image_program->setUniformValue("foreground_image", 1);
        image_program->setUniformValue("mask_image", 2);
        image_program->setUniformValue("curve_lookup", 3);
        image_program->setUniformValue("mask_bottom_left", bool(operation.mask.texture));
        image_program->setUniformValue("has_track_matte",bool(operation.mask.rgba || operation.mask.texture || operation.mask.native));
        image_program->setUniformValue("track_matte_mode",static_cast<int>(operation.track_matte_mode));
        image_program->setUniformValue("background_bottom_left", bool(operation.background.texture));
        image_program->setUniformValue("foreground_bottom_left", bool(operation.foreground.texture));
        image_program->setUniformValue("operation", static_cast<int>(operation.kind));
        image_program->setUniformValue("canvas_height", height);
        image_program->setUniformValue("color_count", static_cast<int>(operation.colors.size()));
        const auto geometry = [&](const char* name, double x, double y) {
            if (precise_geometry) {
                const GLdouble values[]{x, y}; uniform2dv(image_program->uniformLocation(name), 1, values);
            } else image_program->setUniformValue(name, QVector2D(float(x), float(y)));
        };
        if(operation.kind==OpenGlImageOperationKind::DropShadow) {
            const auto bounds=effects::dropShadowBounds(operation.background.width(),operation.background.height(),operation.drop_shadow);
            geometry("shadow_delta",bounds.dx,bounds.dy);
            gl->glUniform2i(image_program->uniformLocation("shadow_origin"),bounds.left,bounds.top);
            const auto& c=operation.drop_shadow.color;
            geometry("shadow_strength",operation.drop_shadow.opacity_percent,0);
            gl->glUniform3i(image_program->uniformLocation("shadow_rgb"),c[0],c[1],c[2]);
        }
        if (operation.kind == OpenGlImageOperationKind::Glow) {
            const auto bounds = effects::glowBounds(operation.background.width(),
                operation.background.height(), operation.glow);
            gl->glUniform2i(image_program->uniformLocation("glow_origin"), bounds.left, bounds.top);
            const int inner_halo = operation.glow.mode == effects::GlowMode::Inner
                ? 3 * static_cast<int>(std::lround(operation.glow.radius_pixels)) + 1 : 0;
            gl->glUniform2i(image_program->uniformLocation("glow_blur_origin"), inner_halo, inner_halo);
            gl->glUniform3i(image_program->uniformLocation("glow_rgb"), operation.glow.color[0],
                operation.glow.color[1], operation.glow.color[2]);
            geometry("glow_strength", operation.glow.opacity_percent, 0);
            gl->glUniform1i(image_program->uniformLocation("glow_mode"),
                static_cast<int>(operation.glow.mode));
        }
        image_program->setUniformValue("use_affine",operation.source_to_canvas.has_value());
        if(operation.source_to_canvas) {
            const auto inverse=requireInverseAffine(*operation.source_to_canvas);
            geometry("affine_x",inverse.a,inverse.b);geometry("affine_y",inverse.c,inverse.d);geometry("affine_translation",inverse.tx,inverse.ty);
        }
        const auto& transform = operation.transform;
        const double angle = transform.rotation_degrees * std::numbers::pi / 180.0;
        geometry("center", transform.position_x * width, transform.position_y * height);
        geometry("rotation_cs", std::cos(angle), std::sin(angle));
        geometry("scale_opacity", transform.scale, std::clamp(transform.opacity, 0.0, 1.0));
        if (operation.kind == OpenGlImageOperationKind::Merge) {
            const double scale = std::min(double(width) / operation.foreground.width(),
                double(height) / operation.foreground.height());
            const int fitted_width = std::max(1, int(std::lround(operation.foreground.width() * scale)));
            const int fitted_height = std::max(1, int(std::lround(operation.foreground.height() * scale)));
            geometry("merge_scale", scale, 0);
            gl->glUniform2i(image_program->uniformLocation("merge_origin"), (width - fitted_width) / 2, (height - fitted_height) / 2);
            gl->glUniform2i(image_program->uniformLocation("merge_size"), fitted_width, fitted_height);
        }
        std::array<std::array<float, 4>, 1024> colors{};
        std::array<std::array<float, 4>, 768> log_colors{};
        std::array<std::array<float, 4>, 768> curve_qualifiers{};
        std::vector<std::uint8_t> curve_luts(256 * 9 * operation.colors.size(), 0);
        for (std::size_t i = 0; i < operation.colors.size(); ++i) {
            const auto& value = operation.colors[i];
            if (!effects::isValidColorAdjustment(value))
                return result(OpenGlCompositionStatus::Unsupported, "check-image-color", "Invalid image color parameters.");
            const auto affine = effects::colorAdjustmentAffine(value);
            colors[4 * i] = {float(affine[0]), float(affine[1]), float(affine[2]), float(affine[3])};
            colors[4 * i + 1] = {float(value.lift_red / 100.0), float(value.lift_green / 100.0),
                float(value.lift_blue / 100.0), float(value.saturation_percent / 100.0)};
            colors[4 * i + 2] = {float(100.0 / value.gamma_red), float(100.0 / value.gamma_green),
                float(100.0 / value.gamma_blue), float(value.red_gain_percent / 100.0)};
            colors[4 * i + 3] = {float(value.green_gain_percent / 100.0),
                float(value.blue_gain_percent / 100.0), 0.0F, 0.0F};
            log_colors[3 * i] = {float(value.log_shadows_red / 100.0),
                float(value.log_shadows_green / 100.0),
                float(value.log_shadows_blue / 100.0), 0.0F};
            log_colors[3 * i + 1] = {float(value.log_midtones_red / 100.0),
                float(value.log_midtones_green / 100.0),
                float(value.log_midtones_blue / 100.0), 0.0F};
            log_colors[3 * i + 2] = {float(value.log_highlights_red / 100.0),
                float(value.log_highlights_green / 100.0),
                float(value.log_highlights_blue / 100.0), 0.0F};
            if (value.video_curves) {
                const auto tables = effects::videoCurvesLuts(*value.video_curves);
                for (std::size_t curve = 0; curve < tables.size(); ++curve)
                    std::copy(tables[curve].begin(), tables[curve].end(),
                        curve_luts.begin() + static_cast<std::ptrdiff_t>(
                            (i * 9 + curve) * 256));
                const auto& q = value.video_curves->qualifier;
                curve_qualifiers[3 * i] = {float(q.hue_min), float(q.hue_max),
                    float(q.saturation_min), float(q.saturation_max)};
                curve_qualifiers[3 * i + 1] = {float(q.luminance_min),
                    float(q.luminance_max), float(q.hue_softness * 3.6),
                    float(q.saturation_softness)};
                curve_qualifiers[3 * i + 2] = {float(q.luminance_softness),
                    q.enabled ? 1.0F : 0.0F, 1.0F, 0.0F};
            }
        }
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[2]);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, 4 * operation.colors.size() * sizeof(colors[0]), colors.data());
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 2, lookup_buffers[2]);
        gl->glUniformBlockBinding(image_program->programId(), gl->glGetUniformBlockIndex(image_program->programId(), "ColorAdjustments"), 2);
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[3]);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
            3 * operation.colors.size() * sizeof(log_colors[0]), log_colors.data());
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, lookup_buffers[3]);
        gl->glUniformBlockBinding(image_program->programId(),
            gl->glGetUniformBlockIndex(image_program->programId(), "LogAdjustments"), 3);
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[4]);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
            3 * operation.colors.size() * sizeof(curve_qualifiers[0]),
            curve_qualifiers.data());
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 4, lookup_buffers[4]);
        gl->glUniformBlockBinding(image_program->programId(),
            gl->glGetUniformBlockIndex(image_program->programId(), "CurveQualifiers"), 4);
        gl->glActiveTexture(GL_TEXTURE3);
        gl->glBindTexture(GL_TEXTURE_2D, curve_lut_texture);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256,
            static_cast<GLsizei>(9 * operation.colors.size()), 0, GL_RED,
            GL_UNSIGNED_BYTE, curve_luts.data());
        curve_lut_bytes = curve_luts.size();
        const bool refined_color = operation.kind == OpenGlImageOperationKind::Color &&
            operation.colors.size() == 1 && requiresCpuMatteRefinement(operation.colors.front());
        if (refined_color) {
            const auto& qualifier = operation.colors.front().video_curves->qualifier;
            if (!ensureMatteScratch(width, height))
                return result(OpenGlCompositionStatus::Unsupported, "color-matte-budget",
                    "The bounded R8 qualifier matte intermediates are unavailable.");
            gl->glActiveTexture(GL_TEXTURE3);
            gl->glBindTexture(GL_TEXTURE_2D, curve_lut_texture);
            gl->glActiveTexture(GL_TEXTURE2);
            gl->glUniform1i(image_program->uniformLocation("use_refined_matte"), GL_FALSE);
            gl->glUniform4f(image_program->uniformLocation("matte_parameters"), 0, 0, 0, 0);
            gl->glUniform1i(image_program->uniformLocation("matte_radius"), 0);
            gl->glUniform1i(image_program->uniformLocation("matte_horizontal"), 0);
            const auto draw_matte_pass = [&](unsigned destination, int opcode,
                unsigned source_matte, float first, float second, float third,
                int radius, int horizontal) {
                if (cancel && cancel()) return OpenGlCompositionStatus::Cancelled;
                gl->glBindFramebuffer(GL_FRAMEBUFFER, effect_source_fbo);
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, matte_textures[destination], 0);
                if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    return OpenGlCompositionStatus::Failed;
                gl->glViewport(0, 0, width, height);
                gl->glActiveTexture(GL_TEXTURE2);
                gl->glBindTexture(GL_TEXTURE_2D, source_matte);
                gl->glUniform1i(image_program->uniformLocation("operation"), opcode);
                gl->glUniform4f(image_program->uniformLocation("matte_parameters"),
                    first, second, third, 0.0F);
                gl->glUniform1i(image_program->uniformLocation("matte_radius"), radius);
                gl->glUniform1i(image_program->uniformLocation("matte_horizontal"), horizontal);
                gl->glDrawArrays(GL_TRIANGLES, 0, 3);
                if (gl->glGetError() != GL_NO_ERROR) return OpenGlCompositionStatus::Failed;
                return OpenGlCompositionStatus::Complete;
            };
            auto matte_index = 0U;
            auto pass = draw_matte_pass(matte_index, 12, 0, 0, 0, 0, 0, 0);
            if (pass != OpenGlCompositionStatus::Complete)
                return result(pass, "color-matte-source", "Cannot generate the R8 qualifier matte.");
            const auto run_filter = [&](int opcode, float first, float second,
                float third, int radius, int horizontal) {
                const auto destination = 1U - matte_index;
                const auto status = draw_matte_pass(destination, opcode,
                    matte_textures[matte_index], first, second, third, radius, horizontal);
                if (status == OpenGlCompositionStatus::Complete) matte_index = destination;
                return status;
            };
            if (qualifier.denoise_percent > 0.0) {
                pass = run_filter(13, float(qualifier.denoise_percent), 0, 0, 0, 0);
                if (pass != OpenGlCompositionStatus::Complete)
                    return result(pass, "color-matte-denoise", "The R8 median denoise pass failed.");
            }
            if (qualifier.clean_black_percent > 0.0 ||
                qualifier.clean_white_percent < 100.0 || qualifier.in_out_percent != 0.0) {
                pass = run_filter(14, float(qualifier.clean_black_percent),
                    float(qualifier.clean_white_percent), float(qualifier.in_out_percent), 0, 0);
                if (pass != OpenGlCompositionStatus::Complete)
                    return result(pass, "color-matte-levels", "The R8 clean-level pass failed.");
            }
            const int radius = static_cast<int>(std::lround(qualifier.blur_radius_pixels));
            if (radius > 0) {
                pass = run_filter(15, 0, 0, 0, radius, 1);
                if (pass == OpenGlCompositionStatus::Complete)
                    pass = run_filter(15, 0, 0, 0, radius, 0);
                if (pass != OpenGlCompositionStatus::Complete)
                    return result(pass, "color-matte-blur", "The separable R8 Gaussian blur failed.");
            }
            if (!selected_output->bind())
                return result(OpenGlCompositionStatus::Failed, "color-matte-output",
                    "Cannot bind the corrected RGBA output target.");
            gl->glViewport(0, 0, width, height);
            gl->glActiveTexture(GL_TEXTURE2);
            gl->glBindTexture(GL_TEXTURE_2D, matte_textures[matte_index]);
            gl->glUniform1i(image_program->uniformLocation("operation"), 2);
            gl->glUniform1i(image_program->uniformLocation("use_refined_matte"), GL_TRUE);
            gl->glUniform1i(image_program->uniformLocation("color_count"), 1);
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            gl->glUniform1i(image_program->uniformLocation("use_refined_matte"), GL_FALSE);
            if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
                return result(OpenGlCompositionStatus::Failed, "color-matte-grade",
                    "The refined Curves correction pass failed.", code);
            measured.color_adjustment_count += 1;
            measured.draw_submission_nanoseconds += elapsed(started);
            for (const auto& frame : uses.frames) {
                std::string cause; std::int64_t code = 0;
                if (!frame->endUse(context.get(), cause, code))
                    return result(OpenGlCompositionStatus::Failed, "protect-image-texture", cause, code);
            }
            uses.frames.clear();
            if (interop_guard.adapter) {
                auto released = interop_guard.adapter->end(); interop_guard.adapter = nullptr;
                if (released.status != OpenGlCompositionStatus::Complete) return released;
            }
            if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
            return result(OpenGlCompositionStatus::Complete);
        }
        gl->glActiveTexture(GL_TEXTURE0);
        measured.color_adjustment_count += operation.colors.size();
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        measured.draw_submission_nanoseconds += elapsed(started);
        if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "draw-image-operation", "Image operation failed.", code);
        for (const auto& frame : uses.frames) {
            std::string cause; std::int64_t code = 0;
            if (!frame->endUse(context.get(), cause, code))
                return result(OpenGlCompositionStatus::Failed, "protect-image-texture", cause, code);
        }
        uses.frames.clear();
        if (interop_guard.adapter) {
            auto released = interop_guard.adapter->end(); interop_guard.adapter = nullptr;
            if (released.status != OpenGlCompositionStatus::Complete) return released;
        }
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        return result(OpenGlCompositionStatus::Complete);
    }
    OpenGlCompositionResult collect() {
        for (auto& target : targets) {
            if (!target.state) continue;
            auto& state = *target.state;
            std::lock_guard lock(state.mutex);
            if (state.unsafe) return result(OpenGlCompositionStatus::Failed, "consumer-fence",
                "A consumer could not protect its texture draw.");
            if ((image_operation || raster_request) && target.state.use_count() == 1) {
                // Graph evaluation and interactive raster composition can reuse
                // released targets immediately. Order outstanding producer and
                // consumer work on the GPU before overwriting their pixels.
                if (state.producer) {
                    gl->glWaitSync(state.producer, 0, GL_TIMEOUT_IGNORED);
                    gl->glDeleteSync(state.producer); state.producer = nullptr;
                }
                for (const auto fence : state.consumers) {
                    gl->glWaitSync(fence, 0, GL_TIMEOUT_IGNORED); gl->glDeleteSync(fence);
                }
                state.consumers.clear();
            }
            for (auto it = state.consumers.begin(); it != state.consumers.end();) {
                const auto status = gl->glClientWaitSync(*it, 0, 0);
                if (status == GL_WAIT_FAILED) return result(OpenGlCompositionStatus::Failed,
                    "collect-fence", "Cannot query the texture consumer fence.", gl->glGetError());
                if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
                    gl->glDeleteSync(*it); it = state.consumers.erase(it);
                } else ++it;
            }
            if (target.state.use_count() == 1 && state.producer && state.consumers.empty()) {
                const auto status = gl->glClientWaitSync(state.producer, 0, 0);
                if (status == GL_WAIT_FAILED) return result(OpenGlCompositionStatus::Failed,
                    "collect-fence", "Cannot query the texture producer fence.", gl->glGetError());
                if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
                    gl->glDeleteSync(state.producer); state.producer = nullptr;
                }
            }
        }
        return result(OpenGlCompositionStatus::Complete);
    }
    bool available(const Target& target) const {
        if (!target.state) return true;
        std::lock_guard lock(target.state->mutex);
        return target.state.use_count() == 1 && !target.state->producer &&
            target.state->consumers.empty() && !target.state->unsafe;
    }
    OpenGlResourceUsage resources() const noexcept {
        std::uint64_t bytes = static_cast<std::uint64_t>(source_width) * source_height * 4;
        bytes += image_texture_bytes[0] + image_texture_bytes[1] + image_texture_bytes[2]
            - matte_upload_reserved[0] - matte_upload_reserved[1] - matte_upload_reserved[2];
        if (video_interop) bytes += video_interop->reservedBytes();
        if (output && output->isValid()) bytes += static_cast<std::uint64_t>(output->width()) * output->height() * 4;
        for (const auto& target : targets) if (target.output && target.output->isValid()) bytes += target.bytes;
        if (effect_scratch && effect_scratch->isValid())
            bytes += static_cast<std::uint64_t>(effect_scratch->width()) *
                effect_scratch->height() * 4;
        bytes += raster_reserved;
        const std::uint64_t geometry = (lookup_buffers[0] ? 16384ULL : 0) +
            (lookup_buffers[1] ? 16384ULL : 0) + (lookup_buffers[2] ? 16384ULL : 0) +
            (lookup_buffers[3] ? 12288ULL : 0) + (lookup_buffers[4] ? 12288ULL : 0) +
            curve_lut_bytes;
        return {bytes, geometry, std::max(peak_known_bytes, bytes + geometry)};
    }
    void rememberResourcePeak() noexcept { peak_known_bytes = resources().peak_known_bytes; }
};

std::unique_ptr<QOffscreenSurface> OpenGlFrameCompositor::createSurface() {
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app || QThread::currentThread() != app->thread()) return {};
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(0);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    auto surface = std::make_unique<QOffscreenSurface>();
    // Surface creation resolves the native buffer configuration on the GUI
    // thread. Only the worker creates a context, using that resolved format.
    surface->setFormat(format);
    surface->create();
    return surface->isValid() ? std::move(surface) : nullptr;
}

OpenGlFrameCompositor::OpenGlFrameCompositor(QOffscreenSurface* surface, OpenGlPrecisionPolicy precision,
    QOpenGLContext* share_context, std::shared_ptr<OpenGlTexturePoolBudget> budget)
    : impl_(std::make_unique<Impl>(surface, precision, share_context, std::move(budget))) {}
OpenGlFrameCompositor::~OpenGlFrameCompositor() = default;

int OpenGlImageInput::width() const noexcept {
#ifdef CS_NATIVE_VIDEO_INTEROP
    if (native) return native->width();
#else
    if (native) return 0;
#endif
    return texture ? texture->width() : rgba ? rgba->width : 0;
}
int OpenGlImageInput::height() const noexcept {
#ifdef CS_NATIVE_VIDEO_INTEROP
    if (native) return native->height();
#else
    if (native) return 0;
#endif
    return texture ? texture->height() : rgba ? rgba->height : 0;
}

OpenGlTextureCompositionResult OpenGlFrameCompositor::importFrame(
    const media::NativeVideoFramePtr& frame, const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    OpenGlImageOperation operation;
    operation.background.native = frame;
    return processImage(operation, cancel, timings);
}

OpenGlCompositionResult OpenGlFrameCompositor::copyToNative(const OpenGlTextureFramePtr& source,
    const std::shared_ptr<const media::NativeVideoFrame>& destination, const CancellationPredicate& cancel) {
#if defined(CS_NATIVE_VIDEO_INTEROP)
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        if (!source || !destination || source->width() != destination->width() || source->height() != destination->height())
            return {OpenGlCompositionStatus::Unsupported, {}, "check-native-output", "Source and encoding surface dimensions must match."};
        auto active = p.activate(); if (active.status != OpenGlCompositionStatus::Complete) return active;
        std::string cause; std::int64_t code = 0;
        if (!source->beginUse(p.context.get(), cause, code)) return {OpenGlCompositionStatus::Failed, {}, "wait-native-output", cause, code};
        if (!p.video_interop) p.video_interop = std::make_unique<detail::WindowsVideoInterop>(p.context.get(), p.gl);
        auto result = p.video_interop->write(source->texture(), destination);
        if (!source->endUse(p.context.get(), cause, code)) return {OpenGlCompositionStatus::Failed, {}, "release-native-output", cause, code};
        p.rememberResourcePeak();
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        return result;
    } catch (const std::exception& error) { return {OpenGlCompositionStatus::Failed, {}, "copy-native-output", error.what()}; }
#else
    (void)source; (void)destination;
    if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
    return {OpenGlCompositionStatus::Unsupported, {}, "native-video-disabled", "Native video integration is not compiled in this raster-only backend."};
#endif
}

OpenGlTextureCompositionResult OpenGlFrameCompositor::processImage(
    const OpenGlImageOperation& operation, const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    const bool has_refined_color = operation.kind == OpenGlImageOperationKind::Color &&
        std::any_of(operation.colors.begin(), operation.colors.end(), requiresCpuMatteRefinement);
    if (has_refined_color && operation.colors.size() > 1) {
        OpenGlImageInput source = operation.background;
        OpenGlTextureFramePtr retained;
        OpenGlCompositionTimings total;
        for (const auto& color : operation.colors) {
            if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
            auto single = operation;
            single.background = source;
            single.colors = {color};
            OpenGlCompositionTimings measured;
            auto processed = processImage(single, cancel, &measured);
            total.uploaded_bytes += measured.uploaded_bytes;
            total.uploaded_layers += measured.uploaded_layers;
            total.upload_nanoseconds += measured.upload_nanoseconds;
            total.draw_submission_nanoseconds += measured.draw_submission_nanoseconds;
            total.color_adjustment_submission_nanoseconds += measured.color_adjustment_submission_nanoseconds;
            total.color_adjustment_count += measured.color_adjustment_count;
            if (timings) *timings = total;
            if (processed.status != OpenGlCompositionStatus::Complete) return processed;
            retained = processed.frame;
            source = {nullptr, retained};
        }
        if (timings) *timings = total;
        return {OpenGlCompositionStatus::Complete, std::move(retained)};
    }
    if (operation.kind == OpenGlImageOperationKind::Glow && !operation.foreground.texture) {
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        if (!effects::validGlow(operation.glow))
            return {OpenGlCompositionStatus::Unsupported, {}, "glow-parameters", "Invalid Glow parameters."};
        effects::GlowBounds bounds;
        try { bounds = effects::glowBounds(operation.background.width(), operation.background.height(), operation.glow); }
        catch (const std::exception& error) {
            return {OpenGlCompositionStatus::Unsupported, {}, "glow-bounds", error.what()};
        }
        if (operation.glow.opacity_percent == 0.0 || std::lround(operation.glow.radius_pixels) == 0) {
            OpenGlImageOperation copy;
            copy.background = operation.background;
            return processImage(copy, cancel, timings);
        }
        OpenGlCompositionTimings total;
        const auto pass = [&](OpenGlImageOperation image_operation) {
            OpenGlCompositionTimings measured;
            auto result = processImage(image_operation, cancel, &measured);
            total.uploaded_bytes += measured.uploaded_bytes;
            total.uploaded_layers += measured.uploaded_layers;
            total.upload_nanoseconds += measured.upload_nanoseconds;
            total.draw_submission_nanoseconds += measured.draw_submission_nanoseconds;
            total.producer_fence_submission_nanoseconds += measured.producer_fence_submission_nanoseconds;
            total.gaussian_blur_count += measured.gaussian_blur_count;
            total.gaussian_blur_submission_nanoseconds += measured.gaussian_blur_submission_nanoseconds;
            if (timings) *timings = total;
            return result;
        };
        OpenGlImageInput source = operation.background;
        int blur_width = bounds.width;
        int blur_height = bounds.height;
        int blur_left = bounds.left;
        int blur_top = bounds.top;
        if (operation.glow.mode == effects::GlowMode::Inner) {
            const int halo = 3 * static_cast<int>(std::lround(operation.glow.radius_pixels)) + 1;
            blur_width += 2 * halo;
            blur_height += 2 * halo;
            blur_left = halo;
            blur_top = halo;
        }
        if (blur_width > 16384 || blur_height > 16384 ||
            std::uint64_t(blur_width) * blur_height * 4 > 64ULL * 1024 * 1024)
            return {OpenGlCompositionStatus::Unsupported, {}, "glow-blur-bounds",
                "Glow blur image exceeds the 64 MiB / 16384 pixel limit."};
        if (blur_left || blur_top || blur_width != source.width() || blur_height != source.height()) {
            OpenGlImageOperation pad;
            pad.kind = OpenGlImageOperationKind::Transform;
            pad.background = source;
            pad.output_width = blur_width;
            pad.output_height = blur_height;
            pad.source_to_canvas = AffineTransform2D{1, 0, 0, 1, double(blur_left), double(blur_top)};
            auto padded = pass(pad);
            if (padded.status != OpenGlCompositionStatus::Complete) return padded;
            source = {nullptr, padded.frame};
        }
        OpenGlImageOperation blur;
        blur.kind = OpenGlImageOperationKind::GaussianBlur;
        blur.background = source;
        blur.blur_radius_pixels = operation.glow.radius_pixels;
        auto blurred = pass(blur);
        if (blurred.status != OpenGlCompositionStatus::Complete) return blurred;
        OpenGlImageOperation merge;
        merge.kind = OpenGlImageOperationKind::Glow;
        merge.background = operation.background;
        merge.foreground = {nullptr, blurred.frame};
        merge.glow = operation.glow;
        merge.output_width = bounds.width;
        merge.output_height = bounds.height;
        return pass(merge);
    }
    if (operation.kind == OpenGlImageOperationKind::Glow) {
        effects::GlowBounds bounds;
        try { bounds = effects::glowBounds(operation.background.width(), operation.background.height(), operation.glow); }
        catch (const std::exception& error) {
            return {OpenGlCompositionStatus::Unsupported, {}, "glow-bounds", error.what()};
        }
        const int halo = operation.glow.mode == effects::GlowMode::Inner
            ? 3 * static_cast<int>(std::lround(operation.glow.radius_pixels)) + 1 : 0;
        const int expected_blur_width = bounds.width + 2 * halo;
        const int expected_blur_height = bounds.height + 2 * halo;
        if (operation.output_width != bounds.width || operation.output_height != bounds.height ||
            operation.foreground.width() != expected_blur_width ||
            operation.foreground.height() != expected_blur_height)
            return {OpenGlCompositionStatus::Unsupported, {}, "glow-geometry",
                "Prepared Glow dimensions do not match the requested mode."};
    }
    if(operation.kind==OpenGlImageOperationKind::DropShadow) {
        if(!effects::validDropShadow(operation.drop_shadow))return {OpenGlCompositionStatus::Unsupported,{},"drop-shadow-parameters","Invalid Drop Shadow parameters."};
        if(!operation.foreground.texture) {
            effects::DropShadowBounds bounds;
            try{bounds=effects::dropShadowBounds(operation.background.width(),operation.background.height(),operation.drop_shadow);}
            catch(const std::exception& e){return {OpenGlCompositionStatus::Unsupported,{},"drop-shadow-bounds",e.what()};}
            if(operation.drop_shadow.opacity_percent==0){OpenGlImageOperation copy;copy.background=operation.background;return processImage(copy,cancel,timings);}
            OpenGlCompositionTimings total;
            const auto pass=[&](OpenGlImageOperation op){OpenGlCompositionTimings t;auto r=processImage(op,cancel,&t);
                total.uploaded_bytes+=t.uploaded_bytes;total.uploaded_layers+=t.uploaded_layers;total.upload_nanoseconds+=t.upload_nanoseconds;
                total.draw_submission_nanoseconds+=t.draw_submission_nanoseconds;total.producer_fence_submission_nanoseconds+=t.producer_fence_submission_nanoseconds;
                if(timings)*timings=total;return r;};
            OpenGlImageOperation pad;pad.kind=OpenGlImageOperationKind::Transform;pad.background=operation.background;
            pad.output_width=bounds.width;pad.output_height=bounds.height;pad.source_to_canvas=AffineTransform2D{1,0,0,1,double(bounds.left),double(bounds.top)};
            auto padded=pass(pad);if(padded.status!=OpenGlCompositionStatus::Complete)return padded;
            OpenGlImageOperation blur;blur.kind=OpenGlImageOperationKind::GaussianBlur;blur.background={nullptr,padded.frame};blur.blur_radius_pixels=operation.drop_shadow.softness_pixels;
            auto blurred=pass(blur);if(blurred.status!=OpenGlCompositionStatus::Complete)return blurred;
            padded.frame.reset();blur.background={};
            OpenGlImageOperation merge;merge.kind=OpenGlImageOperationKind::DropShadow;merge.background=operation.background;merge.foreground={nullptr,blurred.frame};merge.drop_shadow=operation.drop_shadow;
            merge.output_width=bounds.width;merge.output_height=bounds.height;
            return pass(merge);
        }
        effects::DropShadowBounds expected;
        try{expected=effects::dropShadowBounds(operation.background.width(),operation.background.height(),operation.drop_shadow);}
        catch(const std::exception& e){return {OpenGlCompositionStatus::Unsupported,{},"drop-shadow-bounds",e.what()};}
        if(operation.output_width!=expected.width||operation.output_height!=expected.height||operation.foreground.width()!=operation.output_width||operation.foreground.height()!=operation.output_height)return {OpenGlCompositionStatus::Unsupported,{},"drop-shadow-geometry","Prepared alpha dimensions must match the input."};
    }
    if(operation.kind==OpenGlImageOperationKind::ApplyTrackMatte && !validTrackMatteMode(operation.track_matte_mode))
        return {OpenGlCompositionStatus::Unsupported,{},"track-matte-mode","Invalid track matte mode."};
    if(operation.kind==OpenGlImageOperationKind::GaussianBlur) {
        if(!std::isfinite(operation.blur_radius_pixels)||operation.blur_radius_pixels<0||operation.blur_radius_pixels>100)
            return {OpenGlCompositionStatus::Unsupported,{},"image-blur-radius","Blur radius must be between 0 and 100."};
        if(std::lround(operation.blur_radius_pixels)==0) {
            auto copy=operation;copy.kind=OpenGlImageOperationKind::Copy;return processImage(copy,cancel,timings);
        }
        if(!operation.background.texture && !operation.background.native) {
            OpenGlImageOperation copy;copy.background=operation.background;
            OpenGlCompositionTimings uploaded, blurred;
            auto prepared=processImage(copy,cancel,&uploaded);
            if(prepared.status!=OpenGlCompositionStatus::Complete)return prepared;
            auto gpu=operation;gpu.background={nullptr,prepared.frame};
            auto result=processImage(gpu,cancel,&blurred);
            if(timings){*timings=blurred;timings->uploaded_bytes+=uploaded.uploaded_bytes;timings->uploaded_layers+=uploaded.uploaded_layers;timings->upload_nanoseconds+=uploaded.upload_nanoseconds;}
            return result;
        }
    }
    if (operation.output_width < 0 || operation.output_height < 0 ||
        ((operation.output_width == 0) != (operation.output_height == 0)) ||
        (operation.output_width > 0 && operation.kind != OpenGlImageOperationKind::Transform && operation.kind != OpenGlImageOperationKind::DropShadow && operation.kind != OpenGlImageOperationKind::Glow))
        return {OpenGlCompositionStatus::Unsupported, {}, "image-output-geometry", "Explicit output dimensions require a Transform and a complete positive canvas."};
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { p.image_operation = nullptr; } } guard{p};
    p.image_operation = &operation;
    return composeTexture(operation.output_width > 0 ? operation.output_width : operation.background.width(), operation.output_height > 0 ? operation.output_height : operation.background.height(), {}, cancel, timings);
}

OpenGlCompositionResult OpenGlFrameCompositor::compose(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings, CompositionOutputAlpha output_alpha) {
    return render(width, height, layers, cancel, timings, true, output_alpha);
}

OpenGlCompositionResult OpenGlFrameCompositor::prepareAsyncReadback(
    int width, int height, std::uint64_t staging_budget_bytes,
    unsigned maximum_slots, unsigned* prepared_slots,
    bool fail_allocation_for_testing) {
    if (prepared_slots) *prepared_slots = 0;
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return active;
        if (!bounded(width, height, p.texture_limit))
            return result(OpenGlCompositionStatus::Unsupported, "check-pbo-limits",
                "Canvas exceeds the device limits for asynchronous readback.");
        const auto frame_bytes = static_cast<std::uint64_t>(width) * height * 4;
        maximum_slots = std::min(maximum_slots, 2U);
        const auto affordable = frame_bytes == 0 ? 0 : staging_budget_bytes / frame_bytes;
        const auto slot_count = static_cast<unsigned>(std::min<std::uint64_t>(maximum_slots, affordable));
        if (slot_count == 0 || frame_bytes > static_cast<std::uint64_t>(
                std::numeric_limits<GLsizeiptr>::max()))
            return result(OpenGlCompositionStatus::Unsupported, "pbo-budget",
                "The export staging budget cannot hold an asynchronous readback slot.");
        if (fail_allocation_for_testing)
            return result(OpenGlCompositionStatus::Failed, "allocate-pbo",
                "Injected asynchronous readback allocation failure for regression coverage.");
        if (std::any_of(p.readback_slots.begin(), p.readback_slots.end(),
                [](const auto& slot) { return slot.busy; }))
            return result(OpenGlCompositionStatus::Busy, "pbo-reconfigure",
                "Pending readback tickets must be collected before resizing the PBO pool.");
        if (p.output && p.output->width() == width && p.output->height() == height &&
            p.readback_slots.size() == slot_count &&
            std::all_of(p.readback_slots.begin(), p.readback_slots.end(),
                [frame_bytes](const auto& slot) { return slot.bytes == frame_bytes; })) {
            if (prepared_slots) *prepared_slots = slot_count;
            return result(OpenGlCompositionStatus::Complete);
        }

        if (!p.output || p.output->width() != width || p.output->height() != height) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            p.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!p.output->isValid()) return result(OpenGlCompositionStatus::Failed,
                "allocate-framebuffer", "Cannot allocate the output RGBA8 framebuffer.");
            p.rememberResourcePeak();
        }
        for (auto& slot : p.readback_slots) {
            if (slot.fence) p.gl->glDeleteSync(slot.fence);
            if (slot.buffer) p.gl->glDeleteBuffers(1, &slot.buffer);
        }
        p.readback_slots.clear();
        p.readback_slots.resize(slot_count);
        while (p.gl->glGetError() != GL_NO_ERROR) {}
        for (auto& slot : p.readback_slots) {
            p.gl->glGenBuffers(1, &slot.buffer);
            p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
            p.gl->glBufferData(GL_PIXEL_PACK_BUFFER,
                static_cast<GLsizeiptr>(frame_bytes), nullptr, GL_STREAM_READ);
            slot.bytes = frame_bytes;
            slot.width = width;
            slot.height = height;
            if (!slot.buffer || p.gl->glGetError() != GL_NO_ERROR) {
                for (auto& allocated : p.readback_slots) {
                    if (allocated.buffer) p.gl->glDeleteBuffers(1, &allocated.buffer);
                }
                p.readback_slots.clear();
                return result(OpenGlCompositionStatus::Failed, "allocate-pbo",
                    "The driver could not allocate the bounded pixel-pack buffer pool.",
                    p.gl->glGetError());
            }
        }
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (prepared_slots) *prepared_slots = slot_count;
        return result(OpenGlCompositionStatus::Complete);
    } catch (const std::exception& error) {
        return result(OpenGlCompositionStatus::Failed, "prepare-pbo", error.what());
    }
}

OpenGlReadbackResult OpenGlFrameCompositor::submitAsyncReadback(
    int width, int height, const std::vector<CompositionLayer>& layers,
    const CancellationPredicate& cancel, CompositionOutputAlpha output_alpha) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    const auto cancelled = [&] { return cancel && cancel(); };
    try {
        if (cancelled()) return {OpenGlCompositionStatus::Cancelled};
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete)
            return {active.status, {}, {}, std::move(active.operation), std::move(active.cause), active.error_code};
        const auto free_slot = std::find_if(p.readback_slots.begin(), p.readback_slots.end(),
            [](const auto& slot) { return !slot.busy; });
        if (free_slot == p.readback_slots.end())
            return {OpenGlCompositionStatus::Busy, {}, {}, "pbo-backpressure",
                "All bounded asynchronous readback slots are occupied."};
        if (free_slot->width != width || free_slot->height != height || !p.output ||
            p.output->width() != width || p.output->height() != height)
            return {OpenGlCompositionStatus::Unsupported, {}, {}, "pbo-size",
                "The asynchronous readback pool was not prepared for this canvas."};
        p.selected_output = p.output.get();
        OpenGlCompositionTimings composition_timings;
        auto rendered = render(width, height, layers, cancel, &composition_timings, false,
            output_alpha);
        p.selected_output = nullptr;
        if (rendered.status != OpenGlCompositionStatus::Complete) {
            OpenGlReadbackResult failed{rendered.status, {}, {}, std::move(rendered.operation),
                std::move(rendered.cause), rendered.error_code};
            failed.composition_timings = composition_timings;
            return failed;
        }
        if (cancelled()) return {OpenGlCompositionStatus::Cancelled};
        const auto submit_started = Clock::now();
        if (!p.output->bind())
            return {OpenGlCompositionStatus::Failed, {}, {}, "bind-pbo-source",
                "Cannot bind the completed composition framebuffer."};
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, free_slot->buffer);
        p.gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        p.gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        p.gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        free_slot->fence = p.gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        p.gl->glFlush();
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        const auto code = p.gl->glGetError();
        if (!free_slot->fence || code) {
            if (free_slot->fence) p.gl->glDeleteSync(free_slot->fence);
            free_slot->fence = nullptr;
            return {OpenGlCompositionStatus::Failed, {}, {}, "submit-pbo-readback",
                "The driver could not submit the asynchronous framebuffer readback.", code};
        }
        free_slot->ticket = ++p.next_readback_ticket;
        free_slot->preserve_alpha = output_alpha == CompositionOutputAlpha::Preserve;
        free_slot->busy = true;
        OpenGlReadbackResult submitted{OpenGlCompositionStatus::Complete, {free_slot->ticket}};
        submitted.composition_timings = composition_timings;
        submitted.submission_nanoseconds = elapsed(submit_started);
        submitted.bytes = free_slot->bytes;
        return submitted;
    } catch (const std::exception& error) {
        p.selected_output = nullptr;
        return {OpenGlCompositionStatus::Failed, {}, {}, "submit-pbo-readback", error.what()};
    }
}

OpenGlCompositionResult OpenGlFrameCompositor::pollAsyncReadback(
    OpenGlReadbackTicket ticket) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return active;
        const auto slot = std::find_if(p.readback_slots.begin(), p.readback_slots.end(),
            [ticket](const auto& item) { return item.busy && item.ticket == ticket.value; });
        if (slot == p.readback_slots.end())
            return result(OpenGlCompositionStatus::Unsupported, "poll-pbo-ticket",
                "The asynchronous readback ticket is unknown or has already been released.");
        const auto oldest = std::min_element(p.readback_slots.begin(), p.readback_slots.end(),
            [](const auto& a, const auto& b) {
                if (a.busy != b.busy) return a.busy;
                return a.busy && a.ticket < b.ticket;
            });
        if (oldest == p.readback_slots.end() || oldest != slot)
            return result(OpenGlCompositionStatus::Busy, "poll-pbo-order",
                "Asynchronous readback tickets must be polled in submission order.");
        const auto status = p.gl->glClientWaitSync(slot->fence, 0, 0);
        if (status == GL_TIMEOUT_EXPIRED)
            return result(OpenGlCompositionStatus::Busy, "poll-pbo-fence",
                "The asynchronous readback is still in flight.");
        if (status == GL_WAIT_FAILED)
            return result(OpenGlCompositionStatus::Failed, "poll-pbo-fence",
                "The driver failed while polling the asynchronous readback fence.",
                p.gl->glGetError());
        return result(OpenGlCompositionStatus::Complete);
    } catch (const std::exception& error) {
        return result(OpenGlCompositionStatus::Failed, "poll-pbo-fence", error.what());
    }
}

OpenGlReadbackResult OpenGlFrameCompositor::collectAsyncReadback(
    OpenGlReadbackTicket ticket, const CancellationPredicate& cancel,
    bool fail_collection_for_testing) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete)
            return {active.status, {}, {}, std::move(active.operation), std::move(active.cause), active.error_code};
        const auto slot = std::find_if(p.readback_slots.begin(), p.readback_slots.end(),
            [ticket](const auto& item) { return item.busy && item.ticket == ticket.value; });
        if (slot == p.readback_slots.end())
            return {OpenGlCompositionStatus::Unsupported, {}, {}, "collect-pbo-ticket",
                "The asynchronous readback ticket is unknown or has already been released."};
        const auto oldest = std::min_element(p.readback_slots.begin(), p.readback_slots.end(),
            [](const auto& a, const auto& b) {
                if (a.busy != b.busy) return a.busy;
                return a.busy && a.ticket < b.ticket;
            });
        if (oldest == p.readback_slots.end() || oldest != slot)
            return {OpenGlCompositionStatus::Busy, {}, {}, "collect-pbo-order",
                "Asynchronous readback tickets must be collected in submission order."};
        if (fail_collection_for_testing)
            return {OpenGlCompositionStatus::Failed, ticket, {}, "map-pbo",
                "Injected asynchronous readback collection failure for regression coverage.", -1};

        const auto wait_started = Clock::now();
        for (;;) {
            if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
            const auto status = p.gl->glClientWaitSync(slot->fence,
                GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000);
            if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) break;
            if (status == GL_WAIT_FAILED)
                return {OpenGlCompositionStatus::Failed, {}, {}, "wait-pbo-fence",
                    "The driver failed while waiting for the asynchronous readback fence.",
                    p.gl->glGetError()};
        }
        const auto wait_nanoseconds = elapsed(wait_started);
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        const auto copy_started = Clock::now();
        media::RgbaFrame frame{slot->width, slot->height, slot->width * 4, {}};
        frame.rgba_pixels.resize(static_cast<std::size_t>(slot->bytes));
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->buffer);
        const auto* mapped = static_cast<const std::uint8_t*>(p.gl->glMapBufferRange(
            GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(slot->bytes), GL_MAP_READ_BIT));
        if (!mapped) {
            p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            return {OpenGlCompositionStatus::Failed, {}, {}, "map-pbo",
                "The completed asynchronous readback buffer could not be mapped.",
                p.gl->glGetError()};
        }
        std::memcpy(frame.rgba_pixels.data(), mapped, frame.rgba_pixels.size());
        const bool unmapped = p.gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (!unmapped)
            return {OpenGlCompositionStatus::Failed, {}, {}, "unmap-pbo",
                "The asynchronous readback buffer contents became invalid."};
        for (int y = 0; y < frame.height / 2; ++y) {
            auto first = frame.rgba_pixels.begin() + static_cast<std::size_t>(y) * frame.stride;
            auto last = frame.rgba_pixels.begin() + static_cast<std::size_t>(frame.height - 1 - y) * frame.stride;
            std::swap_ranges(first, first + frame.stride, last);
        }
        if (slot->preserve_alpha && !unpremultiplyRgba(frame, cancel))
            return {OpenGlCompositionStatus::Cancelled};
        const auto copy_nanoseconds = elapsed(copy_started);
        p.gl->glDeleteSync(slot->fence);
        slot->fence = nullptr;
        slot->busy = false;
        slot->ticket = 0;
        slot->preserve_alpha = false;
        OpenGlReadbackResult completed{OpenGlCompositionStatus::Complete, {}, std::move(frame)};
        completed.fence_wait_nanoseconds = wait_nanoseconds;
        completed.copy_nanoseconds = copy_nanoseconds;
        completed.bytes = slot->bytes;
        return completed;
    } catch (const std::exception& error) {
        return {OpenGlCompositionStatus::Failed, {}, {}, "collect-pbo-readback", error.what()};
    }
}

void OpenGlFrameCompositor::discardAsyncReadbacks() noexcept {
    auto& p = *impl_;
    if (!p.context || p.readback_slots.empty()) return;
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return;
        if (std::any_of(p.readback_slots.begin(), p.readback_slots.end(),
                [](const auto& slot) { return slot.busy; })) p.gl->glFinish();
        for (auto& slot : p.readback_slots) {
            if (slot.fence) p.gl->glDeleteSync(slot.fence);
            slot.fence = nullptr;
            slot.ticket = 0;
            slot.busy = false;
        }
        p.context->doneCurrent();
    } catch (...) {}
}

unsigned OpenGlFrameCompositor::asyncReadbackSlotCount() const noexcept {
    return static_cast<unsigned>(impl_->readback_slots.size());
}

OpenGlTextureCompositionResult OpenGlFrameCompositor::composeTexture(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings) {
    if (timings) *timings = {};
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    const auto converted = [](OpenGlCompositionResult r) {
        return OpenGlTextureCompositionResult{r.status, {}, std::move(r.operation), std::move(r.cause), r.error_code};
    };
    try {
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        if (p.retiring) return {OpenGlCompositionStatus::Unsupported, {}, "retired-session",
            "This compositor session no longer publishes textures."};
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return converted(std::move(active));
        if (!p.image_operation && (!p.share_context || !QOpenGLContext::areSharing(p.context.get(), p.share_context)))
            return {OpenGlCompositionStatus::Unsupported, {}, "check-sharing",
                "The producer has no verified shared context."};
        if (!bounded(width, height, p.texture_limit) ||
            static_cast<std::uint64_t>(width) * height * 4 > p.budget->impl_->byte_limit)
            return {OpenGlCompositionStatus::Unsupported, {}, "check-pool-limits",
                "Canvas exceeds the shared texture pool budget or device limits."};
        if(!p.image_operation || p.image_operation->kind!=OpenGlImageOperationKind::ApplyTrackMatte)p.discardMatteUploads();
        if(!p.raster_request && std::none_of(layers.begin(),layers.end(),[](const auto& l){return l.blend_mode!=BlendMode::Normal;}))p.discardBlendBackdrop();
        if (p.raster_request && std::all_of(p.raster_request->nodes.begin(),p.raster_request->nodes.end(),[](const auto& node) {
            return node.blend_mode==BlendMode::Normal && std::all_of(node.layers.begin(),node.layers.end(),[](const auto& layer){return !layer.adjustment && layer.blend_mode==BlendMode::Normal;});
        })) p.discardBlendBackdrop();
        auto collected = p.collect();
        if (collected.status != OpenGlCompositionStatus::Complete) return converted(std::move(collected));
        std::size_t index = p.targets.size();
        for (std::size_t i = 0; i < p.targets.size(); ++i) {
            if (p.available(p.targets[i])) {
                index = i;
                if (p.targets[i].output && p.targets[i].output->width() == width &&
                    p.targets[i].output->height() == height) break;
            }
        }
        const auto bytes = static_cast<std::uint64_t>(width) * height * 4;
        const bool adding = index == p.targets.size();
        const auto old_bytes = adding ? 0 : p.targets[index].bytes;
        p.targets.reserve(p.budget->impl_->target_limit);
        {
            std::lock_guard lock(p.budget->impl_->mutex);
            if (adding && p.budget->impl_->targets >= p.budget->impl_->target_limit)
                return {OpenGlCompositionStatus::Busy};
            if(p.budget->impl_->bytes - old_bytes + bytes > p.budget->impl_->byte_limit) {
                // A displayed lease cannot return until its replacement arrives.
                // Recover instead of retrying Busy forever when the backdrop
                // leaves no room for that replacement output.
                const bool raster_blend=p.raster_request && std::any_of(p.raster_request->nodes.begin(),p.raster_request->nodes.end(),[](const auto& node) {
                    return node.blend_mode!=BlendMode::Normal || std::any_of(node.layers.begin(),node.layers.end(),[](const auto& layer){return layer.adjustment || layer.blend_mode!=BlendMode::Normal;});
                });
                if(p.blend_reserved || raster_blend)return {OpenGlCompositionStatus::Unsupported,{},"blend-output-budget",
                    "Leased composition outputs and blend backdrop exceed the shared texture budget."};
                return {OpenGlCompositionStatus::Busy};
            }
            if (adding) { p.targets.emplace_back(); ++p.budget->impl_->targets; }
            p.budget->impl_->bytes = p.budget->impl_->bytes - old_bytes + bytes;
        }
        auto& target = p.targets[index];
        target.bytes = bytes;
        if (!target.output || target.output->width() != width || target.output->height() != height) {
            target.output.reset();
            QOpenGLFramebufferObjectFormat format; format.setInternalTextureFormat(GL_RGBA8);
            target.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!target.output->isValid()) return {OpenGlCompositionStatus::Failed, {},
                "allocate-texture-target", "Cannot allocate a shared RGBA8 target."};
            p.rememberResourcePeak();
        }
        target.state = std::make_shared<OpenGlTextureFrame::State>();
        // Allocate the bounded consumer registry on the worker, never while drawing.
        target.state->consumers.reserve(128);
        target.state->width = width; target.state->height = height;
        target.state->texture = target.output->texture(); target.state->session = p.session;
        target.state->group = p.context->shareGroup();
        p.selected_output = target.output.get();
        auto rendered = render(width, height, layers, cancel, timings, false);
        p.selected_output = nullptr;
        if (rendered.status != OpenGlCompositionStatus::Complete) return converted(std::move(rendered));
        p.gl->glBindTexture(GL_TEXTURE_2D, target.state->texture);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const auto fence_started = Clock::now();
        target.state->producer = p.gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        p.gl->glFlush();
        if (timings) timings->producer_fence_submission_nanoseconds = elapsed(fence_started);
        const auto code = p.gl->glGetError();
        if (!target.state->producer || code) return {OpenGlCompositionStatus::Failed, {},
            "publish-texture-fence", "Cannot publish the texture producer fence.", code};
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        return {OpenGlCompositionStatus::Complete,
            OpenGlTextureFramePtr(new OpenGlTextureFrame(target.state))};
    } catch (const std::exception& error) {
        p.selected_output = nullptr;
        return {OpenGlCompositionStatus::Failed, {}, "compose-texture", error.what()};
    }
}

OpenGlTextureCompositionResult OpenGlFrameCompositor::composeRasterTexture(
    const OpenGlRasterRequest& request, const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    impl_->raster_request = &request;
    struct Guard { Impl& p; ~Guard() { p.raster_request = nullptr; } } guard{*impl_};
    return composeTexture(request.size.width(), request.size.height(), {}, cancel, timings);
}

OpenGlCompositionResult OpenGlFrameCompositor::readback(const OpenGlTextureFramePtr& frame,
    const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    if (frame && frame->backend() != GraphicsBackend::OpenGl)
        return {OpenGlCompositionStatus::Unsupported, {}, "check-image-backend", "OpenGL readback cannot consume a Vulkan image."};
    if (timings) *timings = {};
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        if (!frame || !frame->valid() || frame->session() != p.session)
            return result(OpenGlCompositionStatus::Unsupported, "read-texture", "Texture lease is no longer available.");
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return active;
        auto target = std::find_if(p.targets.begin(), p.targets.end(), [&](const auto& t) {
            return t.state == frame->state_;
        });
        if (target == p.targets.end() || !target->output->bind())
            return result(OpenGlCompositionStatus::Failed, "read-texture", "Cannot bind the leased framebuffer.");
        const auto started = Clock::now();
        p.gl->glWaitSync(frame->state_->producer, 0, GL_TIMEOUT_IGNORED);
        media::RgbaFrame rgba{frame->width(), frame->height(), frame->width() * 4, {}};
        rgba.rgba_pixels.resize(static_cast<std::size_t>(rgba.stride) * rgba.height);
        p.gl->glPixelStorei(GL_PACK_ALIGNMENT, 1); p.gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        p.gl->glReadPixels(0, 0, rgba.width, rgba.height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.rgba_pixels.data());
        if (timings) {
            timings->readback_bytes = rgba.rgba_pixels.size(); timings->readback_nanoseconds = elapsed(started);
        }
        if (auto code = p.gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "read-texture", "Texture readback failed.", code);
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        for (int y = 0; y < rgba.height / 2; ++y) {
            auto first = rgba.rgba_pixels.begin() + static_cast<std::size_t>(y) * rgba.stride;
            auto last = rgba.rgba_pixels.begin() + static_cast<std::size_t>(rgba.height - 1 - y) * rgba.stride;
            std::swap_ranges(first, first + rgba.stride, last);
        }
        if (timings) timings->readback_nanoseconds = elapsed(started);
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        return {OpenGlCompositionStatus::Complete, std::move(rgba)};
    } catch (const std::exception& error) { return result(OpenGlCompositionStatus::Failed, "read-texture", error.what()); }
}

void OpenGlFrameCompositor::retireTextureFrames() noexcept { impl_->retiring = true; }

OpenGlCompositionResult OpenGlFrameCompositor::collectReleasedTextureFrames() {
    auto& p = *impl_;
    if (p.targets.empty() && !(p.retiring && (p.blend_reserved || p.matte_upload_reserved[0] || p.matte_upload_reserved[1] || p.matte_upload_reserved[2]))) return result(OpenGlCompositionStatus::Complete);
    auto active = p.activate();
    if (active.status != OpenGlCompositionStatus::Complete) return active;
    if(p.retiring){p.discardBlendBackdrop();p.discardMatteUploads();}
    auto collected = p.collect();
    if (p.retiring) {
        // A displayed lease must not retain unused slots and starve the next
        // activation. Only completed, unleased targets can be deleted here.
        for (auto it = p.targets.begin(); it != p.targets.end();) {
            if (!p.available(*it)) { ++it; continue; }
            it->output.reset();
            {
                std::lock_guard lock(p.budget->impl_->mutex);
                p.budget->impl_->bytes -= it->bytes;
                --p.budget->impl_->targets;
            }
            it = p.targets.erase(it);
        }
    }
    p.context->doneCurrent(); return collected;
}
bool OpenGlFrameCompositor::hasPendingTextureFrames() const {
    return std::any_of(impl_->targets.begin(), impl_->targets.end(), [&](const auto& t) {
        return !impl_->available(t);
    });
}
std::uint64_t OpenGlFrameCompositor::texturePoolBytes() const noexcept { return impl_->budget->bytes(); }
unsigned OpenGlFrameCompositor::texturePoolOccupancy() const {
    return static_cast<unsigned>(std::count_if(impl_->targets.begin(), impl_->targets.end(),
        [](const auto& t) { return t.state && t.state.use_count() > 1; }));
}
OpenGlResourceUsage OpenGlFrameCompositor::resourceUsage() const noexcept { return impl_->resources(); }

OpenGlCompositionResult OpenGlFrameCompositor::render(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings, bool read_output,
    CompositionOutputAlpha output_alpha) {
    if (output_alpha != CompositionOutputAlpha::Opaque &&
        output_alpha != CompositionOutputAlpha::Preserve)
        return result(OpenGlCompositionStatus::Unsupported, "validate-output-alpha",
            "The requested composition output alpha mode is invalid.");
    for(std::size_t i=0;i<layers.size();++i)if(!validBlendMode(layers[i].blend_mode))
        return result(OpenGlCompositionStatus::Unsupported,"check-blend-mode","Invalid layer blend mode.",0,static_cast<int>(i));
    const bool blending=std::any_of(layers.begin(),layers.end(),[](const auto& l){return l.blend_mode!=BlendMode::Normal;});
    if (std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return layer.texture_frame && layer.texture_frame->backend() != GraphicsBackend::OpenGl;
        })) return result(OpenGlCompositionStatus::Unsupported, "check-image-backend", "An OpenGL compositor cannot consume a Vulkan image.");
    if (std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return !layer.effect_stack.empty(); })) {
        auto prepared_layers = layers;
        for (auto& layer : prepared_layers) {
            if (layer.effect_stack.empty()) continue;
            if (!layer.gpu_color_adjustments.empty() || !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "prepare-clip-effects",
                    "A source cannot combine processed and unprocessed effect stacks.");
            auto passes = effects::colorAdjustmentPasses(layer.effect_stack);
            if (!passes) return result(OpenGlCompositionStatus::Unsupported, "prepare-clip-effects",
                "The clip effect stack is outside the shared GPU effect contract.");
            layer.gpu_color_adjustments = std::move(*passes);
            layer.effect_stack = {};
        }
        return render(width, height, prepared_layers, cancel, timings, read_output, output_alpha);
    }
    const bool needs_refined_preprocess = std::any_of(layers.begin(), layers.end(),
        [](const auto& layer) {
            return std::any_of(layer.gpu_color_adjustments.begin(),
                    layer.gpu_color_adjustments.end(), requiresCpuMatteRefinement) ||
                std::any_of(layer.gpu_effects.begin(), layer.gpu_effects.end(), [](const auto& effect) {
                    const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect);
                    return adjustment && requiresCpuMatteRefinement(*adjustment);
                });
        });
    if (needs_refined_preprocess) {
        auto prepared_layers = layers;
        for (std::size_t index = 0; index < prepared_layers.size(); ++index) {
            auto& layer = prepared_layers[index];
            const bool refined = std::any_of(layer.gpu_color_adjustments.begin(),
                    layer.gpu_color_adjustments.end(), requiresCpuMatteRefinement) ||
                std::any_of(layer.gpu_effects.begin(), layer.gpu_effects.end(), [](const auto& effect) {
                    const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect);
                    return adjustment && requiresCpuMatteRefinement(*adjustment);
                });
            if (!refined) continue;
            if (!layer.gpu_color_adjustments.empty() && !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "A layer cannot combine legacy and ordered GPU effect lists.", 0,
                    static_cast<int>(index));
            OpenGlImageInput source{layer.frame, layer.texture_frame, layer.native_frame};
            const auto apply = [&](OpenGlImageOperation operation) -> OpenGlCompositionResult {
                operation.background = source;
                const auto processed = processImage(operation, cancel);
                if (processed.status != OpenGlCompositionStatus::Complete || !processed.frame)
                    return result(processed.status, processed.operation.c_str(), processed.cause,
                        processed.error_code, static_cast<int>(index));
                source = {nullptr, processed.frame};
                return result(OpenGlCompositionStatus::Complete);
            };
            if (!layer.gpu_effects.empty()) {
                for (const auto& effect : layer.gpu_effects) {
                    OpenGlImageOperation operation;
                    if (const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect)) {
                        operation.kind = OpenGlImageOperationKind::Color;
                        operation.colors = {*adjustment};
                    } else {
                        operation.kind = OpenGlImageOperationKind::GaussianBlur;
                        operation.blur_radius_pixels = std::get<GpuGaussianBlurParameters>(effect).radius_pixels;
                    }
                    const auto processed = apply(std::move(operation));
                    if (processed.status != OpenGlCompositionStatus::Complete) return processed;
                }
            } else {
                OpenGlImageOperation operation;
                operation.kind = OpenGlImageOperationKind::Color;
                operation.colors = layer.gpu_color_adjustments;
                const auto processed = apply(std::move(operation));
                if (processed.status != OpenGlCompositionStatus::Complete) return processed;
            }
            layer.texture_frame = source.texture;
            layer.native_frame.reset();
            layer.gpu_color_adjustments.clear();
            layer.gpu_effects.clear();
        }
        return render(width, height, prepared_layers, cancel, timings, read_output, output_alpha);
    }
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto& layer = layers[index];
        const bool refined_adjustment = std::any_of(layer.gpu_color_adjustments.begin(),
            layer.gpu_color_adjustments.end(), requiresCpuMatteRefinement) ||
            std::any_of(layer.gpu_effects.begin(), layer.gpu_effects.end(), [](const auto& effect) {
                const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect);
                return adjustment && requiresCpuMatteRefinement(*adjustment);
            });
        if (refined_adjustment)
            return result(OpenGlCompositionStatus::Unsupported, "color-matte-refinement",
                "The spatial Color qualifier could not be prepared for GPU processing.",
                0, static_cast<int>(index));
    }
    OpenGlCompositionTimings measured;
    struct TimingsGuard {
        OpenGlCompositionTimings* output;
        const OpenGlCompositionTimings& measured;
        ~TimingsGuard() { if (output) *output = measured; }
    } timings_guard{timings, measured};
    if (timings) *timings = {};
    const auto cancelled = [&] { return cancel && cancel(); };
    if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
    auto& p = *impl_;
    struct CurrentGuard {
        Impl& p;
        bool release;
        ~CurrentGuard() { if (release && p.context) p.context->doneCurrent(); }
    } guard{p, read_output};
    try {
        auto activated = p.activate();
        if (activated.status != OpenGlCompositionStatus::Complete) return activated;
        auto* gl = p.gl;
        if (!bounded(width, height, p.texture_limit))
            return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                "Canvas exceeds the device texture limit or 256 MiB texture budget.");
        if(!p.image_operation || p.image_operation->kind!=OpenGlImageOperationKind::ApplyTrackMatte)p.discardMatteUploads();
        if(!blending && !p.raster_request)p.discardBlendBackdrop();
        if (p.raster_request) return p.renderRaster(*p.raster_request, cancel, measured);
        if (p.image_operation) return p.renderImage(*p.image_operation, cancel, measured);
        for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
            const auto& layer = layers[layer_index];
            if ((layer.texture_frame || layer.native_frame) && !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "check-texture-effects",
                    "Ordered blur effects on a borrowed texture require a separate image operation.");
            if (!layer.gpu_color_adjustments.empty() && !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "A layer cannot combine legacy and ordered GPU effect lists.", 0,
                    static_cast<int>(layer_index));
            if (layer.gpu_color_adjustments.size() > 256)
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "Color adjustment stack exceeds the 256-effect GPU limit.", 0,
                    static_cast<int>(layer_index));
            for (const auto& adjustment : layer.gpu_color_adjustments) {
                if (!effects::isValidColorAdjustment(adjustment))
                    return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                        "Color adjustment parameters are outside the shared effect contract.", 0,
                        static_cast<int>(layer_index));
            }
            if (layer.gpu_effects.size() > 256)
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "Ordered effect stack exceeds the 256-effect GPU limit.", 0,
                    static_cast<int>(layer_index));
            bool has_gpu_effect_work = false;
            for (const auto& effect : layer.gpu_effects) {
                if (const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect)) {
                    if (!effects::isValidColorAdjustment(*adjustment))
                        return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                            "Color adjustment parameters are outside the shared effect contract.", 0,
                            static_cast<int>(layer_index));
                    has_gpu_effect_work = true;
                } else {
                    const auto& blur = std::get<GpuGaussianBlurParameters>(effect);
                    if (!std::isfinite(blur.radius_pixels) || blur.radius_pixels < 0.0 ||
                        blur.radius_pixels > 100.0)
                        return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                            "Gaussian Blur parameters are outside the shared effect contract.", 0,
                            static_cast<int>(layer_index));
                    has_gpu_effect_work = has_gpu_effect_work || std::lround(blur.radius_pixels) > 0;
                }
            }
            if (has_gpu_effect_work && usable(layer) &&
                static_cast<std::uint64_t>(layer.frame->width) * layer.frame->height * 4 >
                    maximum_effect_scratch_bytes)
                return result(OpenGlCompositionStatus::Unsupported, "check-effect-memory",
                    "Ordered GPU effects exceed the 64 MiB per-worker temporary texture limit.",
                    0, static_cast<int>(layer_index));
            if (usable(layer) && layer.transform.opacity > 0 &&
                (layer.transform.rotation_degrees != 0 || layer.source_to_canvas) && !p.precise_geometry)
                return result(OpenGlCompositionStatus::Unsupported, "check-precision",
                    "Exact rotated or affine nearest sampling requires ARB_gpu_shader_fp64 and ARB_gpu_shader5.");
            if (usable(layer) && !bounded(layer.frame->width, layer.frame->height, p.texture_limit))
                return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                    "Source exceeds the device texture limit or 256 MiB texture budget.");
            if (usable(layer) && !layer.source_to_canvas && layer.transform.rotation_degrees == 0.0 &&
                (width > 4096 || height > 4096))
                return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                    "Exact nearest sampling exceeds the 4096-entry lookup budget for an axis.");
            if (usable(layer)) {
                const auto& t = layer.transform;
                const auto& f = *layer.frame;
                const double fit = std::min(static_cast<double>(width) / f.width,
                    static_cast<double>(height) / f.height);
                const double displayed_width = f.width * fit * t.scale;
                const double displayed_height = f.height * fit * t.scale;
                const auto representable = [](double value) {
                    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
                };
                if (!representable(t.position_x * width) || !representable(t.position_y * height) ||
                    !representable(displayed_width) || !representable(displayed_height) ||
                    static_cast<float>(displayed_width) <= 0 || static_cast<float>(displayed_height) <= 0)
                    return result(OpenGlCompositionStatus::Unsupported, "check-geometry",
                        "Layer geometry cannot be represented by the GPU shader.");
            }
        }
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        if (read_output && (!p.output || p.output->width() != width || p.output->height() != height)) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            p.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!p.output->isValid()) return result(OpenGlCompositionStatus::Failed,
                "allocate-framebuffer", "Cannot allocate the output RGBA8 framebuffer.");
            p.rememberResourcePeak();
        }
        if(blending && (!p.blend_backdrop || p.blend_backdrop->width()!=width || p.blend_backdrop->height()!=height)) {
            p.blend_backdrop.reset();
            p.releaseRaster(p.blend_reserved);p.blend_reserved=0;
            const auto bytes=static_cast<std::uint64_t>(width)*height*4;
            if(!p.reserveRaster(bytes))return result(OpenGlCompositionStatus::Unsupported,"blend-backdrop-budget","Blend backdrop exceeds the shared texture budget.");
            p.blend_reserved=bytes;
            QOpenGLFramebufferObjectFormat format;format.setInternalTextureFormat(GL_RGBA8);
            p.blend_backdrop=std::make_unique<QOpenGLFramebufferObject>(width,height,format);
            if(!p.blend_backdrop->isValid()) {
                p.blend_backdrop.reset();p.releaseRaster(p.blend_reserved);p.blend_reserved=0;
                return result(OpenGlCompositionStatus::Failed,"blend-backdrop-target","Cannot allocate the blend backdrop.");
            }
            p.rememberResourcePeak();
        }
        auto* output = read_output ? p.output.get() : p.selected_output;
        const auto setup_started = Clock::now();
        if (!output || !output->bind() || !p.program->bind())
            return result(OpenGlCompositionStatus::Failed, "bind-output",
                "Cannot bind the output framebuffer or shader program.");
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_STENCIL_TEST);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_DITHER);
        gl->glDisable(GL_FRAMEBUFFER_SRGB);
        gl->glDisable(GL_MULTISAMPLE);
        gl->glEnable(GL_BLEND);
        gl->glBlendEquation(GL_FUNC_ADD);
        gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        const bool preserve_alpha = output_alpha == CompositionOutputAlpha::Preserve;
        gl->glClearColor(0, 0, 0, blending || preserve_alpha ? 0 : 1);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glBindVertexArray(p.vao);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, p.texture);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.lookup_buffers[0]);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 1, p.lookup_buffers[1]);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 2, p.lookup_buffers[2]);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, p.lookup_buffers[3]);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 4, p.lookup_buffers[4]);
        p.program->setUniformValue("source_image", 0);
        p.program->setUniformValue("backdrop_image",1);
        p.program->setUniformValue("curve_lookup", 2);
        p.program->setUniformValue("canvas_height", static_cast<float>(height));
        measured.draw_submission_nanoseconds += elapsed(setup_started);
        for (const auto& layer : layers) {
            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
            if (!usable(layer) || layer.transform.opacity == 0) continue;
            const auto& f = *layer.frame;
            if(layer.source_to_canvas && !affinePixelBounds(*layer.source_to_canvas,f.width,f.height,width,height))continue;
            struct NativeUse {
                detail::WindowsVideoInterop* adapter = nullptr;
                ~NativeUse() { if (adapter) (void)adapter->end(); }
            } native_use;
            if (layer.native_frame) {
                if (!p.video_interop) p.video_interop = std::make_unique<detail::WindowsVideoInterop>(p.context.get(), gl);
                const auto started = Clock::now();
                auto imported = p.video_interop->begin(layer.native_frame);
                if (imported.status != OpenGlCompositionStatus::Complete) return imported;
                native_use.adapter = p.video_interop.get();
                ++measured.native_video_imports;
                measured.native_video_conversion_nanoseconds += elapsed(started);
                p.rememberResourcePeak();
            }
            struct TextureUse {
                OpenGlTextureFramePtr frame; QOpenGLContext* context;
                ~TextureUse() {
                    if (frame) { std::string cause; std::int64_t code = 0; (void)frame->endUse(context, cause, code); }
                }
            } texture_use{layer.texture_frame, p.context.get()};
            if (layer.texture_frame) {
                std::string cause; std::int64_t code = 0;
                if (!layer.texture_frame->beginUse(p.context.get(), cause, code)) {
                    texture_use.frame.reset();
                    return result(OpenGlCompositionStatus::Failed, "consume-layer-texture", cause, code);
                }
            }
            p.program->setUniformValue("source_bottom_left", bool(layer.texture_frame));
            p.program->setUniformValue("color_adjustment_count",
                layer.gpu_effects.empty()
                    ? static_cast<int>(layer.gpu_color_adjustments.size()) : 0);
            const auto color_adjustment_started = layer.gpu_color_adjustments.empty()
                ? Clock::time_point{} : Clock::now();
            if (!layer.gpu_color_adjustments.empty()) {
                std::array<std::array<float, 4>, 1024> packed_adjustments{};
                std::array<std::array<float, 4>, 768> packed_log_adjustments{};
                std::array<std::array<float, 4>, 768> packed_curve_qualifiers{};
                std::vector<std::uint8_t> packed_curve_luts(
                    256 * 9 * layer.gpu_color_adjustments.size(), 0);
                for (std::size_t i = 0; i < layer.gpu_color_adjustments.size(); ++i) {
                    const auto& adjustment = layer.gpu_color_adjustments[i];
                    const auto affine = effects::colorAdjustmentAffine(adjustment);
                    packed_adjustments[4 * i] = {float(affine[0]), float(affine[1]),
                        float(affine[2]), float(affine[3])};
                    packed_adjustments[4 * i + 1] = {float(adjustment.lift_red / 100.0),
                        float(adjustment.lift_green / 100.0), float(adjustment.lift_blue / 100.0),
                        float(adjustment.saturation_percent / 100.0)};
                    packed_adjustments[4 * i + 2] = {float(100.0 / adjustment.gamma_red),
                        float(100.0 / adjustment.gamma_green), float(100.0 / adjustment.gamma_blue),
                        float(adjustment.red_gain_percent / 100.0)};
                    packed_adjustments[4 * i + 3] = {float(adjustment.green_gain_percent / 100.0),
                        float(adjustment.blue_gain_percent / 100.0), 0.0F, 0.0F};
                    packed_log_adjustments[3 * i] = {
                        float(adjustment.log_shadows_red / 100.0),
                        float(adjustment.log_shadows_green / 100.0),
                        float(adjustment.log_shadows_blue / 100.0), 0.0F};
                    packed_log_adjustments[3 * i + 1] = {
                        float(adjustment.log_midtones_red / 100.0),
                        float(adjustment.log_midtones_green / 100.0),
                        float(adjustment.log_midtones_blue / 100.0), 0.0F};
                    packed_log_adjustments[3 * i + 2] = {
                        float(adjustment.log_highlights_red / 100.0),
                        float(adjustment.log_highlights_green / 100.0),
                        float(adjustment.log_highlights_blue / 100.0), 0.0F};
                    if (adjustment.video_curves) {
                        const auto tables = effects::videoCurvesLuts(*adjustment.video_curves);
                        for (std::size_t curve = 0; curve < tables.size(); ++curve)
                            std::copy(tables[curve].begin(), tables[curve].end(),
                                packed_curve_luts.begin() + static_cast<std::ptrdiff_t>(
                                    (i * 9 + curve) * 256));
                        const auto& q = adjustment.video_curves->qualifier;
                        packed_curve_qualifiers[3 * i] = {float(q.hue_min), float(q.hue_max),
                            float(q.saturation_min), float(q.saturation_max)};
                        packed_curve_qualifiers[3 * i + 1] = {float(q.luminance_min),
                            float(q.luminance_max), float(q.hue_softness * 3.6),
                            float(q.saturation_softness)};
                        packed_curve_qualifiers[3 * i + 2] = {
                            float(q.luminance_softness), q.enabled ? 1.0F : 0.0F, 1.0F, 0.0F};
                    }
                }
                const auto byte_count = static_cast<GLsizeiptr>(
                    4 * layer.gpu_color_adjustments.size() * sizeof(packed_adjustments[0]));
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[2]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, byte_count,
                    packed_adjustments.data());
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[3]);
                const auto log_byte_count = static_cast<GLsizeiptr>(
                    3 * layer.gpu_color_adjustments.size() * sizeof(packed_log_adjustments[0]));
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, log_byte_count,
                    packed_log_adjustments.data());
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[4]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>(3 * layer.gpu_color_adjustments.size() * sizeof(packed_curve_qualifiers[0])),
                    packed_curve_qualifiers.data());
                gl->glBindBufferBase(GL_UNIFORM_BUFFER, 4, p.lookup_buffers[4]);
                gl->glActiveTexture(GL_TEXTURE2);
                gl->glBindTexture(GL_TEXTURE_2D, p.curve_lut_texture);
                gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256,
                    static_cast<GLsizei>(9 * layer.gpu_color_adjustments.size()), 0,
                    GL_RED, GL_UNSIGNED_BYTE, packed_curve_luts.data());
                p.curve_lut_bytes = packed_curve_luts.size();
                gl->glActiveTexture(GL_TEXTURE0);
                measured.uploaded_bytes += static_cast<std::uint64_t>(byte_count);
                measured.color_adjustment_count += layer.gpu_color_adjustments.size();
            }
            const auto upload_started = Clock::now();
            if (!layer.texture_frame && !layer.native_frame) {
            gl->glBindTexture(GL_TEXTURE_2D, p.texture);
            if (p.source_width != f.width || p.source_height != f.height) {
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f.width, f.height, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
                p.source_width = f.width;
                p.source_height = f.height;
                p.rememberResourcePeak();
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            if (f.stride % 4 == 0) {
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, f.stride / 4);
                gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, GL_RGBA,
                    GL_UNSIGNED_BYTE, f.rgba_pixels.data());
            } else {
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                for (int row = 0; row < f.height; ++row)
                    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, f.width, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, f.rgba_pixels.data() + static_cast<std::size_t>(row) * f.stride);
            }
            gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            measured.upload_nanoseconds += elapsed(upload_started);
            measured.uploaded_bytes += static_cast<std::uint64_t>(f.width) * f.height * 4;
            ++measured.uploaded_layers;
            }
            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
            GLuint layer_texture = layer.native_frame ? p.video_interop->texture() :
                layer.texture_frame ? layer.texture_frame->texture() : p.texture;
            const bool has_gpu_effect_work = std::any_of(layer.gpu_effects.begin(),
                layer.gpu_effects.end(), [](const auto& effect) {
                    if (std::holds_alternative<effects::ColorAdjustmentParameters>(effect))
                        return true;
                    return std::lround(std::get<GpuGaussianBlurParameters>(effect).radius_pixels) > 0;
                });
            if (has_gpu_effect_work) {
                const int layer_index = static_cast<int>(&layer - layers.data());
                if (!p.effect_scratch || p.effect_scratch->width() != f.width ||
                    p.effect_scratch->height() != f.height) {
                    p.effect_scratch.reset();
                    QOpenGLFramebufferObjectFormat format;
                    format.setInternalTextureFormat(GL_RGBA8);
                    p.effect_scratch = std::make_unique<QOpenGLFramebufferObject>(
                        f.width, f.height, format);
                    if (!p.effect_scratch->isValid())
                        return result(OpenGlCompositionStatus::Failed, "allocate-effect-target",
                            "Cannot allocate the bounded RGBA8 effect target.", 0, layer_index);
                    p.rememberResourcePeak();
                }
                gl->glBindFramebuffer(GL_FRAMEBUFFER, p.effect_source_fbo);
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, p.texture, 0);
                if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    return result(OpenGlCompositionStatus::Failed, "attach-effect-source",
                        "Cannot attach the uploaded layer texture as an effect target.",
                        gl->glGetError(), layer_index);
                if (!p.effect_program->bind())
                    return result(OpenGlCompositionStatus::Failed, "bind-effect-shader",
                        "Cannot bind the ordered effect shader.", 0, layer_index);
                gl->glDisable(GL_BLEND);
                gl->glDisable(GL_DITHER);
                gl->glDisable(GL_FRAMEBUFFER_SRGB);
                gl->glViewport(0, 0, f.width, f.height);
                p.effect_program->setUniformValue("source_image", 0);
                const auto render_effect_pass = [&](GLuint source_texture,
                                                    GLuint destination_texture,
                                                    GLuint destination_fbo) {
                    if (cancelled()) return false;
                    gl->glBindFramebuffer(GL_FRAMEBUFFER, destination_fbo);
                    if (destination_fbo == p.effect_source_fbo) {
                        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, destination_texture, 0);
                        if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                            return false;
                    }
                    gl->glActiveTexture(GL_TEXTURE0);
                    gl->glBindTexture(GL_TEXTURE_2D, source_texture);
                    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
                    return gl->glGetError() == GL_NO_ERROR;
                };
                GLuint current_texture = p.texture;
                for (const auto& effect : layer.gpu_effects) {
                    const auto effect_started = Clock::now();
                    if (const auto* adjustment =
                            std::get_if<effects::ColorAdjustmentParameters>(&effect)) {
                        const auto affine = effects::colorAdjustmentAffine(*adjustment);
                        p.effect_program->setUniformValue("effect_kind", 0);
                        p.effect_program->setUniformValue("color_affine", QVector4D(
                            float(affine[0]), float(affine[1]), float(affine[2]),
                            float(affine[3])));
                        p.effect_program->setUniformValue("lift_saturation", QVector4D(
                            float(adjustment->lift_red / 100.0),
                            float(adjustment->lift_green / 100.0),
                            float(adjustment->lift_blue / 100.0),
                            float(adjustment->saturation_percent / 100.0)));
                        p.effect_program->setUniformValue("gamma_gain_red", QVector4D(
                            float(100.0 / adjustment->gamma_red),
                            float(100.0 / adjustment->gamma_green),
                            float(100.0 / adjustment->gamma_blue),
                            float(adjustment->red_gain_percent / 100.0)));
                        p.effect_program->setUniformValue("gain_green_blue", QVector4D(
                            float(adjustment->green_gain_percent / 100.0),
                            float(adjustment->blue_gain_percent / 100.0), 0.0F, 0.0F));
                        p.effect_program->setUniformValue("log_shadows", QVector3D(
                            float(adjustment->log_shadows_red / 100.0),
                            float(adjustment->log_shadows_green / 100.0),
                            float(adjustment->log_shadows_blue / 100.0)));
                        p.effect_program->setUniformValue("log_midtones", QVector3D(
                            float(adjustment->log_midtones_red / 100.0),
                            float(adjustment->log_midtones_green / 100.0),
                            float(adjustment->log_midtones_blue / 100.0)));
                        p.effect_program->setUniformValue("log_highlights", QVector3D(
                            float(adjustment->log_highlights_red / 100.0),
                            float(adjustment->log_highlights_green / 100.0),
                            float(adjustment->log_highlights_blue / 100.0)));
                        p.effect_program->setUniformValue("premultiply_alpha", false);
                        p.effect_program->setUniformValue("unpremultiply_alpha", false);
                        p.effect_program->setUniformValue("horizontal_pass", true);
                        p.effect_program->setUniformValue("blur_radius", 0);
                        const GLuint destination = current_texture == p.texture
                            ? p.effect_scratch->texture() : p.texture;
                        const GLuint framebuffer = destination == p.texture
                            ? p.effect_source_fbo : p.effect_scratch->handle();
                        if (!render_effect_pass(current_texture, destination, framebuffer)) {
                            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                            return result(OpenGlCompositionStatus::Failed, "apply-color-adjustment",
                                "OpenGL failed to process an ordered Color Adjustment.",
                                gl->glGetError(), layer_index);
                        }
                        current_texture = destination;
                        ++measured.color_adjustment_count;
                        measured.color_adjustment_submission_nanoseconds += elapsed(effect_started);
                    } else {
                        const auto& blur = std::get<GpuGaussianBlurParameters>(effect);
                        const int radius = static_cast<int>(std::lround(blur.radius_pixels));
                        if (radius <= 0) continue;
                        p.effect_program->setUniformValue("effect_kind", 1);
                        p.effect_program->setUniformValue("blur_radius", radius);
                        p.effect_program->setUniformValue("premultiply_alpha", true);
                        p.effect_program->setUniformValue("unpremultiply_alpha", false);
                        p.effect_program->setUniformValue("horizontal_pass", true);
                        GLuint destination = current_texture == p.texture
                            ? p.effect_scratch->texture() : p.texture;
                        GLuint framebuffer = destination == p.texture
                            ? p.effect_source_fbo : p.effect_scratch->handle();
                        if (!render_effect_pass(current_texture, destination, framebuffer)) {
                            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                            return result(OpenGlCompositionStatus::Failed, "premultiply-blur-input",
                                "OpenGL failed to premultiply Gaussian Blur input.",
                                gl->glGetError(), layer_index);
                        }
                        current_texture = destination;
                        p.effect_program->setUniformValue("premultiply_alpha", false);
                        for (int pass = 0; pass < 3; ++pass) {
                            for (const bool horizontal : {true, false}) {
                                p.effect_program->setUniformValue("horizontal_pass", horizontal);
                                destination = current_texture == p.texture
                                    ? p.effect_scratch->texture() : p.texture;
                                framebuffer = destination == p.texture
                                    ? p.effect_source_fbo : p.effect_scratch->handle();
                                if (!render_effect_pass(current_texture, destination, framebuffer)) {
                                    if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                                    return result(OpenGlCompositionStatus::Failed,
                                        horizontal ? "blur-horizontal" : "blur-vertical",
                                        "OpenGL failed during a Gaussian Blur pass.",
                                        gl->glGetError(), layer_index);
                                }
                                current_texture = destination;
                            }
                        }
                        p.effect_program->setUniformValue("unpremultiply_alpha", true);
                        destination = current_texture == p.texture
                            ? p.effect_scratch->texture() : p.texture;
                        framebuffer = destination == p.texture
                            ? p.effect_source_fbo : p.effect_scratch->handle();
                        if (!render_effect_pass(current_texture, destination, framebuffer)) {
                            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                            return result(OpenGlCompositionStatus::Failed, "unpremultiply-blur-output",
                                "OpenGL failed to restore straight alpha after Gaussian Blur.",
                                gl->glGetError(), layer_index);
                        }
                        current_texture = destination;
                        p.effect_program->setUniformValue("unpremultiply_alpha", false);
                        ++measured.gaussian_blur_count;
                        measured.gaussian_blur_submission_nanoseconds += elapsed(effect_started);
                    }
                }
                layer_texture = current_texture;
                if (!output->bind() || !p.program->bind())
                    return result(OpenGlCompositionStatus::Failed, "restore-composition-target",
                        "Cannot restore the composition framebuffer after effects.", 0, layer_index);
                gl->glViewport(0, 0, width, height);
                gl->glEnable(GL_BLEND);
                gl->glBlendEquation(GL_FUNC_ADD);
                gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
            const auto draw_started = Clock::now();
            std::uint64_t geometry_upload_ns = 0;
            const auto& t = layer.transform;
            const double fit = std::min(static_cast<double>(width) / f.width,
                static_cast<double>(height) / f.height);
            const double displayed_width = f.width * fit * t.scale;
            const double displayed_height = f.height * fit * t.scale;
            const double radians = t.rotation_degrees * std::numbers::pi / 180.0;
            const bool axis = t.rotation_degrees == 0.0 && !layer.source_to_canvas;
            p.program->setUniformValue("axis_aligned", axis);
            if (axis) {
                // Double-precision geometry matches the CPU's nearest sampling
                // exactly at texel boundaries. Only O(width+height) indices are
                // transferred, never rasterized pixels or another source texture.
                const int y_offset = (width + 3) / 4 * 4;
                p.source_lookup.resize(static_cast<std::size_t>(y_offset + (height + 3) / 4 * 4));
                const auto map = [&](int count, double center, double displayed, int source, int offset) {
                    for (int i = 0; i < count; ++i) {
                        const double d = i + .5 - center;
                        p.source_lookup[static_cast<std::size_t>(offset + i)] = std::abs(d) > displayed * .5
                            ? -1 : std::clamp(static_cast<int>(std::floor((d / displayed + .5) * source)), 0, source - 1);
                    }
                };
                map(width, t.position_x * width, displayed_width, f.width, 0);
                map(height, t.position_y * height, displayed_height, f.height, y_offset);
                const auto geometry_upload_started = Clock::now();
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[0]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>(y_offset * sizeof(GLint)), p.source_lookup.data());
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[1]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>((p.source_lookup.size() - y_offset) * sizeof(GLint)),
                    p.source_lookup.data() + y_offset);
                geometry_upload_ns = elapsed(geometry_upload_started);
                measured.upload_nanoseconds += geometry_upload_ns;
                measured.uploaded_bytes += p.source_lookup.size() * sizeof(GLint);
            }
            const auto set_geometry = [&](const char* name, double x, double y) {
                if (p.precise_geometry) {
                    const GLdouble values[]{x, y};
                    p.uniform2dv(p.program->uniformLocation(name), 1, values);
                } else {
                    p.program->setUniformValue(name, QVector2D(static_cast<float>(x), static_cast<float>(y)));
                }
            };
            p.program->setUniformValue("use_affine",layer.source_to_canvas.has_value());
            if(layer.source_to_canvas) {
                const auto inverse=requireInverseAffine(*layer.source_to_canvas);
                set_geometry("affine_x",inverse.a,inverse.b);set_geometry("affine_y",inverse.c,inverse.d);set_geometry("affine_translation",inverse.tx,inverse.ty);
            }
            set_geometry("center", t.position_x * width, t.position_y * height);
            set_geometry("displayed_size", displayed_width, displayed_height);
            set_geometry("rotation_cs", std::cos(radians), std::sin(radians));
            p.program->setUniformValue("opacity", static_cast<float>(t.opacity));
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, layer_texture);
            p.program->setUniformValue("blend_mode",static_cast<int>(layer.blend_mode));
            if(layer.blend_mode!=BlendMode::Normal) {
                // Snapshot the completed backdrop on GPU. Never sample a draw attachment.
                gl->glActiveTexture(GL_TEXTURE1);
                gl->glBindTexture(GL_TEXTURE_2D,p.blend_backdrop->texture());
                gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
                gl->glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
                gl->glActiveTexture(GL_TEXTURE0);gl->glDisable(GL_BLEND);
            } else gl->glEnable(GL_BLEND);
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            if (native_use.adapter) {
                auto released = native_use.adapter->end(); native_use.adapter = nullptr;
                if (released.status != OpenGlCompositionStatus::Complete) return released;
            }
            if (texture_use.frame) {
                std::string cause; std::int64_t code = 0;
                if (!texture_use.frame->endUse(p.context.get(), cause, code))
                    return result(OpenGlCompositionStatus::Failed, "protect-layer-texture", cause, code);
                texture_use.frame.reset();
            }
            if (!layer.gpu_color_adjustments.empty())
                measured.color_adjustment_submission_nanoseconds +=
                    elapsed(color_adjustment_started);
            measured.draw_submission_nanoseconds += elapsed(draw_started) - geometry_upload_ns;
            if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
                return result(OpenGlCompositionStatus::Failed, "draw-layer",
                    "OpenGL reported an upload or drawing error.", code);
        }
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        if(blending && !preserve_alpha) {
            // Accumulated RGB is already premultiplied over black; publish opaque RGBA.
            gl->glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);
            gl->glClearColor(0,0,0,1);gl->glClear(GL_COLOR_BUFFER_BIT);
            gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            if(const auto code=gl->glGetError();code!=GL_NO_ERROR)
                return result(OpenGlCompositionStatus::Failed,"flatten-blend-output","Cannot finalize the opaque composition.",code);
        }
        if (!read_output) return result(OpenGlCompositionStatus::Complete);
        const auto read_started = Clock::now();
        media::RgbaFrame frame{width, height, width * 4, {}};
        frame.rgba_pixels.resize(static_cast<std::size_t>(frame.stride) * height);
        gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba_pixels.data());
        measured.readback_nanoseconds = elapsed(read_started);
        measured.readback_bytes = frame.rgba_pixels.size();
        if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "readback",
                "OpenGL reported a framebuffer readback error.", code);
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        for (int y = 0; y < height / 2; ++y) {
            auto first = frame.rgba_pixels.begin() + static_cast<std::size_t>(y) * frame.stride;
            auto last = frame.rgba_pixels.begin() + static_cast<std::size_t>(height - 1 - y) * frame.stride;
            std::swap_ranges(first, first + frame.stride, last);
        }
        if (preserve_alpha && !unpremultiplyRgba(frame, cancel))
            return result(OpenGlCompositionStatus::Cancelled);
        measured.readback_nanoseconds = elapsed(read_started);
        measured.readback_bytes = frame.rgba_pixels.size();
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        return {OpenGlCompositionStatus::Complete, std::move(frame), {}, {}, 0};
    } catch (const std::exception& error) {
        return result(OpenGlCompositionStatus::Failed, "compose", error.what());
    }
}

} // namespace creative_suite::composition
