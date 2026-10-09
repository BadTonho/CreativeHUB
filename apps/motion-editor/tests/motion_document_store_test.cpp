#include "model/composition_document.h"
#include "model/motion_project_data.h"
#include "persistence/motion_document_store.h"

#include <creative_suite/media/media_library.h>
#include <creative_suite/motion_handoff/request.h>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<typename Function>
motion::persistence::MotionDocumentErrorCode requireDocumentError(
    Function&& function,
    const char* message)
{
    try {
        function();
    } catch (const motion::persistence::MotionDocumentError& error) {
        return error.code();
    }
    require(false, message);
    return motion::persistence::MotionDocumentErrorCode::InvalidValue;
}

void writeBytes(const std::filesystem::path& path, const QByteArray& bytes)
{
    const auto encoded_path = path.u8string();
    QFile file(QString::fromUtf8(reinterpret_cast<const char*>(encoded_path.data()),
                                 static_cast<qsizetype>(encoded_path.size())));
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "test file opens for writing");
    require(file.write(bytes) == bytes.size(), "test file bytes are written");
}

QByteArray readBytes(const std::filesystem::path& path)
{
    const auto encoded_path = path.u8string();
    QFile file(QString::fromUtf8(reinterpret_cast<const char*>(encoded_path.data()),
                                 static_cast<qsizetype>(encoded_path.size())));
    require(file.open(QIODevice::ReadOnly), "test file opens for reading");
    return file.readAll();
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(first, first + bytes.size()));
}

motion::model::MotionProjectData populatedProject(const std::filesystem::path& root)
{
    using namespace motion::model;
    using creative_suite::media::MediaKind;
    using creative_suite::media::MediaLibrary;
    using creative_suite::media::VideoMetadata;

    const auto inside_image = MediaLibrary::canonicalPath(root / "assets" / u8"still-é.png");
    const auto external_video = MediaLibrary::canonicalPath(root.parent_path() / u8"outside-影片.mkv");
    const auto unused_image = MediaLibrary::canonicalPath(root.parent_path() / "unused.png");

    MotionProjectData project;
    project.composition = {{1920, 1080}, {30000, 1001}};
    project.bins = {"Unsorted", "Assets", "Assets/Stills", "Footage", "Footage/Unused"};
    project.media = {
        {inside_image, MediaKind::Image, "Renamed still", "Assets/Stills"},
        {external_video, MediaKind::Video, "External clip", "Footage"},
        {unused_image, MediaKind::Image, "Unused media", "Footage/Unused"},
    };

    CompositionDocument document(1920, 1080, {30000, 1001});
    VideoMetadata image;
    image.kind = MediaKind::Image;
    image.source_path = inside_image;
    image.display_name = "Still";
    LayerId still_id = 0;
    require(document.addMediaLayer(image, 0, &still_id) == AddMediaLayerResult::Added,
            "still layer can be built for project serialization");
    auto transform = document.layers().back().transform;
    transform.position_x = 0.25;
    transform.position_y = 0.75;
    transform.scale = 1.5;
    transform.rotation_degrees = 17.25;
    transform.opacity = 0.65;
    require(document.setLayerTransform(still_id, transform), "non-default transform is stored");
    require(document.setLayerKeyframe(still_id, creative_suite::animation::TransformProperty::PositionX,
                                      0, 0.25), "first keyframe is stored");
    require(document.setLayerKeyframe(still_id, creative_suite::animation::TransformProperty::PositionX,
                                      29, 0.5), "second keyframe is stored");
    require(document.setLayerKeyframeInterpolation(
                still_id, creative_suite::animation::TransformProperty::PositionX, 0,
                creative_suite::animation::InterpolationMode::CubicBezier,
                {0.3, 0.1, 0.7, 0.9}),
            "a custom outgoing curve is stored");
    require(document.setLayerVisible(still_id, false), "visibility is stored");
    require(document.setLayerEffects(still_id, {
                GaussianBlurEffect{true, 4.5},
                ColorAdjustmentEffect{false, 12.0, 125.0, 80.0}}),
            "image effect stack can be built for persistence");

    VideoMetadata video;
    video.kind = MediaKind::Video;
    video.source_path = external_video;
    video.display_name = "Source video";
    video.frame_rate = 24.0;
    video.frame_count = 48;
    LayerId video_id = 0;
    require(document.addMediaLayer(video, 20, &video_id) == AddMediaLayerResult::Added,
            "video layer can be built for project serialization");
    require(document.setVideoSourceRange(video_id, 6, 30, 53),
            "a video source-in can be retained before serialization");
    require(document.moveLayer(video_id, 0), "layer order can be changed before saving");
    require(document.setLayerEffects(video_id, {
                ColorAdjustmentEffect{true, -15.0, 110.0, 140.0}}),
            "video color adjustment can be built for persistence");

    LayerId text_id = 0;
    require(document.addContentLayer(LayerKind::Text, "Title", 45, &text_id),
            "text content layer can be built for project serialization");
    auto text_content = std::get<TextLayerContent>(document.layers().back().content);
    text_content.text = "Hello \xE4\xB8\x96\xE7\x95\x8C\nMotion Studio";
    text_content.font_family = "Sans Serif";
    text_content.alignment = TextAlignment::Right;
    text_content.color = {10, 20, 30, 220};
    require(document.setTextLayerContent(text_id, text_content),
            "text appearance and Unicode content are stored");
    require(document.setLayerEffects(text_id, {GaussianBlurEffect{false, 0.0}}),
            "disabled effects are retained on text layers");

    LayerId ellipse_id = 0;
    require(document.addContentLayer(LayerKind::Shape, "Ellipse", 90, &ellipse_id),
            "shape content layer can be built for project serialization");
    auto shape_content = std::get<ShapeLayerContent>(document.layers().back().content);
    shape_content.shape = ShapeKind::Ellipse;
    shape_content.width = 311;
    shape_content.height = 157;
    shape_content.fill_color = {120, 80, 40, 190};
    shape_content.stroke_color = {255, 200, 10, 255};
    shape_content.stroke_width_pixels = 7;
    require(document.setShapeLayerContent(ellipse_id, shape_content),
            "shape primitive, dimensions, fill, and stroke are stored");
    require(document.setLayerEffects(ellipse_id, {
                GaussianBlurEffect{true, 18.0}, GaussianBlurEffect{true, 2.0}}),
            "repeated effect types retain their order on shape layers");

    project.layers = document.layers();
    const auto image_layer = std::find_if(project.layers.begin(), project.layers.end(),
        [](const auto& layer) { return layer.kind == LayerKind::Image; });
    require(image_layer != project.layers.end(), "the project contains an image layer");
    image_layer->linked_image = LinkedImageDocument{
        MediaLibrary::canonicalPath(root / "linked assets" / u8"edição.cimg"),
        MediaLibrary::canonicalPath(root / "linked assets" / u8"publicado final.png"),
        MediaLibrary::canonicalPath(root / "linked assets" / u8"origem.png")};
    return project;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary_directory;
    require(temporary_directory.isValid(), "temporary project directory is available");
    const auto root = pathFromQString(temporary_directory.path());
    const auto document_path = root / "composition.motion";

    motion::model::MotionProjectData empty;
    empty.composition = {{640, 360}, {24000, 1001}};
    motion::persistence::MotionDocumentStore::save(document_path, empty);
    require(motion::persistence::MotionDocumentStore::load(document_path) == empty,
            "an empty composition round-trips with its exact fractional rate");

    auto populated = populatedProject(root);
    motion::persistence::MotionDocumentStore::save(document_path, populated);
    const auto round_trip = motion::persistence::MotionDocumentStore::load(document_path);
    require(round_trip == populated,
            "layers, transforms, curves, media, bins, Unicode and unused items round-trip");
    const auto round_trip_curve_layer = std::find_if(
        round_trip.layers.begin(), round_trip.layers.end(), [](const auto& layer) {
            return !layer.keyframes.position_x.empty();
        });
    require(round_trip_curve_layer != round_trip.layers.end() &&
                round_trip_curve_layer->keyframes.position_x.front().interpolation ==
                creative_suite::animation::InterpolationMode::CubicBezier &&
                round_trip_curve_layer->keyframes.position_x.front().easing ==
                    creative_suite::animation::CubicBezierEasing{0.3, 0.1, 0.7, 0.9},
            "custom interpolation and controls survive the native document round trip");
    require(round_trip.layers.size() == 4 && round_trip.layers[0].id == populated.layers[0].id &&
                round_trip.layers[1].id == populated.layers[1].id &&
                round_trip.layers[2].id == populated.layers[2].id &&
                round_trip.layers[3].id == populated.layers[3].id,
            "layer IDs and back-to-front ordering remain stable");
    const auto round_trip_video = std::find_if(
        round_trip.layers.begin(), round_trip.layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Video;
        });
    require(round_trip_video != round_trip.layers.end() &&
                round_trip_video->source_start_frame == 6 &&
                round_trip_video->duration_frames == 30 &&
                round_trip_video->maximum_timeline_duration_frames == 53,
            "video source-in, selected duration, and available source tail round-trip");
    const auto json = readBytes(document_path);
    require(QJsonDocument::fromJson(json).object().value(QStringLiteral("version")).toInt() == 6,
            "documents with linked images are written using schema version 6");
    const auto round_trip_image = std::find_if(
        round_trip.layers.begin(), round_trip.layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Image;
        });
    const auto populated_image = std::find_if(
        populated.layers.begin(), populated.layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Image;
        });
    require(round_trip_image != round_trip.layers.end() &&
                populated_image != populated.layers.end() &&
                round_trip_image->linked_image == populated_image->linked_image,
            "per-layer Image Editor paths round-trip with Unicode and spaces");
    require(json.contains("assets/still-é.png") || json.contains("assets/still-Ã©.png"),
            "a source beneath the document directory is encoded as a relative path");
    require(json.contains("outside-影片.mkv") || json.contains("outside-\xE5\xBD\xB1\xE7\x89\x87.mkv"),
            "an external source remains represented by an absolute path");

    motion::model::CompositionDocument restored(
        round_trip.composition.canvas_size.width, round_trip.composition.canvas_size.height,
        round_trip.composition.frame_rate, round_trip.layers);
    const auto new_id = restored.addLayer(motion::model::LayerKind::Shape, "Next");
    require(new_id > round_trip.layers[0].id && new_id > round_trip.layers[1].id,
            "reconstruction advances the ID allocator beyond restored layer IDs");

    const QByteArray prior_contents("existing document must survive validation failure");
    writeBytes(document_path, prior_contents);
    auto invalid = populated;
    invalid.composition.canvas_size.width = 0;
    requireDocumentError([&] {
        motion::persistence::MotionDocumentStore::save(document_path, invalid);
    }, "invalid project data is rejected before it can replace a document");
    require(readBytes(document_path) == prior_contents,
            "failed save leaves the existing file untouched");

    motion::persistence::MotionDocumentStore::save(document_path, populated);
    const auto valid_json = QJsonDocument::fromJson(readBytes(document_path)).object();
    const auto requireInvalidLoad = [&](QJsonObject invalid_json, const char* message) {
        writeBytes(document_path, QJsonDocument(invalid_json).toJson());
        require(requireDocumentError([&] {
            (void)motion::persistence::MotionDocumentStore::load(document_path);
        }, message) == motion::persistence::MotionDocumentErrorCode::InvalidValue, message);
    };

    auto aliased_image_link = valid_json;
    auto aliased_layers = aliased_image_link.value(QStringLiteral("layers")).toArray();
    for (qsizetype index = 0; index < aliased_layers.size(); ++index) {
        auto layer = aliased_layers[index].toObject();
        if (layer.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
            auto link = layer.value(QStringLiteral("linked_image")).toObject();
            link.insert(QStringLiteral("published_output_path"),
                        layer.value(QStringLiteral("source_path")));
            layer.insert(QStringLiteral("linked_image"), link);
            aliased_layers[index] = layer;
            break;
        }
    }
    aliased_image_link.insert(QStringLiteral("layers"), aliased_layers);
    requireInvalidLoad(aliased_image_link,
        "a linked PNG cannot alias the original image source");

    auto shared_image_link = valid_json;
    auto shared_layers = shared_image_link.value(QStringLiteral("layers")).toArray();
    QJsonObject linked_image_layer;
    for (const auto& value : shared_layers) {
        const auto layer = value.toObject();
        if (layer.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
            linked_image_layer = layer;
            break;
        }
    }
    require(!linked_image_layer.isEmpty(), "the fixture contains a linked image layer");
    linked_image_layer.insert(QStringLiteral("id"), QStringLiteral("987654321"));
    linked_image_layer.insert(QStringLiteral("name"), QStringLiteral("Second Poster"));
    shared_layers.append(linked_image_layer);
    shared_image_link.insert(QStringLiteral("layers"), shared_layers);
    requireInvalidLoad(shared_image_link,
        "two Motion image layers cannot share one linked Image Editor sidecar");

    auto legacy_v1 = valid_json;
    legacy_v1.insert(QStringLiteral("version"), 1);
    auto legacy_layers = legacy_v1.value(QStringLiteral("layers")).toArray();
    for (qsizetype index = 0; index < legacy_layers.size(); ++index) {
        auto layer = legacy_layers[index].toObject();
        if (layer.value(QStringLiteral("kind")).toString() == QLatin1String("text"))
            layer.remove(QStringLiteral("text_content"));
        if (layer.value(QStringLiteral("kind")).toString() == QLatin1String("shape"))
            layer.remove(QStringLiteral("shape_content"));
        legacy_layers[index] = layer;
    }
    legacy_v1.insert(QStringLiteral("layers"), legacy_layers);
    writeBytes(document_path, QJsonDocument(legacy_v1).toJson());
    const auto migrated_v1 = motion::persistence::MotionDocumentStore::load(document_path);
    const auto migrated_text = std::find_if(
        migrated_v1.layers.begin(), migrated_v1.layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Text;
        });
    const auto migrated_shape = std::find_if(
        migrated_v1.layers.begin(), migrated_v1.layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Shape;
        });
    require(migrated_text != migrated_v1.layers.end() &&
                std::get<motion::model::TextLayerContent>(migrated_text->content) ==
                    motion::model::defaultTextLayerContent({1920, 1080}) &&
                migrated_shape != migrated_v1.layers.end() &&
                std::get<motion::model::ShapeLayerContent>(migrated_shape->content) ==
                    motion::model::defaultShapeLayerContent({1920, 1080}),
            "v1 text and shape records migrate to documented default content");
    motion::persistence::MotionDocumentStore::save(document_path, migrated_v1);
    const auto saved_legacy_v1 = motion::persistence::MotionDocumentStore::load(document_path);
    require(QJsonDocument::fromJson(readBytes(document_path)).object()
                .value(QStringLiteral("version")).toInt() == 6 &&
                std::none_of(saved_legacy_v1.layers.begin(), saved_legacy_v1.layers.end(),
                    [](const auto& layer) { return layer.linked_image.has_value(); }),
            "saving v1 upgrades to v6 while legacy links remain absent");

    auto legacy_v2 = valid_json;
    legacy_v2.insert(QStringLiteral("version"), 2);
    auto legacy_v2_layers = legacy_v2.value(QStringLiteral("layers")).toArray();
    for (qsizetype layer_index = 0; layer_index < legacy_v2_layers.size(); ++layer_index) {
        auto layer = legacy_v2_layers[layer_index].toObject();
        auto keyframes = layer.value(QStringLiteral("keyframes")).toObject();
        for (const auto* property : {"position_x", "position_y", "scale", "rotation", "opacity"}) {
            const auto property_name = QString::fromLatin1(property);
            auto values = keyframes.value(property_name).toArray();
            for (qsizetype key_index = 0; key_index < values.size(); ++key_index) {
                auto key = values[key_index].toObject();
                key.remove(QStringLiteral("interpolation"));
                key.remove(QStringLiteral("easing"));
                values[key_index] = key;
            }
            keyframes.insert(property_name, values);
        }
        layer.insert(QStringLiteral("keyframes"), keyframes);
        legacy_v2_layers[layer_index] = layer;
    }
    legacy_v2.insert(QStringLiteral("layers"), legacy_v2_layers);
    writeBytes(document_path, QJsonDocument(legacy_v2).toJson());
    const auto migrated_v2 = motion::persistence::MotionDocumentStore::load(document_path);
    const auto migrated_curve_layer = std::find_if(
        migrated_v2.layers.begin(), migrated_v2.layers.end(), [](const auto& layer) {
            return !layer.keyframes.position_x.empty();
        });
    require(migrated_curve_layer != migrated_v2.layers.end() &&
                std::all_of(migrated_curve_layer->keyframes.position_x.begin(),
                    migrated_curve_layer->keyframes.position_x.end(), [](const auto& key) {
                        return key.interpolation ==
                            creative_suite::animation::InterpolationMode::Linear;
                    }),
            "version 2 keys migrate with Linear interpolation");
    motion::persistence::MotionDocumentStore::save(document_path, migrated_v2);
    require(QJsonDocument::fromJson(readBytes(document_path)).object()
                .value(QStringLiteral("version")).toInt() == 6,
            "saving a loaded v2 project upgrades it to v6");

    auto legacy_v3 = valid_json;
    legacy_v3.insert(QStringLiteral("version"), 3);
    auto legacy_v3_layers = legacy_v3.value(QStringLiteral("layers")).toArray();
    for (qsizetype index = 0; index < legacy_v3_layers.size(); ++index) {
        auto layer = legacy_v3_layers[index].toObject();
        layer.remove(QStringLiteral("effects"));
        legacy_v3_layers[index] = layer;
    }
    legacy_v3.insert(QStringLiteral("layers"), legacy_v3_layers);
    writeBytes(document_path, QJsonDocument(legacy_v3).toJson());
    const auto migrated_v3 = motion::persistence::MotionDocumentStore::load(document_path);
    require(std::all_of(migrated_v3.layers.begin(), migrated_v3.layers.end(),
                [](const auto& layer) { return layer.effects.empty(); }) &&
                std::any_of(migrated_v3.layers.begin(), migrated_v3.layers.end(),
                    [](const auto& layer) {
                        return !layer.keyframes.position_x.empty() &&
                            layer.keyframes.position_x.front().interpolation ==
                                creative_suite::animation::InterpolationMode::CubicBezier;
                    }),
            "version 3 documents retain curves and migrate with empty effect stacks");
    motion::persistence::MotionDocumentStore::save(document_path, migrated_v3);
    require(QJsonDocument::fromJson(readBytes(document_path)).object()
                .value(QStringLiteral("version")).toInt() == 6,
            "saving a loaded v3 project upgrades it to v6");

    auto legacy_v4 = valid_json;
    legacy_v4.insert(QStringLiteral("version"), 4);
    legacy_v4.remove(QStringLiteral("revision"));
    auto legacy_v4_layers = legacy_v4.value(QStringLiteral("layers")).toArray();
    for (qsizetype index = 0; index < legacy_v4_layers.size(); ++index) {
        auto layer = legacy_v4_layers[index].toObject();
        layer.remove(QStringLiteral("source_start_frame"));
        legacy_v4_layers[index] = layer;
    }
    legacy_v4.insert(QStringLiteral("layers"), legacy_v4_layers);
    writeBytes(document_path, QJsonDocument(legacy_v4).toJson());
    const auto migrated_v4 = motion::persistence::MotionDocumentStore::load(document_path);
    const auto migrated_v4_video = std::find_if(
        migrated_v4.layers.begin(), migrated_v4.layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Video;
        });
    require(migrated_v4_video != migrated_v4.layers.end() &&
                migrated_v4_video->source_start_frame == 0,
            "version 4 video layers migrate with a zero source-in frame");
    motion::persistence::MotionDocumentStore::save(document_path, migrated_v4);
    require(QJsonDocument::fromJson(readBytes(document_path)).object()
                .value(QStringLiteral("version")).toInt() == 6,
            "saving a loaded v4 project upgrades it to v6");

    auto legacy_v5 = valid_json;
    legacy_v5.insert(QStringLiteral("version"), 5);
    writeBytes(document_path, QJsonDocument(legacy_v5).toJson());
    const auto migrated_v5 = motion::persistence::MotionDocumentStore::load(document_path);
    require(std::none_of(migrated_v5.layers.begin(), migrated_v5.layers.end(),
                [](const auto& layer) { return layer.linked_image.has_value(); }),
            "v5 documents ignore v6-only linked image metadata");
    motion::persistence::MotionDocumentStore::save(document_path, migrated_v5);
    require(QJsonDocument::fromJson(readBytes(document_path)).object()
                .value(QStringLiteral("version")).toInt() == 6,
            "saving a loaded v5 project upgrades it to v6");

    creative_suite::motion_handoff::Request request;
    request.origin_kind = QStringLiteral("timeline_clip");
    request.source_kind = QStringLiteral("video");
    request.source_path = QString::fromUtf8("C:/Mídia/clipe final.mp4");
    request.document_path = QStringLiteral("C:/Projeto/cena.motion");
    request.published_output_path = QStringLiteral("C:/Projeto/render final.mp4");
    request.container = QStringLiteral("mp4");
    request.codec = QStringLiteral("libx264");
    request.quality = QStringLiteral("Balanced (10 Mbps)");
    request.source_start_frame = 17;
    request.timeline_duration_frames = 45;
    request.source_duration_frames = 56;
    request.source_frame_count = 240;
    creative_suite::motion_handoff::Request parsed_request;
    require(creative_suite::motion_handoff::Request::parse(
                request.toJson(), &parsed_request) &&
                parsed_request.source_path == request.source_path &&
                parsed_request.published_output_path == request.published_output_path &&
                parsed_request.source_start_frame == 17 &&
                parsed_request.timeline_duration_frames == 45,
            "the versioned Motion handoff preserves Unicode paths and source timing");
    auto unsupported_request = request.toJson();
    unsupported_request.insert(QStringLiteral("version"), 2);
    require(!creative_suite::motion_handoff::Request::parse(
                unsupported_request, &parsed_request),
            "unknown Motion handoff versions are rejected");
    auto image_clip_request = request.toJson();
    image_clip_request.insert(QStringLiteral("source_kind"), QStringLiteral("image"));
    require(!creative_suite::motion_handoff::Request::parse(
                image_clip_request, &parsed_request),
            "timeline clip handoffs reject image sources");
    auto out_of_range_request = request.toJson();
    out_of_range_request.insert(QStringLiteral("source_start_frame"), 240);
    require(!creative_suite::motion_handoff::Request::parse(
                out_of_range_request, &parsed_request),
            "Motion handoffs reject source-in frames beyond the source video");

    motion::persistence::MotionDocumentStore::save(document_path, populated);
    auto invalid_text = valid_json;
    auto invalid_content_layers = invalid_text.value(QStringLiteral("layers")).toArray();
    auto text_layer = std::find_if(invalid_content_layers.begin(), invalid_content_layers.end(),
        [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("kind")).toString() ==
                QLatin1String("text");
        });
    require(text_layer != invalid_content_layers.end(), "serialized text layer exists");
    auto text_layer_object = text_layer->toObject();
    auto text_payload = text_layer_object.value(QStringLiteral("text_content")).toObject();
    text_payload.insert(QStringLiteral("box_width"), 0);
    text_layer_object.insert(QStringLiteral("text_content"), text_payload);
    *text_layer = text_layer_object;
    invalid_text.insert(QStringLiteral("layers"), invalid_content_layers);
    requireInvalidLoad(invalid_text, "invalid text-box dimensions are rejected");

    auto invalid_curve = valid_json;
    auto invalid_curve_layers = invalid_curve.value(QStringLiteral("layers")).toArray();
    auto curve_layer = std::find_if(invalid_curve_layers.begin(), invalid_curve_layers.end(),
        [](const QJsonValue& value) {
            return !value.toObject().value(QStringLiteral("keyframes")).toObject()
                .value(QStringLiteral("position_x")).toArray().isEmpty();
        });
    require(curve_layer != invalid_curve_layers.end(), "serialized curve layer exists");
    auto curve_layer_object = curve_layer->toObject();
    auto curve_keyframes = curve_layer_object.value(QStringLiteral("keyframes")).toObject();
    auto curve_keys = curve_keyframes.value(QStringLiteral("position_x")).toArray();
    auto curve_key = curve_keys.at(0).toObject();
    auto easing = curve_key.value(QStringLiteral("easing")).toObject();
    easing.insert(QStringLiteral("x1"), 0.9);
    easing.insert(QStringLiteral("x2"), 0.1);
    curve_key.insert(QStringLiteral("easing"), easing);
    curve_keys[0] = curve_key;
    curve_keyframes.insert(QStringLiteral("position_x"), curve_keys);
    curve_layer_object.insert(QStringLiteral("keyframes"), curve_keyframes);
    *curve_layer = curve_layer_object;
    invalid_curve.insert(QStringLiteral("layers"), invalid_curve_layers);
    requireInvalidLoad(invalid_curve, "invalid easing controls are rejected");

    auto invalid_color = valid_json;
    auto invalid_color_layers = invalid_color.value(QStringLiteral("layers")).toArray();
    text_layer = std::find_if(invalid_color_layers.begin(), invalid_color_layers.end(),
        [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("kind")).toString() ==
                QLatin1String("text");
        });
    require(text_layer != invalid_color_layers.end(), "text layer exists for color validation");
    text_layer_object = text_layer->toObject();
    text_payload = text_layer_object.value(QStringLiteral("text_content")).toObject();
    auto encoded_color = text_payload.value(QStringLiteral("color")).toArray();
    encoded_color.replace(3, 256);
    text_payload.insert(QStringLiteral("color"), encoded_color);
    text_layer_object.insert(QStringLiteral("text_content"), text_payload);
    *text_layer = text_layer_object;
    invalid_color.insert(QStringLiteral("layers"), invalid_color_layers);
    requireInvalidLoad(invalid_color, "RGBA channels outside the byte range are rejected");

    auto invalid_shape = valid_json;
    auto invalid_shape_layers = invalid_shape.value(QStringLiteral("layers")).toArray();
    auto shape_layer = std::find_if(invalid_shape_layers.begin(), invalid_shape_layers.end(),
        [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("kind")).toString() ==
                QLatin1String("shape");
        });
    require(shape_layer != invalid_shape_layers.end(), "serialized shape layer exists");
    auto shape_layer_object = shape_layer->toObject();
    auto shape_payload = shape_layer_object.value(QStringLiteral("shape_content")).toObject();
    shape_payload.insert(QStringLiteral("stroke_width_pixels"), -1);
    shape_layer_object.insert(QStringLiteral("shape_content"), shape_payload);
    *shape_layer = shape_layer_object;
    invalid_shape.insert(QStringLiteral("layers"), invalid_shape_layers);
    requireInvalidLoad(invalid_shape, "negative shape stroke widths are rejected");

    auto invalid_effect = valid_json;
    auto invalid_effect_layers = invalid_effect.value(QStringLiteral("layers")).toArray();
    auto effect_layer = invalid_effect_layers[0].toObject();
    QJsonObject blur;
    blur.insert(QStringLiteral("type"), QStringLiteral("gaussian_blur"));
    blur.insert(QStringLiteral("enabled"), true);
    blur.insert(QStringLiteral("radius_pixels"), 100.1);
    effect_layer.insert(QStringLiteral("effects"), QJsonArray{blur});
    invalid_effect_layers[0] = effect_layer;
    invalid_effect.insert(QStringLiteral("layers"), invalid_effect_layers);
    requireInvalidLoad(invalid_effect, "out-of-range effect parameters are rejected on load");

    auto invalid_dimensions = valid_json;
    auto invalid_composition = invalid_dimensions.value(QStringLiteral("composition")).toObject();
    auto invalid_canvas = invalid_composition.value(QStringLiteral("canvas")).toObject();
    invalid_canvas.insert(QStringLiteral("width"), 0);
    invalid_composition.insert(QStringLiteral("canvas"), invalid_canvas);
    invalid_dimensions.insert(QStringLiteral("composition"), invalid_composition);
    requireInvalidLoad(invalid_dimensions, "non-positive canvas dimensions are rejected on load");

    auto invalid_rate = valid_json;
    invalid_composition = invalid_rate.value(QStringLiteral("composition")).toObject();
    auto encoded_rate = invalid_composition.value(QStringLiteral("frame_rate")).toObject();
    encoded_rate.insert(QStringLiteral("numerator"), QStringLiteral("2997"));
    encoded_rate.insert(QStringLiteral("denominator"), QStringLiteral("100"));
    invalid_composition.insert(QStringLiteral("frame_rate"), encoded_rate);
    invalid_rate.insert(QStringLiteral("composition"), invalid_composition);
    requireInvalidLoad(invalid_rate, "unsupported exact frame rates are rejected on load");

    auto duplicate_ids_json = valid_json;
    auto encoded_layers = duplicate_ids_json.value(QStringLiteral("layers")).toArray();
    auto first_layer = encoded_layers[0].toObject();
    auto second_layer = encoded_layers[1].toObject();
    second_layer.insert(QStringLiteral("id"), first_layer.value(QStringLiteral("id")));
    encoded_layers[1] = second_layer;
    duplicate_ids_json.insert(QStringLiteral("layers"), encoded_layers);
    requireInvalidLoad(duplicate_ids_json, "duplicate layer IDs are rejected on load");

    auto invalid_timing_json = valid_json;
    encoded_layers = invalid_timing_json.value(QStringLiteral("layers")).toArray();
    first_layer = encoded_layers[0].toObject();
    first_layer.insert(QStringLiteral("duration_frames"), QStringLiteral("-1"));
    encoded_layers[0] = first_layer;
    invalid_timing_json.insert(QStringLiteral("layers"), encoded_layers);
    requireInvalidLoad(invalid_timing_json, "invalid layer timing is rejected on load");

    auto escaping_path_json = valid_json;
    auto encoded_pool = escaping_path_json.value(QStringLiteral("media_pool")).toObject();
    auto encoded_media = encoded_pool.value(QStringLiteral("items")).toArray();
    auto first_media = encoded_media[0].toObject();
    first_media.insert(QStringLiteral("path"), QStringLiteral("../escape.png"));
    encoded_media[0] = first_media;
    encoded_pool.insert(QStringLiteral("items"), encoded_media);
    escaping_path_json.insert(QStringLiteral("media_pool"), encoded_pool);
    requireInvalidLoad(escaping_path_json,
                       "relative media paths cannot escape the document directory");

    writeBytes(document_path, QByteArray("{ this is not json"));
    require(requireDocumentError([&] {
        (void)motion::persistence::MotionDocumentStore::load(document_path);
    }, "malformed JSON is rejected") ==
                motion::persistence::MotionDocumentErrorCode::InvalidFormat,
            "malformed JSON reports a format error");

    motion::persistence::MotionDocumentStore::save(document_path, empty);
    auto future_json = QJsonDocument::fromJson(readBytes(document_path)).object();
    future_json.insert(QStringLiteral("version"), 999);
    const auto future_version_bytes = QJsonDocument(future_json).toJson();
    writeBytes(document_path, future_version_bytes);
    require(requireDocumentError([&] {
        (void)motion::persistence::MotionDocumentStore::load(document_path);
    }, "future versions are rejected") ==
                motion::persistence::MotionDocumentErrorCode::UnsupportedVersion,
            "future versions have a typed error and are never rewritten");
    require(readBytes(document_path) == future_version_bytes,
            "opening a future version leaves its source file untouched");

    auto duplicate_ids = populated;
    duplicate_ids.layers[1].id = duplicate_ids.layers[0].id;
    requireDocumentError([&] {
        motion::persistence::MotionDocumentStore::save(document_path, duplicate_ids);
    }, "duplicate persisted layer IDs are rejected");
    auto invalid_keyframes = populated;
    const auto keyed_layer = std::find_if(
        invalid_keyframes.layers.begin(), invalid_keyframes.layers.end(), [](const auto& layer) {
            return layer.keyframes.position_x.size() >= 2;
        });
    require(keyed_layer != invalid_keyframes.layers.end(), "a persisted layer has multiple keyframes");
    keyed_layer->keyframes.position_x[1].frame = 0;
    requireDocumentError([&] {
        motion::persistence::MotionDocumentStore::save(document_path, invalid_keyframes);
    }, "unsorted duplicate keyframes are rejected");

    std::cout << "Motion document store tests passed\n";
    return EXIT_SUCCESS;
}
