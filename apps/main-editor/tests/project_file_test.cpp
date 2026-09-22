#include "project/project_file.h"

#include <QCoreApplication>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
        const auto outside_source = std::filesystem::temp_directory_path() /
            ("creative-suite-project-outside-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
        std::ofstream(first_source, std::ios::binary).close();
        std::ofstream(outside_source, std::ios::binary).close();

        project::ProjectDocument original;
        require(original.timeline_row_height == 70.0,
                "A new project document did not start with the 70-pixel default row height.");
        original.media = {
            {first_source, "First Video", "Footage/Scenes", false},
            {outside_source, "Offline Asset", "Unsorted", true},
        };
        original.timeline_tracks = {
            {"Video 1", 0.75, true, {
                {first_source, 0, 30, 60, 0.5, true},
                {first_source, 60, 0, 30, 1.25, false},
            }},
        };
        project::ProjectClip title_clip;
        title_clip.timeline_start_frame = 10;
        title_clip.duration_frames = 20;
        title_clip.kind = timeline::ClipKind::Text;
        title_clip.text.content = "Title";
        title_clip.text.font_size_pixels = 64.0;
        title_clip.text.alignment = timeline::TextAlignment::Left;
        title_clip.text.color = {255, 200, 100, 230};
        original.timeline_tracks.front().clips.push_back(title_clip);
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
        original.timeline_clips = original.timeline_tracks.front().clips;
        project::save(project_path, original);

        const auto loaded = project::load(project_path);
        require(loaded == original, "A project did not round-trip through JSON.");
        require(loaded.timeline_clips[0].source_start_frame == 30,
                "A source offset was not preserved.");
        require(loaded.timeline_clips[1].source_path == loaded.timeline_clips[0].source_path,
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
        require(saved_json.find("\"version\": 7") != std::string::npos &&
                    saved_json.find("\"zoom\": 512") != std::string::npos &&
                    saved_json.find("\"row_height\": 123.5") != std::string::npos &&
                    saved_json.find("\"transitions\"") != std::string::npos &&
                    saved_json.find("cross_dissolve") != std::string::npos,
                "Timeline zoom, row height, and transition data were not written to the version 7 project.");

        const auto original_contents = saved_json;
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
                    !legacy.media.front().offline,
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

        writeText(
            project_path,
            R"({"format":"creative-suite.main-editor","version":2,"media":[],"bins":["Unsorted"],"timeline":{"tracks":[{"name":"Video 1","clips":[{"source":"a.mkv","timeline_start_frame":0,"source_start_frame":0,"duration_frames":10},{"source":"b.mkv","timeline_start_frame":5,"source_start_frame":0,"duration_frames":10}]}]}})");
        try {
            static_cast<void>(project::load(project_path));
            throw std::runtime_error("Overlapping clips on one track were accepted.");
        } catch (const project::ProjectError& error) {
            require(error.code() == project::ProjectErrorCode::InvalidTimeline,
                    "Track overlap returned the wrong error category.");
        }

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
