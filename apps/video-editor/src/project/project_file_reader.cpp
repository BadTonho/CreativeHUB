#include "project_file.h"
#include "project_file_detail.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
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

creative_suite::effects::EffectInstance migratePrimaryGrade(
    creative_suite::effects::EffectInstance effect,
    int version,
    const std::filesystem::path& project_path) {
    using namespace creative_suite::effects;
    if (version >= primary_grade_log_controls_format_version ||
        effect.id != "video.primary_grade") return effect;

    if (version >= primary_grade_controls_format_version) {
        constexpr std::array<std::string_view, 17> expanded_ids{
            "lift_red", "lift_green", "lift_blue",
            "gamma_red", "gamma_green", "gamma_blue",
            "red_gain", "green_gain", "blue_gain",
            "offset_red", "offset_green", "offset_blue",
            "contrast", "saturation", "pivot", "temperature", "tint"};
        if (effect.parameters.size() != expanded_ids.size())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "A version 35 Primary Grade contains an invalid parameter set.");
        auto migrated = makeDefaultInstance("video.primary_grade");
        migrated.enabled = effect.enabled;
        std::unordered_set<std::string_view> seen;
        for (const auto& parameter : effect.parameters) {
            if (std::find(expanded_ids.begin(), expanded_ids.end(), parameter.id) ==
                    expanded_ids.end() ||
                !seen.insert(parameter.id).second ||
                !setParameterValue(migrated, parameter.id, parameter.value)) {
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A version 35 Primary Grade contains an invalid parameter.");
            }
        }
        for (const auto id : expanded_ids) {
            if (!seen.contains(id))
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A version 35 Primary Grade is missing a parameter.");
        }
        return migrated;
    }

    constexpr std::array<std::string_view, 6> legacy_ids{
        "brightness", "contrast", "saturation", "red_gain", "green_gain", "blue_gain"};
    if (effect.parameters.size() != legacy_ids.size())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A legacy Primary Grade contains an invalid parameter set.");
    std::unordered_set<std::string_view> seen;
    for (const auto& parameter : effect.parameters) {
        if (std::find(legacy_ids.begin(), legacy_ids.end(), parameter.id) == legacy_ids.end() ||
            !seen.insert(parameter.id).second || !std::isfinite(parameter.value))
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "A legacy Primary Grade contains an invalid parameter.");
    }
    for (const auto id : legacy_ids)
        if (!seen.contains(id))
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "A legacy Primary Grade is missing a parameter.");

    const auto old_value = [&effect](std::string_view id) {
        const auto found = std::find_if(effect.parameters.begin(), effect.parameters.end(),
            [id](const ParameterValue& value) { return value.id == id; });
        return found->value;
    };
    auto migrated = makeDefaultInstance("video.primary_grade");
    migrated.enabled = effect.enabled;
    const double brightness = old_value("brightness");
    const bool accepted = setParameterValue(migrated, "offset_red", brightness) &&
        setParameterValue(migrated, "offset_green", brightness) &&
        setParameterValue(migrated, "offset_blue", brightness) &&
        setParameterValue(migrated, "contrast", old_value("contrast")) &&
        setParameterValue(migrated, "saturation", old_value("saturation")) &&
        setParameterValue(migrated, "red_gain", old_value("red_gain")) &&
        setParameterValue(migrated, "green_gain", old_value("green_gain")) &&
        setParameterValue(migrated, "blue_gain", old_value("blue_gain"));
    if (!accepted)
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A legacy Primary Grade value is outside its supported range.");
    return migrated;
}

creative_suite::effects::CurvesPointList parseCurvesPointList(
    const QJsonValue& value, const std::filesystem::path& project_path) {
    if (!value.isArray())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A Color curve must be stored as an array of points.");
    creative_suite::effects::CurvesPointList points;
    for (const auto& item : value.toArray()) {
        if (!item.isArray() || item.toArray().size() != 2 ||
            !item.toArray()[0].isDouble() || !item.toArray()[1].isDouble() ||
            item.toArray()[0].toInteger() < 0 || item.toArray()[0].toInteger() > 255 ||
            item.toArray()[1].toInteger() < 0 || item.toArray()[1].toInteger() > 255)
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "A Color curve contains an invalid point.");
        points.push_back({static_cast<int>(item.toArray()[0].toInteger()),
                          static_cast<int>(item.toArray()[1].toInteger())});
    }
    if (!creative_suite::effects::validCurvePoints(points))
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A Color curve has invalid endpoints, ordering, or point count.");
    return points;
}

creative_suite::effects::VideoCurvesParameters parseVideoCurves(
    const QJsonValue& value, const std::filesystem::path& project_path) {
    using namespace creative_suite::effects;
    if (!value.isObject())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A Color Curves node is missing its curve data.");
    const auto object = value.toObject();
    const auto rgb_value = object.value("rgb");
    const auto targeted_value = object.value("targeted");
    const auto qualifier_value = object.value("qualifier");
    if (!rgb_value.isObject() || !targeted_value.isArray() ||
        targeted_value.toArray().size() != 5 || !qualifier_value.isObject())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A Color Curves node contains incomplete settings.");
    const auto rgb = rgb_value.toObject();
    VideoCurvesParameters result;
    result.rgb.points = parseCurvesPointList(rgb.value("master"), project_path);
    result.rgb.channels[0] = parseCurvesPointList(rgb.value("red"), project_path);
    result.rgb.channels[1] = parseCurvesPointList(rgb.value("green"), project_path);
    result.rgb.channels[2] = parseCurvesPointList(rgb.value("blue"), project_path);
    for (std::size_t i = 0; i < result.targeted.size(); ++i)
        result.targeted[i] = parseCurvesPointList(targeted_value.toArray()[static_cast<qsizetype>(i)], project_path);
    const auto qualifier = qualifier_value.toObject();
    const char* scalar_keys[]{"hue_min", "hue_max", "saturation_min", "saturation_max",
        "luminance_min", "luminance_max", "hue_softness", "saturation_softness",
        "luminance_softness"};
    for (const auto* key : scalar_keys)
        if (!qualifier.value(QLatin1String(key)).isDouble())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "A Color Curves qualifier is missing a numeric range value.");
    if (!qualifier.value("enabled").isBool())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A Color Curves qualifier is missing its enabled state.");
    result.qualifier = {qualifier.value("enabled").toBool(),
        qualifier.value("hue_min").toDouble(), qualifier.value("hue_max").toDouble(),
        qualifier.value("saturation_min").toDouble(), qualifier.value("saturation_max").toDouble(),
        qualifier.value("luminance_min").toDouble(), qualifier.value("luminance_max").toDouble(),
        qualifier.value("hue_softness").toDouble(), qualifier.value("saturation_softness").toDouble(),
        qualifier.value("luminance_softness").toDouble()};
    if (!validVideoCurves(result))
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "A Color Curves node contains out-of-range settings.");
    return result;
}

color::Graph parseColorGraph(const QJsonValue& value,
                             const std::filesystem::path& project_path,
                             int version) {
    if (!value.isObject())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Color correction graph.");
    const auto object = value.toObject();
    const auto next_id = object.value("next_id");
    const auto nodes_value = object.value("nodes");
    const auto connections_value = object.value("connections");
    if (!next_id.isDouble() || next_id.toInteger() <= 0 ||
        !nodes_value.isArray() || !connections_value.isArray())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an incomplete Color correction graph.");
    color::Graph graph;
    graph.next_id = static_cast<color::NodeId>(next_id.toInteger());
    for (const auto& node_value : nodes_value.toArray()) {
        if (!node_value.isObject())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Color graph node.");
        const auto node_object = node_value.toObject();
        const auto id = node_object.value("id");
        const auto type = node_object.value("type");
        const auto x = node_object.value("x");
        const auto y = node_object.value("y");
        if (!id.isDouble() || id.toInteger() <= 0 || !type.isString() ||
            !x.isDouble() || !y.isDouble())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an incomplete Color graph node.");
        color::Node node;
        node.id = static_cast<color::NodeId>(id.toInteger());
        const auto type_name = type.toString();
        if (type_name == QLatin1String("input")) node.type = color::NodeType::Input;
        else if (type_name == QLatin1String("effect")) node.type = color::NodeType::Effect;
        else if (type_name == QLatin1String("curves")) {
            if (version < color_curves_format_version)
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Color Curves nodes require CSP version 38 or later.");
            node.type = color::NodeType::Curves;
        }
        else if (type_name == QLatin1String("parallel_mixer"))
            node.type = color::NodeType::ParallelMixer;
        else if (type_name == QLatin1String("output")) node.type = color::NodeType::Output;
        else throwJson(ProjectErrorCode::InvalidValue, project_path,
                       "Project JSON contains an unknown Color graph node type.");
        node.x = x.toDouble();
        node.y = y.toDouble();
        if (node.type == color::NodeType::Effect) {
            const auto effect_value = node_object.value("effect");
            if (!effect_value.isObject())
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A Color effect node is missing its settings.");
            const auto effect_object = effect_value.toObject();
            const auto effect_id = effect_object.value("id");
            const auto enabled = effect_object.value("enabled");
            const auto parameters = effect_object.value("parameters");
            if (!effect_id.isString() || !enabled.isBool() || !parameters.isArray())
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A Color effect node contains incomplete settings.");
            creative_suite::effects::EffectInstance effect;
            const auto effect_id_bytes = effect_id.toString().toUtf8();
            effect.id.assign(effect_id_bytes.constData(),
                             static_cast<std::size_t>(effect_id_bytes.size()));
            effect.enabled = enabled.toBool();
            for (const auto& parameter_value : parameters.toArray()) {
                if (!parameter_value.isObject() ||
                    !parameter_value.toObject().value("id").isString() ||
                    !parameter_value.toObject().value("value").isDouble())
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Color effect node contains an invalid parameter.");
                const auto parameter = parameter_value.toObject();
                const auto parameter_id = parameter.value("id").toString().toUtf8();
                effect.parameters.push_back({
                    std::string(parameter_id.constData(),
                                static_cast<std::size_t>(parameter_id.size())),
                    parameter.value("value").toDouble()});
            }
            node.effect = migratePrimaryGrade(std::move(effect), version, project_path);
        } else if (node.type == color::NodeType::Curves) {
            const auto curves_value = node_object.value("curves");
            if (!curves_value.isObject() ||
                !curves_value.toObject().value("enabled").isBool() ||
                !curves_value.toObject().value("parameters").isObject())
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "A Color Curves node is missing its settings.");
            const auto curves_object = curves_value.toObject();
            node.curves = color::CurvesNode{curves_object.value("enabled").toBool(),
                parseVideoCurves(curves_object.value("parameters"), project_path)};
        } else if (node_object.contains("effect")) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Only Color effect nodes may contain effect settings.");
        } else if (node_object.contains("curves")) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Only Color Curves nodes may contain curve settings.");
        }
        graph.nodes.push_back(std::move(node));
    }
    for (const auto& edge_value : connections_value.toArray()) {
        if (!edge_value.isObject())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Color graph connection.");
        const auto edge_object = edge_value.toObject();
        const auto from = edge_object.value("from");
        const auto to = edge_object.value("to");
        const auto input = edge_object.value("input");
        const auto weight = edge_object.value("weight");
        if (!from.isDouble() || from.toInteger() <= 0 || !to.isDouble() ||
            to.toInteger() <= 0 || !input.isDouble() || input.toInteger() < 0 ||
            input.toInteger() > static_cast<qint64>(std::numeric_limits<std::uint32_t>::max()) ||
            !weight.isDouble())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an incomplete Color graph connection.");
        graph.connections.push_back({
            static_cast<color::NodeId>(from.toInteger()),
            static_cast<color::NodeId>(to.toInteger()),
            static_cast<std::uint32_t>(input.toInteger()), weight.toDouble()});
    }
    if (!color::validate(graph))
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid or cyclic Color correction graph.");
    return graph;
}

std::vector<timeline::ColorGradeVersion> parseColorGradeVersions(
    const QJsonValue& value,
    const std::filesystem::path& project_path,
    int version) {
    if (!value.isArray())
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Color grade versions list.");
    std::vector<timeline::ColorGradeVersion> result;
    for (const auto& version_value : value.toArray()) {
        if (!version_value.isObject())
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Color grade version.");
        const auto object = version_value.toObject();
        if (!object.value("id").isString() || !object.value("name").isString() ||
            !object.value("graph").isObject())
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "A Color grade version is missing its identity, name, or graph.");
        const auto id = object.value("id").toString().toUtf8();
        const auto name = object.value("name").toString().toUtf8();
        result.push_back({
            std::string(id.constData(), static_cast<std::size_t>(id.size())),
            std::string(name.constData(), static_cast<std::size_t>(name.size())),
            parseColorGraph(object.value("graph"), project_path, version)});
    }
    if (!timeline::validColorGradeVersions(result))
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains invalid or duplicate Color grade versions.");
    return result;
}



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
    if (name == QLatin1String("gaussian_blur")) return fusion::nodes::NodeType::GaussianBlur;
    if (name == QLatin1String("color")) return fusion::nodes::NodeType::Color;
    if (name == QLatin1String("merge")) return fusion::nodes::NodeType::Merge;
    if (name == QLatin1String("output")) return fusion::nodes::NodeType::Output;
    if (name == QLatin1String("effect")) return fusion::nodes::NodeType::Effect;
    if (name == QLatin1String("background")) return fusion::nodes::NodeType::Background;
    if (name == QLatin1String("text")) return fusion::nodes::NodeType::Text;
    if (name == QLatin1String("rectangle_mask")) return fusion::nodes::NodeType::RectangleMask;
    if (name == QLatin1String("ellipse_mask")) return fusion::nodes::NodeType::EllipseMask;
    throwJson(ProjectErrorCode::InvalidValue, project_path,
              "Project JSON contains an unsupported Fusion node type.");
}

timeline::Keyframe readCurveEasing(timeline::Keyframe key, const QJsonObject& object,
    int version, const std::filesystem::path& project_path) {
    if (version < fusion_composition_format_version || !object.contains("interpolation")) return key;
    if (object.value("interpolation") != "cubic_bezier" || !object.value("easing").isArray())
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Invalid keyframe interpolation.");
    const auto points = object.value("easing").toArray();
    if (points.size() != 4 || std::any_of(points.begin(), points.end(), [](const auto& value) { return !value.isDouble(); }))
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Invalid keyframe easing points.");
    key.interpolation = creative_suite::animation::InterpolationMode::CubicBezier;
    key.easing = {points[0].toDouble(), points[1].toDouble(), points[2].toDouble(), points[3].toDouble()};
    if (!creative_suite::animation::validCubicBezierEasing(key.easing))
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Invalid keyframe easing curve.");
    return key;
}

timeline::TextStyle parseTextStyle(const QJsonObject&, const std::filesystem::path&);

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
        if (node_object.contains("presentation")) {
            if (version < 30 || !node_object.value("presentation").isObject())
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Presentation metadata requires project format 30.");
            node.presentation = creative_suite::composition_exchange::parsePresentation(node_object.value("presentation").toObject());
        }
        node.id = static_cast<fusion::nodes::NodeId>(id.toInteger());
        node.type = parseNodeType(node_object.value("type"), project_path);
        if (node.type == fusion::nodes::NodeType::GaussianBlur || node_object.contains("gaussian_blur")) {
            const auto blur=node_object.value("gaussian_blur").toObject();
            if (version<31 || node.type!=fusion::nodes::NodeType::GaussianBlur ||
                !blur.value("radius_pixels").isDouble() || !blur.value("enabled").isBool())
                throwJson(ProjectErrorCode::InvalidValue,project_path,"Gaussian Blur requires valid static parameters in project format 31.");
            node.gaussian_blur={blur.value("radius_pixels").toDouble(),blur.value("enabled").toBool()};
        }
        if (fusion::nodes::isMaskNode(node.type) || node_object.contains("mask") || node_object.contains("mask_keyframes")) {
            if (version<fusion_masks_format_version || !fusion::nodes::isMaskNode(node.type) || !node_object.value("mask").isObject())
                throwJson(ProjectErrorCode::InvalidValue,project_path,"Mask data requires a mask node in project format 28.");
            const auto mask=node_object.value("mask").toObject();
            const char* names[]{"center_x","center_y","width","height","rotation"};
            for (std::size_t i=0;i<5;++i) {
                if (!mask.value(names[i]).isDouble()) throwJson(ProjectErrorCode::InvalidValue,project_path,"Invalid mask geometry.");
                fusion::nodes::setMaskValue(node.mask,i,mask.value(names[i]).toDouble());
            }
            if (!mask.value("soft_edge").isDouble() || !mask.value("invert").isBool())
                throwJson(ProjectErrorCode::InvalidValue,project_path,"Invalid static mask properties.");
            node.mask.soft_edge=mask.value("soft_edge").toDouble(); node.mask.invert=mask.value("invert").toBool();
        }
        if (node.type == fusion::nodes::NodeType::Background || node.type == fusion::nodes::NodeType::Text) {
            if (version < fusion_composition_format_version)
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Generator nodes require project format version 27.");
            if (node.type == fusion::nodes::NodeType::Text) node.text = parseTextStyle(node_object, project_path);
            else {
                if (!node_object.value("background_color").isObject())
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Missing Background color.");
                QJsonObject style; style.insert("content", ""); style.insert("color", node_object.value("background_color"));
                QJsonObject owner; owner.insert("text", style);
                node.background_color = parseTextStyle(owner, project_path).color;
            }
        }
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
        if (color.contains("enabled")) {
            if (version < 30 || !color.value("enabled").isBool())
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Color enable state requires project format 30.");
            node.color.enabled = color.value("enabled").toBool();
        }
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
                    output.push_back(readCurveEasing({frame, numeric.toDouble()}, key, version, project_path));
                }
            };
            if (node_object.contains("mask_keyframes")) {
                if (!node_object.value("mask_keyframes").isObject()) throwJson(ProjectErrorCode::InvalidValue,project_path,"Invalid mask curves.");
                const auto curves=node_object.value("mask_keyframes").toObject();
                const char* names[]{"center_x","center_y","width","height","rotation"};
                for (std::size_t i=0;i<5;++i) parse_scalar_keyframes(curves.value(names[i]),node.mask_keyframes[i]);
            }
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
            input.toInteger() < 0 || input.toInteger() > (version>=fusion_masks_format_version ? 2 : 1))
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Fusion connection endpoint.");
        graph.connections.push_back({static_cast<fusion::nodes::NodeId>(from.toInteger()),
            static_cast<fusion::nodes::NodeId>(to.toInteger()),
            static_cast<std::uint8_t>(input.toInteger())});
    }
    if ((version < fusion_composition_format_version && std::none_of(graph.nodes.begin(), graph.nodes.end(),
            [](const auto& node) { return node.type == fusion::nodes::NodeType::Input; })) || !fusion::nodes::validate(graph))
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

media::MotionLinkReference parseMotionLinkReference(
    const QJsonObject& owner,
    const char* key,
    const std::filesystem::path& project_path, int version) {
    const auto value = owner.value(QLatin1String(key));
    if (!value.isObject()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Motion Studio link.");
    }
    const auto object = value.toObject();
    media::MotionLinkReference link;
    link.id = requiredString(object, "id", project_path).toUtf8().toStdString();
    link.document_path = resolvedPath(project_path,
        requiredString(object, "document", project_path));
    link.published_output_path = resolvedPath(project_path,
        requiredString(object, "output", project_path));
    link.source_path = resolvedPath(project_path,
        requiredString(object, "source", project_path));
    link.source_kind = requiredString(object, "source_kind", project_path).toUtf8().toStdString();
    link.container = requiredString(object, "container", project_path).toUtf8().toStdString();
    link.codec = requiredString(object, "codec", project_path).toUtf8().toStdString();
    link.quality = requiredString(object, "quality", project_path).toUtf8().toStdString();
    const auto bitrate = object.value(QStringLiteral("bitrate_mbps"));
    if (!bitrate.isDouble()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Motion Studio quality profile.");
    }
    link.bitrate_mbps = bitrate.toDouble();
    if (version >= motion_render_offset_format_version) {
        link.render_start_time_us = requiredInteger(object, "render_start_time_us", project_path);
    }
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
        if (version >= linked_motion_format_version && object.contains("motion_link")) {
            media.motion_link = parseMotionLinkReference(object, "motion_link", project_path, version);
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
                    } else if (version >= fusion_composition_format_version && kind.toString() == QLatin1String("fusion_composition")) {
                        clip.kind = timeline::ClipKind::FusionComposition;
                    } else if (version >= adjustment_clip_format_version &&
                               kind.toString() == QLatin1String("adjustment")) {
                        clip.kind = timeline::ClipKind::Adjustment;
                    } else if (kind.toString() == QLatin1String("text")) {
                        clip.kind = timeline::ClipKind::Text;
                    } else if (version >= audio_tracks_format_version &&
                               kind.toString() == QLatin1String("audio")) {
                        clip.kind = timeline::ClipKind::Audio;
                    } else {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported clip kind.");
                    }
                }
                if (clip.kind == timeline::ClipKind::FusionComposition &&
                    (clip_object.contains("audio_gain") || clip_object.contains("audio_muted") ||
                     clip_object.contains("audio_extracted") || clip_object.contains("audio_companion_pending") ||
                     clip_object.contains("linked_clip_id") || clip_object.contains("source_duration_migration_pending")))
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Unexpected audio or source metadata on a Fusion composition.");
                if (clip_object.contains("composition_input_offset_frames")) {
                    if (version < fusion_composition_format_version || clip.kind != timeline::ClipKind::FusionComposition)
                        throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Unexpected composition input offset.");
                    const auto offset = clip_object.value("composition_input_offset_frames");
                    const double value = offset.toDouble();
                    constexpr double int64_upper_exclusive = 9223372036854775808.0;
                    constexpr double int64_lower_inclusive = -9223372036854775808.0;
                    if (!offset.isDouble() || !std::isfinite(value) || std::trunc(value) != value ||
                        value < int64_lower_inclusive || value >= int64_upper_exclusive)
                        throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Invalid composition input offset.");
                    clip.composition_input_offset_frames = static_cast<std::int64_t>(value);
                }
                if (timeline::isMediaClipKind(clip.kind)) {
                    clip.source_path = resolvedPath(
                        project_path, requiredString(clip_object, "source", project_path));
                } else if (clip.kind == timeline::ClipKind::Text) {
                    clip.text = parseTextStyle(clip_object, project_path);
                } else if ((clip.kind == timeline::ClipKind::Adjustment || clip.kind == timeline::ClipKind::FusionComposition) &&
                           (clip_object.contains("source") || clip_object.contains("text") ||
                            clip_object.contains("source_duration_frames") ||
                            clip_object.contains("source_start_time_us") ||
                            clip_object.contains("source_duration_time_us"))) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                              "A generated clip cannot contain primary media or clip text fields.");
                }
                if (clip_object.contains("display_name")) {
                    if (version < composition_name_format_version || clip.kind != timeline::ClipKind::FusionComposition ||
                        !clip_object.value("display_name").isString())
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Composition display names require project format version 29.");
                    clip.display_name = clip_object.value("display_name").toString().toUtf8().toStdString();
                }
                if (version >= linked_image_format_version &&
                    clip_object.contains("image_editor_variant")) {
                    clip.image_editor_variant = parseLinkedImageReference(
                        clip_object, "image_editor_variant", project_path);
                }
                if (version >= linked_motion_format_version && clip_object.contains("motion_link")) {
                    clip.motion_link = parseMotionLinkReference(
                        clip_object, "motion_link", project_path, version);
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
                        const double frame_value = value.toObject().value("frame").toDouble();
                        const double gain_value = value.toObject().value("gain").toDouble();
                        constexpr double int64_upper_exclusive = 9223372036854775808.0;
                        constexpr double int64_lower_inclusive = -9223372036854775808.0;
                        if (!std::isfinite(frame_value) || std::trunc(frame_value) != frame_value ||
                            frame_value < int64_lower_inclusive || frame_value >= int64_upper_exclusive ||
                            !std::isfinite(gain_value)) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path,
                                      "Project JSON contains an out-of-range audio gain envelope point.");
                        }
                        clip.audio_gain_keyframes.push_back({
                            static_cast<std::int64_t>(frame_value), gain_value});
                    }
                }
                if (version >= clip_effects_format_version &&
                    version < color_graph_format_version &&
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
                        if (effect.id == "video.primary_grade" &&
                            version < primary_grade_format_version) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path,
                                      "Primary Grade requires project format version 32.");
                        }
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
                        clip.effects.push_back(migratePrimaryGrade(
                            std::move(effect), version, project_path));
                    }
                }
                if (timeline::supportsClipEffects(clip.kind)) {
                    if (version >= color_graph_format_version) {
                        if (!clip_object.contains("color_graph"))
                            throwJson(ProjectErrorCode::MissingField, project_path,
                                      "A version 34 or later visual clip is missing its Color graph.");
                        clip.color_graph = parseColorGraph(
                            clip_object.value("color_graph"), project_path, version);
                        clip.effects = color::effectInstancesInOrder(*clip.color_graph);
                    } else {
                        clip.color_graph = color::fromEffectStack(clip.effects);
                    }
                    if (version >= color_grade_versions_format_version) {
                        if (!clip_object.contains("color_grade_versions"))
                            throwJson(ProjectErrorCode::MissingField, project_path,
                                      "A version 37 visual clip is missing its Color grade versions.");
                        clip.color_grade_versions = parseColorGradeVersions(
                            clip_object.value("color_grade_versions"), project_path, version);
                    }
                } else if (version >= color_graph_format_version &&
                           clip_object.contains("color_graph")) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "A Color correction graph is not valid on this clip kind.");
                } else if (version >= color_grade_versions_format_version &&
                           clip_object.contains("color_grade_versions")) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path,
                              "Color grade versions are not valid on this clip kind.");
                }
                if (version >= node_graph_format_version &&
                    clip_object.contains("node_graph")) {
                    clip.node_graph = parseNodeGraph(
                        clip_object.value("node_graph"), project_path, version);
                }
                if (version >= audio_companion_format_version && clip.kind != timeline::ClipKind::FusionComposition) {
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
                            output.push_back(readCurveEasing({frame, numeric.toDouble()}, keyframe_object, version, project_path));
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
    if (version >= color_gallery_format_version) {
        const auto gallery_value = root.value("color_gallery");
        if (!gallery_value.isArray()) {
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "Project JSON is missing the Color Gallery array.");
        }
        auto gallery_directory = project_path;
        gallery_directory += ".gallery";
        const QFileInfo gallery_info(pathToQString(gallery_directory));
        const bool gallery_directory_available = gallery_info.isDir() &&
            !gallery_info.isSymLink();
        for (const auto& value : gallery_value.toArray()) {
            if (!value.isObject()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Project JSON contains invalid Color Gallery metadata.");
            }
            const auto item = value.toObject();
            ColorGalleryStill still;
            still.id = requiredString(item, "id", project_path).toUtf8().toStdString();
            still.name = requiredString(item, "name", project_path).toUtf8().toStdString();
            const auto source_clip_id = requiredInteger(item, "source_clip_id", project_path);
            if (source_clip_id <= 0) {
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Project JSON contains an invalid Color Gallery source clip.");
            }
            still.source_clip_id = static_cast<timeline::ClipId>(source_clip_id);
            still.timeline_frame = requiredInteger(item, "timeline_frame", project_path);
            still.image_file = requiredString(item, "file", project_path).toUtf8().toStdString();
            const std::filesystem::path image_filename(still.image_file);
            if (image_filename.empty() || image_filename.filename() != image_filename ||
                image_filename.extension() != ".png") {
                throwJson(ProjectErrorCode::InvalidValue, project_path,
                          "Project JSON contains an invalid Color Gallery image filename.");
            }
            const auto image_path = gallery_directory / image_filename;
            const QFileInfo image_info(pathToQString(image_path));
            QFile image_file(pathToQString(image_path));
            if (gallery_directory_available && !image_info.isSymLink() &&
                image_info.exists() && image_info.isFile() &&
                !image_file.open(QIODevice::ReadOnly)) {
                throw ProjectError(ProjectErrorCode::Io,
                    "Could not open a Color Gallery still image.",
                    image_file.error(), image_path);
            }
            if (gallery_directory_available && !image_info.isSymLink() &&
                image_info.isFile() &&
                image_file.isOpen()) {
                const auto png = image_file.readAll();
                if (image_file.error() != QFileDevice::NoError) {
                    throw ProjectError(ProjectErrorCode::Io,
                        "Could not read a Color Gallery still image.",
                        image_file.error(), image_path);
                }
                if (!png.isEmpty()) {
                    auto image_png = std::make_shared<std::vector<std::uint8_t>>();
                    image_png->assign(
                        reinterpret_cast<const std::uint8_t*>(png.constData()),
                        reinterpret_cast<const std::uint8_t*>(png.constData()) + png.size());
                    still.image_png = std::move(image_png);
                }
            }
            still.image_unavailable = !still.image_png || still.image_png->empty();
            document.color_gallery_stills.push_back(std::move(still));
        }
    }
    detail::validateDocument(document, project_path);
    return document;
}

} // namespace project
