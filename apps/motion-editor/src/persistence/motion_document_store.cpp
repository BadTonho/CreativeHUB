#include "motion_document_store.h"

#include <creative_suite/animation/animation.h>
#include <creative_suite/media/media_library.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace motion::persistence {
namespace {

using creative_suite::media::MediaKind;
using creative_suite::media::MediaLibrary;
using creative_suite::media::default_bin;
using model::CompositionDocument;
using model::CompositionLayer;
using model::FrameRate;
using model::LayerKind;
using model::ShapeKind;
using model::MotionMediaEntryData;
using model::MotionProjectData;

constexpr std::int64_t kMaximumDocumentBytes = 128LL * 1024LL * 1024LL;
constexpr int kRecoveryFormatVersion = 1;
constexpr auto kRecoveryFormatIdentifier = "creative-suite.motion-studio-recovery";
constexpr qsizetype kMaximumLayerCount = 100'000;
constexpr qsizetype kMaximumMediaCount = 100'000;
constexpr qsizetype kMaximumBinCount = 100'000;
constexpr qsizetype kMaximumKeyframeCount = 2'000'000;
constexpr qsizetype kMaximumStringBytes = 32'768;

[[noreturn]] void fail(MotionDocumentErrorCode code,
                       const std::filesystem::path& path,
                       const QString& message,
                       std::optional<int> system_error = std::nullopt)
{
    throw MotionDocumentError(code, message.toStdString(), path, system_error);
}

QString pathToQString(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()),
                              static_cast<qsizetype>(value.size()));
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(first, first + bytes.size()));
}

QString stringFromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString storedPath(const std::filesystem::path& document_path,
                   const std::filesystem::path& source_path)
{
    const auto source = MediaLibrary::canonicalPath(source_path);
    const auto document_directory =
        MediaLibrary::canonicalPath(document_path).parent_path();
    const auto relative = source.lexically_relative(document_directory);
    if (!relative.empty() && !relative.is_absolute() &&
        *relative.begin() != std::filesystem::path("..")) {
        return stringFromUtf8(pathToUtf8(relative));
    }
    return stringFromUtf8(pathToUtf8(source));
}

std::filesystem::path resolvedPath(const std::filesystem::path& document_path,
                                   const QString& encoded_path)
{
    auto path = pathFromQString(encoded_path);
    const bool was_relative = path.is_relative();
    const auto base = MediaLibrary::canonicalPath(document_path).parent_path();
    if (path.is_relative()) {
        const auto normalized = path.lexically_normal();
        if (normalized.empty() || *normalized.begin() == std::filesystem::path("..")) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("A relative media path cannot escape the document directory."));
        }
        path = base / normalized;
    }
    path = MediaLibrary::canonicalPath(path);
    if (was_relative) {
        const auto relative = path.lexically_relative(base);
        if (relative.empty() || relative.is_absolute() ||
            *relative.begin() == std::filesystem::path("..")) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("A relative media path resolves outside the document directory."));
        }
    }
    return path;
}

QString requiredString(const QJsonObject& object,
                       const char* key,
                       const std::filesystem::path& path)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isString()) {
        fail(object.contains(QLatin1String(key))
                 ? MotionDocumentErrorCode::InvalidValue
                 : MotionDocumentErrorCode::MissingField,
             path, QStringLiteral("The document field '%1' must be a string.").arg(QLatin1String(key)));
    }
    return value.toString();
}

QJsonObject requiredObject(const QJsonObject& object,
                           const char* key,
                           const std::filesystem::path& path)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isObject()) {
        fail(object.contains(QLatin1String(key))
                 ? MotionDocumentErrorCode::InvalidValue
                 : MotionDocumentErrorCode::MissingField,
             path, QStringLiteral("The document field '%1' must be an object.").arg(QLatin1String(key)));
    }
    return value.toObject();
}

QJsonArray requiredArray(const QJsonObject& object,
                         const char* key,
                         const std::filesystem::path& path)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isArray()) {
        fail(object.contains(QLatin1String(key))
                 ? MotionDocumentErrorCode::InvalidValue
                 : MotionDocumentErrorCode::MissingField,
             path, QStringLiteral("The document field '%1' must be an array.").arg(QLatin1String(key)));
    }
    return value.toArray();
}

double requiredNumber(const QJsonObject& object,
                      const char* key,
                      const std::filesystem::path& path)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        fail(object.contains(QLatin1String(key))
                 ? MotionDocumentErrorCode::InvalidValue
                 : MotionDocumentErrorCode::MissingField,
             path, QStringLiteral("The document field '%1' must be a finite number.").arg(QLatin1String(key)));
    }
    return value.toDouble();
}

bool requiredBool(const QJsonObject& object,
                  const char* key,
                  const std::filesystem::path& path)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isBool()) {
        fail(object.contains(QLatin1String(key))
                 ? MotionDocumentErrorCode::InvalidValue
                 : MotionDocumentErrorCode::MissingField,
             path, QStringLiteral("The document field '%1' must be a boolean.").arg(QLatin1String(key)));
    }
    return value.toBool();
}

std::int64_t requiredInt64(const QJsonObject& object,
                           const char* key,
                           const std::filesystem::path& path)
{
    const auto value = requiredString(object, key, path).toUtf8();
    std::int64_t result = 0;
    const auto parsed = std::from_chars(value.constData(), value.constData() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.constData() + value.size()) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document field '%1' is not an exact signed 64-bit integer.")
                 .arg(QLatin1String(key)));
    }
    return result;
}

std::uint64_t requiredUInt64(const QJsonObject& object,
                             const char* key,
                             const std::filesystem::path& path)
{
    const auto value = requiredString(object, key, path).toUtf8();
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.constData(), value.constData() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.constData() + value.size()) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document field '%1' is not an exact unsigned 64-bit integer.")
                 .arg(QLatin1String(key)));
    }
    return result;
}

int requiredInt(const QJsonObject& object,
                const char* key,
                const std::filesystem::path& path)
{
    const double number = requiredNumber(object, key, path);
    if (std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document field '%1' is outside the supported integer range.")
                 .arg(QLatin1String(key)));
    }
    return static_cast<int>(number);
}

QString encodedInt64(std::int64_t value)
{
    return QString::number(static_cast<qlonglong>(value));
}

QString encodedUInt64(std::uint64_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}

QString layerKindName(LayerKind kind)
{
    switch (kind) {
    case LayerKind::Text: return QStringLiteral("text");
    case LayerKind::Shape: return QStringLiteral("shape");
    case LayerKind::Image: return QStringLiteral("image");
    case LayerKind::Video: return QStringLiteral("video");
    }
    return {};
}

LayerKind parseLayerKind(const QString& value,
                         const std::filesystem::path& path)
{
    if (value == QLatin1String("text")) return LayerKind::Text;
    if (value == QLatin1String("shape")) return LayerKind::Shape;
    if (value == QLatin1String("image")) return LayerKind::Image;
    if (value == QLatin1String("video")) return LayerKind::Video;
    fail(MotionDocumentErrorCode::InvalidValue, path,
         QStringLiteral("The document contains an unsupported layer kind."));
}

QString textAlignmentName(model::TextAlignment alignment)
{
    switch (alignment) {
    case model::TextAlignment::Left: return QStringLiteral("left");
    case model::TextAlignment::Center: return QStringLiteral("center");
    case model::TextAlignment::Right: return QStringLiteral("right");
    }
    return {};
}

model::TextAlignment parseTextAlignment(const QString& value,
                                        const std::filesystem::path& path)
{
    if (value == QLatin1String("left")) return model::TextAlignment::Left;
    if (value == QLatin1String("center")) return model::TextAlignment::Center;
    if (value == QLatin1String("right")) return model::TextAlignment::Right;
    fail(MotionDocumentErrorCode::InvalidValue, path,
         QStringLiteral("The text layer has an unsupported alignment."));
}

QString shapeKindName(ShapeKind shape)
{
    switch (shape) {
    case ShapeKind::Rectangle: return QStringLiteral("rectangle");
    case ShapeKind::Ellipse: return QStringLiteral("ellipse");
    }
    return {};
}

ShapeKind parseShapeKind(const QString& value, const std::filesystem::path& path)
{
    if (value == QLatin1String("rectangle")) return ShapeKind::Rectangle;
    if (value == QLatin1String("ellipse")) return ShapeKind::Ellipse;
    fail(MotionDocumentErrorCode::InvalidValue, path,
         QStringLiteral("The shape layer has an unsupported primitive."));
}

QJsonArray writeColor(const model::ColorRgba& color)
{
    QJsonArray array;
    for (const auto channel : color) array.append(static_cast<int>(channel));
    return array;
}

model::ColorRgba parseColor(const QJsonValue& value,
                            const std::filesystem::path& path)
{
    if (!value.isArray() || value.toArray().size() != 4) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("A layer color must contain four RGBA channels."));
    }
    const auto array = value.toArray();
    model::ColorRgba color{};
    for (qsizetype index = 0; index < 4; ++index) {
        const auto channel = array.at(index);
        if (!channel.isDouble() || !std::isfinite(channel.toDouble()) ||
            std::floor(channel.toDouble()) != channel.toDouble() ||
            channel.toDouble() < 0.0 || channel.toDouble() > 255.0) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("A layer color channel must be an integer in the range 0 to 255."));
        }
        color[static_cast<std::size_t>(index)] =
            static_cast<std::uint8_t>(channel.toInt());
    }
    return color;
}

QString mediaKindName(MediaKind kind)
{
    return kind == MediaKind::Image ? QStringLiteral("image") : QStringLiteral("video");
}

MediaKind parseMediaKind(const QString& value,
                         const std::filesystem::path& path)
{
    if (value == QLatin1String("image")) return MediaKind::Image;
    if (value == QLatin1String("video")) return MediaKind::Video;
    fail(MotionDocumentErrorCode::InvalidValue, path,
         QStringLiteral("The document contains an unsupported media kind."));
}

void validateDocument(const MotionProjectData& document,
                      const std::filesystem::path& path)
{
    if (!model::isSupportedFrameRate(document.composition.frame_rate)) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The composition frame rate is not supported."));
    }
    if (document.layers.size() > static_cast<std::size_t>(kMaximumLayerCount) ||
        document.media.size() > static_cast<std::size_t>(kMaximumMediaCount) ||
        document.bins.size() > static_cast<std::size_t>(kMaximumBinCount)) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document contains too many layers, media items, or bins."));
    }

    std::unordered_set<std::string> bin_paths;
    bin_paths.reserve(document.bins.size());
    for (const auto& bin : document.bins) {
        if (bin.size() > static_cast<std::size_t>(kMaximumStringBytes) ||
            !MediaLibrary::validBinPath(bin) || !bin_paths.insert(bin).second) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The document contains an invalid or duplicate Media Pool bin."));
        }
    }
    if (!bin_paths.contains(std::string(default_bin))) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The Media Pool must contain its default bin."));
    }

    std::unordered_map<std::filesystem::path, MediaKind> media_by_path;
    media_by_path.reserve(document.media.size());
    for (const auto& media : document.media) {
        const auto encoded_source_path = pathToUtf8(media.source_path);
        if (media.source_path.empty() || !media.source_path.is_absolute() ||
            (media.kind != MediaKind::Image && media.kind != MediaKind::Video) ||
            encoded_source_path.size() > static_cast<std::size_t>(kMaximumStringBytes) ||
            media.display_name.size() > static_cast<std::size_t>(kMaximumStringBytes) ||
            media.bin_path.size() > static_cast<std::size_t>(kMaximumStringBytes) ||
            !MediaLibrary::validBinPath(media.bin_path) || !bin_paths.contains(media.bin_path)) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The document contains invalid Media Pool metadata."));
        }
        const auto canonical = MediaLibrary::canonicalPath(media.source_path);
        if (!media_by_path.emplace(canonical, media.kind).second) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The Media Pool contains duplicate source paths."));
        }
    }

    if (document.composition.canvas_size.width <= 0 ||
        document.composition.canvas_size.height <= 0) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The composition canvas dimensions must be positive."));
    }

    std::size_t keyframe_count = 0;
    for (const auto& layer : document.layers) {
        keyframe_count += layer.keyframes.position_x.size() +
            layer.keyframes.position_y.size() + layer.keyframes.scale.size() +
            layer.keyframes.rotation.size() + layer.keyframes.opacity.size();
        if (keyframe_count > static_cast<std::size_t>(kMaximumKeyframeCount)) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The document contains too many keyframes."));
        }
        if (!layer.source_path.empty()) {
            if (pathToUtf8(layer.source_path).size() >
                static_cast<std::size_t>(kMaximumStringBytes)) {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("A layer source path exceeds the supported length."));
            }
            if (!layer.source_path.is_absolute()) {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("Media layer paths must be absolute in the in-memory document."));
            }
            const auto found = media_by_path.find(MediaLibrary::canonicalPath(layer.source_path));
            if (found == media_by_path.end()) {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("A media layer references an item missing from the Media Pool."));
            }
            const auto expected_kind = layer.kind == LayerKind::Image
                ? MediaKind::Image : MediaKind::Video;
            if ((layer.kind != LayerKind::Image && layer.kind != LayerKind::Video) ||
                found->second != expected_kind) {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("A media layer kind does not match its Media Pool item."));
            }
        }
    }

    try {
        (void)CompositionDocument(document.composition.canvas_size.width,
                                  document.composition.canvas_size.height,
                                  document.composition.frame_rate,
                                  document.layers);
    } catch (const std::exception& error) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QString::fromUtf8(error.what()));
    }
}

QJsonObject writeTransform(const creative_suite::animation::Transform2D& transform)
{
    QJsonObject object;
    object.insert(QStringLiteral("position_x"), transform.position_x);
    object.insert(QStringLiteral("position_y"), transform.position_y);
    object.insert(QStringLiteral("scale"), transform.scale);
    object.insert(QStringLiteral("rotation_degrees"), transform.rotation_degrees);
    object.insert(QStringLiteral("opacity"), transform.opacity);
    return object;
}

QJsonArray writeKeyframes(const std::vector<creative_suite::animation::Keyframe>& keyframes)
{
    QJsonArray array;
    for (const auto& keyframe : keyframes) {
        QJsonObject object;
        object.insert(QStringLiteral("frame"), encodedInt64(keyframe.frame));
        object.insert(QStringLiteral("value"), keyframe.value);
        array.append(object);
    }
    return array;
}

QJsonObject writeLayer(const CompositionLayer& layer,
                       const std::filesystem::path& document_path)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), encodedUInt64(layer.id));
    object.insert(QStringLiteral("kind"), layerKindName(layer.kind));
    object.insert(QStringLiteral("name"), stringFromUtf8(layer.name));
    object.insert(QStringLiteral("source_path"), layer.source_path.empty()
        ? QString{} : storedPath(document_path, layer.source_path));
    object.insert(QStringLiteral("timeline_start_frame"), encodedInt64(layer.timeline_start_frame));
    object.insert(QStringLiteral("duration_frames"), encodedInt64(layer.duration_frames));
    object.insert(QStringLiteral("source_frame_count"), encodedInt64(layer.source_frame_count));
    object.insert(QStringLiteral("source_duration_frames"), encodedInt64(layer.source_duration_frames));
    object.insert(QStringLiteral("maximum_timeline_duration_frames"),
                   encodedInt64(layer.maximum_timeline_duration_frames));
    object.insert(QStringLiteral("source_frame_rate"), layer.source_frame_rate);
    object.insert(QStringLiteral("visible"), layer.visible);
    object.insert(QStringLiteral("transform"), writeTransform(layer.transform));
    QJsonObject keyframes;
    keyframes.insert(QStringLiteral("position_x"), writeKeyframes(layer.keyframes.position_x));
    keyframes.insert(QStringLiteral("position_y"), writeKeyframes(layer.keyframes.position_y));
    keyframes.insert(QStringLiteral("scale"), writeKeyframes(layer.keyframes.scale));
    keyframes.insert(QStringLiteral("rotation"), writeKeyframes(layer.keyframes.rotation));
    keyframes.insert(QStringLiteral("opacity"), writeKeyframes(layer.keyframes.opacity));
    object.insert(QStringLiteral("keyframes"), keyframes);
    if (layer.kind == LayerKind::Text) {
        const auto& text = std::get<model::TextLayerContent>(layer.content);
        QJsonObject content;
        content.insert(QStringLiteral("text"), stringFromUtf8(text.text));
        content.insert(QStringLiteral("font_family"), stringFromUtf8(text.font_family));
        content.insert(QStringLiteral("font_size_pixels"), text.font_size_pixels);
        content.insert(QStringLiteral("color"), writeColor(text.color));
        content.insert(QStringLiteral("alignment"), textAlignmentName(text.alignment));
        content.insert(QStringLiteral("box_width"), text.box_width);
        content.insert(QStringLiteral("box_height"), text.box_height);
        object.insert(QStringLiteral("text_content"), content);
    } else if (layer.kind == LayerKind::Shape) {
        const auto& shape = std::get<model::ShapeLayerContent>(layer.content);
        QJsonObject content;
        content.insert(QStringLiteral("primitive"), shapeKindName(shape.shape));
        content.insert(QStringLiteral("width"), shape.width);
        content.insert(QStringLiteral("height"), shape.height);
        content.insert(QStringLiteral("fill_color"), writeColor(shape.fill_color));
        content.insert(QStringLiteral("stroke_color"), writeColor(shape.stroke_color));
        content.insert(QStringLiteral("stroke_width_pixels"), shape.stroke_width_pixels);
        object.insert(QStringLiteral("shape_content"), content);
    }
    return object;
}

std::vector<creative_suite::animation::Keyframe> parseKeyframes(
    const QJsonArray& array,
    const std::filesystem::path& path,
    std::size_t& keyframe_count)
{
    using creative_suite::animation::Keyframe;
    if (keyframe_count + static_cast<std::size_t>(array.size()) >
        static_cast<std::size_t>(kMaximumKeyframeCount)) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document contains too many keyframes."));
    }
    keyframe_count += static_cast<std::size_t>(array.size());
    std::vector<Keyframe> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array) {
        if (!value.isObject()) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("A keyframe entry must be an object."));
        }
        const auto object = value.toObject();
        result.push_back({requiredInt64(object, "frame", path),
                          requiredNumber(object, "value", path)});
    }
    return result;
}

CompositionLayer parseLayer(const QJsonValue& value,
                            const std::filesystem::path& document_path,
                            std::size_t& keyframe_count,
                            int document_version,
                            model::CanvasSize canvas_size)
{
    if (!value.isObject()) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("A layer entry must be an object."));
    }
    const auto object = value.toObject();
    CompositionLayer layer{};
    layer.id = requiredUInt64(object, "id", document_path);
    layer.kind = parseLayerKind(requiredString(object, "kind", document_path), document_path);
    layer.name = requiredString(object, "name", document_path).toUtf8().toStdString();
    if (layer.name.size() > static_cast<std::size_t>(kMaximumStringBytes)) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("A layer name exceeds the supported length."));
    }
    const auto source_path = requiredString(object, "source_path", document_path);
    if (!source_path.isEmpty()) layer.source_path = resolvedPath(document_path, source_path);
    layer.timeline_start_frame = requiredInt64(object, "timeline_start_frame", document_path);
    layer.duration_frames = requiredInt64(object, "duration_frames", document_path);
    layer.source_frame_count = requiredInt64(object, "source_frame_count", document_path);
    layer.source_duration_frames = requiredInt64(object, "source_duration_frames", document_path);
    layer.maximum_timeline_duration_frames = requiredInt64(
        object, "maximum_timeline_duration_frames", document_path);
    layer.source_frame_rate = requiredNumber(object, "source_frame_rate", document_path);
    layer.visible = requiredBool(object, "visible", document_path);

    const auto transform = requiredObject(object, "transform", document_path);
    layer.transform.position_x = requiredNumber(transform, "position_x", document_path);
    layer.transform.position_y = requiredNumber(transform, "position_y", document_path);
    layer.transform.scale = requiredNumber(transform, "scale", document_path);
    layer.transform.rotation_degrees = requiredNumber(transform, "rotation_degrees", document_path);
    layer.transform.opacity = requiredNumber(transform, "opacity", document_path);

    const auto keyframes = requiredObject(object, "keyframes", document_path);
    layer.keyframes.position_x = parseKeyframes(
        requiredArray(keyframes, "position_x", document_path), document_path, keyframe_count);
    layer.keyframes.position_y = parseKeyframes(
        requiredArray(keyframes, "position_y", document_path), document_path, keyframe_count);
    layer.keyframes.scale = parseKeyframes(
        requiredArray(keyframes, "scale", document_path), document_path, keyframe_count);
    layer.keyframes.rotation = parseKeyframes(
        requiredArray(keyframes, "rotation", document_path), document_path, keyframe_count);
    layer.keyframes.opacity = parseKeyframes(
        requiredArray(keyframes, "opacity", document_path), document_path, keyframe_count);
    if (document_version == 1 && layer.kind == LayerKind::Text) {
        layer.content = model::defaultTextLayerContent(canvas_size);
    } else if (document_version == 1 && layer.kind == LayerKind::Shape) {
        layer.content = model::defaultShapeLayerContent(canvas_size);
    } else if (layer.kind == LayerKind::Text) {
        const auto content = requiredObject(object, "text_content", document_path);
        model::TextLayerContent text;
        text.text = requiredString(content, "text", document_path).toUtf8().toStdString();
        text.font_family = requiredString(content, "font_family", document_path)
            .toUtf8().toStdString();
        text.font_size_pixels = requiredInt(content, "font_size_pixels", document_path);
        text.color = parseColor(content.value(QStringLiteral("color")), document_path);
        text.alignment = parseTextAlignment(
            requiredString(content, "alignment", document_path), document_path);
        text.box_width = requiredInt(content, "box_width", document_path);
        text.box_height = requiredInt(content, "box_height", document_path);
        layer.content = std::move(text);
    } else if (layer.kind == LayerKind::Shape) {
        const auto content = requiredObject(object, "shape_content", document_path);
        model::ShapeLayerContent shape;
        shape.shape = parseShapeKind(
            requiredString(content, "primitive", document_path), document_path);
        shape.width = requiredInt(content, "width", document_path);
        shape.height = requiredInt(content, "height", document_path);
        shape.fill_color = parseColor(content.value(QStringLiteral("fill_color")), document_path);
        shape.stroke_color = parseColor(content.value(QStringLiteral("stroke_color")), document_path);
        shape.stroke_width_pixels = requiredInt(content, "stroke_width_pixels", document_path);
        layer.content = shape;
    }
    return layer;
}

MotionProjectData parseDocument(const QJsonObject& root,
                                const std::filesystem::path& document_path)
{
    if (requiredString(root, "format", document_path) !=
        QLatin1String(MotionDocumentStore::format_identifier)) {
        fail(MotionDocumentErrorCode::InvalidFormat, document_path,
             QStringLiteral("The file is not a Motion Studio document."));
    }
    const int version = requiredInt(root, "version", document_path);
    if (version != 1 && version != MotionDocumentStore::current_format_version) {
        fail(MotionDocumentErrorCode::UnsupportedVersion, document_path,
             QStringLiteral("Motion Studio document version %1 is not supported.").arg(version));
    }

    MotionProjectData result;
    const auto composition = requiredObject(root, "composition", document_path);
    const auto canvas = requiredObject(composition, "canvas", document_path);
    result.composition.canvas_size.width = requiredInt(canvas, "width", document_path);
    result.composition.canvas_size.height = requiredInt(canvas, "height", document_path);
    const auto frame_rate = requiredObject(composition, "frame_rate", document_path);
    result.composition.frame_rate.numerator = requiredInt64(
        frame_rate, "numerator", document_path);
    result.composition.frame_rate.denominator = requiredInt64(
        frame_rate, "denominator", document_path);
    const auto canvas_size = result.composition.canvas_size;

    const auto media_pool = requiredObject(root, "media_pool", document_path);
    const auto bins = requiredArray(media_pool, "bins", document_path);
    if (bins.size() > kMaximumBinCount) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("The document contains too many Media Pool bins."));
    }
    result.bins.clear();
    result.bins.reserve(static_cast<std::size_t>(bins.size()));
    for (const auto& value : bins) {
        if (!value.isString()) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("A Media Pool bin path must be a string."));
        }
        result.bins.push_back(value.toString().toUtf8().toStdString());
    }

    const auto media = requiredArray(media_pool, "items", document_path);
    if (media.size() > kMaximumMediaCount) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("The document contains too many Media Pool items."));
    }
    result.media.reserve(static_cast<std::size_t>(media.size()));
    for (const auto& value : media) {
        if (!value.isObject()) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("A Media Pool item must be an object."));
        }
        const auto object = value.toObject();
        MotionMediaEntryData item;
        const auto encoded_path = requiredString(object, "path", document_path);
        if (encoded_path.isEmpty()) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("A Media Pool item must include a source path."));
        }
        item.source_path = resolvedPath(document_path, encoded_path);
        item.kind = parseMediaKind(
            requiredString(object, "kind", document_path), document_path);
        item.display_name = requiredString(object, "name", document_path)
            .toUtf8().toStdString();
        item.bin_path = requiredString(object, "bin", document_path)
            .toUtf8().toStdString();
        result.media.push_back(std::move(item));
    }

    const auto layers = requiredArray(root, "layers", document_path);
    if (layers.size() > kMaximumLayerCount) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("The document contains too many layers."));
    }
    result.layers.reserve(static_cast<std::size_t>(layers.size()));
    std::size_t keyframe_count = 0;
    for (const auto& value : layers) {
        result.layers.push_back(parseLayer(
            value, document_path, keyframe_count, version, canvas_size));
    }
    validateDocument(result, document_path);
    return result;
}

QJsonObject encodeDocument(const MotionProjectData& document,
                           const std::filesystem::path& document_path)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"),
                QString::fromLatin1(MotionDocumentStore::format_identifier));
    root.insert(QStringLiteral("version"), MotionDocumentStore::current_format_version);

    QJsonObject composition;
    QJsonObject canvas;
    canvas.insert(QStringLiteral("width"), document.composition.canvas_size.width);
    canvas.insert(QStringLiteral("height"), document.composition.canvas_size.height);
    composition.insert(QStringLiteral("canvas"), canvas);
    QJsonObject frame_rate;
    frame_rate.insert(QStringLiteral("numerator"),
                      encodedInt64(document.composition.frame_rate.numerator));
    frame_rate.insert(QStringLiteral("denominator"),
                      encodedInt64(document.composition.frame_rate.denominator));
    composition.insert(QStringLiteral("frame_rate"), frame_rate);
    root.insert(QStringLiteral("composition"), composition);

    QJsonObject media_pool;
    QJsonArray bins;
    for (const auto& bin : document.bins) bins.append(stringFromUtf8(bin));
    media_pool.insert(QStringLiteral("bins"), bins);
    QJsonArray media;
    for (const auto& item : document.media) {
        QJsonObject object;
        object.insert(QStringLiteral("path"), storedPath(document_path, item.source_path));
        object.insert(QStringLiteral("kind"), mediaKindName(item.kind));
        object.insert(QStringLiteral("name"), stringFromUtf8(item.display_name));
        object.insert(QStringLiteral("bin"), stringFromUtf8(item.bin_path));
        media.append(object);
    }
    media_pool.insert(QStringLiteral("items"), media);
    root.insert(QStringLiteral("media_pool"), media_pool);

    QJsonArray layers;
    for (const auto& layer : document.layers) {
        layers.append(writeLayer(layer, document_path));
    }
    root.insert(QStringLiteral("layers"), layers);
    return root;
}

} // namespace

MotionDocumentError::MotionDocumentError(MotionDocumentErrorCode code,
                                         std::string message,
                                         std::filesystem::path path,
                                         std::optional<int> system_error)
    : std::runtime_error(std::move(message)),
      code_(code),
      path_(std::move(path)),
      system_error_(system_error)
{
}

MotionProjectData MotionDocumentStore::load(
    const std::filesystem::path& document_path)
{
    if (document_path.empty()) {
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("A document path is required."));
    }
    QFile file(pathToQString(document_path));
    if (!file.open(QIODevice::ReadOnly)) {
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("The Motion Studio document could not be opened for reading."),
             static_cast<int>(file.error()));
    }
    if (file.size() < 0 || file.size() > kMaximumDocumentBytes) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("The Motion Studio document exceeds the supported file size."));
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("The Motion Studio document could not be read completely."),
             static_cast<int>(file.error()));
    }
    QJsonParseError parse_error;
    const auto parsed = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
        fail(MotionDocumentErrorCode::InvalidFormat, document_path,
             QStringLiteral("The Motion Studio document is not valid JSON."));
    }
    return parseDocument(parsed.object(), MediaLibrary::canonicalPath(document_path));
}

void MotionDocumentStore::save(
    const std::filesystem::path& document_path,
    const MotionProjectData& document)
{
    if (document_path.empty()) {
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("A document path is required."));
    }
    validateDocument(document, document_path);
    const auto json = QJsonDocument(encodeDocument(document, document_path))
        .toJson(QJsonDocument::Indented);
    if (json.size() > kMaximumDocumentBytes) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("The Motion Studio document exceeds the supported file size."));
    }

    QSaveFile file(pathToQString(document_path));
    if (!file.open(QIODevice::WriteOnly)) {
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("The Motion Studio document could not be opened for writing."),
             static_cast<int>(file.error()));
    }
    if (file.write(json) != json.size()) {
        const auto error_code = static_cast<int>(file.error());
        file.cancelWriting();
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("The Motion Studio document could not be written completely."),
             error_code);
    }
    if (!file.commit()) {
        fail(MotionDocumentErrorCode::Io, document_path,
             QStringLiteral("The Motion Studio document could not be committed atomically."),
             static_cast<int>(file.error()));
    }
}

MotionRecoveryData MotionDocumentStore::loadRecovery(
    const std::filesystem::path& recovery_path)
{
    if (recovery_path.empty()) {
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("A recovery snapshot path is required."));
    }
    QFile file(pathToQString(recovery_path));
    if (!file.open(QIODevice::ReadOnly)) {
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot could not be opened."),
             static_cast<int>(file.error()));
    }
    if (file.size() < 0 || file.size() > kMaximumDocumentBytes) {
        fail(MotionDocumentErrorCode::InvalidValue, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot exceeds the supported file size."));
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot could not be read completely."),
             static_cast<int>(file.error()));
    }
    QJsonParseError parse_error;
    const auto parsed = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
        fail(MotionDocumentErrorCode::InvalidFormat, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot is not valid JSON."));
    }

    const auto root = parsed.object();
    if (root.value(QStringLiteral("format")).toString() !=
            QLatin1String(kRecoveryFormatIdentifier)) {
        fail(MotionDocumentErrorCode::InvalidFormat, recovery_path,
             QStringLiteral("The file is not a Motion Studio recovery snapshot."));
    }
    const auto version_value = root.value(QStringLiteral("version"));
    if (!version_value.isDouble() ||
        version_value.toDouble() != static_cast<double>(kRecoveryFormatVersion)) {
        fail(MotionDocumentErrorCode::UnsupportedVersion, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot version is not supported."));
    }
    if (!root.value(QStringLiteral("document")).isObject()) {
        fail(MotionDocumentErrorCode::MissingField, recovery_path,
             QStringLiteral("The recovery snapshot does not contain a document."));
    }
    if (!root.value(QStringLiteral("target_document_path")).isString() ||
        !root.value(QStringLiteral("session_id")).isString()) {
        fail(MotionDocumentErrorCode::MissingField, recovery_path,
             QStringLiteral("The recovery snapshot is missing its target or session identity."));
    }

    MotionRecoveryData result;
    const auto target_path = root.value(QStringLiteral("target_document_path")).toString();
    if (!target_path.isEmpty()) {
        result.target_document_path = pathFromQString(target_path);
        if (!result.target_document_path.is_absolute()) {
            fail(MotionDocumentErrorCode::InvalidValue, recovery_path,
                 QStringLiteral("The recovery target path must be absolute."));
        }
        result.target_document_path = MediaLibrary::canonicalPath(result.target_document_path);
    }
    result.session_id = root.value(QStringLiteral("session_id")).toString()
        .toUtf8().toStdString();
    if (result.session_id.empty()) {
        fail(MotionDocumentErrorCode::InvalidValue, recovery_path,
             QStringLiteral("The recovery session identity cannot be empty."));
    }
    const auto path_base = result.target_document_path.empty()
        ? MediaLibrary::canonicalPath(recovery_path)
        : result.target_document_path;
    result.document = parseDocument(
        root.value(QStringLiteral("document")).toObject(), path_base);
    return result;
}

void MotionDocumentStore::saveRecovery(
    const std::filesystem::path& recovery_path,
    const std::filesystem::path& target_document_path,
    const std::string& session_id,
    const MotionProjectData& document)
{
    if (recovery_path.empty()) {
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("A recovery snapshot path is required."));
    }
    if (session_id.empty()) {
        fail(MotionDocumentErrorCode::InvalidValue, recovery_path,
             QStringLiteral("A recovery session identity is required."));
    }

    auto target_path = target_document_path;
    if (!target_path.empty()) target_path = MediaLibrary::canonicalPath(target_path);
    const auto path_base = target_path.empty()
        ? MediaLibrary::canonicalPath(recovery_path)
        : target_path;
    validateDocument(document, path_base);

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kRecoveryFormatIdentifier));
    root.insert(QStringLiteral("version"), kRecoveryFormatVersion);
    root.insert(QStringLiteral("target_document_path"),
                target_path.empty() ? QString{} : pathToQString(target_path));
    root.insert(QStringLiteral("session_id"), QString::fromUtf8(
        session_id.data(), static_cast<qsizetype>(session_id.size())));
    root.insert(QStringLiteral("document"), encodeDocument(document, path_base));
    const auto json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (json.size() > kMaximumDocumentBytes) {
        fail(MotionDocumentErrorCode::InvalidValue, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot exceeds the supported file size."));
    }

    QSaveFile file(pathToQString(recovery_path));
    if (!file.open(QIODevice::WriteOnly)) {
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot could not be opened for writing."),
             static_cast<int>(file.error()));
    }
    if (file.write(json) != json.size()) {
        const auto error_code = static_cast<int>(file.error());
        file.cancelWriting();
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot could not be written completely."),
             error_code);
    }
    if (!file.commit()) {
        fail(MotionDocumentErrorCode::Io, recovery_path,
             QStringLiteral("The Motion Studio recovery snapshot could not be committed atomically."),
             static_cast<int>(file.error()));
    }
}

} // namespace motion::persistence
