#include "model/composition_document.h"
#include "model/motion_project_data.h"
#include "persistence/motion_document_store.h"

#include <creative_suite/media/media_library.h>

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
    require(document.setLayerVisible(still_id, false), "visibility is stored");

    VideoMetadata video;
    video.kind = MediaKind::Video;
    video.source_path = external_video;
    video.display_name = "Source video";
    video.frame_rate = 24.0;
    video.frame_count = 48;
    LayerId video_id = 0;
    require(document.addMediaLayer(video, 20, &video_id) == AddMediaLayerResult::Added,
            "video layer can be built for project serialization");
    require(document.moveLayer(video_id, 0), "layer order can be changed before saving");

    project.layers = document.layers();
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
            "layers, transforms, keyframes, media, bins, Unicode and unused items round-trip");
    require(round_trip.layers.size() == 2 && round_trip.layers[0].id == populated.layers[0].id &&
                round_trip.layers[1].id == populated.layers[1].id,
            "layer IDs and back-to-front ordering remain stable");
    const auto json = readBytes(document_path);
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
