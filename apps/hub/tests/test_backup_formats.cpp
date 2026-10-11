#include "../src/model/backup_manager.h"
#include "../src/model/activity_manager.h"
#include "../src/diagnostics/hub_logger.h"
#include "../../../cmake/test_support/test_check.h"

#ifdef HUB_BACKUP_IMAGE_CONTRACT
#include "../../image-editor/src/core/document/image_document_store.h"
#endif
#ifdef HUB_BACKUP_VIDEO_CONTRACT
#include "../../video-editor/src/project/project_file.h"
#endif
#ifdef HUB_BACKUP_MOTION_CONTRACT
#include "../../motion-editor/src/persistence/motion_document_store.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <filesystem>
#include <iostream>

namespace {

std::filesystem::path fsPath(const QString& path) { return QFileInfo(path).filesystemFilePath(); }

QByteArray readBytes(const QString& path) {
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    CS_TEST_CHECK(opened);
    return file.readAll();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    const auto written = file.write(bytes);
    CS_TEST_CHECK(written == bytes.size());
}

struct Snapshot { QString vault; QString restored; };

Snapshot backup(const QString& root, const QString& source) {
    const auto original = readBytes(source);
    creative_suite::hub::BackupManager manager(QDir(root).filePath("vault"));
    QString path;
    QString error;
    const bool copied = manager.createBackup(source, &path, &error);
    if (!copied) std::cerr << error.toStdString() << '\n';
    CS_TEST_CHECK(copied);
    CS_TEST_CHECK(error.isEmpty());
    CS_TEST_CHECK(readBytes(source) == original);
    const auto originalJson = QJsonDocument::fromJson(original).object();
    const auto snapshotJson = QJsonDocument::fromJson(readBytes(path)).object();
    CS_TEST_CHECK(snapshotJson.value("format") == originalJson.value("format"));
    CS_TEST_CHECK(snapshotJson.value("version") == originalJson.value("version"));
    CS_TEST_CHECK(snapshotJson.value("extra_note") == originalJson.value("extra_note"));
    CS_TEST_CHECK(manager.totalBackupsCount() == 1);
    CS_TEST_CHECK(manager.totalBackupsSize() == QFileInfo(path).size());

    // The snapshot also keeps its references when moved again for restoration.
    const QString restored = QDir(root).filePath("restored/" + QFileInfo(source).fileName());
    const bool directoryCreated = QDir().mkpath(QFileInfo(restored).absolutePath());
    CS_TEST_CHECK(directoryCreated);
    const bool restoredCopy = QFile::copy(path, restored);
    CS_TEST_CHECK(restoredCopy);
    return {path, restored};
}

QString fixtureDirectory(const QString& root, const QString& name) {
    const QString path = QDir(root).filePath(name + "/original/assets");
    const bool created = QDir().mkpath(path);
    CS_TEST_CHECK(created);
    return QDir(path).absolutePath();
}

#ifdef HUB_BACKUP_IMAGE_CONTRACT
void testImageDocuments(const QString& root) {
    using namespace image_editor;
    const auto assets = fixtureDirectory(root, "image-current");
    const auto source = QDir(assets).filePath(QString::fromUtf8("source-ação.png"));
    const auto imported = QDir(assets).filePath("imported image.png");
    QImage pixels(2, 2, QImage::Format_ARGB32);
    pixels.fill(Qt::red);
    const bool sourceSaved = pixels.save(source);
    const bool importedSaved = pixels.save(imported);
    CS_TEST_CHECK(sourceSaved && importedSaved);
    ImageDocumentData document;
    document.source_path = source;
    document.source_size = {2, 2};
    document.canvas_size = {2, 2};
    ImageLayerData background;
    background.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    background.name = "Background";
    background.background = true;
    ImageGroupData group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    group.name = "Images";
    ImageLayerData layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = "Imported";
    layer.parent_group_id = group.id;
    ImageOperation operation;
    operation.kind = OperationKind::RasterImage;
    operation.raster.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.raster.source_path = imported;
    operation.raster.source_size = {2, 2};
    layer.operations = {operation};
    group.layer_ids = {layer.id};
    document.layers = {background, layer};
    document.groups = {group};
    document.root_stack = {{background.id, false}, {group.id, true}};
    const auto producerPath = QDir(QFileInfo(assets).absolutePath()).filePath("producer.cimg");
    QString error;
    const bool produced = ImageDocumentStore::saveDocument(producerPath, document, &error);
    CS_TEST_CHECK(produced);
    const auto encoded = QJsonDocument::fromJson(readBytes(producerPath)).object();
    CS_TEST_CHECK(!QDir::isAbsolutePath(encoded.value("base").toObject().value("path").toString()));

    for (int version = 1; version <= ImageDocumentStore::kCurrentDocumentVersion; ++version) {
        const auto caseRoot = QDir(root).filePath(QStringLiteral("image-v%1").arg(version));
        fixtureDirectory(root, QStringLiteral("image-v%1").arg(version));
        auto fixture = encoded;
        fixture.insert("version", version);
        fixture.insert("extra_note", QJsonObject{{"path", "not a media path"}, {"text", "preserve me"}});
        if (version == 1) { fixture.insert("source", fixture.value("base")); fixture.remove("base"); }
        if (version < 4) fixture.remove("layers");
        else if (version < 11) fixture.insert("layers", QJsonArray{encoded.value("layers").toArray().first()});
        // Use the producer's assets in every case, including legacy documents.
        const auto path = QDir(QFileInfo(assets).absolutePath()).filePath(QStringLiteral("image-v%1.cimg").arg(version));
        writeBytes(path, QJsonDocument(fixture).toJson());
        ImageDocumentData before;
        const bool originalLoaded = ImageDocumentStore::loadDocument(path, &before, &error);
        if (!originalLoaded) std::cerr << error.toStdString() << '\n';
        CS_TEST_CHECK(originalLoaded);
        const auto snapshot = backup(caseRoot, path);
        ImageDocumentData vault;
        CS_TEST_CHECK(ImageDocumentStore::loadDocument(snapshot.vault, &vault, &error));
        CS_TEST_CHECK(vault.source_path == before.source_path);
        if (version >= 4) CS_TEST_CHECK(vault == before);
        ImageDocumentData after;
        const bool loaded = ImageDocumentStore::loadDocument(snapshot.restored, &after, &error);
        CS_TEST_CHECK(loaded);
        CS_TEST_CHECK(after.source_path == before.source_path);
        CS_TEST_CHECK(after.source_size == before.source_size);
        if (version >= 4) CS_TEST_CHECK(after == before);
        CS_TEST_CHECK(!QImage(after.source_path).isNull());
        if (version >= 11) CS_TEST_CHECK(!QImage(after.layers.back().operations.front().raster.source_path).isNull());
    }
    CS_TEST_CHECK(readBytes(source) == readBytes(imported));
}
#endif

#ifdef HUB_BACKUP_VIDEO_CONTRACT
void testVideoDocuments(const QString& root) {
    const auto assets = fixtureDirectory(root, "video-current");
    const auto footage = QDir(assets).filePath(QString::fromUtf8("footage-ação.mkv"));
    const auto still = QDir(assets).filePath("still.png");
    writeBytes(footage, "media bytes");
    writeBytes(still, "image bytes");
    project::ProjectDocument document;
    document.media = {{fsPath(footage), "Footage"}, {fsPath(still), "Image", "Unsorted", false, media::MediaKind::Image}};
    document.media.back().image_editor_link = media::LinkedImageReference{
        "image-link", fsPath(QDir(assets).filePath("linked.cimg")), fsPath(QDir(assets).filePath("published.png"))};
    document.media.front().motion_link = media::MotionLinkReference{
        "motion-link", fsPath(QDir(assets).filePath("linked.motion")), fsPath(QDir(assets).filePath("render.mp4")),
        fsPath(footage), "video", "mp4", "libx264", "Balanced", 10.0};
    // Media Pool links reference the published render; timeline clips may use
    // either the render or the original source recorded by the link.
    document.media.front().source_path = document.media.front().motion_link->published_output_path;
    writeBytes(QDir(assets).filePath("render.mp4"), "published render bytes");
    project::ProjectTrack track;
    track.name = "Video 1";
    track.track_id = 1;
    project::ProjectClip clip;
    clip.clip_id = 1;
    clip.source_path = fsPath(footage);
    clip.duration_frames = clip.source_duration_frames = 30;
    clip.motion_link = document.media.front().motion_link;
    clip.node_graph = fusion::nodes::makePassthroughGraph();
    clip.node_graph->nodes.front().source_path = fsPath(still);
    clip.node_graph->nodes.front().source_is_still = true;
    track.clips.push_back(clip);
    clip.clip_id = 2;
    clip.kind = timeline::ClipKind::Image;
    clip.timeline_start_frame = 30;
    clip.source_path = fsPath(still);
    clip.motion_link.reset();
    clip.node_graph.reset();
    clip.image_editor_variant = media::LinkedImageReference{
        "clip-image-link", fsPath(QDir(assets).filePath("variant.cimg")), fsPath(QDir(assets).filePath("variant.png"))};
    track.clips.push_back(clip);
    document.timeline_tracks.push_back(track);
    const auto producerPath = QDir(QFileInfo(assets).absolutePath()).filePath("producer.csp");
    project::save(fsPath(producerPath), document);
    const auto encoded = QJsonDocument::fromJson(readBytes(producerPath)).object();
    CS_TEST_CHECK(!QDir::isAbsolutePath(encoded.value("media").toArray().first().toObject().value("path").toString()));

    for (int version = 1; version <= project::current_format_version; ++version) {
        const auto caseRoot = QDir(root).filePath(QStringLiteral("video-v%1").arg(version));
        fixtureDirectory(root, QStringLiteral("video-v%1").arg(version));
        auto fixture = encoded;
        fixture.insert("version", version);
        fixture.insert("extra_note", "keep this field");
        auto timeline = fixture.value("timeline").toObject();
        timeline.insert("row_height", 70.0);
        if (version < project::media_kind_format_version) {
            auto tracks = timeline.value("tracks").toArray();
            auto legacyTrack = tracks.first().toObject();
            legacyTrack.insert("clips", QJsonArray{legacyTrack.value("clips").toArray().first()});
            tracks[0] = legacyTrack;
            timeline.insert("tracks", tracks);
            if (version == 1) { timeline.insert("clips", legacyTrack.value("clips")); timeline.remove("tracks"); }
        }
        fixture.insert("timeline", timeline);
        const auto path = QDir(QFileInfo(assets).absolutePath()).filePath(QStringLiteral("video-v%1.csp").arg(version));
        writeBytes(path, QJsonDocument(fixture).toJson());
        const auto before = project::load(fsPath(path));
        const auto snapshot = backup(caseRoot, path);
        CS_TEST_CHECK(project::load(fsPath(snapshot.vault)) == before);
        const auto after = project::load(fsPath(snapshot.restored));
        CS_TEST_CHECK(after == before);
        CS_TEST_CHECK(std::filesystem::exists(after.media.front().source_path));
    }
    CS_TEST_CHECK(readBytes(footage) == "media bytes");
    CS_TEST_CHECK(readBytes(still) == "image bytes");
}
#endif

#ifdef HUB_BACKUP_MOTION_CONTRACT
void testMotionDocuments(const QString& root) {
    using namespace motion::model;
    using Store = motion::persistence::MotionDocumentStore;
    const auto assets = fixtureDirectory(root, "motion-current");
    const auto source = QDir(assets).filePath(QString::fromUtf8("source-ação.png"));
    writeBytes(source, "original image bytes");
    MotionProjectData project;
    project.composition = {{1920, 1080}, {30, 1}};
    project.media = {{fsPath(source), creative_suite::media::MediaKind::Image, "Image", "Unsorted"}};
    CompositionDocument composition(1920, 1080, {30, 1});
    creative_suite::media::VideoMetadata media;
    media.kind = creative_suite::media::MediaKind::Image;
    media.source_path = fsPath(source);
    media.display_name = "Image";
    LayerId id = 0;
    const auto added = composition.addMediaLayer(media, 0, &id);
    CS_TEST_CHECK(added == AddMediaLayerResult::Added);
    const bool linked = composition.setLayerLinkedImage(id, LinkedImageDocument{
        fsPath(QDir(assets).filePath("linked.cimg")), fsPath(QDir(assets).filePath("published.png")),
        fsPath(QDir(assets).filePath("source snapshot.png"))});
    CS_TEST_CHECK(linked);
    project.layers = composition.layers();
    const auto producerPath = QDir(QFileInfo(assets).absolutePath()).filePath("producer.motion");
    Store::save(fsPath(producerPath), project);
    const auto encoded = QJsonDocument::fromJson(readBytes(producerPath)).object();
    CS_TEST_CHECK(!QDir::isAbsolutePath(encoded.value("layers").toArray().first().toObject().value("source_path").toString()));

    for (int version = 1; version <= Store::current_format_version; ++version) {
        const auto caseRoot = QDir(root).filePath(QStringLiteral("motion-v%1").arg(version));
        fixtureDirectory(root, QStringLiteral("motion-v%1").arg(version));
        auto fixture = encoded;
        fixture.insert("version", version);
        fixture.insert("extra_note", "keep this field");
        const auto path = QDir(QFileInfo(assets).absolutePath()).filePath(QStringLiteral("motion-v%1.motion").arg(version));
        writeBytes(path, QJsonDocument(fixture).toJson());
        const auto before = Store::load(fsPath(path));
        const auto snapshot = backup(caseRoot, path);
        CS_TEST_CHECK(Store::load(fsPath(snapshot.vault)) == before);
        const auto after = Store::load(fsPath(snapshot.restored));
        CS_TEST_CHECK(after == before);
        CS_TEST_CHECK(std::filesystem::exists(after.layers.front().source_path));
    }
    CS_TEST_CHECK(readBytes(source) == "original image bytes");
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    CS_TEST_CHECK(directory.isValid());
    creative_suite::hub::ActivityManager::instance().setStoragePath(directory.filePath("activities.json"));
    creative_suite::hub::HubLogger::instance().setLogFilePath(directory.filePath("hub.log"));
    try {
#ifdef HUB_BACKUP_IMAGE_CONTRACT
        testImageDocuments(directory.path());
#endif
#ifdef HUB_BACKUP_VIDEO_CONTRACT
        testVideoDocuments(directory.path());
#endif
#ifdef HUB_BACKUP_MOTION_CONTRACT
        testMotionDocuments(directory.path());
#endif
    } catch (const std::exception& error) {
        std::cerr << "Backup consumer contract failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Backup producer/consumer contracts passed.\n";
    return 0;
}
