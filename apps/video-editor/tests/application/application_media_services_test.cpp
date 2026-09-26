#include "application/media_controller.h"
#include "application/media_import_service.h"
#include "application/project_controller.h"
#include "application/project_open_service.h"

#include "project/project_file.h"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireFfmpeg(int result, const char* operation) {
    if (result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string(operation) + ": " + message);
}

std::filesystem::path create24FpsVideo(const std::filesystem::path& path) {
    AVFormatContext* output = nullptr;
    const auto path_u8 = path.u8string();
    const std::string path_utf8(
        reinterpret_cast<const char*>(path_u8.data()), path_u8.size());
    requireFfmpeg(avformat_alloc_output_context2(
                      &output, nullptr, "matroska", path_utf8.c_str()),
                  "Creating a 24 FPS migration fixture");
    require(output != nullptr, "Could not allocate the migration fixture container.");
    const AVCodec* encoder = avcodec_find_encoder_by_name("mpeg4");
    require(encoder != nullptr, "The FFmpeg build has no MPEG-4 fixture encoder.");
    AVCodecContext* codec = avcodec_alloc_context3(encoder);
    require(codec != nullptr, "Could not allocate the migration fixture encoder.");
    codec->width = 32;
    codec->height = 24;
    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->time_base = AVRational{1, 24};
    codec->framerate = AVRational{24, 1};
    codec->bit_rate = 200000;
    if ((output->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    requireFfmpeg(avcodec_open2(codec, encoder, nullptr),
                  "Opening the migration fixture encoder");
    AVStream* stream = avformat_new_stream(output, nullptr);
    require(stream != nullptr, "Could not create the migration fixture video stream.");
    stream->time_base = codec->time_base;
    requireFfmpeg(avcodec_parameters_from_context(stream->codecpar, codec),
                  "Copying migration fixture stream parameters");
    if ((output->oformat->flags & AVFMT_NOFILE) == 0) {
        requireFfmpeg(avio_open(&output->pb, path_utf8.c_str(), AVIO_FLAG_WRITE),
                      "Opening the migration fixture file");
    }
    requireFfmpeg(avformat_write_header(output, nullptr),
                  "Writing the migration fixture header");
    AVFrame* frame = av_frame_alloc();
    require(frame != nullptr, "Could not allocate a migration fixture frame.");
    frame->format = codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;
    requireFfmpeg(av_frame_get_buffer(frame, 32),
                  "Allocating migration fixture pixels");
    const auto write_packets = [&](bool flush) {
        requireFfmpeg(avcodec_send_frame(codec, flush ? nullptr : frame),
                      "Encoding migration fixture frames");
        while (true) {
            AVPacket* packet = av_packet_alloc();
            require(packet != nullptr, "Could not allocate a migration fixture packet.");
            const int received = avcodec_receive_packet(codec, packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                av_packet_free(&packet);
                break;
            }
            requireFfmpeg(received, "Receiving migration fixture packets");
            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            requireFfmpeg(av_interleaved_write_frame(output, packet),
                          "Writing migration fixture packets");
            av_packet_free(&packet);
        }
    };
    for (std::int64_t index = 0; index < 48; ++index) {
        requireFfmpeg(av_frame_make_writable(frame),
                      "Preparing migration fixture pixels");
        for (int y = 0; y < frame->height; ++y) {
            std::fill_n(frame->data[0] + y * frame->linesize[0],
                        frame->width, static_cast<std::uint8_t>(40 + index));
        }
        for (int y = 0; y < frame->height / 2; ++y) {
            std::fill_n(frame->data[1] + y * frame->linesize[1],
                        frame->width / 2, 128);
            std::fill_n(frame->data[2] + y * frame->linesize[2],
                        frame->width / 2, 128);
        }
        frame->pts = index;
        write_packets(false);
    }
    write_packets(true);
    requireFfmpeg(av_write_trailer(output), "Finishing the migration fixture");
    av_frame_free(&frame);
    avcodec_free_context(&codec);
    if ((output->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&output->pb);
    }
    avformat_free_context(output);
    return path;
}

media::MediaItem itemAt(const std::filesystem::path& path) {
    media::VideoMetadata metadata;
    metadata.source_path = path;
    metadata.display_name = "Media";
    metadata.frame_rate = 30.0;
    metadata.duration_seconds = 2.0;
    metadata.frame_count = 60;
    return {std::move(metadata), {}, "Media", "Unsorted", false};
}

void testMediaController() {
    application::EditorSession session;
    application::MediaController controller(session);
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-controller-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "clip.mkv";

    require(controller.commitImported(itemAt(source)).changed(),
            "A media item was not committed into the session library.");
    require(session.legacyTimelineForUi().addClip(
                0, controller.library().items().front().metadata, 0) ==
                timeline::AddClipResult::Added,
            "A clip referencing renamed media could not be prepared.");
    require(controller.library().contains(source),
            "The canonical media path index did not resolve the imported item.");
    require(controller.commitImported(itemAt(source)).code ==
                application::MediaCommandCode::Duplicate,
            "An online duplicate was not rejected as an expected domain result.");
    require(controller.rename(source, "Renamed clip").changed(),
            "The media controller did not rename an item.");
    require(session.timeline().tracks().front().clips.front().display_name == "Renamed clip",
            "Renaming a media item did not synchronize its timeline clip labels.");
    require(controller.moveToBin(source, "Footage/Selected").changed(),
            "The media controller did not move an item to a bin.");
    require(controller.createBin("Footage/Empty").changed(),
            "The media controller did not create an empty bin.");
    require(controller.markOffline(source).changed(),
            "The media controller did not mark media offline.");
    require(controller.library().items().front().offline,
            "The offline state was not kept in the session library.");

    auto restored = itemAt(source);
    restored.metadata.display_name = "Disk name";
    require(controller.commitImported(std::move(restored)).changed(),
            "Importing an offline path did not restore the existing item.");
    require(controller.library().items().front().display_name == "Renamed clip",
            "Restoring media discarded its project display name.");
    require(controller.library().items().front().bin_path == "Footage/Selected",
            "Restoring media discarded its bin assignment.");

    require(controller.renameBin("Footage", "Archive").changed(),
            "The media controller did not rename a bin subtree.");
    require(controller.library().items().front().bin_path == "Archive/Selected",
            "Renaming a bin did not update media assignments.");
    controller.clear();
    require(controller.library().empty() && controller.library().bins().size() == 1,
            "Clearing the session library did not restore the default bin.");
}

void testSequentialImportAndPartialFailure() {
    std::vector<std::filesystem::path> invoked;
    application::MediaImportService service([&invoked](const auto& path) {
        invoked.push_back(path);
        if (path.filename() == "bad.mkv") throw media::MediaError("probe failed", -17);
        return itemAt(path);
    });
    std::atomic_bool cancel{false};
    std::vector<std::size_t> progress;
    const auto result = service.process(
        41, 12, 8,
        {"first.mkv", "bad.mkv", "last.mkv"},
        cancel,
        [&progress](std::size_t current, std::size_t, const auto&) {
            progress.push_back(current);
        });

    require(result.work_id == 41 && result.project_generation == 12 &&
                result.selection_generation == 8,
            "Import generations were not propagated to the result.");
    require(invoked.size() == 3 && progress == std::vector<std::size_t>{0, 1, 2},
            "Files were not processed sequentially after an individual failure.");
    require(result.files.size() == 3 &&
                result.files[0].status == application::MediaImportFileStatus::Imported &&
                result.files[1].status == application::MediaImportFileStatus::Failed &&
                result.files[1].error_code == -17 &&
                result.files[2].status == application::MediaImportFileStatus::Imported,
            "A partial import failure prevented later files from finishing.");
}

void testCancellationDiscardsActiveAndSkipsFollowingFiles() {
    std::atomic_bool cancel{false};
    std::vector<std::string> invoked;
    application::MediaImportService service([&](const auto& path) {
        invoked.push_back(path.filename().string());
        if (path.filename() == "active.mkv") cancel.store(true);
        return itemAt(path);
    });

    const auto result = service.process(
        9, 3, 2,
        {"done.mkv", "active.mkv", "skipped.mkv"},
        cancel);
    require(result.cancelled, "A cancellation request was not reflected in the batch result.");
    require(invoked == std::vector<std::string>{"done.mkv", "active.mkv"},
            "The importer started a later file after cancellation.");
    require(result.files.size() == 2 &&
                result.files[0].status == application::MediaImportFileStatus::Imported &&
                result.files[1].status == application::MediaImportFileStatus::Discarded &&
                !result.files[1].item.has_value(),
            "Cancellation did not preserve completed files and discard the active result.");
}

void testProjectControllerDirtyAutosaveSaveAndReset() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-project-controller-" + unique);
    std::filesystem::create_directories(root);

    application::EditorSession session;
    application::ProjectController controller(session, root / "recovery", "test-session");
    const auto presentation = application::TimelinePresentationState{1.25, 46.0};
    const auto baseline = controller.document(presentation);
    const auto original_track_name = session.timeline().tracks().front().name;
    controller.establishBaseline(baseline);
    require(!controller.updateDirtyState(presentation),
            "An untouched session was marked dirty.");

    require(session.legacyTimelineForUi().renameTrack(0, "Edited Track") ==
                timeline::TrackMutationResult::Changed,
            "The test could not mutate the timeline.");
    require(controller.updateDirtyState(presentation),
            "A changed timeline was not reflected in the dirty state.");
    require(session.legacyTimelineForUi().renameTrack(0, original_track_name) ==
                timeline::TrackMutationResult::Changed &&
                !controller.updateDirtyState(presentation) &&
                controller.document(presentation) == baseline,
            "Returning to the canonical saved document did not clear the dirty state.");
    require(session.legacyTimelineForUi().renameTrack(0, "Edited Track") ==
                timeline::TrackMutationResult::Changed &&
                controller.updateDirtyState(presentation),
            "A new edit after restoring the baseline was not marked dirty.");
    const auto autosave = controller.autosave(true, 5, presentation);
    require(autosave.status == application::ProjectOperationStatus::Applied,
            "The controller did not create an autosave snapshot.");
    require(!controller.unsavedSnapshots().empty(),
            "The unsaved recovery snapshot was not discoverable.");

    const auto project_path = root / "roundtrip.csp";
    const auto saved = controller.saveTo(project_path, presentation);
    require(saved.succeeded() && saved.status == application::ProjectOperationStatus::Applied,
            "The controller did not save a project document.");
    require(!controller.dirty() && controller.projectPath() ==
                std::optional<std::filesystem::path>(
                    media::MediaLibrary::canonicalPath(project_path)),
            "Saving did not update the session path and dirty baseline.");
    require(project::load(project_path) == controller.document(presentation),
            "The saved project did not round-trip through the project file facade.");

    controller.reset();
    require(!controller.projectPath().has_value() && !controller.dirty() &&
                session.mediaLibrary().empty() && session.timeline().trackCount() == 1,
            "Reset did not return the session to a clean blank project.");
    std::filesystem::remove_all(root);
}

void testProjectOpenPreparationIsTransactionalAndPreservesOfflineMedia() {
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-open-service-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto missing_media = root / "missing.mkv";
    const auto project_path = root / "offline.csp";
    project::ProjectDocument document;
    document.bins = {"Unsorted", "Footage/Offline"};
    document.media.push_back({missing_media, "Offline footage", "Footage/Offline", false,
                              media::MediaKind::Video});
    project::ProjectTrack track;
    track.track_id = 17;
    track.name = "V1";
    project::ProjectClip clip;
    clip.clip_id = 29;
    clip.source_path = missing_media;
    clip.duration_frames = 12;
    clip.source_duration_frames = 12;
    track.clips.push_back(clip);
    document.timeline_tracks.push_back(track);
    project::save(project_path, document);

    std::atomic_bool cancel{false};
    application::ProjectOpenService service;
    const auto prepared = service.prepare(
        project_path, std::nullopt, std::nullopt, cancel);
    require(prepared.status == application::ProjectOpenStatus::Prepared &&
                prepared.prepared.has_value(),
            "A project with missing media was not prepared.");
    require(prepared.prepared->media_library.size() == 1 &&
                prepared.prepared->media_library.items().front().offline,
            "Missing project media was not represented as offline.");
    require(prepared.prepared->timeline.tracks.front().track_id == 17 &&
                prepared.prepared->timeline.tracks.front().clips.front().clip_id == 29,
            "Stable track and clip IDs were not preserved during preparation.");
    require(!prepared.warnings.empty(),
            "Missing media was not surfaced as a nonfatal project-open warning.");

    cancel.store(true);
    const auto cancelled = service.prepare(
        project_path, std::nullopt, std::nullopt, cancel);
    require(cancelled.status == application::ProjectOpenStatus::Cancelled &&
                !cancelled.prepared.has_value(),
            "A cancelled preparation returned a commit-ready project.");

    const auto corrupt_path = root / "corrupt.csp";
    {
        std::ofstream file(corrupt_path, std::ios::binary);
        file << "not a project document";
    }
    cancel.store(false);
    const auto corrupt = service.prepare(
        corrupt_path, std::nullopt, std::nullopt, cancel);
    require(corrupt.status == application::ProjectOpenStatus::Failed &&
                corrupt.failure.has_value(),
            "An invalid project file was accepted for preparation.");

    const auto invalid_media = root / "invalid.mkv";
    {
        std::ofstream file(invalid_media, std::ios::binary);
        file << "not a decodable media stream";
    }
    project::ProjectDocument invalid_media_document;
    invalid_media_document.media.push_back({
        invalid_media, "Invalid", "Unsorted", false, media::MediaKind::Video});
    const auto invalid_media_project = root / "invalid-media.csp";
    project::save(invalid_media_project, invalid_media_document);
    const auto decode_failure = service.prepare(
        invalid_media_project, std::nullopt, std::nullopt, cancel);
    require(decode_failure.status == application::ProjectOpenStatus::Failed &&
                decode_failure.failure.has_value() &&
                decode_failure.failure->kind == application::ProjectOpenIssueKind::Media,
            "A decoding failure for existing project media did not fail preparation.");

    const auto legacy_gif = root / "legacy-video.gif";
    const auto gif_bytes = QByteArray::fromBase64(
        "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
    {
        std::ofstream file(legacy_gif, std::ios::binary);
        file.write(gif_bytes.constData(), gif_bytes.size());
    }
    project::ProjectDocument legacy_document;
    legacy_document.media.push_back({
        legacy_gif, "Legacy animated media", "Unsorted", false,
        media::MediaKind::Video});
    const auto legacy_project = root / "legacy-gif.csp";
    project::save(legacy_project, legacy_document);
    const auto legacy_prepared = service.prepare(
        legacy_project, std::nullopt, std::nullopt, cancel);
    require(legacy_prepared.status == application::ProjectOpenStatus::Prepared &&
                legacy_prepared.prepared.has_value() &&
                legacy_prepared.prepared->media_library.items().front().metadata.kind ==
                    media::MediaKind::Video,
            "Project opening did not preserve the stored media kind for legacy GIF media.");

    std::filesystem::remove_all(root);
}

QColor framePixel(const media::VideoFrame& frame, int x, int y) {
    const auto offset = static_cast<std::size_t>(y) * frame.stride +
        static_cast<std::size_t>(x) * 4;
    return QColor(frame.rgba_pixels.at(offset), frame.rgba_pixels.at(offset + 1),
                  frame.rgba_pixels.at(offset + 2), frame.rgba_pixels.at(offset + 3));
}

void testLinkedImageProjectOpenUsesSharedOutputAndClipVariant() {
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-linked-open-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto missing_source = root / "missing-source.png";
    const auto shared_document = root / "shared.cimg";
    const auto shared_output = root / "shared.png";
    const auto variant_document = root / "variant.cimg";
    const auto variant_output = root / "variant.png";
    QImage shared_image(4, 4, QImage::Format_ARGB32);
    shared_image.fill(QColor(20, 220, 30, 255));
    QImage variant_image(4, 4, QImage::Format_ARGB32);
    variant_image.fill(QColor(20, 30, 230, 255));
    require(shared_image.save(QString::fromStdString(shared_output.string())) &&
                variant_image.save(QString::fromStdString(variant_output.string())),
            "The linked-output fixtures could not be written.");

    project::ProjectDocument document;
    const media::LinkedImageReference shared_link{
        "shared-image-id", shared_document, shared_output};
    const media::LinkedImageReference variant_link{
        "clip-variant-id", variant_document, variant_output};
    document.media.push_back({missing_source, "Linked still", "Stills", true,
                              media::MediaKind::Image, shared_link});
    project::ProjectTrack track;
    track.track_id = 3;
    track.name = "V1";
    project::ProjectClip clip;
    clip.clip_id = 4;
    clip.source_path = missing_source;
    clip.duration_frames = 30;
    clip.source_duration_frames = 30;
    clip.kind = timeline::ClipKind::Image;
    clip.image_editor_variant = variant_link;
    track.clips.push_back(clip);
    document.timeline_tracks.push_back(track);
    const auto project_path = root / "linked.csp";
    project::save(project_path, document);

    std::atomic_bool cancel{false};
    application::ProjectOpenService service;
    const auto prepared = service.prepare(
        project_path, std::nullopt, std::nullopt, cancel);
    require(prepared.status == application::ProjectOpenStatus::Prepared &&
                prepared.prepared.has_value(),
            "A linked project with a missing original source did not open.");
    const auto& media_item = prepared.prepared->media_library.items().front();
    require(!media_item.offline && media_item.metadata.kind == media::MediaKind::Image &&
                media_item.metadata.source_path ==
                    media::MediaLibrary::canonicalPath(missing_source) &&
                media_item.image_editor_link == shared_link &&
                framePixel(media_item.first_frame, 1, 1) == QColor(20, 220, 30, 255),
            "Project open did not use the shared output while preserving original media identity.");
    const auto& prepared_clip = prepared.prepared->timeline.tracks.front().clips.front();
    require(prepared_clip.image_editor_variant == variant_link &&
                prepared_clip.still_image_override != nullptr &&
                framePixel(*prepared_clip.still_image_override, 1, 1) ==
                    QColor(20, 30, 230, 255),
            "A clip-specific output did not take precedence over the shared image.");

    std::filesystem::remove(variant_output);
    const auto missing_variant = service.prepare(
        project_path, std::nullopt, std::nullopt, cancel);
    require(missing_variant.status == application::ProjectOpenStatus::Prepared &&
                missing_variant.prepared.has_value() &&
                !missing_variant.prepared->timeline.tracks.front().clips.front()
                     .still_image_override &&
                !missing_variant.warnings.empty(),
            "A missing clip variant did not safely fall back to the shared Media Pool image.");

    std::filesystem::remove_all(root);
}

void testLegacyTimelineRateMigrationAndOfflineReconnect() {
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-timing-migration-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto video = create24FpsVideo(root / "twenty-four-fps.mkv");
    const auto offline_image = root / "offline-still.png";
    const auto project_path = root / "legacy-v10.csp";

    project::ProjectDocument document;
    document.media.push_back({
        video, "24 FPS source", "Unsorted", false, media::MediaKind::Video});
    document.media.push_back({
        offline_image, "Offline still", "Unsorted", false, media::MediaKind::Image});
    project::ProjectTrack track;
    track.track_id = 1;
    track.name = "V1";
    project::ProjectClip first;
    first.clip_id = 1;
    first.source_path = video;
    first.duration_frames = 24;
    first.source_duration_frames = 24;
    project::ProjectClip still;
    still.clip_id = 2;
    still.kind = timeline::ClipKind::Image;
    still.source_path = offline_image;
    still.timeline_start_frame = 24;
    still.duration_frames = 150;
    still.source_duration_frames = 150;
    project::ProjectClip last;
    last.clip_id = 3;
    last.source_path = video;
    last.timeline_start_frame = 174;
    last.source_start_frame = 8;
    last.duration_frames = 24;
    last.source_duration_frames = 24;
    track.clips = {first, still, last};
    track.transitions.push_back({
        1, 2, timeline::TransitionKind::CrossDissolve, 15});
    document.timeline_tracks.push_back(track);
    project::save(project_path, document);

    std::ifstream project_input(project_path, std::ios::binary);
    const std::string current_json{
        std::istreambuf_iterator<char>(project_input),
        std::istreambuf_iterator<char>()};
    project_input.close();
    auto root_object = QJsonDocument::fromJson(
        QByteArray::fromStdString(current_json)).object();
    root_object.insert("version", 10);
    auto timeline_object = root_object.value("timeline").toObject();
    timeline_object.remove("frame_rate");
    auto tracks = timeline_object.value("tracks").toArray();
    for (qsizetype track_index = 0; track_index < tracks.size(); ++track_index) {
        auto track_object = tracks.at(track_index).toObject();
        auto clips = track_object.value("clips").toArray();
        for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
            auto clip_object = clips.at(clip_index).toObject();
            clip_object.remove("source_duration_frames");
            clip_object.remove("source_duration_migration_pending");
            clips.replace(clip_index, clip_object);
        }
        track_object.insert("clips", clips);
        tracks.replace(track_index, track_object);
    }
    timeline_object.insert("tracks", tracks);
    root_object.insert("timeline", timeline_object);
    {
        std::ofstream output(project_path, std::ios::binary | std::ios::trunc);
        const auto json = QJsonDocument(root_object).toJson();
        output.write(json.constData(), json.size());
    }

    std::atomic_bool cancel{false};
    application::ProjectOpenService service;
    auto migrated = service.prepare(
        project_path, project_path, std::nullopt, cancel);
    if (migrated.status != application::ProjectOpenStatus::Prepared ||
        !migrated.prepared.has_value()) {
        throw std::runtime_error(
            "A v10 project with offline media did not prepare for timing migration: " +
            (migrated.failure.has_value() ? migrated.failure->cause :
                std::string("no failure details")));
    }
    auto& prepared = *migrated.prepared;
    require(prepared.document.timeline_frame_rate == timeline::FrameRate{24, 1} &&
                !prepared.document.timing_migration_required &&
                prepared.timeline.frame_rate == timeline::FrameRate{24, 1},
            "The v10 project did not infer and preserve its first online video rate.");
    const auto& offline_clip = prepared.document.timeline_tracks.front().clips[1];
    require(offline_clip.source_duration_frames == 150 &&
                offline_clip.duration_frames == 150 &&
                offline_clip.source_duration_migration_pending,
            "An offline legacy clip did not keep its source duration pending reconnection.");
    require(prepared.document.timeline_tracks.front().transitions.size() == 1 &&
                prepared.document.timeline_tracks.front().clips[2].timeline_start_frame == 174,
            "The legacy transition or following clip moved before the offline source was available.");
    project::save(project_path, prepared.document);
    application::EditorSession migrated_session;
    application::ProjectController migrated_controller(migrated_session);
    migrated_controller.commitPrepared(
        std::move(prepared.media_library), std::move(prepared.timeline),
        project_path, prepared.document);
    require(!migrated_controller.dirty(),
            "Opening a migrated v10 project marked the project dirty.");

    QImage still_image(8, 8, QImage::Format_RGBA8888);
    still_image.fill(QColor(30, 120, 220, 255));
    require(still_image.save(QString::fromStdString(offline_image.string())),
            "Could not create the offline still used for the reconnect test.");
    auto reconnected = service.prepare(
        project_path, project_path, std::nullopt, cancel);
    require(reconnected.status == application::ProjectOpenStatus::Prepared &&
                reconnected.prepared.has_value(),
            "Reconnecting a pending offline clip failed project preparation.");
    const auto& reconnected_document = reconnected.prepared->document;
    const auto& reconnected_track = reconnected_document.timeline_tracks.front();
    require(reconnected_document.timeline_frame_rate == timeline::FrameRate{24, 1} &&
                !reconnected_track.clips[1].source_duration_migration_pending &&
                reconnected_track.clips[1].source_duration_frames == 150 &&
                reconnected_track.clips[1].duration_frames == 120 &&
                reconnected_track.clips[2].timeline_start_frame == 144 &&
                reconnected_track.clips[1].timeline_start_frame +
                    reconnected_track.clips[1].duration_frames ==
                    reconnected_track.clips[2].timeline_start_frame &&
                reconnected_track.transitions.size() == 1,
            "Reconnection did not convert the source duration once and preserve transition continuity.");

    const auto fallback_path = root / "legacy-without-video.csp";
    project::ProjectDocument fallback_document;
    fallback_document.media.push_back({
        offline_image, "Still image", "Unsorted", false, media::MediaKind::Image});
    project::ProjectTrack fallback_track;
    fallback_track.track_id = 1;
    fallback_track.name = "V1";
    project::ProjectClip fallback_clip;
    fallback_clip.clip_id = 1;
    fallback_clip.kind = timeline::ClipKind::Image;
    fallback_clip.source_path = offline_image;
    fallback_clip.duration_frames = 150;
    fallback_clip.source_duration_frames = 150;
    fallback_track.clips.push_back(fallback_clip);
    fallback_document.timeline_tracks.push_back(fallback_track);
    project::save(fallback_path, fallback_document);
    std::ifstream fallback_input(fallback_path, std::ios::binary);
    const std::string fallback_json{
        std::istreambuf_iterator<char>(fallback_input),
        std::istreambuf_iterator<char>()};
    fallback_input.close();
    auto fallback_root = QJsonDocument::fromJson(
        QByteArray::fromStdString(fallback_json)).object();
    fallback_root.insert("version", 10);
    auto fallback_timeline = fallback_root.value("timeline").toObject();
    fallback_timeline.remove("frame_rate");
    auto fallback_tracks = fallback_timeline.value("tracks").toArray();
    auto fallback_track_object = fallback_tracks.at(0).toObject();
    auto fallback_clips = fallback_track_object.value("clips").toArray();
    auto fallback_clip_object = fallback_clips.at(0).toObject();
    fallback_clip_object.remove("source_duration_frames");
    fallback_clip_object.remove("source_duration_migration_pending");
    fallback_clips.replace(0, fallback_clip_object);
    fallback_track_object.insert("clips", fallback_clips);
    fallback_tracks.replace(0, fallback_track_object);
    fallback_timeline.insert("tracks", fallback_tracks);
    fallback_root.insert("timeline", fallback_timeline);
    {
        std::ofstream output(fallback_path, std::ios::binary | std::ios::trunc);
        const auto json = QJsonDocument(fallback_root).toJson();
        output.write(json.constData(), json.size());
    }
    const auto fallback_result = service.prepare(
        fallback_path, fallback_path, std::nullopt, cancel);
    require(fallback_result.status == application::ProjectOpenStatus::Prepared &&
                fallback_result.prepared.has_value() &&
                fallback_result.prepared->document.timeline_frame_rate ==
                    timeline::FrameRate{30, 1},
            "A legacy project without an online video did not use the 30/1 FPS fallback.");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testMediaController();
        testSequentialImportAndPartialFailure();
        testCancellationDiscardsActiveAndSkipsFollowingFiles();
        testProjectControllerDirtyAutosaveSaveAndReset();
        testProjectOpenPreparationIsTransactionalAndPreservesOfflineMedia();
        testLegacyTimelineRateMigrationAndOfflineReconnect();
        testLinkedImageProjectOpenUsesSharedOutputAndClipVariant();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
