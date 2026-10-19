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
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
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
constexpr qsizetype kMaximumKeyframeCount = static_cast<qsizetype>(
    MotionDocumentStore::maximum_keyframe_count);
constexpr qsizetype kMaximumEffectCount = 1'000'000;
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
    case LayerKind::Null: return QStringLiteral("null");
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
    if (value == QLatin1String("null")) return LayerKind::Null;
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

QJsonArray writeKeyframes(const std::vector<creative_suite::animation::Keyframe>& keyframes);
std::vector<creative_suite::animation::Keyframe> parseKeyframes(
    const QJsonArray& array,
    const std::filesystem::path& path,
    std::size_t& keyframe_count,
    int document_version);

QJsonArray writeEffects(const std::vector<model::LayerEffect>& effects)
{
    QJsonArray array;
    for (const auto& effect : effects) {
        QJsonObject object;
        std::visit([&object](const auto& value) {
            using Effect = std::decay_t<decltype(value)>;
            object.insert(QStringLiteral("enabled"), value.enabled);
            if(value.mask)object.insert("mask",encodedInt64(static_cast<std::int64_t>(value.mask)));
            if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                object.insert(QStringLiteral("type"), QStringLiteral("gaussian_blur"));
                object.insert(QStringLiteral("radius_pixels"), value.radius_pixels);
                if (!value.radius_keyframes.empty()) {
                    QJsonObject keyframes;
                    keyframes.insert(QStringLiteral("radius_pixels"),
                                     writeKeyframes(value.radius_keyframes));
                    object.insert(QStringLiteral("keyframes"), keyframes);
                }
            } else {
                object.insert(QStringLiteral("type"), QStringLiteral("color_adjustment"));
                object.insert(QStringLiteral("brightness"), value.brightness);
                object.insert(QStringLiteral("contrast_percent"), value.contrast_percent);
                object.insert(QStringLiteral("saturation_percent"), value.saturation_percent);
                if (!value.brightness_keyframes.empty() || !value.contrast_keyframes.empty() ||
                    !value.saturation_keyframes.empty()) {
                    QJsonObject keyframes;
                    keyframes.insert(QStringLiteral("brightness"),
                                     writeKeyframes(value.brightness_keyframes));
                    keyframes.insert(QStringLiteral("contrast_percent"),
                                     writeKeyframes(value.contrast_keyframes));
                    keyframes.insert(QStringLiteral("saturation_percent"),
                                     writeKeyframes(value.saturation_keyframes));
                    object.insert(QStringLiteral("keyframes"), keyframes);
                }
            }
        }, effect);
        array.append(object);
    }
    return array;
}

std::vector<model::LayerEffect> parseEffects(const QJsonArray& array,
                                            const std::filesystem::path& path,
                                            std::size_t& effect_count,
                                            std::size_t& keyframe_count,
                                            int document_version)
{
    if (effect_count + static_cast<std::size_t>(array.size()) >
        static_cast<std::size_t>(kMaximumEffectCount)) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document contains too many layer effects."));
    }
    effect_count += static_cast<std::size_t>(array.size());
    std::vector<model::LayerEffect> effects;
    effects.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array) {
        if (!value.isObject()) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("A layer effect entry must be an object."));
        }
        const auto object = value.toObject();
        const auto type = requiredString(object, "type", path);
        if(object.contains("mask") && document_version<8)fail(MotionDocumentErrorCode::InvalidValue,path,QStringLiteral("Masks require version 8."));
        if (object.contains(QStringLiteral("keyframes")) && document_version < 9) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("Effect keyframes require version 9."));
        }
        const auto mask=object.contains("mask")?requiredInt64(object,"mask",path):0;
        if(mask<0)fail(MotionDocumentErrorCode::InvalidValue,path,QStringLiteral("Invalid mask reference."));
        const auto enabled = requiredBool(object, "enabled", path);
        if (type == QLatin1String("gaussian_blur")) {
            model::GaussianBlurEffect blur;
            blur.enabled = enabled;
            blur.mask=mask;
            blur.radius_pixels = requiredNumber(object, "radius_pixels", path);
            if (object.contains(QStringLiteral("keyframes"))) {
                const auto keyframes = requiredObject(object, "keyframes", path);
                for (auto it = keyframes.begin(); it != keyframes.end(); ++it) {
                    if (it.key() != QLatin1String("radius_pixels")) {
                        fail(MotionDocumentErrorCode::InvalidValue, path,
                             QStringLiteral("The document contains an unsupported Gaussian Blur keyframe property."));
                    }
                }
                if (keyframes.contains(QStringLiteral("radius_pixels"))) {
                    blur.radius_keyframes = parseKeyframes(
                        requiredArray(keyframes, "radius_pixels", path),
                        path, keyframe_count, document_version);
                }
            }
            effects.emplace_back(std::move(blur));
        } else if (type == QLatin1String("color_adjustment")) {
            model::ColorAdjustmentEffect color;
            color.enabled = enabled;
            color.mask=mask;
            color.brightness = requiredNumber(object, "brightness", path);
            color.contrast_percent = requiredNumber(object, "contrast_percent", path);
            color.saturation_percent = requiredNumber(object, "saturation_percent", path);
            if (object.contains(QStringLiteral("keyframes"))) {
                const auto keyframes = requiredObject(object, "keyframes", path);
                for (auto it = keyframes.begin(); it != keyframes.end(); ++it) {
                    if (it.key() != QLatin1String("brightness") &&
                        it.key() != QLatin1String("contrast_percent") &&
                        it.key() != QLatin1String("saturation_percent")) {
                        fail(MotionDocumentErrorCode::InvalidValue, path,
                             QStringLiteral("The document contains an unsupported Color Adjustment keyframe property."));
                    }
                }
                if (keyframes.contains(QStringLiteral("brightness"))) {
                    color.brightness_keyframes = parseKeyframes(
                        requiredArray(keyframes, "brightness", path),
                        path, keyframe_count, document_version);
                }
                if (keyframes.contains(QStringLiteral("contrast_percent"))) {
                    color.contrast_keyframes = parseKeyframes(
                        requiredArray(keyframes, "contrast_percent", path),
                        path, keyframe_count, document_version);
                }
                if (keyframes.contains(QStringLiteral("saturation_percent"))) {
                    color.saturation_keyframes = parseKeyframes(
                        requiredArray(keyframes, "saturation_percent", path),
                        path, keyframe_count, document_version);
                }
            }
            effects.emplace_back(std::move(color));
        } else {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The document contains an unsupported layer effect."));
        }
    }
    if (!model::validLayerEffects(effects)) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The document contains invalid layer effect parameters."));
    }
    return effects;
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

    if (document.composition.duration < 0 || document.composition.duration >= (std::int64_t(1) << 50) || document.composition.name.size() > static_cast<std::size_t>(kMaximumStringBytes))
        fail(MotionDocumentErrorCode::InvalidValue, path, QStringLiteral("Invalid composition name or duration."));

    if (document.composition.canvas_size.width <= 0 ||
        document.composition.canvas_size.height <= 0) {
        fail(MotionDocumentErrorCode::InvalidValue, path,
             QStringLiteral("The composition canvas dimensions must be positive."));
    }

    std::size_t keyframe_count = 0;
    std::size_t effect_count = 0;
    std::unordered_set<std::filesystem::path> linked_image_paths;
    for (const auto& layer : document.layers) {
        keyframe_count += layer.keyframes.position_x.size() +
            layer.keyframes.position_y.size() + layer.keyframes.scale.size() +
            layer.keyframes.rotation.size() + layer.keyframes.opacity.size();
        keyframe_count += layer.anchor_keyframes[0].size() + layer.anchor_keyframes[1].size();
        if(layer.scale_y)keyframe_count+=layer.scale_y->keyframes.size();
        for (const auto& effect : layer.effects) {
            std::visit([&keyframe_count](const auto& value) {
                using Effect = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                    keyframe_count += value.radius_keyframes.size();
                } else {
                    keyframe_count += value.brightness_keyframes.size() +
                        value.contrast_keyframes.size() +
                        value.saturation_keyframes.size();
                }
            }, effect);
        }
        if (keyframe_count > static_cast<std::size_t>(kMaximumKeyframeCount)) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The document contains too many keyframes."));
        }
        effect_count += layer.effects.size();
        if (effect_count > static_cast<std::size_t>(kMaximumEffectCount)) {
            fail(MotionDocumentErrorCode::InvalidValue, path,
                 QStringLiteral("The document contains too many layer effects."));
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
        if (layer.linked_image.has_value()) {
            const auto& link = *layer.linked_image;
            const std::array canonical_link_paths{
                MediaLibrary::canonicalPath(link.document_path),
                MediaLibrary::canonicalPath(link.published_output_path),
                MediaLibrary::canonicalPath(link.source_snapshot_path)};
            if (layer.kind != LayerKind::Image || link.document_path.empty() ||
                link.published_output_path.empty() || link.source_snapshot_path.empty() ||
                !link.document_path.is_absolute() ||
                !link.published_output_path.is_absolute() ||
                !link.source_snapshot_path.is_absolute() ||
                pathToUtf8(link.document_path).size() >
                    static_cast<std::size_t>(kMaximumStringBytes) ||
                pathToUtf8(link.published_output_path).size() >
                    static_cast<std::size_t>(kMaximumStringBytes) ||
                pathToUtf8(link.source_snapshot_path).size() >
                    static_cast<std::size_t>(kMaximumStringBytes) ||
                MediaLibrary::canonicalPath(link.document_path) ==
                    MediaLibrary::canonicalPath(link.published_output_path) ||
                MediaLibrary::canonicalPath(link.document_path) ==
                    MediaLibrary::canonicalPath(link.source_snapshot_path) ||
                MediaLibrary::canonicalPath(link.published_output_path) ==
                    MediaLibrary::canonicalPath(link.source_snapshot_path) ||
                MediaLibrary::canonicalPath(link.document_path) ==
                    MediaLibrary::canonicalPath(layer.source_path) ||
                MediaLibrary::canonicalPath(link.published_output_path) ==
                    MediaLibrary::canonicalPath(layer.source_path) ||
                MediaLibrary::canonicalPath(link.source_snapshot_path) ==
                    MediaLibrary::canonicalPath(layer.source_path) ||
                std::any_of(canonical_link_paths.begin(), canonical_link_paths.end(),
                    [&media_by_path](const auto& linked_path) {
                        return media_by_path.contains(linked_path);
                    }) ||
                std::any_of(canonical_link_paths.begin(), canonical_link_paths.end(),
                    [&linked_image_paths](const auto& linked_path) {
                        return !linked_image_paths.insert(linked_path).second;
                    })) {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("A linked Image Editor layer has invalid or shared paths or kind."));
            }
        }
    }

    for(const auto& mask:document.masks)for(const auto& keys:mask.keyframes) {
        keyframe_count+=keys.size();
        if(keyframe_count>static_cast<std::size_t>(kMaximumKeyframeCount))fail(MotionDocumentErrorCode::InvalidValue,path,QStringLiteral("Too many keyframes."));
    }
    try {
        (void)CompositionDocument(document.composition.canvas_size.width,
                                  document.composition.canvas_size.height,
                                  document.composition.frame_rate,
                                  document.layers, document.masks);
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
        object.insert(QStringLiteral("interpolation"),
            keyframe.interpolation == creative_suite::animation::InterpolationMode::CubicBezier
                ? QStringLiteral("cubic_bezier") : QStringLiteral("linear"));
        QJsonObject easing;
        easing.insert(QStringLiteral("x1"), keyframe.easing.x1);
        easing.insert(QStringLiteral("y1"), keyframe.easing.y1);
        easing.insert(QStringLiteral("x2"), keyframe.easing.x2);
        easing.insert(QStringLiteral("y2"), keyframe.easing.y2);
        object.insert(QStringLiteral("easing"), easing);
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
    if(layer.blend_mode!=creative_suite::composition::BlendMode::Normal)
        object.insert("blend_mode",QString::fromLatin1(creative_suite::composition::blendModeIdentifier(layer.blend_mode).data()));
    if (layer.anchor_point != model::AnchorPoint{} || !layer.anchor_keyframes[0].empty() || !layer.anchor_keyframes[1].empty()) {
        QJsonObject anchor{{"x_pixels",layer.anchor_point.x_pixels},{"y_pixels",layer.anchor_point.y_pixels}};
        QJsonObject keys;
        if (!layer.anchor_keyframes[0].empty()) keys.insert("x_pixels",writeKeyframes(layer.anchor_keyframes[0]));
        if (!layer.anchor_keyframes[1].empty()) keys.insert("y_pixels",writeKeyframes(layer.anchor_keyframes[1]));
        if (!keys.isEmpty()) anchor.insert("keyframes",keys);
        object.insert("anchor_point",anchor);
    }
    if(layer.scale_y) {
        QJsonObject channel{{"value",layer.scale_y->value}};
        if(!layer.scale_y->keyframes.empty())channel.insert("keyframes",writeKeyframes(layer.scale_y->keyframes));
        object.insert("scale_y",channel);
    }
    if(!layer.scale_linked)object.insert("scale_linked",false);
    if(layer.parent_affine) {
        const auto& m=*layer.parent_affine;
        object.insert("parent_affine",QJsonObject{{"a",m.a},{"b",m.b},{"c",m.c},{"d",m.d},{"tx",m.tx},{"ty",m.ty}});
    }
    if(layer.track_matte)object.insert("track_matte",QJsonObject{
        {"source_id",encodedUInt64(layer.track_matte->source_id)},
        {"mode",QString::fromStdString(std::string(creative_suite::composition::trackMatteModeIdentifier(layer.track_matte->mode)))}});
    if (layer.parent_id) object.insert("parent_id", encodedUInt64(layer.parent_id));
    if (layer.parent_offset != model::ParentTransformOffset{}) {
        const auto& offset = layer.parent_offset;
        object.insert("parent_offset", QJsonObject{
            {"translation_x_pixels", offset.translation_x_pixels},
            {"translation_y_pixels", offset.translation_y_pixels},
            {"scale", offset.scale}, {"rotation_degrees", offset.rotation_degrees}});
    }
    object.insert(QStringLiteral("name"), stringFromUtf8(layer.name));
    if (layer.presentation) object.insert("presentation", creative_suite::composition_exchange::presentationJson(*layer.presentation));
    object.insert(QStringLiteral("source_path"), layer.source_path.empty()
        ? QString{} : storedPath(document_path, layer.source_path));
    if (layer.linked_image.has_value()) {
        QJsonObject linked_image;
        linked_image.insert(QStringLiteral("document_path"),
            storedPath(document_path, layer.linked_image->document_path));
        linked_image.insert(QStringLiteral("published_output_path"),
            storedPath(document_path, layer.linked_image->published_output_path));
        linked_image.insert(QStringLiteral("source_snapshot_path"),
            storedPath(document_path, layer.linked_image->source_snapshot_path));
        object.insert(QStringLiteral("linked_image"), linked_image);
    }
    object.insert(QStringLiteral("timeline_start_frame"), encodedInt64(layer.timeline_start_frame));
    object.insert(QStringLiteral("duration_frames"), encodedInt64(layer.duration_frames));
    object.insert(QStringLiteral("source_frame_count"), encodedInt64(layer.source_frame_count));
    object.insert(QStringLiteral("source_start_frame"), encodedInt64(layer.source_start_frame));
    object.insert(QStringLiteral("source_duration_frames"), encodedInt64(layer.source_duration_frames));
    object.insert(QStringLiteral("maximum_timeline_duration_frames"),
                   encodedInt64(layer.maximum_timeline_duration_frames));
    object.insert(QStringLiteral("source_frame_rate"), layer.source_frame_rate);
    object.insert(QStringLiteral("visible"), layer.visible);
    if(layer.source_mask)object.insert("source_mask",encodedInt64(static_cast<std::int64_t>(layer.source_mask)));
    if(layer.transform_mask)object.insert("transform_mask",encodedInt64(static_cast<std::int64_t>(layer.transform_mask)));
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
    object.insert(QStringLiteral("effects"), writeEffects(layer.effects));
    return object;
}

std::vector<creative_suite::animation::Keyframe> parseKeyframes(
    const QJsonArray& array,
    const std::filesystem::path& path,
    std::size_t& keyframe_count,
    int document_version)
{
    using creative_suite::animation::Keyframe;
    using creative_suite::animation::InterpolationMode;
    using creative_suite::animation::validKeyframeInterpolation;
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
        Keyframe keyframe;
        keyframe.frame = requiredInt64(object, "frame", path);
        keyframe.value = requiredNumber(object, "value", path);
        if (document_version >= 3) {
            const auto interpolation = requiredString(object, "interpolation", path);
            if (interpolation == QLatin1String("linear")) {
                keyframe.interpolation = InterpolationMode::Linear;
            } else if (interpolation == QLatin1String("cubic_bezier")) {
                keyframe.interpolation = InterpolationMode::CubicBezier;
            } else {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("A keyframe interpolation mode is not supported."));
            }
            const auto easing = requiredObject(object, "easing", path);
            keyframe.easing = {
                requiredNumber(easing, "x1", path),
                requiredNumber(easing, "y1", path),
                requiredNumber(easing, "x2", path),
                requiredNumber(easing, "y2", path)};
            if (!validKeyframeInterpolation(keyframe.interpolation, keyframe.easing)) {
                fail(MotionDocumentErrorCode::InvalidValue, path,
                     QStringLiteral("A keyframe contains invalid easing controls."));
            }
        }
        result.push_back(keyframe);
    }
    return result;
}

CompositionLayer parseLayer(const QJsonValue& value,
                            const std::filesystem::path& document_path,
                            std::size_t& keyframe_count,
                            std::size_t& effect_count,
                            int document_version,
                            model::CanvasSize canvas_size)
{
    if (!value.isObject()) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("A layer entry must be an object."));
    }
    const auto object = value.toObject();
    CompositionLayer layer{};
    if (object.contains("presentation")) {
        if (document_version < 7 || !object.value("presentation").isObject())
            fail(MotionDocumentErrorCode::InvalidValue, document_path, QStringLiteral("Presentation metadata requires format version 7."));
        layer.presentation = creative_suite::composition_exchange::parsePresentation(object.value("presentation").toObject());
    }
    layer.id = requiredUInt64(object, "id", document_path);
    layer.kind = parseLayerKind(requiredString(object, "kind", document_path), document_path);
    if(object.contains("blend_mode")) {
        if(document_version<13)fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Blend modes require document version 13."));
        const auto name=requiredString(object,"blend_mode",document_path).toStdString();
        const auto mode=creative_suite::composition::parseBlendMode(name);
        if(!mode || (layer.kind==model::LayerKind::Null && *mode!=creative_suite::composition::BlendMode::Normal))
            fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Invalid layer blend mode."));
        layer.blend_mode=*mode;
    }
    if(document_version<12 && (object.contains("scale_y") || object.contains("scale_linked") || object.contains("parent_affine")))
        fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Axis scale and affine bindings require document version 12."));
    if(object.contains("scale_y")) {
        const auto channel=requiredObject(object,"scale_y",document_path);
        for(auto it=channel.begin();it!=channel.end();++it)if(it.key()!="value" && it.key()!="keyframes")
            fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Unknown Y scale field."));
        layer.scale_y=model::ScaleChannel{requiredNumber(channel,"value",document_path),{}};
        if(channel.contains("keyframes"))layer.scale_y->keyframes=parseKeyframes(requiredArray(channel,"keyframes",document_path),document_path,keyframe_count,document_version);
    }
    if(object.contains("scale_linked"))layer.scale_linked=requiredBool(object,"scale_linked",document_path);
    if(object.contains("parent_affine")) {
        if(object.contains("parent_offset"))fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Conflicting parent compensation representations."));
        const auto m=requiredObject(object,"parent_affine",document_path);
        for(auto it=m.begin();it!=m.end();++it)if(it.key()!="a" && it.key()!="b" && it.key()!="c" && it.key()!="d" && it.key()!="tx" && it.key()!="ty")
            fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Unknown affine binding field."));
        layer.parent_affine=creative_suite::composition::AffineTransform2D{requiredNumber(m,"a",document_path),requiredNumber(m,"b",document_path),requiredNumber(m,"c",document_path),requiredNumber(m,"d",document_path),requiredNumber(m,"tx",document_path),requiredNumber(m,"ty",document_path)};
    }
    if (object.contains("anchor_point")) {
        if (document_version < 11) fail(MotionDocumentErrorCode::InvalidValue,document_path,
            QStringLiteral("Anchor points require document version 11."));
        const auto anchor = requiredObject(object,"anchor_point",document_path);
        for (auto it=anchor.begin();it!=anchor.end();++it)
            if(it.key()!="x_pixels" && it.key()!="y_pixels" && it.key()!="keyframes")
                fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Unknown anchor point field."));
        layer.anchor_point={requiredNumber(anchor,"x_pixels",document_path),requiredNumber(anchor,"y_pixels",document_path)};
        if (anchor.contains("keyframes")) {
            const auto keys=requiredObject(anchor,"keyframes",document_path);
            for(auto it=keys.begin();it!=keys.end();++it)
                if(it.key()!="x_pixels" && it.key()!="y_pixels")
                    fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Unknown anchor keyframe property."));
            if(keys.contains("x_pixels"))layer.anchor_keyframes[0]=parseKeyframes(requiredArray(keys,"x_pixels",document_path),document_path,keyframe_count,document_version);
            if(keys.contains("y_pixels"))layer.anchor_keyframes[1]=parseKeyframes(requiredArray(keys,"y_pixels",document_path),document_path,keyframe_count,document_version);
        }
    }
    if (document_version < 10 && (layer.kind == LayerKind::Null ||
        object.contains("parent_id") || object.contains("parent_offset")))
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("Null layers and parenting require document version 10."));
    if(object.contains("track_matte")) {
        if(document_version<14)fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Track mattes require document version 14."));
        const auto matte=requiredObject(object,"track_matte",document_path);
        for(auto it=matte.begin();it!=matte.end();++it)if(it.key()!="source_id" && it.key()!="mode")
            fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Unknown track matte field."));
        const auto mode=creative_suite::composition::parseTrackMatteMode(requiredString(matte,"mode",document_path).toStdString());
        if(!mode)fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Invalid track matte mode."));
        layer.track_matte=model::TrackMatteBinding{requiredUInt64(matte,"source_id",document_path),*mode};
    }
    if (object.contains("parent_id")) layer.parent_id = requiredUInt64(object, "parent_id", document_path);
    if (object.contains("parent_offset")) {
        if (!object.value("parent_offset").isObject())
            fail(MotionDocumentErrorCode::InvalidValue, document_path, QStringLiteral("Invalid parent compensation."));
        const auto offset = object.value("parent_offset").toObject();
        layer.parent_offset = {requiredNumber(offset, "translation_x_pixels", document_path),
            requiredNumber(offset, "translation_y_pixels", document_path),
            requiredNumber(offset, "scale", document_path),
            requiredNumber(offset, "rotation_degrees", document_path)};
    }
    layer.name = requiredString(object, "name", document_path).toUtf8().toStdString();
    if (layer.name.size() > static_cast<std::size_t>(kMaximumStringBytes)) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("A layer name exceeds the supported length."));
    }
    const auto source_path = requiredString(object, "source_path", document_path);
    if (!source_path.isEmpty()) layer.source_path = resolvedPath(document_path, source_path);
    if (document_version >= 6 && object.contains(QStringLiteral("linked_image"))) {
        if (!object.value(QStringLiteral("linked_image")).isObject()) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("A linked Image Editor reference must be an object."));
        }
        const auto linked = object.value(QStringLiteral("linked_image")).toObject();
        model::LinkedImageDocument reference;
        reference.document_path = resolvedPath(
            document_path, requiredString(linked, "document_path", document_path));
        reference.published_output_path = resolvedPath(
            document_path, requiredString(linked, "published_output_path", document_path));
        reference.source_snapshot_path = resolvedPath(
            document_path, requiredString(linked, "source_snapshot_path", document_path));
        layer.linked_image = std::move(reference);
    }
    layer.timeline_start_frame = requiredInt64(object, "timeline_start_frame", document_path);
    layer.duration_frames = requiredInt64(object, "duration_frames", document_path);
    layer.source_frame_count = requiredInt64(object, "source_frame_count", document_path);
    if (document_version >= 5) {
        layer.source_start_frame = requiredInt64(object, "source_start_frame", document_path);
    }
    layer.source_duration_frames = requiredInt64(object, "source_duration_frames", document_path);
    layer.maximum_timeline_duration_frames = requiredInt64(
        object, "maximum_timeline_duration_frames", document_path);
    layer.source_frame_rate = requiredNumber(object, "source_frame_rate", document_path);
    layer.visible = requiredBool(object, "visible", document_path);
    const auto read_mask=[&](const char* key) -> model::MaskId {
        if(!object.contains(key))return 0;
        if(document_version<8)fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Masks require version 8."));
        const auto id=requiredInt64(object,key,document_path);if(id<0)fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Invalid mask reference."));return id;
    };
    layer.source_mask=read_mask("source_mask");layer.transform_mask=read_mask("transform_mask");

    const auto transform = requiredObject(object, "transform", document_path);
    layer.transform.position_x = requiredNumber(transform, "position_x", document_path);
    layer.transform.position_y = requiredNumber(transform, "position_y", document_path);
    layer.transform.scale = requiredNumber(transform, "scale", document_path);
    layer.transform.rotation_degrees = requiredNumber(transform, "rotation_degrees", document_path);
    layer.transform.opacity = requiredNumber(transform, "opacity", document_path);

    const auto keyframes = requiredObject(object, "keyframes", document_path);
    layer.keyframes.position_x = parseKeyframes(
        requiredArray(keyframes, "position_x", document_path), document_path,
        keyframe_count, document_version);
    layer.keyframes.position_y = parseKeyframes(
        requiredArray(keyframes, "position_y", document_path), document_path,
        keyframe_count, document_version);
    layer.keyframes.scale = parseKeyframes(
        requiredArray(keyframes, "scale", document_path), document_path,
        keyframe_count, document_version);
    layer.keyframes.rotation = parseKeyframes(
        requiredArray(keyframes, "rotation", document_path), document_path,
        keyframe_count, document_version);
    layer.keyframes.opacity = parseKeyframes(
        requiredArray(keyframes, "opacity", document_path), document_path,
        keyframe_count, document_version);
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
        text.font_size_pixels = document_version >= 7 ? requiredNumber(content, "font_size_pixels", document_path) : requiredInt(content, "font_size_pixels", document_path);
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
    if (document_version >= 4) {
        layer.effects = parseEffects(requiredArray(object, "effects", document_path),
                                     document_path, effect_count, keyframe_count,
                                     document_version);
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
    if (version < 1 || version > MotionDocumentStore::current_format_version) {
        fail(MotionDocumentErrorCode::UnsupportedVersion, document_path,
             QStringLiteral("Motion Studio document version %1 is not supported.").arg(version));
    }

    MotionProjectData result;
    if (version >= 5) {
        result.document_revision = requiredInt64(root, "revision", document_path);
        if (result.document_revision < 0) {
            fail(MotionDocumentErrorCode::InvalidValue, document_path,
                 QStringLiteral("The Motion Studio revision must not be negative."));
        }
    }
    const auto composition = requiredObject(root, "composition", document_path);
    if (version >= 7) {
        result.composition.name = requiredString(composition, "name", document_path).toUtf8().toStdString();
        result.composition.duration = requiredInt64(composition, "duration", document_path);
        if (result.composition.duration < 0) fail(MotionDocumentErrorCode::InvalidValue, document_path, QStringLiteral("Invalid composition duration."));
    }
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

    if(root.contains("masks")) {
        if(version<8 || !root.value("masks").isArray())fail(MotionDocumentErrorCode::InvalidValue,document_path,QStringLiteral("Masks require version 8."));
        try {result.masks=creative_suite::composition_exchange::parseMasksJson(root.value("masks").toArray());}
        catch(const std::exception& e){fail(MotionDocumentErrorCode::InvalidValue,document_path,QString::fromUtf8(e.what()));}
    }
    const auto layers = requiredArray(root, "layers", document_path);
    if (layers.size() > kMaximumLayerCount) {
        fail(MotionDocumentErrorCode::InvalidValue, document_path,
             QStringLiteral("The document contains too many layers."));
    }
    result.layers.reserve(static_cast<std::size_t>(layers.size()));
    std::size_t keyframe_count = 0;
    std::size_t effect_count = 0;
    for (const auto& value : layers) {
        result.layers.push_back(parseLayer(
            value, document_path, keyframe_count, effect_count, version, canvas_size));
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
    root.insert(QStringLiteral("revision"), encodedInt64(document.document_revision));

    QJsonObject composition;
    composition.insert("name", stringFromUtf8(document.composition.name));
    composition.insert("duration", encodedInt64(document.composition.duration));
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
    if(!document.masks.empty())root.insert("masks",creative_suite::composition_exchange::masksJson(document.masks));
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
