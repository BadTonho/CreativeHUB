#include "project/project_file.h"

#include <creative_suite/effects/effects.h>

#include <QCoreApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-project-test-" + std::to_string(stamp));
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory / "media");
        const auto project_path = directory / "project.csp";
        const auto first_source = directory / "media" / "first video.mkv";
        const auto image_source = directory / "media" / "still.png";
        const auto outside_source = std::filesystem::temp_directory_path() /
            ("creative-suite-project-outside-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
        std::ofstream(first_source, std::ios::binary).close();
        std::ofstream(image_source, std::ios::binary).close();
        std::ofstream(outside_source, std::ios::binary).close();

        project::ProjectDocument original;
        original.timeline_frame_rate = {30000, 1001};
        require(original.timeline_row_height == 70.0,
                "A new project document did not start with the 70-pixel default row height.");
        original.media = {
            {first_source, "First Video", "Footage/Scenes", false},
            {outside_source, "Offline Asset", "Unsorted", true},
            {image_source, "Still Image", "Footage/Stills", false, media::MediaKind::Image},
        };
        original.media.back().image_editor_link = media::LinkedImageReference{
            "shared-still",
            directory / "media" / "still.png.image-editor" / "asset.cimg",
            directory / "media" / "still.png.image-editor" / "asset.png"};
        original.timeline_tracks = {
            {"Video 1", 0.75, true, {
                {first_source, 0, 30, 60, 0.5, true},
                {first_source, 60, 0, 30, 1.25, false},
            }},
        };
        original.timeline_tracks.front().clips[0].source_duration_frames = 60;
        original.timeline_tracks.front().clips[1].source_duration_frames = 30;
        original.timeline_tracks.front().track_id = 1;
        original.timeline_tracks.front().clips[0].clip_id = 1;
        original.timeline_tracks.front().clips[1].clip_id = 2;
        project::ProjectClip title_clip;
        title_clip.clip_id = 3;
        title_clip.timeline_start_frame = 10;
        title_clip.duration_frames = 20;
        title_clip.kind = timeline::ClipKind::Text;
        title_clip.text.content = "Title";
        title_clip.text.font_size_pixels = 64.0;
        title_clip.text.alignment = timeline::TextAlignment::Left;
        title_clip.text.color = {255, 200, 100, 230};
        original.timeline_tracks.front().clips.push_back(title_clip);
        project::ProjectClip image_clip;
        image_clip.clip_id = 4;
        image_clip.source_path = image_source;
        image_clip.timeline_start_frame = 100;
        image_clip.duration_frames = 150;
        image_clip.source_duration_frames = 150;
        image_clip.kind = timeline::ClipKind::Image;
        image_clip.image_editor_variant = media::LinkedImageReference{
            "clip-still-uuid",
            directory / "media" / "still.png.image-editor" / "clips" /
                "clip-still-uuid" / "document.cimg",
            directory / "media" / "still.png.image-editor" / "clips" /
                "clip-still-uuid" / "output.png"};
        original.timeline_tracks.front().clips.push_back(image_clip);
        auto node_graph = fusion::nodes::makePassthroughGraph();
        node_graph.nodes.push_back(fusion::nodes::Node{
            3, fusion::nodes::NodeType::Input, 80.0, 280.0});
        node_graph.nodes.push_back(fusion::nodes::Node{
            4, fusion::nodes::NodeType::Merge, 380.0, 180.0});
        node_graph.nodes[2].source_path = image_source;
        node_graph.nodes[2].source_is_still = true;
        node_graph.connections = {{1, 4, 0}, {3, 4, 1}, {4, 2, 0}};
        node_graph.next_id = 5;
        original.timeline_tracks.front().clips.front().node_graph = node_graph;
        auto disconnected_graph = fusion::nodes::makePassthroughGraph();
        disconnected_graph.connections.clear();
        original.timeline_tracks.front().clips.back().node_graph = disconnected_graph;
        original.timeline_tracks.front().clips[1].timeline_start_frame = 45;
        original.timeline_tracks.front().clips.back().timeline_start_frame = 85;
        original.timeline_tracks.front().transitions.push_back(
            project::ProjectTransition{
                0,
                1,
                timeline::TransitionKind::CrossDissolve,
                15});
        original.canvas_width = 1920;
        original.canvas_height = 1080;
        original.timeline_zoom = 512.0;
        original.timeline_row_height = 123.5;
        original.timeline_tracks.front().clips.front().transform.position_x = 0.25;
        original.timeline_tracks.front().clips.front().transform.rotation_degrees = 12.0;
        original.timeline_tracks.front().clips.front().keyframes.position_x = {{0, 0.25}, {30, 0.75}};
        original.timeline_tracks.front().clips.front().effects = {
            creative_suite::effects::makeDefaultInstance("video.grayscale"),
            creative_suite::effects::makeDefaultInstance("video.brightness")};
        require(creative_suite::effects::setParameterValue(
                    original.timeline_tracks.front().clips.front().effects.back(),
                    "amount", 24.0),
                "Could not set a non-default effect value for project coverage.");
        original.timeline_tracks.front().clips.front().effects.back().enabled = false;
        project::ProjectClip second_track_title;
        second_track_title.clip_id = 5;
        second_track_title.timeline_start_frame = 0;
        second_track_title.duration_frames = 15;
        second_track_title.kind = timeline::ClipKind::Text;
        second_track_title.text.content = "Second track";
        second_track_title.text.font_size_pixels = 48.0;
        original.timeline_tracks.push_back(
            {"Video 2", 1.0, false, {second_track_title}, {}});
        original.timeline_tracks.back().track_id = 2;
        project::save(project_path, original);

        const auto loaded = project::load(project_path);
        require(loaded == original, "A project did not round-trip through JSON.");
        auto portrait_project = original;
        portrait_project.canvas_width = 1080;
        portrait_project.canvas_height = 1920;
        const auto portrait_project_path = directory / "portrait.csp";
        project::save(portrait_project_path, portrait_project);
        require(project::load(portrait_project_path) == portrait_project,
                "A version 20 portrait project did not preserve its canvas and project data.");

        auto invalid_canvas_project = portrait_project;
        invalid_canvas_project.canvas_width = 1440;
        bool unsupported_canvas_rejected = false;
        try {
            project::save(directory / "unsupported-canvas.csp", invalid_canvas_project);
        } catch (const project::ProjectError& error) {
            unsupported_canvas_rejected =
                error.code() == project::ProjectErrorCode::InvalidValue;
        }
        require(unsupported_canvas_rejected,
                "A project canvas outside the two supported formats was accepted.");
        require(loaded.timeline_tracks.size() == 2 &&
                    loaded.timeline_tracks[1].name == "Video 2" &&
                    loaded.timeline_tracks[1].clips.size() == 1 &&
                    loaded.timeline_tracks[0].clips.size() == 4,
                "A multi-track project did not preserve every track and clip.");
        require(loaded.timeline_tracks[0].clips[0].source_start_frame == 30,
                "A source offset was not preserved.");
        require(loaded.timeline_tracks[0].clips[1].source_path ==
                    loaded.timeline_tracks[0].clips[0].source_path,
                "Repeated source occurrences were not preserved.");
        require(loaded.timeline_tracks[0].audio_gain == 0.75 &&
                    loaded.timeline_tracks[0].audio_muted &&
                    loaded.timeline_tracks[0].clips[0].audio_gain == 0.5 &&
                    loaded.timeline_tracks[0].clips[0].audio_muted,
                "Audio gain and mute settings were not preserved.");
        require(loaded.canvas_width == 1920 && loaded.canvas_height == 1080 &&
                    loaded.timeline_tracks[0].clips[0].transform.position_x == 0.25 &&
                    loaded.timeline_tracks[0].clips[0].transform.rotation_degrees == 12.0 &&
                    loaded.timeline_tracks[0].clips[0].keyframes.position_x.size() == 2,
                "Transform and keyframe data was not preserved.");
        require(loaded.timeline_tracks[0].clips[0].effects ==
                    original.timeline_tracks[0].clips[0].effects,
                "The ordered effect stack and parameters were not preserved.");
        require(loaded.media.back().image_editor_link ==
                    original.media.back().image_editor_link &&
                    loaded.timeline_tracks.front().clips.back().image_editor_variant ==
                    original.timeline_tracks.front().clips.back().image_editor_variant,
                "Version 10 linked media and clip variant references were not preserved.");

        std::ifstream saved_file(project_path, std::ios::binary);
        const std::string saved_json{
            std::istreambuf_iterator<char>(saved_file),
            std::istreambuf_iterator<char>()};
        saved_file.close();
        require(saved_json.find("media/first video.mkv") != std::string::npos,
                "A source inside the project was not stored relatively.");
        require(saved_json.find("creative-suite-project-outside-") != std::string::npos,
                "An outside source was not stored in the project.");
        require(saved_json.find("Footage/Scenes") != std::string::npos,
                "The media bin was not stored in the project.");
        require(saved_json.find("audio_gain") != std::string::npos &&
                    saved_json.find("audio_muted") != std::string::npos,
                "Audio settings were not written to the project.");
        require(saved_json.find("\"kind\": \"text\"") != std::string::npos &&
                    saved_json.find("\"content\": \"Title\"") != std::string::npos,
                "Text clip content and kind were not written to the project.");
        require(saved_json.find("\"track_id\": 1") != std::string::npos &&
                    saved_json.find("\"clip_id\": 1") != std::string::npos,
                "Stable track and clip identifiers were not written to the project.");
        require(saved_json.find("\"version\": 20") != std::string::npos &&
                    saved_json.find("\"frame_rate\"") != std::string::npos &&
                    saved_json.find("\"numerator\": 30000") != std::string::npos &&
                    saved_json.find("\"denominator\": 1001") != std::string::npos &&
                    saved_json.find("\"source_duration_frames\": 60") != std::string::npos &&
                    saved_json.find("\"zoom\": 512") != std::string::npos &&
                    saved_json.find("\"row_height\": 123.5") != std::string::npos &&
                    saved_json.find("\"transitions\"") != std::string::npos &&
                    saved_json.find("cross_dissolve") != std::string::npos &&
                    saved_json.find("\"effects\"") != std::string::npos &&
                    saved_json.find("video.grayscale") != std::string::npos &&
                    saved_json.find("\"node_graph\"") != std::string::npos &&
                    saved_json.find("\"type\": \"merge\"") != std::string::npos &&
                    saved_json.find("\"enabled\": false") != std::string::npos &&
                    saved_json.find("image_editor_link") != std::string::npos &&
                    saved_json.find("image_editor_variant") != std::string::npos,
                "Timeline timing, effect states, Fusion nodes, and linked image references were not written to the version 20 project.");

        auto legacy_v19_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        legacy_v19_json.insert("version", 19);
        const auto legacy_v19_path = directory / "legacy-v19.csp";
        writeText(legacy_v19_path,
                  QJsonDocument(legacy_v19_json).toJson().toStdString());
        const auto legacy_v19 = project::load(legacy_v19_path);
        require(!legacy_v19.timeline_tracks.front().clips.front().node_graph.has_value(),
                "A version 19 project must open without applying v20 node graphs.");

        auto legacy_v18_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        legacy_v18_json.insert("version", 18);
        const auto legacy_v18_path = directory / "legacy-v18.csp";
        writeText(legacy_v18_path,
                  QJsonDocument(legacy_v18_json).toJson().toStdString());
        const auto legacy_v18 = project::load(legacy_v18_path);
        require(legacy_v18.canvas_width == 1920 &&
                    legacy_v18.canvas_height == 1080 &&
                    legacy_v18.timeline_frame_rate == original.timeline_frame_rate,
                "A version 18 project did not retain its legacy canvas and saved Timeline rate.");

        auto legacy_portrait_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        legacy_portrait_json.insert("version", 18);
        auto legacy_portrait_canvas = legacy_portrait_json.value("canvas").toObject();
        legacy_portrait_canvas.insert("width", 1080);
        legacy_portrait_canvas.insert("height", 1920);
        legacy_portrait_json.insert("canvas", legacy_portrait_canvas);
        const auto legacy_portrait_path = directory / "legacy-portrait.csp";
        writeText(legacy_portrait_path,
                  QJsonDocument(legacy_portrait_json).toJson().toStdString());
        bool legacy_portrait_rejected = false;
        try {
            static_cast<void>(project::load(legacy_portrait_path));
        } catch (const project::ProjectError& error) {
            legacy_portrait_rejected =
                error.code() == project::ProjectErrorCode::InvalidValue;
        }
        require(legacy_portrait_rejected,
                "A pre-v19 project with portrait canvas dimensions was accepted.");

        auto unsupported_current_canvas_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        auto unsupported_current_canvas = unsupported_current_canvas_json
            .value("canvas").toObject();
        unsupported_current_canvas.insert("width", 1440);
        unsupported_current_canvas.insert("height", 1080);
        unsupported_current_canvas_json.insert("canvas", unsupported_current_canvas);
        const auto unsupported_current_canvas_path =
            directory / "unsupported-current-canvas.csp";
        writeText(unsupported_current_canvas_path,
                  QJsonDocument(unsupported_current_canvas_json).toJson().toStdString());
        bool unsupported_current_canvas_rejected = false;
        try {
            static_cast<void>(project::load(unsupported_current_canvas_path));
        } catch (const project::ProjectError& error) {
            unsupported_current_canvas_rejected =
                error.code() == project::ProjectErrorCode::InvalidValue;
        }
        require(unsupported_current_canvas_rejected,
                "A version 20 project with unsupported dimensions was accepted.");

        auto version_17_effects_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        version_17_effects_json.insert("version", 17);
        auto version_17_timeline = version_17_effects_json.value("timeline").toObject();
        auto version_17_tracks = version_17_timeline.value("tracks").toArray();
        for (qsizetype track_index = 0; track_index < version_17_tracks.size(); ++track_index) {
            auto track = version_17_tracks[track_index].toObject();
            auto clips = track.value("clips").toArray();
            for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
                auto clip = clips[clip_index].toObject();
                auto effects = clip.value("effects").toArray();
                for (qsizetype effect_index = 0; effect_index < effects.size(); ++effect_index) {
                    auto effect = effects[effect_index].toObject();
                    effect.remove("enabled");
                    effects.replace(effect_index, effect);
                }
                if (!effects.isEmpty()) clip.insert("effects", effects);
                clips.replace(clip_index, clip);
            }
            track.insert("clips", clips);
            version_17_tracks.replace(track_index, track);
        }
        version_17_timeline.insert("tracks", version_17_tracks);
        version_17_effects_json.insert("timeline", version_17_timeline);
        const auto version_17_effects_path = directory / "version-17-effects.csp";
        writeText(version_17_effects_path,
                  QJsonDocument(version_17_effects_json).toJson().toStdString());
        const auto reopened_version_17_effects = project::load(version_17_effects_path);
        require(reopened_version_17_effects.timeline_tracks.front().clips.front()
                        .effects.size() == 2 &&
                    std::all_of(
                        reopened_version_17_effects.timeline_tracks.front().clips.front()
                            .effects.begin(),
                        reopened_version_17_effects.timeline_tracks.front().clips.front()
                            .effects.end(),
                        [](const auto& effect) { return effect.enabled; }),
                "Version 17 effects must migrate as enabled by default.");

        for (const auto invalid_enabled_value : {
                 QJsonValue(QStringLiteral("false")), QJsonValue(1),
                 QJsonValue(QJsonValue::Undefined)}) {
            auto invalid_effect_state_json = QJsonDocument::fromJson(
                QByteArray::fromStdString(saved_json)).object();
            auto invalid_timeline = invalid_effect_state_json.value("timeline").toObject();
            auto invalid_tracks = invalid_timeline.value("tracks").toArray();
            auto invalid_track = invalid_tracks.at(0).toObject();
            auto invalid_clips = invalid_track.value("clips").toArray();
            auto invalid_clip = invalid_clips.at(0).toObject();
            auto invalid_effects = invalid_clip.value("effects").toArray();
            auto invalid_effect = invalid_effects.at(0).toObject();
            invalid_effect.insert("enabled", invalid_enabled_value);
            invalid_effects.replace(0, invalid_effect);
            invalid_clip.insert("effects", invalid_effects);
            invalid_clips.replace(0, invalid_clip);
            invalid_track.insert("clips", invalid_clips);
            invalid_tracks.replace(0, invalid_track);
            invalid_timeline.insert("tracks", invalid_tracks);
            invalid_effect_state_json.insert("timeline", invalid_timeline);
            const auto invalid_effect_state_path = directory / "invalid-effect-enabled.csp";
            writeText(invalid_effect_state_path,
                      QJsonDocument(invalid_effect_state_json).toJson().toStdString());
            try {
                static_cast<void>(project::load(invalid_effect_state_path));
                throw std::runtime_error("An invalid effect enabled state was accepted.");
            } catch (const project::ProjectError& error) {
                require(error.code() == project::ProjectErrorCode::InvalidValue,
                        "An invalid effect enabled state returned the wrong error category.");
            }
        }
        auto version_16_effects_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        version_16_effects_json.insert("version", 16);
        const auto version_16_effects_path = directory / "version-16-effects.csp";
        writeText(version_16_effects_path,
                  QJsonDocument(version_16_effects_json).toJson().toStdString());
        const auto reopened_version_16_effects = project::load(version_16_effects_path);
        require(std::all_of(
                    reopened_version_16_effects.timeline_tracks.begin(),
                    reopened_version_16_effects.timeline_tracks.end(),
                    [](const project::ProjectTrack& track) {
                        return std::all_of(track.clips.begin(), track.clips.end(),
                            [](const project::ProjectClip& clip) {
                                return clip.effects.empty();
                            });
                    }),
                "A version 16 project must load without visual effect stacks.");
        require(saved_json.find("\"kind\": \"image\"") != std::string::npos &&
                    loaded.media.back().kind == media::MediaKind::Image &&
                    loaded.timeline_tracks.front().clips.back().kind == timeline::ClipKind::Image,
                "Image media and image clip kinds were not persisted.");

        const auto audio_project_path = directory / "audio-project.csp";
        const auto audio_source = directory / "media" / "music.wav";
        project::ProjectDocument audio_document;
        audio_document.timeline_frame_rate = {30000, 1001};
        audio_document.media.push_back({
            audio_source, "Music", "Audio", false, media::MediaKind::Audio});
        project::ProjectTrack audio_track;
        audio_track.track_id = 1;
        audio_track.name = "Audio 1";
        audio_track.kind = timeline::TrackKind::Audio;
        project::ProjectClip audio_clip;
        audio_clip.clip_id = 1;
        audio_clip.source_path = audio_source;
        audio_clip.timeline_start_frame = 14;
        audio_clip.duration_frames = 45;
        audio_clip.kind = timeline::ClipKind::Audio;
        audio_clip.source_start_time_us = 275000;
        audio_clip.source_duration_time_us = 1500000;
        audio_clip.audio_gain = 0.75;
        audio_clip.audio_muted = true;
        audio_clip.audio_gain_keyframes = {{0, 0.0}, {45, 1.5}};
        audio_track.clips.push_back(audio_clip);
        auto incoming_audio_clip = audio_clip;
        incoming_audio_clip.clip_id = 2;
        incoming_audio_clip.timeline_start_frame = 44;
        incoming_audio_clip.source_start_time_us = 1'500'000;
        incoming_audio_clip.source_duration_time_us = 1'500'000;
        audio_track.clips.push_back(incoming_audio_clip);
        audio_track.transitions.push_back(project::ProjectTransition{
            0, 1, timeline::TransitionKind::AudioCrossfade, 15});
        audio_document.timeline_tracks.push_back(audio_track);
        project::save(audio_project_path, audio_document);
        std::ifstream audio_file(audio_project_path, std::ios::binary);
        const std::string audio_json(
            (std::istreambuf_iterator<char>(audio_file)),
            std::istreambuf_iterator<char>{});
        audio_file.close();
        require(audio_json.find("\"source_start_time_us\"") != std::string::npos &&
                    audio_json.find("\"source_duration_time_us\"") != std::string::npos &&
                    audio_json.find("\"audio_gain_keyframes\"") != std::string::npos &&
                    audio_json.find("\"source_start_frame\"") == std::string::npos &&
                    audio_json.find("\"source_duration_frames\"") == std::string::npos,
                "Version 16 did not serialize the Audio crossfade, envelope, and microsecond source timing.");
        const auto reopened_audio = project::load(audio_project_path);
        require(reopened_audio == audio_document &&
                    reopened_audio.timeline_tracks.front().kind ==
                        timeline::TrackKind::Audio &&
                    reopened_audio.timeline_tracks.front().clips.front()
                            .source_start_time_us == 275000 &&
                    reopened_audio.timeline_tracks.front().clips.front()
                            .source_duration_time_us == 1500000 &&
                reopened_audio.timeline_tracks.front().transitions.size() == 1 &&
                reopened_audio.timeline_tracks.front().transitions.front().kind ==
                    timeline::TransitionKind::AudioCrossfade &&
                reopened_audio.timeline_tracks.front().transitions.front().duration_frames == 15,
                "Version 16 did not preserve Audio track types, envelopes, source offsets, and crossfades.");
        auto version_15_audio_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(audio_json)).object();
        version_15_audio_json.insert("version", 15);
        auto version_15_timeline = version_15_audio_json.value("timeline").toObject();
        auto version_15_tracks = version_15_timeline.value("tracks").toArray();
        auto version_15_track = version_15_tracks.at(0).toObject();
        version_15_track.insert("clips", QJsonArray{
            version_15_track.value("clips").toArray().at(0)});
        version_15_track.insert("transitions", QJsonArray{});
        version_15_tracks.replace(0, version_15_track);
        version_15_timeline.insert("tracks", version_15_tracks);
        version_15_audio_json.insert("timeline", version_15_timeline);
        writeText(audio_project_path,
                  QJsonDocument(version_15_audio_json).toJson().toStdString());
        const auto reopened_version_15 = project::load(audio_project_path);
        require(reopened_version_15.timeline_tracks.front().transitions.empty() &&
                    reopened_version_15.timeline_tracks.front().clips.front()
                        .audio_gain_keyframes ==
                        std::vector<timeline::AudioGainKeyframe>{{0, 0.0}, {45, 1.5}},
                "A version 15 project inferred an Audio Crossfade or lost its volume envelope.");
        auto version_14_audio_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(audio_json)).object();
        version_14_audio_json.insert("version", 14);
        auto version_14_timeline = version_14_audio_json.value("timeline").toObject();
        auto version_14_tracks = version_14_timeline.value("tracks").toArray();
        auto version_14_track = version_14_tracks.at(0).toObject();
        version_14_track.insert("clips", QJsonArray{version_14_track.value("clips").toArray().at(0)});
        version_14_track.insert("transitions", QJsonArray{});
        version_14_tracks.replace(0, version_14_track);
        version_14_timeline.insert("tracks", version_14_tracks);
        version_14_audio_json.insert("timeline", version_14_timeline);
        writeText(audio_project_path,
                  QJsonDocument(version_14_audio_json).toJson().toStdString());
        const auto reopened_version_14 = project::load(audio_project_path);
        require(reopened_version_14.timeline_tracks.front().clips.front()
                    .audio_gain_keyframes.empty(),
                "A version 14 audio clip did not load with a flat 100% envelope.");
        auto version_13_audio_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(audio_json)).object();
        version_13_audio_json.insert("version", 13);
        auto version_13_timeline = version_13_audio_json.value("timeline").toObject();
        auto version_13_tracks = version_13_timeline.value("tracks").toArray();
        auto version_13_track = version_13_tracks.at(0).toObject();
        auto version_13_clips = version_13_track.value("clips").toArray();
        auto version_13_clip = version_13_clips.at(0).toObject();
        version_13_clip.remove("audio_extracted");
        version_13_clip.remove("audio_companion_pending");
        version_13_clip.remove("linked_clip_id");
        version_13_clips.replace(0, version_13_clip);
        version_13_clips = QJsonArray{version_13_clip};
        version_13_track.insert("clips", version_13_clips);
        version_13_track.insert("transitions", QJsonArray{});
        version_13_tracks.replace(0, version_13_track);
        version_13_timeline.insert("tracks", version_13_tracks);
        version_13_audio_json.insert("timeline", version_13_timeline);
        writeText(audio_project_path,
                  QJsonDocument(version_13_audio_json).toJson().toStdString());
        const auto reopened_version_13 = project::load(audio_project_path);
        require(reopened_version_13.audio_companion_migration_required &&
                    !reopened_version_13.timeline_tracks.front().clips.front()
                         .linked_clip_id.has_value(),
                "A version 13 project did not request one-time audio companion migration.");
        auto mismatched_audio = audio_document;
        mismatched_audio.timeline_tracks.front().kind = timeline::TrackKind::Video;
        bool rejected_mismatched_audio = false;
        try {
            project::save(directory / "incompatible-audio.csp", mismatched_audio);
        } catch (const project::ProjectError& error) {
            rejected_mismatched_audio =
                error.code() == project::ProjectErrorCode::InvalidTimeline;
        }
        require(rejected_mismatched_audio,
                "Version 16 accepted an Audio clip on a Video track.");
        auto invalid_visual_envelope = original;
        invalid_visual_envelope.timeline_tracks.front().clips.front()
            .audio_gain_keyframes = {{0, 0.5}};
        bool rejected_visual_envelope = false;
        try {
            project::save(directory / "visual-envelope.csp", invalid_visual_envelope);
        } catch (const project::ProjectError& error) {
            rejected_visual_envelope =
                error.code() == project::ProjectErrorCode::InvalidValue;
        }
        require(rejected_visual_envelope,
                "A visual clip with an audio envelope was not rejected.");
        auto invalid_audio_effect = audio_document;
        invalid_audio_effect.timeline_tracks.front().clips.front().effects = {
            creative_suite::effects::makeDefaultInstance("video.grayscale")};
        bool rejected_audio_effect = false;
        try {
            project::save(directory / "audio-visual-effect.csp", invalid_audio_effect);
        } catch (const project::ProjectError& error) {
            rejected_audio_effect =
                error.code() == project::ProjectErrorCode::InvalidValue;
        }
        require(rejected_audio_effect,
                "A visual filter stack on an Audio clip was not rejected.");

        const auto linked_audio_path = directory / "linked-video-audio.csp";
        project::ProjectDocument linked_audio_document;
        linked_audio_document.media.push_back({
            first_source, "Video with audio", "Footage", false,
            media::MediaKind::Video});
        project::ProjectTrack linked_video_track;
        linked_video_track.track_id = 20;
        linked_video_track.name = "Video 1";
        project::ProjectClip linked_video_clip;
        linked_video_clip.clip_id = 21;
        linked_video_clip.source_path = first_source;
        linked_video_clip.timeline_start_frame = 17;
        linked_video_clip.duration_frames = 60;
        linked_video_clip.source_duration_frames = 60;
        linked_video_clip.linked_clip_id = 22;
        linked_video_clip.audio_extracted = true;
        linked_video_track.clips.push_back(linked_video_clip);
        project::ProjectTrack linked_audio_track;
        linked_audio_track.track_id = 23;
        linked_audio_track.name = "Audio 1";
        linked_audio_track.kind = timeline::TrackKind::Audio;
        project::ProjectClip linked_audio_clip;
        linked_audio_clip.clip_id = 22;
        linked_audio_clip.source_path = first_source;
        linked_audio_clip.kind = timeline::ClipKind::Audio;
        linked_audio_clip.timeline_start_frame = 17;
        linked_audio_clip.duration_frames = 60;
        linked_audio_clip.source_start_time_us = 500000;
        linked_audio_clip.source_duration_time_us = 2000000;
        linked_audio_clip.linked_clip_id = 21;
        linked_audio_track.clips.push_back(linked_audio_clip);
        linked_audio_document.timeline_tracks = {
            linked_video_track, linked_audio_track};
        project::save(linked_audio_path, linked_audio_document);
        require(project::load(linked_audio_path) == linked_audio_document,
                "Version 15 did not round-trip linked video and audio clips.");
        auto invalid_audio_link = linked_audio_document;
        invalid_audio_link.timeline_tracks.back().clips.front().linked_clip_id = 999;
        bool rejected_invalid_audio_link = false;
        try {
            project::save(directory / "invalid-audio-link.csp", invalid_audio_link);
        } catch (const project::ProjectError& error) {
            rejected_invalid_audio_link =
                error.code() == project::ProjectErrorCode::InvalidTimeline;
        }
        require(rejected_invalid_audio_link,
                "A project with a missing linked clip ID was accepted.");

        auto version_12_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        version_12_json.insert("version", 12);
        writeText(project_path,
                  QJsonDocument(version_12_json).toJson().toStdString());
        const auto reopened_version_12 = project::load(project_path);
        require(!reopened_version_12.timeline_tracks.empty() &&
                    reopened_version_12.audio_companion_migration_required &&
                    std::all_of(
                        reopened_version_12.timeline_tracks.begin(),
                        reopened_version_12.timeline_tracks.end(),
                        [](const project::ProjectTrack& track) {
                            return track.kind == timeline::TrackKind::Video;
                        }),
                "A version 12 project did not migrate its tracks as Video tracks.");

        auto version_9_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        version_9_json.insert("version", 9);
        auto restore_legacy_transition_geometry = [](QJsonObject& json) {
            auto timeline = json.value("timeline").toObject();
            auto tracks = timeline.value("tracks").toArray();
            for (qsizetype index = 0; index < tracks.size(); ++index) {
                auto track = tracks.at(index).toObject();
                auto clips = track.value("clips").toArray();
                if (clips.size() >= 4) {
                    auto incoming = clips.at(1).toObject();
                    incoming.insert("timeline_start_frame", 60);
                    clips.replace(1, incoming);
                    auto following = clips.at(3).toObject();
                    following.insert("timeline_start_frame", 100);
                    clips.replace(3, following);
                }
                track.insert("clips", clips);
                tracks.replace(index, track);
            }
            timeline.insert("tracks", tracks);
            json.insert("timeline", timeline);
        };
        restore_legacy_transition_geometry(version_9_json);
        auto version_9_media = version_9_json.value("media").toArray();
        for (qsizetype index = 0; index < version_9_media.size(); ++index) {
            auto media_item = version_9_media.at(index).toObject();
            media_item.remove("image_editor_link");
            version_9_media.replace(index, media_item);
        }
        version_9_json.insert("media", version_9_media);
        auto version_9_timeline = version_9_json.value("timeline").toObject();
        auto version_9_tracks = version_9_timeline.value("tracks").toArray();
        for (qsizetype track_index = 0; track_index < version_9_tracks.size(); ++track_index) {
            auto track = version_9_tracks.at(track_index).toObject();
            auto clips = track.value("clips").toArray();
            for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
                auto clip = clips.at(clip_index).toObject();
                clip.remove("image_editor_variant");
                clips.replace(clip_index, clip);
            }
            track.insert("clips", clips);
            version_9_tracks.replace(track_index, track);
        }
        version_9_timeline.insert("tracks", version_9_tracks);
        version_9_json.insert("timeline", version_9_timeline);
        writeText(project_path,
                  QJsonDocument(version_9_json).toJson().toStdString());
        const auto version_9_document = project::load(project_path);
        require(!version_9_document.media.back().image_editor_link.has_value() &&
                    !version_9_document.timeline_tracks.front().clips.back()
                         .image_editor_variant.has_value(),
                "A version 9 project did not load without new linked-image fields.");
        project::save(project_path, original);

        auto version_10_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        version_10_json.insert("version", 10);
        restore_legacy_transition_geometry(version_10_json);
        auto version_10_timeline = version_10_json.value("timeline").toObject();
        version_10_timeline.remove("frame_rate");
        auto version_10_tracks = version_10_timeline.value("tracks").toArray();
        for (qsizetype track_index = 0; track_index < version_10_tracks.size(); ++track_index) {
            auto track = version_10_tracks.at(track_index).toObject();
            auto clips = track.value("clips").toArray();
            for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
                auto clip = clips.at(clip_index).toObject();
                clip.remove("source_duration_frames");
                clip.remove("source_duration_migration_pending");
                clips.replace(clip_index, clip);
            }
            track.insert("clips", clips);
            version_10_tracks.replace(track_index, track);
        }
        version_10_timeline.insert("tracks", version_10_tracks);
        version_10_json.insert("timeline", version_10_timeline);
        writeText(project_path,
                  QJsonDocument(version_10_json).toJson().toStdString());
        const auto version_10_document = project::load(project_path);
        require(version_10_document.timing_migration_required &&
                    version_10_document.timeline_tracks.front().clips.front()
                        .source_duration_frames == 60 &&
                    version_10_document.timeline_tracks.front().clips.front()
                        .source_duration_migration_pending &&
                    version_10_document.timeline_tracks.front().clips[1]
                        .timeline_start_frame == 45 &&
                    version_10_document.timeline_tracks.front().clips[3]
                        .timeline_start_frame == 85,
                "A version 10 project did not load with deferred timing migration metadata.");
        project::save(project_path, original);

        auto version_11_json = QJsonDocument::fromJson(
            QByteArray::fromStdString(saved_json)).object();
        version_11_json.insert("version", 11);
        restore_legacy_transition_geometry(version_11_json);
        auto version_11_timeline = version_11_json.value("timeline").toObject();
        auto version_11_tracks = version_11_timeline.value("tracks").toArray();
        auto version_11_track = version_11_tracks.at(0).toObject();
        auto version_11_clips = version_11_track.value("clips").toArray();
        auto legacy_title = version_11_clips.at(2).toObject();
        legacy_title.insert("timeline_start_frame", 90);
        version_11_clips.replace(2, legacy_title);
        auto legacy_image = version_11_clips.at(3).toObject();
        legacy_image.insert("timeline_start_frame", 110);
        version_11_clips.replace(3, legacy_image);
        version_11_track.insert("clips", version_11_clips);
        auto version_11_transitions = version_11_track.value("transitions").toArray();
        version_11_transitions.push_back(QJsonObject{
            {"from_clip", 1},
            {"to_clip", 2},
            {"kind", "fade_to_black"},
            {"duration_frames", 5}});
        version_11_track.insert("transitions", version_11_transitions);
        version_11_tracks.replace(0, version_11_track);
        version_11_timeline.insert("tracks", version_11_tracks);
        version_11_json.insert("timeline", version_11_timeline);
        writeText(project_path,
                  QJsonDocument(version_11_json).toJson().toStdString());
        const auto migrated_version_11 = project::load(project_path);
        const auto& migrated_track = migrated_version_11.timeline_tracks.front();
        require(migrated_track.clips[1].timeline_start_frame == 45 &&
                    migrated_track.clips[2].timeline_start_frame == 75 &&
                    migrated_track.clips[3].timeline_start_frame == 95 &&
                    migrated_version_11.timeline_tracks[1].clips.front()
                        .timeline_start_frame == 0 &&
                    migrated_track.transitions.size() == 2 &&
                    migrated_track.transitions[0].kind ==
                        timeline::TransitionKind::CrossDissolve &&
                    migrated_track.transitions[1].kind ==
                        timeline::TransitionKind::FadeToBlack &&
                    migrated_version_11.timeline_frame_rate ==
                        original.timeline_frame_rate &&
                    !migrated_version_11.timing_migration_required,
                "A version 11 project did not preserve its Timeline rate or migrate Cross Dissolve ripple while preserving Fade to Black.");
        project::save(project_path, migrated_version_11);
        const auto reopened_migrated_version_11 = project::load(project_path);
        auto expected_reopened_version_11 = migrated_version_11;
        expected_reopened_version_11.audio_companion_migration_required = false;
        require(reopened_migrated_version_11 == expected_reopened_version_11,
                "Saving and reopening a migrated version 11 project changed its overlap geometry.");
        project::save(project_path, original);

        for (const auto& invalid_component : {
                 std::pair<const char*, int>{"numerator", 0},
                 std::pair<const char*, int>{"denominator", -1}}) {
            auto invalid_rate_json = QJsonDocument::fromJson(
                QByteArray::fromStdString(saved_json)).object();
            auto invalid_rate_timeline =
                invalid_rate_json.value("timeline").toObject();
            auto invalid_rate =
                invalid_rate_timeline.value("frame_rate").toObject();
            invalid_rate.insert(invalid_component.first, invalid_component.second);
            invalid_rate_timeline.insert("frame_rate", invalid_rate);
            invalid_rate_json.insert("timeline", invalid_rate_timeline);
            writeText(project_path,
                      QJsonDocument(invalid_rate_json).toJson().toStdString());
            try {
                static_cast<void>(project::load(project_path));
                throw std::runtime_error(
                    "An invalid rational Timeline frame rate was accepted.");
            } catch (const project::ProjectError& error) {
                require(error.code() == project::ProjectErrorCode::InvalidValue,
                        "An invalid Timeline frame-rate component returned the wrong error category.");
            }
        }
        project::save(project_path, original);

        const auto original_contents = saved_json;
        auto unsafe_link_document = original;
        unsafe_link_document.media.back().image_editor_link->published_output_path =
            image_source;
        try {
            project::save(project_path, unsafe_link_document);
            throw std::runtime_error(
                "A linked output that aliases its original image was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidValue,
                    "An output/source path collision returned the wrong error category.");
        }
        bool failed = false;
        try {
            project::save(directory, original);
        } catch (const project::ProjectError&) {
            failed = true;
        }
        require(failed, "Saving to an invalid target did not fail.");
        std::ifstream preserved_file(project_path, std::ios::binary);
        const std::string preserved_contents{
            std::istreambuf_iterator<char>(preserved_file),
            std::istreambuf_iterator<char>()};
        preserved_file.close();
        require(preserved_contents == original_contents,
                "A failed save changed the existing project file.");

        writeText(project_path, "{not json");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("Malformed JSON was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidFormat,
                    "Malformed JSON returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":9223372036854775808,"media":[],"timeline":{}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("An integer outside the signed 64-bit range was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidValue,
                    "An out-of-range JSON integer returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":99,"media":[],"timeline":{"clips":[]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("An unsupported version was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::UnsupportedVersion,
                    "Unsupported version returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":1,"media":[{"path":"media/first video.mkv"}],"timeline":{"clips":[{"source":"media/first video.mkv","source_start_frame":0,"duration_frames":0}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("A zero-duration clip was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "Invalid timeline returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":1,"media":[{"path":"media/first video.mkv"}],"timeline":{"clips":[{"source":"media/first video.mkv","source_start_frame":-1,"duration_frames":1}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("A negative source frame was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "Negative source frame returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":8,"canvas":{"width":1920,"height":1080},"media":[],"bins":["Unsorted"],"timeline":{"zoom":1,"row_height":70,"tracks":[{"name":"Video 1","clips":[{"kind":"video","source":"media/first video.mkv","timeline_start_frame":0,"source_start_frame":9223372036854774784,"duration_frames":2048}],"transitions":[]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("A media source range that overflows was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "An overflowing media range returned error code " +
                        std::to_string(static_cast<int>(error.code())) + ": " +
                        error.what());
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":1,"media":[{"path":"media/missing.mkv"}],"timeline":{"clips":[]}})");
        const auto missing = project::load(project_path);
        require(missing.media.size() == 1 && missing.media.front().offline == false,
                "A missing media entry was not preserved for the application to mark offline.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":1,"media":[{"path":"media/first video.mkv"}],"timeline":{"clips":[]}})");
        const auto legacy = project::load(project_path);
        require(legacy.media.front().display_name.empty() &&
                    legacy.media.front().bin_path == "Unsorted" &&
                    !legacy.media.front().offline &&
                    legacy.media.front().kind == media::MediaKind::Video,
                "A path-only version 1 media entry was not kept backward compatible.");
        require(legacy.timeline_tracks.front().audio_gain == 1.0 &&
                    !legacy.timeline_tracks.front().audio_muted &&
                    legacy.timeline_tracks.front().clips.empty(),
                "A path-only legacy project did not receive default track parameters.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":1,"media":[{"path":"media/first video.mkv"}],"timeline":{"clips":[{"source":"media/first video.mkv","source_start_frame":5,"duration_frames":10},{"source":"media/first video.mkv","source_start_frame":20,"duration_frames":4}]}})");
        const auto migrated = project::load(project_path);
        require(migrated.timeline_tracks.size() == 1 &&
                    migrated.timeline_tracks.front().name == "Video 1" &&
                    migrated.timeline_tracks.front().clips.size() == 2 &&
                    migrated.timeline_tracks.front().clips[0].timeline_start_frame == 0 &&
                    migrated.timeline_tracks.front().clips[1].timeline_start_frame == 10,
                "A version 1 timeline was not migrated to sequential Video 1 clips.");
        require(migrated.timeline_tracks.front().track_id == 1 &&
                    migrated.timeline_tracks.front().clips[0].clip_id == 1 &&
                    migrated.timeline_tracks.front().clips[1].clip_id == 2,
                "A version 1 timeline did not receive deterministic stable identifiers.");
        require(migrated.timeline_tracks.front().clips[0].audio_gain == 1.0 &&
                    !migrated.timeline_tracks.front().clips[0].audio_muted,
                "A legacy timeline clip did not receive default audio parameters.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":2,"media":[],"bins":["Unsorted"],"timeline":{"tracks":[{"name":"Video 1","clips":[{"source":"a.mkv","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10}]}]}})");
        const auto migrated_v2 = project::load(project_path);
        require(migrated_v2.canvas_width == 1920 && migrated_v2.canvas_height == 1080 &&
                    migrated_v2.timeline_tracks.front().clips.front().transform ==
                        timeline::Transform2D{},
                "A version 2 project did not receive identity transform defaults.");
        require(migrated_v2.timeline_tracks.front().track_id == 1 &&
                    migrated_v2.timeline_tracks.front().clips.front().clip_id == 1,
                "A version 2 project did not receive stable identifiers.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":9,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"zoom":1,"row_height":70,"tracks":[{"track_id":1,"name":"Video 1","clips":[],"transitions":[]},{"track_id":1,"name":"Video 2","clips":[],"transitions":[]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("Duplicate track identifiers were accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "Duplicate track identifiers returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":9,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"zoom":1,"row_height":70,"tracks":[{"track_id":1,"name":"Video 1","clips":[{"clip_id":1,"kind":"text","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10,"text":{"content":"First"}},{"clip_id":1,"kind":"text","timeline_start_frame":10,"source_start_frame":0,"duration_frames":10,"text":{"content":"Second"}}],"transitions":[]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("Duplicate clip identifiers were accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "Duplicate clip identifiers returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":9,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"zoom":1,"row_height":70,"tracks":[{"track_id":1,"name":"Video 1","clips":[{"clip_id":0,"kind":"text","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10,"text":{"content":"Invalid"}}],"transitions":[]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("Zero clip identifiers were accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidValue,
                    "Zero clip identifiers returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":2,"media":[],"bins":["Unsorted"],"timeline":{"tracks":[{"name":"Video 1","clips":[{"source":"a.mkv","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10},{"source":"b.mkv","timeline_start_frame":5,"source_start_frame":0,"duration_frames":10}]}]}})");
        const auto overlapping_legacy = project::load(project_path);
        require(overlapping_legacy.timeline_tracks.front().clips.size() == 2 &&
                    overlapping_legacy.timeline_tracks.front().clips[0]
                            .timeline_start_frame == 0 &&
                    overlapping_legacy.timeline_tracks.front().clips[1]
                            .timeline_start_frame == 5,
                "A legacy project with an individual media overlap did not load.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":4,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"tracks":[{"name":"Video 1","clips":[{"kind":"text","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10,"text":{"content":"First","font_family":"Sans Serif","font_size_pixels":48,"color":{"r":255,"g":255,"b":255,"a":255},"alignment":"center"}},{"kind":"text","timeline_start_frame":5,"source_start_frame":0,"duration_frames":10,"text":{"content":"Second","font_family":"Sans Serif","font_size_pixels":48,"color":{"r":255,"g":255,"b":255,"a":255},"alignment":"center"}}]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("Overlapping text clips on one track were accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "Overlapping text clips returned the wrong error category.");
        }

        auto overlapping_document = original;
        project::ProjectClip first_overlapping_clip;
        first_overlapping_clip.source_path = first_source;
        first_overlapping_clip.timeline_start_frame = 0;
        first_overlapping_clip.source_start_frame = 0;
        first_overlapping_clip.duration_frames = 60;
        first_overlapping_clip.source_duration_frames = 60;
        project::ProjectClip second_overlapping_clip;
        second_overlapping_clip.source_path = first_source;
        second_overlapping_clip.timeline_start_frame = 40;
        second_overlapping_clip.source_start_frame = 40;
        second_overlapping_clip.duration_frames = 60;
        second_overlapping_clip.source_duration_frames = 60;
        overlapping_document.timeline_tracks = {project::ProjectTrack{
            "Video 1", 1.0, false,
            {first_overlapping_clip, second_overlapping_clip}, {}}};
        overlapping_document.timeline_tracks.front().track_id = 1;
        overlapping_document.timeline_tracks.front().clips[0].clip_id = 1;
        overlapping_document.timeline_tracks.front().clips[1].clip_id = 2;
        const auto overlapping_project_path = directory / "overlapping.csp";
        project::save(overlapping_project_path, overlapping_document);
        const auto loaded_overlapping_document =
            project::load(overlapping_project_path);
        require(loaded_overlapping_document == overlapping_document,
                "A media overlap did not round-trip through the project format.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":3,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"tracks":[{"name":"Video 1","clips":[{"source":"a.mkv","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10}]}]}})");
        const auto migrated_v3 = project::load(project_path);
        require(migrated_v3.timeline_tracks.front().clips.front().kind ==
                    timeline::ClipKind::Video &&
                    migrated_v3.timeline_tracks.front().clips.front().text ==
                        timeline::TextStyle{},
                "A version 3 project was not migrated to default video/text fields.");
        require(migrated_v3.timeline_tracks.front().transitions.empty(),
                "A version 3 project did not receive an empty transition list.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":4,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"tracks":[{"name":"Video 1","clips":[{"kind":"text","timeline_start_frame":0,"source_start_frame":0,"duration_frames":30,"text":{"content":"Bad","font_family":"Sans Serif","font_size_pixels":0,"color":{"r":255,"g":255,"b":255,"a":255},"alignment":"center"}}]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("An invalid text style was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidValue,
                    "Invalid text style returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":5,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"tracks":[{"name":"Video 1","clips":[{"kind":"video","source":"a.mkv","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10},{"kind":"video","source":"b.mkv","timeline_start_frame":20,"source_start_frame":0,"duration_frames":10}],"transitions":[{"from_clip":0,"to_clip":1,"kind":"cross_dissolve","duration_frames":5}]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("A transition across a project gap was accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "An invalid project transition returned the wrong error category.");
        }

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":5,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"tracks":[{"name":"Video 1","clips":[],"transitions":[]}]}})");
        const auto migrated_v5 = project::load(project_path);
        require(migrated_v5.timeline_zoom == 1.0,
                "A version 5 project without timeline zoom did not default to 100%.");
        require(migrated_v5.timeline_row_height == 70.0,
                "An older project without row height did not migrate to 70-pixel Timeline rows.");

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":6,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"zoom":1,"tracks":[{"name":"Video 1","clips":[],"transitions":[]}]}})");
        const auto migrated_v6 = project::load(project_path);
        require(migrated_v6.timeline_row_height == 70.0,
                "A version 6 project without row height did not migrate to 70-pixel Timeline rows.");

        for (const auto invalid_zoom : {
                 0.24, 512.01, std::numeric_limits<double>::quiet_NaN()}) {
            const auto zoom_json = std::isnan(invalid_zoom)
                ? std::string("null")
                : std::to_string(invalid_zoom);
            writeText(
                project_path,
                std::string(R"({"format":"creative-suite.main-editor","version":6,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"zoom":)") +
                    zoom_json +
                    R"(,"tracks":[{"name":"Video 1","clips":[],"transitions":[]}]}})");
            try {
                static_cast<void>(project::load(project_path));
                throw std::runtime_error("An invalid timeline zoom was accepted.");
            } catch (const project::ProjectError& error) {
                require(error.code() == project::ProjectErrorCode::InvalidValue,
                        "Invalid timeline zoom returned the wrong error category.");
            }
        }

        for (const auto invalid_row_height : {
                 29.99, 180.01, std::numeric_limits<double>::quiet_NaN()}) {
            const auto row_height_json = std::isnan(invalid_row_height)
                ? std::string("null")
                : std::to_string(invalid_row_height);
            writeText(
                project_path,
                std::string(R"({"format":"creative-suite.main-editor","version":7,"canvas":{"width":1920,"height":1080},"media":[],"timeline":{"zoom":1,"row_height":)") +
                    row_height_json +
                    R"(,"tracks":[{"name":"Video 1","clips":[],"transitions":[]}]}})");
            try {
                static_cast<void>(project::load(project_path));
                throw std::runtime_error("An invalid timeline row height was accepted.");
            } catch (const project::ProjectError& error) {
                require(error.code() == project::ProjectErrorCode::InvalidValue,
                        "Invalid timeline row height returned the wrong error category.");
            }
        }

        std::error_code cleanup_error;
        std::filesystem::remove(outside_source, cleanup_error);
        std::filesystem::remove_all(directory, cleanup_error);
        return cleanup_error ? 1 : 0;
    } catch (const std::exception& error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
