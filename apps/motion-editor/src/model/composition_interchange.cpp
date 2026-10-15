#include "composition_interchange.h"
#include <algorithm>
#include <set>
#include <sstream>
namespace motion::model {
namespace x = creative_suite::composition_exchange;
namespace {
std::string mediaName(const std::filesystem::path &path) {
    const auto name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}
} // namespace
x::Document exportComposition(const MotionProjectData &project) {
    x::Document d;
    d.width = project.composition.canvas_size.width;
    d.height = project.composition.canvas_size.height;
    d.fps_numerator = project.composition.frame_rate.numerator;
    d.fps_denominator = project.composition.frame_rate.denominator;
    d.name = project.composition.name;
    d.duration = std::max<std::int64_t>(1, project.composition.duration);
    std::vector<x::Issue> issues;
    d.masks=project.masks;
    for (const auto &layer : project.layers) {
        constexpr auto limit = std::int64_t(1) << 50;
        if (layer.timeline_start_frame < 0 || layer.timeline_start_frame >= limit ||
            layer.duration_frames <= 0 || layer.duration_frames >= limit) {
            issues.push_back({layer.id, "Layer activity is outside the interchange time range."});
            continue;
        }
        x::Layer l;
        l.id = layer.id;
        l.presentation = layer.presentation.value_or(x::Presentation{});
        if (!layer.presentation)
            l.presentation.profile = x::Profile::MotionLayer;
        l.presentation.name = layer.name;
        l.presentation.visible = layer.visible;
        l.presentation.start = layer.timeline_start_frame;
        l.presentation.duration = layer.duration_frames;
        l.presentation.source_start = layer.source_start_frame;
        l.transform = layer.transform;
        l.curves = layer.keyframes;
        l.source_mask=layer.source_mask;l.transform_mask=layer.transform_mask;
        l.source = layer.source_path;
        l.source_rate = layer.source_frame_rate;
        l.source_count = layer.source_frame_count;
        if (layer.linked_image)
            issues.push_back({layer.id, "Linked image documents are not supported by independent "
                                        "composition interchange."});
        if (layer.kind == LayerKind::Video)
            l.kind = x::Kind::Video;
        else if (layer.kind == LayerKind::Image)
            l.kind = x::Kind::Image;
        else if (layer.kind == LayerKind::Text) {
            l.kind = x::Kind::Text;
            auto t = std::get<TextLayerContent>(layer.content);
            l.text = {t.text, t.font_family, t.font_size_pixels, int(t.alignment), t.color};
            l.presentation.text_width = t.box_width;
            l.presentation.text_height = t.box_height;
        } else {
            l.kind = x::Kind::Background;
            auto shape = std::get<ShapeLayerContent>(layer.content);
            l.color = shape.fill_color;
            if (shape.shape != ShapeKind::Rectangle || shape.stroke_width_pixels != 0 ||
                shape.width != d.width || shape.height != d.height)
                issues.push_back(
                    {layer.id,
                     "Only full-canvas rectangles without a stroke can become Background nodes."});
        }
        for(const auto& effect:layer.effects) {
            if(const auto* c=std::get_if<ColorAdjustmentEffect>(&effect))
                l.effects.emplace_back(x::ColorEffect{{c->brightness,c->contrast_percent,c->saturation_percent},c->enabled,c->mask});
            else {
                const auto& blur=std::get<GaussianBlurEffect>(effect);
                l.effects.emplace_back(x::BlurEffect{blur.radius_pixels,blur.enabled,blur.mask});
            }
        }
        d.duration = std::max(d.duration, layer.timeline_start_frame + layer.duration_frames);
        d.layers.push_back(std::move(l));
    }
    auto invalid = x::validate(d);
    issues.insert(issues.end(), invalid.begin(), invalid.end());
    if (!issues.empty()) {
        std::ostringstream m;
        for (auto i : issues)
            m << "Layer " << i.id << ": " << i.reason << '\n';
        throw x::Unsupported(m.str());
    }
    return d;
}
MotionProjectData importComposition(const x::Document &d) {
    auto issues = x::validate(d);
    if (!issues.empty())
        throw x::Error(issues.front().reason);
    if (!isSupportedFrameRate({d.fps_numerator, d.fps_denominator}))
        throw x::Unsupported("The composition frame rate is not supported by Motion Studio.");
    MotionProjectData project;
    project.composition = {
        {d.width, d.height}, {d.fps_numerator, d.fps_denominator}, d.name, d.duration};
    project.masks=d.masks;
    std::set<std::filesystem::path> media;
    for (const auto &l : d.layers) {
        CompositionLayer layer{};
        layer.id = l.id;
        layer.name = l.presentation.name;
        layer.presentation = l.presentation;
        layer.timeline_start_frame = l.presentation.start;
        layer.duration_frames = l.presentation.duration > 0 ? l.presentation.duration
                                                            : d.duration - l.presentation.start;
        layer.visible = l.presentation.visible;
        layer.source_path = l.source;
        layer.source_frame_count = l.source_count;
        layer.source_frame_rate = l.source_rate;
        layer.source_start_frame = l.presentation.source_start;
        layer.source_duration_frames = l.source_count;
        layer.maximum_timeline_duration_frames = std::int64_t(1) << 49;
        layer.transform = l.transform;
        layer.keyframes = l.curves;
        layer.source_mask=l.source_mask;layer.transform_mask=l.transform_mask;
        if (l.kind == x::Kind::Video)
            layer.kind = LayerKind::Video;
        else if (l.kind == x::Kind::Image)
            layer.kind = LayerKind::Image;
        else if (l.kind == x::Kind::Background) {
            layer.kind = LayerKind::Shape;
            layer.content = ShapeLayerContent{ShapeKind::Rectangle, d.width, d.height, l.color,
                                              {0, 0, 0, 0},         0};
        } else {
            layer.kind = LayerKind::Text;
            layer.content = TextLayerContent{l.text.content,
                                             l.text.font,
                                             l.text.size,
                                             l.text.color,
                                             TextAlignment(l.text.alignment),
                                             std::max(1, l.presentation.text_width),
                                             std::max(1, l.presentation.text_height)};
        }
        for(const auto& effect:l.effects) {
            if(const auto* c=std::get_if<x::ColorEffect>(&effect))
                layer.effects.emplace_back(ColorAdjustmentEffect{c->enabled,c->parameters.brightness,c->parameters.contrast_percent,c->parameters.saturation_percent,c->mask});
            else {
                const auto& blur=std::get<x::BlurEffect>(effect);
                layer.effects.emplace_back(GaussianBlurEffect{blur.enabled,blur.radius_pixels,blur.mask});
            }
        }
        if (!l.source.empty() && media.insert(l.source).second)
            project.media.push_back({l.source,
                                     l.kind == x::Kind::Video
                                         ? creative_suite::media::MediaKind::Video
                                         : creative_suite::media::MediaKind::Image,
                                     mediaName(l.source)});
        project.layers.push_back(std::move(layer));
    }
    CompositionDocument checked(d.width, d.height, {d.fps_numerator, d.fps_denominator},
                                project.layers, project.masks);
    return project;
}
} // namespace motion::model
