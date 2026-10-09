#include "project_file.h"
#include "project_file_detail.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <algorithm>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "../media/media_library.h"
#include "../timeline/timeline_zoom.h"

namespace project {
namespace {

QString pathToQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(value.data()),
        static_cast<qsizetype>(value.size()));
}


std::filesystem::path pathFromUtf8(const QString& value) {
    const auto bytes = value.toUtf8();
    const auto* begin = reinterpret_cast<const char8_t*>(bytes.constData());
    const auto* end = begin + bytes.size();
    return std::filesystem::path(std::u8string(begin, end));
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;

    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}




[[noreturn]] void throwJson(ProjectErrorCode code,
                             const std::filesystem::path& project_path,
                             const char* message);

std::int64_t requiredInteger(
    const QJsonObject& object,
    const char* key,
    const std::filesystem::path& project_path);

std::filesystem::path resolvedPath(const std::filesystem::path& project_path,
                                   const QString& stored_path);



media::MediaKind parseMediaKind(
    const QJsonObject& object,
    const std::filesystem::path& project_path,
    int version) {
    if (version < media_kind_format_version || !object.contains("kind")) {
        return media::MediaKind::Video;
    }
    const auto value = object.value("kind");
    if (!value.isString()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid media kind.");
    }
    if (value.toString() == QLatin1String("video")) return media::MediaKind::Video;
    if (value.toString() == QLatin1String("image")) return media::MediaKind::Image;
    if (version >= audio_tracks_format_version &&
        value.toString() == QLatin1String("audio")) return media::MediaKind::Audio;
    throwJson(ProjectErrorCode::InvalidValue, project_path,
              "Project JSON contains an unsupported media kind.");
}

timeline::TransitionKind parseTransitionKind(
    const QJsonObject& object,
    const std::filesystem::path& project_path,
    int version) {
    const auto value = object.value("kind");
    if (!value.isString()) {
        throwJson(ProjectErrorCode::MissingField, project_path,
                  "A transition is missing its kind.");
    }
    if (value.toString() == QLatin1String("cross_dissolve")) {
        return timeline::TransitionKind::CrossDissolve;
    }
    if (value.toString() == QLatin1String("fade_to_black")) {
        return timeline::TransitionKind::FadeToBlack;
    }
    if (version >= audio_crossfade_format_version &&
        value.toString() == QLatin1String("audio_crossfade")) {
        return timeline::TransitionKind::AudioCrossfade;
    }
    throwJson(ProjectErrorCode::InvalidValue, project_path,
              "Project JSON contains an unsupported transition kind.");
}

fusion::nodes::NodeType parseNodeType(const QJsonValue& value,
                                      const std::filesystem::path& project_path) {
    if (!value.isString())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Fusion node type.");
    const auto name = value.toString();
    if (name == QLatin1String("input")) return fusion::nodes::NodeType::Input;
    if (name == QLatin1String("transform")) return fusion::nodes::NodeType::Transform;
    if (name == QLatin1String("color")) return fusion::nodes::NodeType::Color;
    if (name == QLatin1String("merge")) return fusion::nodes::NodeType::Merge;
    if (name == QLatin1String("output")) return fusion::nodes::NodeType::Output;
    if (name == QLatin1String("effect")) return fusion::nodes::NodeType::Effect;
    throwJson(ProjectErrorCode::InvalidValue, project_path,
              "Project JSON contains an unsupported Fusion node type.");
}

fusion::nodes::NodeGraph parseNodeGraph(const QJsonValue& value,
                                        const std::filesystem::path& project_path,
                                        int version) {
    if (!value.isObject())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Fusion node graph.");
    const auto object = value.toObject();
    const auto nodes_value = object.value("nodes");
    const auto edges_value = object.value("connections");
    const auto next_id = object.value("next_id");
    if (!nodes_value.isArray() || !edges_value.isArray() ||
        !next_id.isDouble() || next_id.toInteger() <= 0)
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an incomplete Fusion node graph.");
    fusion::nodes::NodeGraph graph;
    graph.next_id = static_cast<fusion::nodes::NodeId>(next_id.toInteger());
    for (const auto& node_value : nodes_value.toArray()) {
        if (!node_value.isObject())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Fusion node.");
        const auto node_object = node_value.toObject();
        const auto id = node_object.value("id");
        if (!id.isDouble() || id.toInteger() <= 0)
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Fusion node identifier.");
        fusion::nodes::Node node;
        node.id = static_cast<fusion::nodes::NodeId>(id.toInteger());
        node.type = parseNodeType(node_object.value("type"), project_path);
        if (node.type == fusion::nodes::NodeType::Effect) {
            if (version < effect_node_format_version)
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Effect nodes require project format version 21 or newer.");
            const auto effect_value = node_object.value("effect");
            if (!effect_value.isObject())
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A Fusion effect node is missing its effect settings.");
            const auto effect_object = effect_value.toObject();
            const auto effect_id = effect_object.value("id");
            const auto parameters = effect_object.value("parameters");
            if (!effect_id.isString() ||
                !effect_object.value("enabled").isBool() ||
                !parameters.isArray())
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A Fusion effect node contains incomplete effect settings.");
            const auto effect_id_bytes = effect_id.toString().toUtf8();
            node.effect.id.assign(effect_id_bytes.constData(),
                                  static_cast<std::size_t>(effect_id_bytes.size()));
            node.effect.enabled = effect_object.value("enabled").toBool();
            for (const auto& parameter_value : parameters.toArray()) {
                if (!parameter_value.isObject() ||
                    !parameter_value.toObject().value("id").isString() ||
                    !parameter_value.toObject().value("value").isDouble())
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Fusion effect node contains an invalid parameter.");
                const auto parameter = parameter_value.toObject();
                const auto parameter_id = parameter.value("id").toString().toUtf8();
                node.effect.parameters.push_back({
                    std::string(parameter_id.constData(),
                                static_cast<std::size_t>(parameter_id.size())),
                    parameter.value("value").toDouble()});
            }
            if (!creative_suite::effects::isValid(node.effect))
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A Fusion effect node contains an unsupported effect or invalid parameter.");
        }
        node.x = node_object.value("x").toDouble(std::numeric_limits<double>::quiet_NaN());
        node.y = node_object.value("y").toDouble(std::numeric_limits<double>::quiet_NaN());
        if (node_object.contains("source")) {
            if (!node_object.value("source").isString())
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Project JSON contains an invalid Fusion input path.");
            node.source_path = resolvedPath(project_path, node_object.value("source").toString());
        }
        node.source_frame_rate = node_object.value("source_frame_rate").toDouble(30.0);
        node.source_frame_count = node_object.value("source_frame_count").toInteger(0);
        node.source_is_still = node_object.value("source_is_still").toBool(false);
        const auto transform = node_object.value("transform").toObject();
        node.transform.position_x = transform.value("x").toDouble(0.5);
        node.transform.position_y = transform.value("y").toDouble(0.5);
        node.transform.scale = transform.value("scale").toDouble(1.0);
        node.transform.rotation_degrees = transform.value("rotation").toDouble(0.0);
        node.transform.opacity = transform.value("opacity").toDouble(1.0);
        const auto color = node_object.value("color").toObject();
        node.color.brightness = color.value("brightness").toDouble(0.0);
        node.color.contrast_percent = color.value("contrast").toDouble(100.0);
        node.color.saturation_percent = color.value("saturation").toDouble(100.0);
        if (version >= node_animation_format_version) {
            const auto parse_scalar_keyframes = [&](const QJsonValue& keys_value,
                                                    auto& output) {
                if (keys_value.isUndefined()) return;
                if (!keys_value.isArray())
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Fusion node contains an invalid animation curve.");
                for (const auto& key_value : keys_value.toArray()) {
                    if (!key_value.isObject())
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "A Fusion node contains an invalid animation keyframe.");
                    const auto key = key_value.toObject();
                    const auto frame = requiredInteger(key, "frame", project_path);
                    const auto numeric = key.value("value");
                    if (frame < 0 || !numeric.isDouble() ||
                        !std::isfinite(numeric.toDouble()))
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "A Fusion node contains an invalid animation keyframe value.");
                    output.push_back({frame, numeric.toDouble()});
                }
            };
            const auto transform_keys_value =
                node_object.value("transform_keyframes");
            if (!transform_keys_value.isUndefined()) {
                if (!transform_keys_value.isObject() ||
                    node.type != fusion::nodes::NodeType::Transform)
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Fusion node contains invalid Transform animation data.");
                const auto transform_keys = transform_keys_value.toObject();
                parse_scalar_keyframes(transform_keys.value("position_x"),
                    node.transform_keyframes.position_x);
                parse_scalar_keyframes(transform_keys.value("position_y"),
                    node.transform_keyframes.position_y);
                parse_scalar_keyframes(transform_keys.value("scale"),
                    node.transform_keyframes.scale);
                parse_scalar_keyframes(transform_keys.value("rotation"),
                    node.transform_keyframes.rotation);
                parse_scalar_keyframes(transform_keys.value("opacity"),
                    node.transform_keyframes.opacity);
                if (!creative_suite::animation::validTransformKeyframes(
                        node.transform_keyframes))
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Fusion Transform node contains invalid animation curves.");
            }
            const auto effect_keys_value =
                node_object.value("effect_parameter_keyframes");
            if (!effect_keys_value.isUndefined()) {
                if (!effect_keys_value.isArray() ||
                    node.type != fusion::nodes::NodeType::Effect)
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Fusion node contains invalid effect animation data.");
                for (const auto& parameter_value : effect_keys_value.toArray()) {
                    if (!parameter_value.isObject() ||
                        !parameter_value.toObject().value("id").isString())
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "A Fusion effect contains an invalid animated parameter.");
                    const auto parameter_object = parameter_value.toObject();
                    const auto parameter_id_bytes =
                        parameter_object.value("id").toString().toUtf8();
                    fusion::nodes::EffectParameterKeyframes animated;
                    animated.parameter_id.assign(parameter_id_bytes.constData(),
                        static_cast<std::size_t>(parameter_id_bytes.size()));
                    parse_scalar_keyframes(parameter_object.value("keyframes"),
                        animated.keyframes);
                    if (animated.keyframes.empty())
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "A Fusion effect animation curve cannot be empty.");
                    node.effect_parameter_keyframes.push_back(std::move(animated));
                }
            }
        }
        graph.nodes.push_back(std::move(node));
    }
    for (const auto& edge_value : edges_value.toArray()) {
        if (!edge_value.isObject())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Fusion connection.");
        const auto edge = edge_value.toObject();
        const auto from = edge.value("from"), to = edge.value("to"), input = edge.value("input");
        if (!from.isDouble() || !to.isDouble() || !input.isDouble() ||
            from.toInteger() <= 0 || to.toInteger() <= 0 ||
            input.toInteger() < 0 || input.toInteger() > 1)
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Fusion connection endpoint.");
        graph.connections.push_back({static_cast<fusion::nodes::NodeId>(from.toInteger()),
            static_cast<fusion::nodes::NodeId>(to.toInteger()),
            static_cast<std::uint8_t>(input.toInteger())});
    }
    if (!fusion::nodes::validate(graph))
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid or cyclic Fusion graph.");
    return graph;
}



std::filesystem::path resolvedPath(const std::filesystem::path& project_path,
                                   const QString& stored_path) {
    const auto stored = pathFromUtf8(stored_path);
    if (stored.is_absolute()) return normalizedPath(stored);
    return normalizedPath(normalizedPath(project_path).parent_path() / stored);
}

[[noreturn]] void throwJson(ProjectErrorCode code,
                             const std::filesystem::path& project_path,
                             const char* message) {
    throw ProjectError(code, message, std::nullopt, project_path);
}

void migrateLegacyCrossDissolves(
    ProjectDocument& document,
    const std::filesystem::path& project_path) {
    for (auto& track : document.timeline_tracks) {
        std::vector<std::size_t> ordered_transitions(track.transitions.size());
        for (std::size_t index = 0; index < ordered_transitions.size(); ++index) {
            ordered_transitions[index] = index;
        }
        std::sort(ordered_transitions.begin(), ordered_transitions.end(),
            [&track](std::size_t left, std::size_t right) {
                const auto& left_transition = track.transitions[left];
                const auto& right_transition = track.transitions[right];
                const auto left_start = left_transition.from_clip_index < track.clips.size()
                    ? track.clips[left_transition.from_clip_index].timeline_start_frame
                    : std::numeric_limits<std::int64_t>::max();
                const auto right_start = right_transition.from_clip_index < track.clips.size()
                    ? track.clips[right_transition.from_clip_index].timeline_start_frame
                    : std::numeric_limits<std::int64_t>::max();
                return left_start < right_start;
            });

        std::vector<std::pair<std::size_t, std::size_t>> seen;
        for (const auto transition_index : ordered_transitions) {
            const auto& transition = track.transitions[transition_index];
            if (transition.from_clip_index >= track.clips.size() ||
                transition.to_clip_index >= track.clips.size() ||
                transition.from_clip_index + 1 != transition.to_clip_index) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "A legacy transition does not connect consecutive clips.");
            }
            const auto pair = std::make_pair(
                transition.from_clip_index, transition.to_clip_index);
            if (std::find(seen.begin(), seen.end(), pair) != seen.end()) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "A legacy project contains duplicate transitions.");
            }
            seen.push_back(pair);
            const auto& from = track.clips[transition.from_clip_index];
            const auto& to = track.clips[transition.to_clip_index];
            if (from.duration_frames <= 0 || to.duration_frames <= 0 ||
                from.timeline_start_frame < 0 || to.timeline_start_frame < 0 ||
                from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                    from.duration_frames ||
                transition.duration_frames <= 0 ||
                transition.duration_frames > std::min(
                    from.duration_frames, to.duration_frames) ||
                from.timeline_start_frame + from.duration_frames !=
                    to.timeline_start_frame) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "A legacy transition has invalid timing or duration.");
            }
        }

        for (const auto transition_index : ordered_transitions) {
            const auto& transition = track.transitions[transition_index];
            if (transition.kind != timeline::TransitionKind::CrossDissolve) continue;
            const auto& from = track.clips[transition.from_clip_index];
            const auto cut_frame = from.timeline_start_frame + from.duration_frames;
            for (const auto& clip : track.clips) {
                if (clip.timeline_start_frame >= cut_frame &&
                    clip.timeline_start_frame < transition.duration_frames) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                              "A legacy Cross Dissolve cannot be migrated without a negative clip position.");
                }
            }
            for (auto& clip : track.clips) {
                if (clip.timeline_start_frame >= cut_frame) {
                    clip.timeline_start_frame -= transition.duration_frames;
                }
            }
        }
    }
}

std::int64_t requiredInteger(const QJsonObject& object,
                             const char* key,
                             const std::filesystem::path& project_path) {
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing a required integer field.");
    }
    if (!value.isDouble()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains a non-numeric integer field.");
    }

    const double number = value.toDouble();
    // 2^63 is representable as a double, but INT64_MAX rounds up to that same
    // value when converted to double. Use an exclusive upper bound so 2^63
    // cannot reach the out-of-range floating-point-to-integer conversion.
    constexpr double int64_upper_exclusive = 0x1p63;
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < -int64_upper_exclusive ||
        number >= int64_upper_exclusive) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid integer value.");
    }
    return static_cast<std::int64_t>(number);
}

std::uint64_t requiredStableId(const QJsonObject& object,
                               const char* key,
                               const std::filesystem::path& project_path) {
    const auto value = requiredInteger(object, key, project_path);
    if (value <= 0) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid stable identifier.");
    }
    return static_cast<std::uint64_t>(value);
}

QString requiredString(const QJsonObject& object,
                       const char* key,
                       const std::filesystem::path& project_path) {
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing a required string field.");
    }
    if (!value.isString() || value.toString().isEmpty()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid string field.");
    }
    return value.toString();
}

media::LinkedImageReference parseLinkedImageReference(
    const QJsonObject& owner,
    const char* key,
    const std::filesystem::path& project_path) {
    const auto value = owner.value(QLatin1String(key));
    if (!value.isObject()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Image Editor link.");
    }
    const auto object = value.toObject();
    media::LinkedImageReference link;
    link.id = requiredString(object, "id", project_path).toUtf8().toStdString();
    link.document_path = resolvedPath(
        project_path, requiredString(object, "document", project_path));
    link.published_output_path = resolvedPath(
        project_path, requiredString(object, "output", project_path));
    return link;
}

timeline::TextStyle parseTextStyle(
    const QJsonObject& clip_object,
    const std::filesystem::path& project_path) {
    const auto text_value = clip_object.value("text");
    if (!text_value.isObject()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "A text clip is missing its text object.");
    }
    const auto object = text_value.toObject();
    timeline::TextStyle text;
    const auto content = object.value("content");
    if (!content.isString()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "A text clip is missing text content.");
    }
    text.content = content.toString().toUtf8().toStdString();
    if (object.contains("font_family")) {
        if (!object.value("font_family").isString()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid font family.");
        }
        text.font_family = object.value("font_family").toString().toUtf8().toStdString();
    }
    if (object.contains("font_size_pixels")) {
        if (!object.value("font_size_pixels").isDouble()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid font size.");
        }
        text.font_size_pixels = object.value("font_size_pixels").toDouble();
    }
    if (object.contains("color")) {
        const auto color_value = object.value("color");
        if (!color_value.isObject()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid color.");
        }
        const auto color = color_value.toObject();
        for (const auto key : {"r", "g", "b", "a"}) {
            const auto component = color.value(QLatin1String(key));
            if (!component.isDouble() || component.toDouble() < 0.0 ||
                component.toDouble() > 255.0 ||
                std::floor(component.toDouble()) != component.toDouble()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid color component.");
            }
        }
        text.color = {
            static_cast<std::uint8_t>(color.value("r").toInt()),
            static_cast<std::uint8_t>(color.value("g").toInt()),
            static_cast<std::uint8_t>(color.value("b").toInt()),
            static_cast<std::uint8_t>(color.value("a").toInt())};
    }
    if (object.contains("alignment")) {
        const auto alignment = object.value("alignment");
        if (!alignment.isString()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid alignment.");
        }
        const auto value = alignment.toString();
        if (value == QLatin1String("left")) text.alignment = timeline::TextAlignment::Left;
        else if (value == QLatin1String("center")) text.alignment = timeline::TextAlignment::Center;
        else if (value == QLatin1String("right")) text.alignment = timeline::TextAlignment::Right;
        else throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an unsupported alignment.");
    }
    return text;
}

} // namespace

ProjectDocument detail::load(const std::filesystem::path& project_path) {
    const QString file_name = pathToQString(project_path);
    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not open the project file for reading.",
            file.error(),
            project_path);
    }

    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not read the project file.",
            file.error(),
            project_path);
    }

    QJsonParseError parse_error;
    const auto json = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !json.isObject()) {
        throw ProjectError(
            ProjectErrorCode::InvalidFormat,
            "The project file is not valid JSON.",
            static_cast<int>(parse_error.error),
            project_path);
    }

    const auto root = json.object();
    if (requiredString(root, "format", project_path) !=
        QString::fromLatin1(format_identifier)) {
        throw ProjectError(
            ProjectErrorCode::InvalidFormat,
            "The project file has an unsupported format identifier.",
            std::nullopt,
            project_path);
    }

    const auto version = requiredInteger(root, "version", project_path);
    if (version < legacy_format_version || version > current_format_version) {
        throw ProjectError(
            ProjectErrorCode::UnsupportedVersion,
            "The project file version is not supported.",
            static_cast<int>(version),
            project_path);
    }

    const auto media_value = root.value("media");
    if (!media_value.isArray()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the media array.");
    }

    ProjectDocument document;
    document.timing_migration_required =
        version < timeline_frame_rate_format_version;
    document.audio_companion_migration_required =
        version < audio_companion_format_version;
    if (version >= canvas_format_version) {
        const auto canvas_value = root.value("canvas");
        if (!canvas_value.isObject()) {
            throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the canvas object.");
        }
        const auto canvas = canvas_value.toObject();
        const auto width = requiredInteger(canvas, "width", project_path);
        const auto height = requiredInteger(canvas, "height", project_path);
        const bool supported_canvas = version >= portrait_canvas_format_version
            ? ((width == 1920 && height == 1080) ||
               (width == 1080 && height == 1920))
            : width == 1920 && height == 1080;
        if (!supported_canvas) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      version >= portrait_canvas_format_version
                          ? "Project JSON contains an unsupported canvas size; expected 1920x1080 or 1080x1920."
                          : "Legacy project versions only support a 1920x1080 canvas.");
        }
        document.canvas_width = static_cast<int>(width);
        document.canvas_height = static_cast<int>(height);
    }
    const auto bins_value = root.value("bins");
    if (!bins_value.isUndefined()) {
        if (!bins_value.isArray()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid bins array.");
        }
        for (const auto& bin_value : bins_value.toArray()) {
            if (!bin_value.isString() || !media::MediaLibrary::validBinPath(
                    bin_value.toString().toUtf8().toStdString())) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid bin path.");
            }
            document.bins.push_back(bin_value.toString().toUtf8().toStdString());
        }
    }
    for (const auto& media_value_item : media_value.toArray()) {
        if (!media_value_item.isObject()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media item.");
        }
        const auto object = media_value_item.toObject();
        ProjectMedia media;
        media.kind = parseMediaKind(object, project_path, static_cast<int>(version));
        media.source_path = resolvedPath(
            project_path,
            requiredString(object, "path", project_path));
        if (object.contains("name")) {
            if (!object.value("name").isString()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media name.");
            }
            media.display_name = object.value("name").toString().toUtf8().toStdString();
        }
        if (object.contains("bin")) {
            if (!object.value("bin").isString()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media bin.");
            }
            media.bin_path = object.value("bin").toString().toUtf8().toStdString();
        }
        if (object.contains("offline")) {
            if (!object.value("offline").isBool()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid offline media flag.");
            }
            media.offline = object.value("offline").toBool();
        }
        if (version >= linked_image_format_version &&
            object.contains("image_editor_link")) {
            media.image_editor_link = parseLinkedImageReference(
                object, "image_editor_link", project_path);
        }
        document.media.push_back(std::move(media));
    }

    const auto timeline_value = root.value("timeline");
    if (!timeline_value.isObject()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the timeline object.");
    }
    const auto timeline_object = timeline_value.toObject();
    if (version >= timeline_frame_rate_format_version) {
        const auto rate_value = timeline_object.value("frame_rate");
        if (!rate_value.isObject()) {
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "Project JSON is missing the timeline frame rate.");
        }
        const auto rate_object = rate_value.toObject();
        document.timeline_frame_rate = {
            requiredInteger(rate_object, "numerator", project_path),
            requiredInteger(rate_object, "denominator", project_path)};
        if (!timeline::validFrameRate(document.timeline_frame_rate)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid rational timeline frame rate.");
        }
        document.timeline_frame_rate = timeline::reducedFrameRate(
            document.timeline_frame_rate);
    }
    if (version >= timeline_zoom_format_version) {
        const auto zoom_value = timeline_object.value("zoom");
        if (zoom_value.isUndefined()) {
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "Project JSON is missing the timeline zoom value.");
        }
        if (!zoom_value.isDouble() || !std::isfinite(zoom_value.toDouble()) ||
            zoom_value.toDouble() < timeline::kMinTimelineZoomFactor ||
            zoom_value.toDouble() > timeline::kMaxTimelineZoomFactor) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid timeline zoom; expected a value from 0.25 to 512.0.");
        }
        document.timeline_zoom = zoom_value.toDouble();
    }
    if (version >= timeline_group_row_heights_format_version) {
        const auto read_row_height = [&timeline_object, &project_path](
                                         const char* field_name) {
            const auto value = timeline_object.value(QLatin1String(field_name));
            if (value.isUndefined()) {
                throwJson(ProjectErrorCode::MissingField, project_path,
                          "Project JSON is missing a Timeline track-group row height.");
            }
            if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
                value.toDouble() < timeline::kMinimumTrackRowHeight ||
                value.toDouble() > timeline::kMaximumTrackRowHeight) {
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Project JSON contains an invalid Timeline track-group row height; expected a value from 30.0 to 180.0.");
            }
            return value.toDouble();
        };
        document.timeline_video_row_height =
            read_row_height("video_row_height");
        document.timeline_audio_row_height =
            read_row_height("audio_row_height");
    } else if (version >= timeline_row_height_format_version) {
        const auto row_height_value = timeline_object.value("row_height");
        if (row_height_value.isUndefined()) {
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "Project JSON is missing the timeline row height value.");
        }
        if (!row_height_value.isDouble() || !std::isfinite(row_height_value.toDouble()) ||
            row_height_value.toDouble() < timeline::kMinimumTrackRowHeight ||
            row_height_value.toDouble() > timeline::kMaximumTrackRowHeight) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid timeline row height; expected a value from 30.0 to 180.0.");
        }
        document.timeline_video_row_height = row_height_value.toDouble();
        document.timeline_audio_row_height = row_height_value.toDouble();
    }
    timeline::TrackId migrated_track_id = 1;
    timeline::ClipId migrated_clip_id = 1;
    if (version == legacy_format_version) {
        const auto clips_value = timeline_object.value("clips");
        if (!clips_value.isArray()) {
            throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the timeline clips array.");
        }
        ProjectTrack track;
        track.track_id = migrated_track_id++;
        track.name = "Video 1";
        std::int64_t timeline_start = 0;
        for (const auto& clip_value : clips_value.toArray()) {
            if (!clip_value.isObject()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid timeline clip.");
            }
            const auto clip_object = clip_value.toObject();
            ProjectClip clip;
            clip.clip_id = migrated_clip_id++;
            clip.source_path = resolvedPath(project_path, requiredString(clip_object, "source", project_path));
            clip.timeline_start_frame = timeline_start;
            clip.source_start_frame = requiredInteger(clip_object, "source_start_frame", project_path);
            clip.duration_frames = requiredInteger(clip_object, "duration_frames", project_path);
            clip.source_duration_frames = clip.duration_frames;
            clip.source_duration_migration_pending = true;
            if (clip.duration_frames > 0 && timeline_start <=
                std::numeric_limits<std::int64_t>::max() - clip.duration_frames) {
                timeline_start += clip.duration_frames;
            }
            track.clips.push_back(std::move(clip));
        }
        document.timeline_tracks.push_back(std::move(track));
    } else {
        const auto tracks_value = timeline_object.value("tracks");
        if (!tracks_value.isArray()) {
            throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the timeline tracks array.");
        }
        for (const auto& track_value : tracks_value.toArray()) {
            if (!track_value.isObject()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid timeline track.");
            }
            const auto track_object = track_value.toObject();
            ProjectTrack track;
            track.track_id = version >= stable_ids_format_version
                ? requiredStableId(track_object, "track_id", project_path)
                : migrated_track_id++;
            track.name = requiredString(track_object, "name", project_path).toUtf8().toStdString();
            if (version >= audio_tracks_format_version) {
                const auto track_kind = track_object.value("kind");
                if (!track_kind.isString()) {
                    throwJson(ProjectErrorCode::MissingField, project_path,
                              "A version 13 Timeline track is missing its kind.");
                }
                if (track_kind.toString() == QLatin1String("audio")) {
                    track.kind = timeline::TrackKind::Audio;
                } else if (track_kind.toString() == QLatin1String("video")) {
                    track.kind = timeline::TrackKind::Video;
                } else {
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "Project JSON contains an unsupported track kind.");
                }
            }
            if (track_object.contains("audio_gain")) {
                if (!track_object.value("audio_gain").isDouble()) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid track audio gain.");
                }
                track.audio_gain = track_object.value("audio_gain").toDouble();
            }
            if (track_object.contains("audio_muted")) {
                if (!track_object.value("audio_muted").isBool()) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid track mute flag.");
                }
                track.audio_muted = track_object.value("audio_muted").toBool();
            }
            const auto track_clips = track_object.value("clips");
            if (!track_clips.isArray()) {
                throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing a track clips array.");
            }
            for (const auto& clip_value : track_clips.toArray()) {
                if (!clip_value.isObject()) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid timeline clip.");
                }
                const auto clip_object = clip_value.toObject();
                ProjectClip clip;
                clip.clip_id = version >= stable_ids_format_version
                    ? requiredStableId(clip_object, "clip_id", project_path)
                    : migrated_clip_id++;
                if (version >= clip_kind_format_version && clip_object.contains("kind")) {
                    const auto kind = clip_object.value("kind");
                    if (!kind.isString()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip kind.");
                    }
                    if (kind.toString() == QLatin1String("video")) {
                        clip.kind = timeline::ClipKind::Video;
                    } else if (kind.toString() == QLatin1String("image")) {
                        clip.kind = timeline::ClipKind::Image;
                    } else if (kind.toString() == QLatin1String("text")) {
                        clip.kind = timeline::ClipKind::Text;
                    } else if (version >= audio_tracks_format_version &&
                               kind.toString() == QLatin1String("audio")) {
                        clip.kind = timeline::ClipKind::Audio;
                    } else {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported clip kind.");
                    }
                }
                if (timeline::isMediaClipKind(clip.kind)) {
                    clip.source_path = resolvedPath(
                        project_path, requiredString(clip_object, "source", project_path));
                } else {
                    clip.text = parseTextStyle(clip_object, project_path);
                }
                if (version >= linked_image_format_version &&
                    clip_object.contains("image_editor_variant")) {
                    clip.image_editor_variant = parseLinkedImageReference(
                        clip_object, "image_editor_variant", project_path);
                }
                clip.timeline_start_frame = requiredInteger(clip_object, "timeline_start_frame", project_path);
                clip.duration_frames = requiredInteger(clip_object, "duration_frames", project_path);
                if (clip.kind == timeline::ClipKind::Audio) {
                    clip.source_start_frame = 0;
                    clip.source_start_time_us = requiredInteger(
                        clip_object, "source_start_time_us", project_path);
                    clip.source_duration_time_us = requiredInteger(
                        clip_object, "source_duration_time_us", project_path);
                    clip.source_duration_frames = 0;
                    clip.source_duration_migration_pending = false;
                } else {
                    clip.source_start_frame = requiredInteger(
                        clip_object, "source_start_frame", project_path);
                    if (version >= separated_source_duration_format_version &&
                        timeline::isFrameTimedMediaClipKind(clip.kind)) {
                        clip.source_duration_frames = requiredInteger(
                            clip_object, "source_duration_frames", project_path);
                        const auto pending = clip_object.value(
                            "source_duration_migration_pending");
                        if (!pending.isBool()) {
                            throwJson(ProjectErrorCode::MissingField, project_path,
                                      "A media clip is missing its source duration migration state.");
                        }
                        clip.source_duration_migration_pending = pending.toBool();
                    } else if (timeline::isFrameTimedMediaClipKind(clip.kind)) {
                        clip.source_duration_frames = clip.duration_frames;
                        clip.source_duration_migration_pending = true;
                    }
                }
                if (clip_object.contains("audio_gain")) {
                    if (!clip_object.value("audio_gain").isDouble()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip audio gain.");
                    }
                    clip.audio_gain = clip_object.value("audio_gain").toDouble();
                }
                if (clip_object.contains("audio_muted")) {
                    if (!clip_object.value("audio_muted").isBool()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip mute flag.");
                    }
                    clip.audio_muted = clip_object.value("audio_muted").toBool();
                }
                if (version >= audio_gain_envelope_format_version &&
                    clip_object.contains("audio_gain_keyframes")) {
                    const auto envelope = clip_object.value("audio_gain_keyframes");
                    if (!envelope.isArray()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "Project JSON contains an invalid audio gain envelope.");
                    }
                    for (const auto& value : envelope.toArray()) {
                        if (!value.isObject() ||
                            !value.toObject().value("frame").isDouble() ||
                            !value.toObject().value("gain").isDouble()) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path,
                                      "Project JSON contains an invalid audio gain envelope point.");
                        }
                        clip.audio_gain_keyframes.push_back({
                            value.toObject().value("frame").toInteger(),
                            value.toObject().value("gain").toDouble()});
                    }
                }
                if (version >= clip_effects_format_version &&
                    clip_object.contains("effects")) {
                    const auto stack = clip_object.value("effects");
                    if (!stack.isArray()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "Project JSON contains an invalid clip effect stack.");
                    }
                    for (const auto& effect_value : stack.toArray()) {
                        if (!effect_value.isObject()) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path,
                                      "Project JSON contains an invalid clip effect.");
                        }
                        const auto effect_object = effect_value.toObject();
                        if (!effect_object.value("id").isString() ||
                            !effect_object.value("parameters").isArray()) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path,
                                      "Project JSON contains an incomplete clip effect.");
                        }
                        creative_suite::effects::EffectInstance effect;
                        if (version >= effect_enabled_format_version) {
                            const auto enabled = effect_object.value("enabled");
                            if (!enabled.isBool()) {
                                throwJson(ProjectErrorCode::InvalidValue, project_path,
                                          "Project JSON contains an invalid effect enabled state.");
                            }
                            effect.enabled = enabled.toBool();
                        }
                        const auto effect_id = effect_object.value("id").toString().toUtf8();
                        effect.id.assign(effect_id.constData(),
                                         static_cast<std::size_t>(effect_id.size()));
                        for (const auto& parameter_value :
                             effect_object.value("parameters").toArray()) {
                            if (!parameter_value.isObject() ||
                                !parameter_value.toObject().value("id").isString() ||
                                !parameter_value.toObject().value("value").isDouble()) {
                                throwJson(ProjectErrorCode::InvalidValue, project_path,
                                          "Project JSON contains an invalid effect parameter.");
                            }
                            const auto parameter_object = parameter_value.toObject();
                            const auto parameter_id = parameter_object.value("id").toString().toUtf8();
                            effect.parameters.push_back({
                                std::string(parameter_id.constData(),
                                            static_cast<std::size_t>(parameter_id.size())),
                                parameter_object.value("value").toDouble()});
                        }
                        clip.effects.push_back(std::move(effect));
                    }
                }
                if (version >= node_graph_format_version &&
                    clip_object.contains("node_graph")) {
                    clip.node_graph = parseNodeGraph(
                        clip_object.value("node_graph"), project_path, version);
                }
                if (version >= audio_companion_format_version) {
                    if (clip_object.contains("linked_clip_id")) {
                        const auto linked_id = clip_object.value("linked_clip_id");
                        if (!linked_id.isDouble() ||
                            linked_id.toInteger() <= 0) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path,
                                      "Project JSON contains an invalid linked audio clip identifier.");
                        }
                        clip.linked_clip_id = static_cast<timeline::ClipId>(
                            linked_id.toInteger());
                    }
                    const auto extracted = clip_object.value("audio_extracted");
                    const auto pending = clip_object.value("audio_companion_pending");
                    if (!extracted.isBool() || !pending.isBool()) {
                        throwJson(ProjectErrorCode::MissingField, project_path,
                                  "A version 14 clip is missing its audio companion state.");
                    }
                    clip.audio_extracted = extracted.toBool();
                    clip.audio_companion_pending = pending.toBool();
                }
                if (version >= canvas_format_version && clip_object.contains("transform")) {
                    const auto transform = clip_object.value("transform");
                    if (!transform.isObject()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip transform.");
                    }
                    const auto transform_object = transform.toObject();
                    const auto position = transform_object.value("position");
                    if (!position.isObject() ||
                        !position.toObject().value("x").isDouble() ||
                        !position.toObject().value("y").isDouble() ||
                        !transform_object.value("scale").isDouble() ||
                        !transform_object.value("rotation").isDouble() ||
                        !transform_object.value("opacity").isDouble()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains incomplete clip transform data.");
                    }
                    clip.transform.position_x = position.toObject().value("x").toDouble();
                    clip.transform.position_y = position.toObject().value("y").toDouble();
                    clip.transform.scale = transform_object.value("scale").toDouble();
                    clip.transform.rotation_degrees = transform_object.value("rotation").toDouble();
                    clip.transform.opacity = transform_object.value("opacity").toDouble();
                }
                if (version >= canvas_format_version && clip_object.contains("keyframes")) {
                    const auto keyframes = clip_object.value("keyframes");
                    if (!keyframes.isObject()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid clip keyframes.");
                    }
                    const auto parse_keyframes = [&](const char* key,
                                                     timeline::TransformProperty property,
                                                     std::vector<timeline::Keyframe>& output) {
                        const auto value = keyframes.toObject().value(QLatin1String(key));
                        if (value.isUndefined()) return;
                        if (!value.isArray()) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid keyframe list.");
                        }
                        for (const auto& keyframe_value : value.toArray()) {
                            if (!keyframe_value.isObject()) {
                                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid keyframe.");
                            }
                            const auto keyframe_object = keyframe_value.toObject();
                            const auto frame = requiredInteger(keyframe_object, "frame", project_path);
                            const auto numeric = keyframe_object.value("value");
                            if (!numeric.isDouble() || !timeline::validKeyframeValue(property, numeric.toDouble())) {
                                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid keyframe value.");
                            }
                            output.push_back({frame, numeric.toDouble()});
                        }
                    };
                    parse_keyframes("position_x", timeline::TransformProperty::PositionX, clip.keyframes.position_x);
                    parse_keyframes("position_y", timeline::TransformProperty::PositionY, clip.keyframes.position_y);
                    parse_keyframes("scale", timeline::TransformProperty::Scale, clip.keyframes.scale);
                    parse_keyframes("rotation", timeline::TransformProperty::Rotation, clip.keyframes.rotation);
                    parse_keyframes("opacity", timeline::TransformProperty::Opacity, clip.keyframes.opacity);
                }
                track.clips.push_back(std::move(clip));
            }
            if (version >= transitions_format_version) {
                const auto transitions_value = track_object.value("transitions");
                if (!transitions_value.isArray()) {
                    throwJson(ProjectErrorCode::MissingField, project_path,
                              "Project JSON is missing a track transitions array.");
                }
                for (const auto& transition_value : transitions_value.toArray()) {
                    if (!transition_value.isObject()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "Project JSON contains an invalid transition.");
                    }
                    const auto transition_object = transition_value.toObject();
                    const auto from_clip = requiredInteger(
                        transition_object, "from_clip", project_path);
                    const auto to_clip = requiredInteger(
                        transition_object, "to_clip", project_path);
                    if (from_clip < 0 || to_clip < 0 ||
                        static_cast<std::uint64_t>(from_clip) >
                            std::numeric_limits<std::size_t>::max() ||
                        static_cast<std::uint64_t>(to_clip) >
                            std::numeric_limits<std::size_t>::max()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "Project JSON contains invalid transition indexes.");
                    }
                    ProjectTransition transition;
                    transition.from_clip_index = static_cast<std::size_t>(from_clip);
                    transition.to_clip_index = static_cast<std::size_t>(to_clip);
                    transition.kind = parseTransitionKind(
                        transition_object, project_path, version);
                    transition.duration_frames = requiredInteger(
                        transition_object, "duration_frames", project_path);
                    track.transitions.push_back(std::move(transition));
                }
            }
            document.timeline_tracks.push_back(std::move(track));
        }
    }

    if (version < cross_dissolve_overlap_format_version) {
        migrateLegacyCrossDissolves(document, project_path);
    }
    detail::validateDocument(document, project_path);
    return document;
}

} // namespace project
