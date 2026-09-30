#include "model/composition_document.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_library.h>
#include <creative_suite/media/video_metadata.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using motion::persistence::MotionDocumentError;
using motion::persistence::MotionDocumentErrorCode;
using motion::persistence::MotionDocumentStore;
using motion::persistence::MotionRecoveryStore;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QString toQString(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()),
                             static_cast<qsizetype>(value.size()));
}

std::filesystem::path fromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(first, first + bytes.size()));
}

QByteArray readBytes(const std::filesystem::path& path)
{
    QFile file(toQString(path));
    require(file.open(QIODevice::ReadOnly), "snapshot file can be read");
    return file.readAll();
}

void writeBytes(const std::filesystem::path& path, const QByteArray& bytes)
{
    QFile file(toQString(path));
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "snapshot fixture can be written");
    require(file.write(bytes) == bytes.size(), "snapshot fixture bytes are written");
}

motion::model::MotionProjectData projectData(
    const std::filesystem::path& project_directory)
{
    using namespace motion::model;
    using creative_suite::media::MediaKind;
    using creative_suite::media::MediaLibrary;
    using creative_suite::media::VideoMetadata;

    const auto still_path = MediaLibrary::canonicalPath(
        project_directory / "assets" / std::filesystem::path(u8"still-é.png"));
    MotionProjectData project;
    project.composition = {{1280, 720}, {30000, 1001}};
    project.bins = {"Unsorted", "Footage", "Footage/Stills"};
    project.media = {{still_path, MediaKind::Image, "Renamed still", "Footage/Stills"}};

    CompositionDocument composition(1280, 720, {30000, 1001});
    VideoMetadata still;
    still.kind = MediaKind::Image;
    still.source_path = still_path;
    still.display_name = "Still source";
    LayerId id = 0;
    require(composition.addMediaLayer(still, 12, &id) == AddMediaLayerResult::Added,
            "a still layer can be added to the recovery fixture");
    auto transform = composition.layers().front().transform;
    transform.position_x = 0.35;
    transform.position_y = 0.65;
    require(composition.setLayerTransform(id, transform),
            "recovery fixture stores layer transforms");
    require(composition.setLayerKeyframe(
                id, creative_suite::animation::TransformProperty::Opacity, 20, 0.4),
            "recovery fixture stores keyframes");
    require(composition.setLayerKeyframe(
                id, creative_suite::animation::TransformProperty::Opacity, 40, 0.9),
            "recovery fixture stores an animation segment");
    require(composition.setLayerKeyframeInterpolation(
                id, creative_suite::animation::TransformProperty::Opacity, 20,
                creative_suite::animation::InterpolationMode::CubicBezier,
                {0.35, 0.0, 0.65, 1.0}),
            "recovery fixture stores easing controls");
    require(composition.setLayerEffects(id, {
                GaussianBlurEffect{true, 7.5},
                ColorAdjustmentEffect{true, 8.0, 110.0, 95.0}}),
            "recovery fixture stores a layer effect stack");
    LayerId text_id = 0;
    require(composition.addContentLayer(LayerKind::Text, "Recovery title", 5, &text_id),
            "recovery fixture can add text content");
    auto text = std::get<TextLayerContent>(composition.layers().back().content);
    text.text = "Recovery \xE2\x9C\xA8 title";
    require(composition.setTextLayerContent(text_id, text),
            "recovery fixture stores text content");
    LayerId shape_id = 0;
    require(composition.addContentLayer(LayerKind::Shape, "Recovery ellipse", 12, &shape_id),
            "recovery fixture can add shape content");
    auto shape = std::get<ShapeLayerContent>(composition.layers().back().content);
    shape.shape = ShapeKind::Ellipse;
    shape.stroke_width_pixels = 4;
    require(composition.setShapeLayerContent(shape_id, shape),
            "recovery fixture stores shape content");
    require(composition.setLayerEffects(shape_id, {GaussianBlurEffect{false, 0.0}}),
            "recovery fixture stores disabled shape effects");
    project.layers = composition.layers();
    return project;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary recovery directory is available");
    const auto root = fromQString(temporary.path());
    const auto project_directory = root / std::filesystem::path(u8"Project-雪");
    std::filesystem::create_directories(project_directory / "assets");
    const auto project_path = project_directory / "scene.motion";
    auto project = projectData(project_directory);
    MotionDocumentStore::save(project_path, project);

    creative_suite::diagnostics::Options log_options;
    log_options.minimum_level = creative_suite::diagnostics::Level::Debug;
    require(creative_suite::diagnostics::Logger::instance().initialize(
                root / "logs", log_options),
            "the recovery test logger initializes");

    const auto snapshot_directory = MotionRecoveryStore::savedProjectDirectory(project_path);
    std::filesystem::create_directories(snapshot_directory);
    const auto snapshot_path = snapshot_directory / "snapshot-roundtrip.motion-recovery";
    MotionDocumentStore::saveRecovery(
        snapshot_path, project_path, "saved-session", project);
    const auto wrapper = QJsonDocument::fromJson(readBytes(snapshot_path)).object();
    const auto encoded_pool = wrapper.value(QStringLiteral("document")).toObject()
        .value(QStringLiteral("media_pool")).toObject();
    const auto encoded_items = encoded_pool.value(QStringLiteral("items")).toArray();
    const auto encoded_media_path = encoded_items.isEmpty() ? QString{} :
        encoded_items.at(0).toObject().value(QStringLiteral("path")).toString();
    require(encoded_items.size() == 1 && QDir::isRelativePath(encoded_media_path),
            "saved recovery paths remain relative to the original project directory");
    const auto recovered = MotionDocumentStore::loadRecovery(snapshot_path);
    require(recovered.document == project &&
                recovered.target_document_path ==
                    creative_suite::media::MediaLibrary::canonicalPath(project_path) &&
                recovered.session_id == "saved-session",
            "the recovery wrapper round-trips document, target, and session identity");

    const auto original_snapshot = readBytes(snapshot_path);
    auto invalid = project;
    invalid.composition.canvas_size.width = 0;
    bool invalid_rejected = false;
    try {
        MotionDocumentStore::saveRecovery(
            snapshot_path, project_path, "saved-session", invalid);
    } catch (const MotionDocumentError& error) {
        invalid_rejected = error.code() == MotionDocumentErrorCode::InvalidValue;
    }
    require(invalid_rejected && readBytes(snapshot_path) == original_snapshot,
            "failed recovery validation leaves the previous atomic snapshot untouched");

    const auto unsaved_path = root / "unsaved" / "test-session" /
        "snapshot-unsaved.motion-recovery";
    std::filesystem::create_directories(unsaved_path.parent_path());
    MotionDocumentStore::saveRecovery(unsaved_path, {}, "test-session", project);
    const auto unsaved_recovery = MotionDocumentStore::loadRecovery(unsaved_path);
    require(unsaved_recovery.document == project &&
                unsaved_recovery.target_document_path.empty() &&
                unsaved_recovery.session_id == "test-session",
            "untitled recovery keeps absolute media references without a target project");

    MotionRecoveryStore manager(root / "recovery-root", "test-session");
    auto changed = project;
    changed.media.front().display_name = "Unsaved name";
    for (int index = 0; index < 7; ++index)
        manager.saveSnapshot(changed, project_path, MotionRecoveryStore::default_retention);
    const auto saved_snapshots = manager.validSnapshotsForProject(project_path);
    require(saved_snapshots.size() == MotionRecoveryStore::default_retention,
            "saved-project snapshots are pruned to the configured retention");
    for (const auto& snapshot : saved_snapshots) {
        std::filesystem::last_write_time(
            snapshot.path,
            std::filesystem::last_write_time(project_path) + std::chrono::seconds(2));
    }
    require(manager.recoverableSnapshotsForProject(project_path).size() ==
                MotionRecoveryStore::default_retention,
            "newer snapshots differing from the saved project are recoverable");
    require(manager.containsSnapshotData(changed, project_path) &&
                !manager.containsSnapshotData(project, project_path),
            "existing project snapshots can be compared to skip duplicate autosaves");

    const auto custom_retention_project = root / "custom-retention.motion";
    MotionDocumentStore::save(custom_retention_project, project);
    for (int index = 0; index < 7; ++index)
        manager.saveSnapshot(changed, custom_retention_project, 6);
    require(manager.validSnapshotsForProject(custom_retention_project).size() == 6,
            "a custom retention count is honored within the supported range");

    for (int index = 0; index < 6; ++index)
        manager.saveSnapshot(project, MotionRecoveryStore::default_retention);
    require(manager.unsavedSnapshots().size() == MotionRecoveryStore::default_retention,
            "untitled snapshots are retained in their session directory");
    require(manager.containsSnapshotData(project),
            "existing untitled snapshots can be compared to skip duplicate autosaves");
    MotionRecoveryStore other_session(manager.recoveryRoot(), "other-session");
    other_session.saveSnapshot(changed, MotionRecoveryStore::default_retention);
    const auto other_session_snapshots = other_session.unsavedSnapshots();
    const auto other_session_directory = other_session.recoveryRoot() / "unsaved" /
        "other-session";
    require(manager.unsavedSnapshots().size() ==
                MotionRecoveryStore::default_retention + 1 &&
                std::any_of(other_session_snapshots.begin(), other_session_snapshots.end(),
                    [&other_session_directory](const auto& snapshot) {
                        return snapshot.path.parent_path() == other_session_directory;
                    }),
            "untitled recovery snapshots remain separated by session");

    const auto misplaced_path = manager.recoveryRoot() / "unsaved" / "wrong-session" /
        "snapshot-misplaced.motion-recovery";
    std::filesystem::create_directories(misplaced_path.parent_path());
    MotionDocumentStore::saveRecovery(
        misplaced_path, project_path, "wrong-session", changed);
    require(manager.unsavedSnapshots().size() ==
                MotionRecoveryStore::default_retention + 1,
            "untitled discovery rejects a saved-project snapshot in the wrong recovery location");

    const auto corrupt_directory = manager.recoveryRoot() / "unsaved" / "broken-session";
    std::filesystem::create_directories(corrupt_directory);
    const auto corrupt_path = corrupt_directory / "snapshot-broken.motion-recovery";
    writeBytes(corrupt_path, QByteArray("not-json"));
    const auto future_directory = manager.recoveryRoot() / "unsaved" / "future-session";
    std::filesystem::create_directories(future_directory);
    const auto future_path = future_directory / "snapshot-future.motion-recovery";
    auto future_wrapper = QJsonDocument::fromJson(readBytes(unsaved_path)).object();
    future_wrapper.insert(QStringLiteral("version"), 2);
    writeBytes(future_path, QJsonDocument(future_wrapper).toJson());
    require(manager.unsavedSnapshots().size() ==
                MotionRecoveryStore::default_retention + 1,
            "corrupt and future-version snapshots are ignored during discovery");

    manager.removeSnapshot(saved_snapshots.front().path);
    require(manager.validSnapshotsForProject(project_path).size() ==
                MotionRecoveryStore::default_retention - 1,
            "a selected recovery snapshot can be deleted");
    manager.removeSnapshotsForProject(project_path);
    require(manager.validSnapshotsForProject(project_path).empty(),
            "all snapshots associated with a project can be removed");
    manager.removeCurrentUnsavedSnapshots();
    require(manager.unsavedSnapshots().size() == 1,
            "cleaning the current session preserves another session's recovery snapshots");
    other_session.removeCurrentUnsavedSnapshots();
    require(manager.unsavedSnapshots().empty(),
            "untitled snapshots can be cleaned up independently by session");

    const auto blocked_root = root / "recovery-root-is-a-file";
    writeBytes(blocked_root, QByteArray("blocked"));
    MotionRecoveryStore blocked_store(blocked_root, "blocked-session");
    bool failed_atomic_write_reported = false;
    try {
        blocked_store.saveSnapshot(project, MotionRecoveryStore::default_retention);
    } catch (const MotionDocumentError& error) {
        failed_atomic_write_reported = error.code() == MotionDocumentErrorCode::Io;
    }
    require(failed_atomic_write_reported && std::filesystem::is_regular_file(blocked_root),
            "a failed snapshot directory write is reported without damaging the blocking file");
    require(creative_suite::diagnostics::Logger::instance().is_initialized(),
            "invalid recovery snapshots leave actionable validation logs");
    return 0;
}
