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
    if (!copied) std::cerr << source.toStdString() << ": " << error.toStdString() << '\n';
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
        if (version >= 17) {
            auto layers = fixture.value("layers").toArray();
            auto groupObject = layers.last().toObject(); auto children = groupObject.value("children").toArray();
            auto childObject = children.first().toObject();
            childObject.insert("outline", QJsonObject{{"enabled",true},{"thickness",7},
                {"color","#c81450f0"},{"position","outside"}});
            children[0]=childObject; groupObject.insert("children",children); layers[layers.size()-1]=groupObject;
            fixture.insert("layers",layers);
        }
        if (version >= 18) {
            auto layers = fixture.value("layers").toArray(); auto groupObject = layers.last().toObject();
            auto children = groupObject.value("children").toArray(); auto child = children.first().toObject();
            child.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
            child.insert("clip_to_base", true); child.insert("operations", QJsonArray{});
            child.insert("mask", QJsonObject{{"enabled",true},{"operations",QJsonArray{}}});
            children.append(child); groupObject.insert("children",children); layers[layers.size()-1]=groupObject;
            fixture.insert("layers",layers);
        }
        if (version >= 19) {
            auto layers=fixture.value("layers").toArray(); auto group=layers.last().toObject();
            group.insert("blend_mode","screen"); auto children=group.value("children").toArray();
            auto base=children[0].toObject(); base.insert("blend_mode","multiply"); children[0]=base;
            auto child=children[1].toObject(); child.insert("blend_mode","difference"); children[1]=child;
            group.insert("children",children); layers[layers.size()-1]=group; fixture.insert("layers",layers);
        }
        if(version>=20){
            auto layers=fixture.value("layers").toArray();auto group=layers.last().toObject();auto children=group.value("children").toArray();auto adjustment=children.last().toObject();
            adjustment.insert("id",QUuid::createUuid().toString(QUuid::WithoutBraces));adjustment.insert("kind","adjustment");adjustment.remove("outline");
            adjustment.insert("adjustment",QJsonObject{{"type","gradient_map"},{"stops",QJsonArray{QJsonObject{{"position",0},{"color","#ff0000"}},QJsonObject{{"position",1},{"color","#0000ff"}}}}});
            adjustment.insert("mask",QJsonObject{{"enabled",true},{"operations",QJsonArray{}},{"initial_selection",QJsonObject{{"fill_rule",0},{"path",QJsonArray{QJsonObject{{"type",0},{"x",0},{"y",0}},QJsonObject{{"type",1},{"x",20},{"y",0}},QJsonObject{{"type",1},{"x",20},{"y",20}},QJsonObject{{"type",1},{"x",0},{"y",20}},QJsonObject{{"type",1},{"x",0},{"y",0}}}}}}});
            children.append(adjustment);group.insert("children",children);layers[layers.size()-1]=group;fixture.insert("layers",layers);
        }
        if(version>=21){
            auto layers=fixture.value("layers").toArray();auto group=layers.last().toObject();auto children=group.value("children").toArray();auto adjustment=children.last().toObject();
            adjustment.insert("id",QUuid::createUuid().toString(QUuid::WithoutBraces));adjustment.insert("adjustment",QJsonObject{{"type","brightness_contrast"},{"brightness",25},{"contrast",-30}});
            children.append(adjustment);group.insert("children",children);layers[layers.size()-1]=group;fixture.insert("layers",layers);
        }
        if (version >= 22) {
            auto layers = fixture.value("layers").toArray();
            auto group = layers.last().toObject();
            auto children = group.value("children").toArray();
            auto adjustment = children.last().toObject();
            adjustment.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
            adjustment.insert("adjustment", QJsonObject{{"type", "exposure_offset_gamma"},
                {"exposure", 1.25}, {"offset", -.0123}, {"gamma", 1.4}});
            children.append(adjustment);
            group.insert("children", children);
            layers[layers.size()-1] = group;
            fixture.insert("layers", layers);
        }
        if (version >= 23) {
            auto layers = fixture.value("layers").toArray();
            auto group = layers.last().toObject();
            auto children = group.value("children").toArray();
            auto adjustment = children.last().toObject();
            adjustment.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
            adjustment.insert("adjustment", QJsonObject{{"type", "hue_saturation"},
                {"hue", 75}, {"saturation", -25}, {"lightness", 10}});
            children.append(adjustment);
            group.insert("children", children);
            layers[layers.size()-1] = group;
            fixture.insert("layers", layers);
        }
        if (version >= 24) {
            auto layers = fixture.value("layers").toArray();
            auto group = layers.last().toObject();
            auto children = group.value("children").toArray();
            auto adjustment = children.last().toObject();
            adjustment.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
            adjustment.insert("adjustment", QJsonObject{{"type", "levels"},
                {"input_black", 20}, {"input_white", 230}, {"gamma", 1.4},
                {"output_black", 10}, {"output_white", 240}});
            children.append(adjustment);
            group.insert("children", children);
            layers[layers.size()-1] = group;
            fixture.insert("layers", layers);
        }
        if (version >= 25) {
            auto layers = fixture.value("layers").toArray();
            auto group = layers.last().toObject();
            auto children = group.value("children").toArray();
            auto adjustment = children.last().toObject();
            adjustment.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
            QJsonObject curves{{"type", "curves"},
                {"points", QJsonArray{QJsonObject{{"input",0},{"output",10}},
                    QJsonObject{{"input",128},{"output",180}},
                    QJsonObject{{"input",255},{"output",245}}}}};
            if (version >= 26) {
                const QJsonArray identityPoints{QJsonObject{{"input", 0}, {"output", 0}},
                    QJsonObject{{"input", 255}, {"output", 255}}};
                curves.insert("channels", QJsonObject{{"red", identityPoints},
                    {"green", identityPoints}, {"blue", identityPoints}});
            }
            adjustment.insert("adjustment", curves);
            children.append(adjustment);
            group.insert("children", children);
            layers[layers.size()-1] = group;
            fixture.insert("layers", layers);
        }
        if (version >= 27) {
            auto layers = fixture.value("layers").toArray();
            auto group = layers.last().toObject();
            auto children = group.value("children").toArray();
            bool found_levels = false;
            for (qsizetype index = 0; index < children.size(); ++index) {
                auto adjustment_layer = children.at(index).toObject();
                auto parameters = adjustment_layer.value("adjustment").toObject();
                if (parameters.value("type") != "levels") continue;
                const QJsonObject identity{{"input_black", 0}, {"input_white", 255},
                    {"gamma", 1.0}, {"output_black", 0}, {"output_white", 255}};
                auto red = identity; red.insert("output_black", 12); red.insert("output_white", 244);
                auto green = identity; green.insert("input_black", 8); green.insert("input_white", 248);
                parameters.insert("channels", QJsonObject{{"red", red}, {"green", green}, {"blue", identity}});
                adjustment_layer.insert("adjustment", parameters);
                children[index] = adjustment_layer;
                found_levels = true;
                break;
            }
            CS_TEST_CHECK(found_levels);
            group.insert("children", children);
            layers[layers.size()-1] = group;
            fixture.insert("layers", layers);
        }
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
        if (version >= 11) CS_TEST_CHECK(!QImage(after.layers.at(1).operations.front().raster.source_path).isNull());
    }
    CS_TEST_CHECK(readBytes(source) == readBytes(imported));
    auto future = encoded;
    future.insert("version", ImageDocumentStore::kCurrentDocumentVersion + 1);
    const auto future_path = QDir(QFileInfo(assets).absolutePath()).filePath("image-future.cimg");
    writeBytes(future_path, QJsonDocument(future).toJson());
    creative_suite::hub::BackupManager manager(QDir(root).filePath("image-future-vault"));
    QString rejected_path, future_error;
    CS_TEST_CHECK(!manager.createBackup(future_path, &rejected_path, &future_error) && !future_error.isEmpty());
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
    clip.motion_link->render_start_time_us = 1234567;
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
    if (QDir::isAbsolutePath(encoded.value("media").toArray().first().toObject().value("path").toString())) {
        std::error_code error;
        const auto relative = std::filesystem::relative(document.media.front().source_path, fsPath(producerPath).parent_path(), error);
        std::cerr << "Producer " << producerPath.toStdString() << " source " << document.media.front().source_path.generic_string()
            << " encoded " << encoded.value("media").toArray().first().toObject().value("path").toString().toStdString()
            << " relative " << relative.generic_string() << " error " << error.message() << '\n';
    }
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
        if (version >= project::motion_render_offset_format_version) {
            CS_TEST_CHECK(after.timeline_tracks[0].clips[0].motion_link->render_start_time_us == 1234567);
        }
        CS_TEST_CHECK(std::filesystem::exists(after.media.front().source_path));
    }
    CS_TEST_CHECK(readBytes(footage) == "media bytes");
    CS_TEST_CHECK(readBytes(still) == "image bytes");
    project::ProjectClip adjustment;
    adjustment.clip_id = 3;
    adjustment.kind = timeline::ClipKind::Adjustment;
    adjustment.duration_frames = 60;
    adjustment.effects = {creative_suite::effects::makeDefaultInstance("video.grayscale")};
    adjustment.transform.opacity = 0.5;
    adjustment.keyframes.scale = {{0, 1.0}, {59, 1.5}};
    project::ProjectTrack adjustment_track;
    adjustment_track.track_id = 2;
    adjustment_track.name = "Adjustment";
    adjustment_track.clips = {adjustment};
    document.timeline_tracks.insert(document.timeline_tracks.begin(), adjustment_track);
    const auto adjustment_path = QDir(QFileInfo(assets).absolutePath()).filePath("adjustment.csp");
    project::save(fsPath(adjustment_path), document);
    const auto saved = project::load(fsPath(adjustment_path));
    const auto snapshot = backup(QDir(root).filePath("video-adjustment-v26"), adjustment_path);
    CS_TEST_CHECK(project::load(fsPath(snapshot.vault)) == saved);
    const auto restored = project::load(fsPath(snapshot.restored));
    CS_TEST_CHECK(restored == saved);
    CS_TEST_CHECK(restored.timeline_tracks.front().clips.front().source_path.empty());
    CS_TEST_CHECK(restored.timeline_tracks.front().clips.front().keyframes == adjustment.keyframes);
    auto composition = adjustment;
    composition.kind = timeline::ClipKind::FusionComposition;
    composition.display_name = "Background";
    composition.node_graph = fusion::nodes::makeCompositionGraph();
    composition.node_graph->nodes[0].background_color = {12, 34, 56, 78};
    composition.node_graph->nodes[0].presentation = creative_suite::composition_exchange::Presentation{};
    composition.node_graph->nodes[0].presentation->name = "Interchange background";
    composition.node_graph->nodes[0].presentation->offset = -12;
    fusion::nodes::Node text; text.id = 3; text.type = fusion::nodes::NodeType::Text;
    text.text.content = "Backup composition";
    fusion::nodes::Node input; input.id = 4; input.source_path = fsPath(still); input.source_is_still = true;
    composition.node_graph->nodes.push_back(text); composition.node_graph->nodes.push_back(input);
    fusion::nodes::Node mask; mask.id=5;mask.type=fusion::nodes::NodeType::EllipseMask;
    mask.mask.invert=true;mask.mask.soft_edge=0.25;
    mask.mask_keyframes[0]={{0,0.2,creative_suite::animation::InterpolationMode::CubicBezier,{0.1,0.6,0.8,0.95}},{59,0.8}};
    composition.node_graph->nodes.push_back(mask); composition.node_graph->connections.push_back({5,2,2});
    fusion::nodes::Node blur;blur.id=6;blur.type=fusion::nodes::NodeType::GaussianBlur;
    blur.gaussian_blur={3.5,false};composition.node_graph->nodes.push_back(blur);
    composition.node_graph->connections.erase(composition.node_graph->connections.begin());
    composition.node_graph->connections.push_back({1,6,0});composition.node_graph->connections.push_back({6,2,0});
    composition.node_graph->connections.push_back({5,6,2});
    composition.node_graph->next_id = 7;
    composition.composition_input_offset_frames = -15;
    document.timeline_tracks.front().clips = {composition};
    const auto fusion_path = QDir(QFileInfo(assets).absolutePath()).filePath("fusion.csp");
    project::save(fsPath(fusion_path), document);
    const auto fusion_saved = project::load(fsPath(fusion_path));
    const auto fusion_backup = backup(QDir(root).filePath("video-background-v29"), fusion_path);
    CS_TEST_CHECK(project::load(fsPath(fusion_backup.vault)) == fusion_saved);
    CS_TEST_CHECK(project::load(fsPath(fusion_backup.restored)) == fusion_saved);
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
    // Materialize linked references: Windows weakly_canonical may report an
    // error for a missing leaf, which would turn this valid-path fixture into
    // an unrelated path-validation case.
    writeBytes(QDir(assets).filePath("linked.cimg"), "{}");
    writeBytes(QDir(assets).filePath("published.png"), readBytes(source));
    writeBytes(QDir(assets).filePath("source snapshot.png"), readBytes(source));
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
    project.layers.front().presentation = creative_suite::composition_exchange::Presentation{};
    project.layers.front().presentation->name = "Interchange image";
    project.layers.front().presentation->offset = -7;
    CompositionDocument masked(1920,1080,{30,1},project.layers);
    const auto mask=masked.createMask(project.layers.front().id,-2,creative_suite::composition::ShapeMaskKind::Ellipse);
    CS_TEST_CHECK(mask && masked.associateMask(project.layers.front().id,-1,mask));
    CS_TEST_CHECK(masked.setMaskKeyframe(mask,0,15,.25));
    CS_TEST_CHECK(masked.setLayerEffects(project.layers.front().id, {GaussianBlurEffect{true, 4.0}}));
    CS_TEST_CHECK(masked.setEffectKeyframe(
        project.layers.front().id, 0, EffectParameterProperty::GaussianBlurRadius, 10, 12.0));
    project.layers=masked.layers();project.masks=masked.masks();
    project.composition.name = "Interchange";
    project.composition.duration = 180;
    const auto interchange_path = QDir(QFileInfo(assets).absolutePath()).filePath("interchange.motion");
    Store::save(fsPath(interchange_path), project);
    const auto interchange_backup = backup(QDir(root).filePath("motion-interchange-v9"), interchange_path);
    CS_TEST_CHECK(Store::load(fsPath(interchange_backup.vault)) == project);
    CS_TEST_CHECK(Store::load(fsPath(interchange_backup.restored)) == project);
    CompositionDocument hierarchy(1920,1080,{30,1},project.layers,project.masks);
    LayerId null_id=0;
    CS_TEST_CHECK(hierarchy.addContentLayer(LayerKind::Null,"Control",0,&null_id));
    auto control=hierarchy.layers().back().transform;control.position_x=.25;control.rotation_degrees=30;
    CS_TEST_CHECK(hierarchy.setLayerTransform(null_id,control));
    CS_TEST_CHECK(hierarchy.setLayerKeyframe(null_id,creative_suite::animation::TransformProperty::Rotation,15,70));
    CS_TEST_CHECK(hierarchy.setLayerParent(id,null_id,7));
    project.layers=hierarchy.layers();
    const auto hierarchy_path=QDir(QFileInfo(assets).absolutePath()).filePath("hierarchy.motion");
    Store::save(fsPath(hierarchy_path),project);
    const auto hierarchy_backup=backup(QDir(root).filePath("motion-parenting-v10"),hierarchy_path);
    CS_TEST_CHECK(Store::load(fsPath(hierarchy_backup.vault))==project);
    const auto restored_hierarchy=Store::load(fsPath(hierarchy_backup.restored));
    CS_TEST_CHECK(restored_hierarchy==project);
    CS_TEST_CHECK(std::filesystem::exists(restored_hierarchy.layers.front().source_path));
    CS_TEST_CHECK(hierarchy.setLayerAnchorPoint(null_id,{-10.5,35.25}));
    CS_TEST_CHECK(hierarchy.setLayerAnchorPoint(id,{4.5,-2}));
    CS_TEST_CHECK(hierarchy.setLayerAnchorKeyframe(id,AnchorProperty::X,0,4.5));
    CS_TEST_CHECK(hierarchy.setLayerAnchorKeyframe(id,AnchorProperty::X,30,40));
    CS_TEST_CHECK(hierarchy.setLayerAnchorKeyframeInterpolation(id,AnchorProperty::X,0,
        creative_suite::animation::InterpolationMode::CubicBezier,{.42,0,1,1}));
    project.layers=hierarchy.layers();
    const auto anchor_path=QDir(QFileInfo(assets).absolutePath()).filePath("anchor.motion");
    Store::save(fsPath(anchor_path),project);
    CS_TEST_CHECK(QJsonDocument::fromJson(readBytes(anchor_path)).object().value("version").toInt()==Store::current_format_version);
    const auto anchor_backup=backup(QDir(root).filePath("motion-anchor-v11"),anchor_path);
    CS_TEST_CHECK(Store::load(fsPath(anchor_backup.vault))==project);
    const auto restored_anchor=Store::load(fsPath(anchor_backup.restored));
    CS_TEST_CHECK(restored_anchor==project);
    CS_TEST_CHECK(std::filesystem::exists(restored_anchor.layers.front().source_path));
    CS_TEST_CHECK(hierarchy.setLayerScaleAxisBase(null_id,ScaleAxis::Y,.45));
    CS_TEST_CHECK(hierarchy.setLayerScaleLinked(id,false));
    CS_TEST_CHECK(hierarchy.setLayerScaleAxisKeyframe(id,ScaleAxis::Y,0,1.25));
    CS_TEST_CHECK(hierarchy.setLayerScaleAxisKeyframe(id,ScaleAxis::Y,30,.75));
    CS_TEST_CHECK(hierarchy.setLayerScaleAxisKeyframeInterpolation(id,ScaleAxis::Y,0,
        creative_suite::animation::InterpolationMode::CubicBezier,{.42,0,1,1}));
    CS_TEST_CHECK(hierarchy.setLayerParent(id,0,7));
    CS_TEST_CHECK(hierarchy.setLayerParent(id,null_id,7));
    CS_TEST_CHECK(hierarchy.layers().front().parent_affine.has_value());
    project.layers=hierarchy.layers();
    const auto scale_path=QDir(QFileInfo(assets).absolutePath()).filePath("scale.motion");
    Store::save(fsPath(scale_path),project);
    const auto scale_backup=backup(QDir(root).filePath("motion-scale-v12"),scale_path);
    CS_TEST_CHECK(Store::load(fsPath(scale_backup.vault))==project);
    const auto restored_scale=Store::load(fsPath(scale_backup.restored));
    CS_TEST_CHECK(restored_scale==project);
    CS_TEST_CHECK(std::filesystem::exists(restored_scale.layers.front().source_path));
    CS_TEST_CHECK(hierarchy.setLayerBlendMode(id,creative_suite::composition::BlendMode::Overlay));
    project.layers=hierarchy.layers();
    const auto blend_path=QDir(QFileInfo(assets).absolutePath()).filePath("blend.motion");
    Store::save(fsPath(blend_path),project);
    const auto blend_backup=backup(QDir(root).filePath("motion-blend-v13"),blend_path);
    CS_TEST_CHECK(Store::load(fsPath(blend_backup.vault))==project);
    CS_TEST_CHECK(Store::load(fsPath(blend_backup.restored))==project);
    CS_TEST_CHECK(std::filesystem::exists(Store::load(fsPath(blend_backup.restored)).layers.front().source_path));
    motion::model::LayerId matte_id=0;
    CS_TEST_CHECK(hierarchy.addContentLayer(LayerKind::Shape,"Matte",0,&matte_id));
    CS_TEST_CHECK(hierarchy.setLayerTrackMatte(id,TrackMatteBinding{matte_id,creative_suite::composition::TrackMatteMode::LumaInverted}));
    project.layers=hierarchy.layers();
    const auto matte_path=QDir(QFileInfo(assets).absolutePath()).filePath("matte.motion");
    Store::save(fsPath(matte_path),project);
    const auto matte_backup=backup(QDir(root).filePath("motion-matte-v14"),matte_path);
    CS_TEST_CHECK(Store::load(fsPath(matte_backup.vault))==project);
    const auto restored_matte=Store::load(fsPath(matte_backup.restored));
    CS_TEST_CHECK(restored_matte==project && !restored_matte.layers.back().visible);
    CS_TEST_CHECK(std::filesystem::exists(restored_matte.layers.front().source_path));
    PathGeometry geometry{{{{-70,-20},{},{20,-10}},{{80,-20},{-20,10},{}},{{0,45},{},{}}},true};
    LayerId path_id=0;
    CS_TEST_CHECK(hierarchy.createPathLayer(geometry,5,&path_id));
    CS_TEST_CHECK(hierarchy.setPathKeyframe({path_id,false},5));
    auto moved=geometry;moved.nodes[0].point.x-=30;
    CS_TEST_CHECK(hierarchy.setPathAtFrame({path_id,false},moved,25));
    PathGeometry coverage{{{{.1,.1},{},{}},{{.9,.1},{},{}},{{.5,.9},{},{}}},true};
    const auto path_mask=hierarchy.createPathMask(path_id,-2,coverage);
    CS_TEST_CHECK(path_mask && hierarchy.setPathKeyframe({path_mask,true},12));
    project.layers=hierarchy.layers();project.masks=hierarchy.masks();
    const auto bezier_path=QDir(QFileInfo(assets).absolutePath()).filePath("bezier.motion");
    Store::save(fsPath(bezier_path),project);
    const auto bezier_backup=backup(QDir(root).filePath("motion-bezier-v15"),bezier_path);
    CS_TEST_CHECK(Store::load(fsPath(bezier_backup.vault))==project);
    CS_TEST_CHECK(Store::load(fsPath(bezier_backup.restored))==project);
    CS_TEST_CHECK(std::filesystem::exists(Store::load(fsPath(bezier_backup.restored)).layers.front().source_path));
    CS_TEST_CHECK(hierarchy.setLayerTrimPathsEnabled(path_id,true));
    CS_TEST_CHECK(hierarchy.setLayerTrimValueAtFrame(path_id,TrimPathProperty::Start,80,5));
    CS_TEST_CHECK(hierarchy.setLayerTrimValueAtFrame(path_id,TrimPathProperty::End,20,5));
    CS_TEST_CHECK(hierarchy.setLayerTrimKeyframe(path_id,TrimPathProperty::Offset,5));
    CS_TEST_CHECK(hierarchy.setLayerTrimKeyframe(path_id,TrimPathProperty::Offset,25));
    CS_TEST_CHECK(hierarchy.setLayerTrimValueAtFrame(path_id,TrimPathProperty::Offset,810.125,25));
    project.layers=hierarchy.layers();Store::save(fsPath(bezier_path),project);
    const auto trim_backup=backup(QDir(root).filePath("motion-trim-v16"),bezier_path);
    CS_TEST_CHECK(Store::load(fsPath(trim_backup.vault))==project);
    CS_TEST_CHECK(Store::load(fsPath(trim_backup.restored))==project);
    CS_TEST_CHECK(std::filesystem::exists(Store::load(fsPath(trim_backup.restored)).layers.front().source_path));
    auto shadow_layer=std::find_if(hierarchy.layers().begin(),hierarchy.layers().end(),[&](const auto& l){return l.id==path_id;});
    auto shadow_effects=shadow_layer->effects;DropShadowEffect shadow;shadow.parameters.color={10,40,130};shadow.parameters.angle_degrees=-1080.125;shadow.parameters.distance_pixels=19.5;shadow.parameters.softness_pixels=3.25;
    if(!hierarchy.masks().empty())shadow.mask=hierarchy.masks().front().id;
    const auto shadow_index=shadow_effects.size();shadow_effects.emplace_back(shadow);
    CS_TEST_CHECK(hierarchy.setLayerEffects(path_id,shadow_effects));
    for(auto property:shadow_properties){CS_TEST_CHECK(hierarchy.setEffectKeyframe(path_id,shadow_index,property,0,shadowBaseValue(shadow,property)));CS_TEST_CHECK(hierarchy.setEffectKeyframe(path_id,shadow_index,property,20,shadowBaseValue(shadow,property)));}
    project.layers=hierarchy.layers();project.masks=hierarchy.masks();Store::save(fsPath(bezier_path),project);
    const auto shadow_backup=backup(QDir(root).filePath("motion-shadow-v17"),bezier_path);
    CS_TEST_CHECK(Store::load(fsPath(shadow_backup.vault))==project);
    CS_TEST_CHECK(Store::load(fsPath(shadow_backup.restored))==project);
    CS_TEST_CHECK(std::filesystem::exists(Store::load(fsPath(shadow_backup.restored)).layers.front().source_path));
    const auto glow_layer = std::find_if(hierarchy.layers().begin(), hierarchy.layers().end(),
        [path_id](const auto& layer) { return layer.id == path_id; });
    CS_TEST_CHECK(glow_layer != hierarchy.layers().end());
    auto glow_effects = glow_layer->effects;
    GlowEffect glow;
    glow.parameters.mode = creative_suite::effects::GlowMode::Both;
    glow.parameters.color = {18, 96, 224};
    glow.parameters.radius_pixels = 7.5;
    glow.parameters.opacity_percent = 68.0;
    if (!hierarchy.masks().empty()) glow.mask = hierarchy.masks().front().id;
    const auto glow_index = glow_effects.size();
    glow_effects.emplace_back(glow);
    CS_TEST_CHECK(hierarchy.setLayerEffects(path_id, glow_effects));
    CS_TEST_CHECK(hierarchy.setEffectKeyframe(path_id, glow_index,
        EffectParameterProperty::GlowOpacityPercent, 0, 40.0));
    CS_TEST_CHECK(hierarchy.setEffectKeyframe(path_id, glow_index,
        EffectParameterProperty::GlowOpacityPercent, 20, 80.0));
    CS_TEST_CHECK(hierarchy.setEffectKeyframe(path_id, glow_index,
        EffectParameterProperty::GlowRadiusPixels, 0, 4.0));
    CS_TEST_CHECK(hierarchy.setEffectKeyframe(path_id, glow_index,
        EffectParameterProperty::GlowRadiusPixels, 20, 12.0));
    project.layers = hierarchy.layers();
    project.masks = hierarchy.masks();
    const auto glow_path = QDir(QFileInfo(assets).absolutePath()).filePath("glow.motion");
    Store::save(fsPath(glow_path), project);
    CS_TEST_CHECK(QJsonDocument::fromJson(readBytes(glow_path)).object().value("version").toInt() == 18);
    const auto glow_backup = backup(QDir(root).filePath("motion-glow-v18"), glow_path);
    CS_TEST_CHECK(Store::load(fsPath(glow_backup.vault)) == project);
    CS_TEST_CHECK(Store::load(fsPath(glow_backup.restored)) == project);
    CS_TEST_CHECK(std::filesystem::exists(Store::load(fsPath(glow_backup.restored)).layers.front().source_path));
    auto future=QJsonDocument::fromJson(readBytes(glow_path)).object();future.insert("version",Store::current_format_version+1);
    const auto future_path=QDir(QFileInfo(assets).absolutePath()).filePath("future-v19.motion");writeBytes(future_path,QJsonDocument(future).toJson());
    creative_suite::hub::BackupManager manager(QDir(root).filePath("future-vault"));QString rejected_path,error;
    CS_TEST_CHECK(!manager.createBackup(future_path,&rejected_path,&error)&&!error.isEmpty());


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
