#include "project_file.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QString>

#include <cmath>
#include <algorithm>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#include "../media/media_library.h"
#include "../timeline/timeline_zoom.h"

namespace project {
namespace {

QString pathToQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(value.data()),
        static_cast<qsizetype>(value.size()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path pathFromUtf8(const QString& value) {
    const auto bytes = value.toUtf8();
    const auto* begin = reinterpret_cast<const char8_t*>(bytes.constData());
    const auto* end = begin + bytes.size();
    return std::filesystem::path(std::u8string(begin, end));
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;

    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

bool isParentRelativePath(const std::filesystem::path& path) {
    const auto normalized = path.lexically_normal();
    if (normalized.empty()) return false;
    const auto first = normalized.begin();
    return first != normalized.end() && *first == "..";
}

bool validAudioGain(double gain) {
    return std::isfinite(gain) && gain >= 0.0 && gain <= 2.0;
}

bool validTextStyle(const timeline::TextStyle& text) {
    if (text.font_family.empty() || !std::isfinite(text.font_size_pixels) ||
        text.font_size_pixels <= 0.0 || text.font_size_pixels > 512.0) {
        return false;
    }
    switch (text.alignment) {
    case timeline::TextAlignment::Left:
    case timeline::TextAlignment::Center:
    case timeline::TextAlignment::Right:
        return true;
    }
    return false;
}

[[noreturn]] void throwJson(ProjectErrorCode code,
                             const std::filesystem::path& project_path,
                             const char* message);

bool validTransitionKind(timeline::TransitionKind kind) {
    switch (kind) {
    case timeline::TransitionKind::CrossDissolve:
    case timeline::TransitionKind::FadeToBlack:
        return true;
    }
    return false;
}

const char* mediaKindName(media::MediaKind kind) {
    return kind == media::MediaKind::Image ? "image" : "video";
}

media::MediaKind parseMediaKind(
    const QJsonObject& object,
    const std::filesystem::path& project_path,
    int version) {
    if (version < media_kind_format_version || !object.contains("kind")) {
        return media::MediaKind::Video;
    }
    const auto value = object.value("kind");
    if (!value.isString()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid media kind.");
    }
    if (value.toString() == QLatin1String("video")) return media::MediaKind::Video;
    if (value.toString() == QLatin1String("image")) return media::MediaKind::Image;
    throwJson(ProjectErrorCode::InvalidValue, project_path,
              "Project JSON contains an unsupported media kind.");
}

timeline::TransitionKind parseTransitionKind(
    const QJsonObject& object,
    const std::filesystem::path& project_path) {
    const auto value = object.value("kind");
    if (!value.isString()) {
        throwJson(ProjectErrorCode::MissingField, project_path,
                  "A transition is missing its kind.");
    }
    if (value.toString() == QLatin1String("cross_dissolve")) {
        return timeline::TransitionKind::CrossDissolve;
    }
    if (value.toString() == QLatin1String("fade_to_black")) {
        return timeline::TransitionKind::FadeToBlack;
    }
    throwJson(ProjectErrorCode::InvalidValue, project_path,
              "Project JSON contains an unsupported transition kind.");
}

bool validKeyframeList(
    const std::vector<timeline::Keyframe>& keyframes,
    timeline::TransformProperty property,
    std::int64_t duration_frames) {
    std::int64_t previous = -1;
    for (const auto& keyframe : keyframes) {
        if (keyframe.frame < 0 || keyframe.frame >= duration_frames ||
            keyframe.frame <= previous ||
            !timeline::validKeyframeValue(property, keyframe.value)) {
            return false;
        }
        previous = keyframe.frame;
    }
    return true;
}

QString storedPath(const std::filesystem::path& project_path,
                  const std::filesystem::path& source_path) {
    const auto project_directory = normalizedPath(project_path).parent_path();
    const auto source = normalizedPath(source_path);
    std::error_code error;
    const auto relative = std::filesystem::relative(source, project_directory, error);
    if (!error && !relative.empty() && !isParentRelativePath(relative)) {
        return QString::fromUtf8(pathToUtf8(relative));
    }
    return QString::fromUtf8(pathToUtf8(source));
}

std::filesystem::path resolvedPath(const std::filesystem::path& project_path,
                                   const QString& stored_path) {
    const auto stored = pathFromUtf8(stored_path);
    if (stored.is_absolute()) return normalizedPath(stored);
    return normalizedPath(normalizedPath(project_path).parent_path() / stored);
}

[[noreturn]] void throwJson(ProjectErrorCode code,
                             const std::filesystem::path& project_path,
                             const char* message) {
    throw ProjectError(code, message, std::nullopt, project_path);
}

std::int64_t requiredInteger(const QJsonObject& object,
                             const char* key,
                             const std::filesystem::path& project_path) {
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing a required integer field.");
    }
    if (!value.isDouble()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains a non-numeric integer field.");
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid integer value.");
    }
    return static_cast<std::int64_t>(number);
}

QString requiredString(const QJsonObject& object,
                       const char* key,
                       const std::filesystem::path& project_path) {
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing a required string field.");
    }
    if (!value.isString() || value.toString().isEmpty()) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid string field.");
    }
    return value.toString();
}

timeline::TextStyle parseTextStyle(
    const QJsonObject& clip_object,
    const std::filesystem::path& project_path) {
    const auto text_value = clip_object.value("text");
    if (!text_value.isObject()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "A text clip is missing its text object.");
    }
    const auto object = text_value.toObject();
    timeline::TextStyle text;
    const auto content = object.value("content");
    if (!content.isString()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "A text clip is missing text content.");
    }
    text.content = content.toString().toUtf8().toStdString();
    if (object.contains("font_family")) {
        if (!object.value("font_family").isString()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid font family.");
        }
        text.font_family = object.value("font_family").toString().toUtf8().toStdString();
    }
    if (object.contains("font_size_pixels")) {
        if (!object.value("font_size_pixels").isDouble()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid font size.");
        }
        text.font_size_pixels = object.value("font_size_pixels").toDouble();
    }
    if (object.contains("color")) {
        const auto color_value = object.value("color");
        if (!color_value.isObject()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid color.");
        }
        const auto color = color_value.toObject();
        for (const auto key : {"r", "g", "b", "a"}) {
            const auto component = color.value(QLatin1String(key));
            if (!component.isDouble() || component.toDouble() < 0.0 ||
                component.toDouble() > 255.0 ||
                std::floor(component.toDouble()) != component.toDouble()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid color component.");
            }
        }
        text.color = {
            static_cast<std::uint8_t>(color.value("r").toInt()),
            static_cast<std::uint8_t>(color.value("g").toInt()),
            static_cast<std::uint8_t>(color.value("b").toInt()),
            static_cast<std::uint8_t>(color.value("a").toInt())};
    }
    if (object.contains("alignment")) {
        const auto alignment = object.value("alignment");
        if (!alignment.isString()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an invalid alignment.");
        }
        const auto value = alignment.toString();
        if (value == QLatin1String("left")) text.alignment = timeline::TextAlignment::Left;
        else if (value == QLatin1String("center")) text.alignment = timeline::TextAlignment::Center;
        else if (value == QLatin1String("right")) text.alignment = timeline::TextAlignment::Right;
        else throwJson(ProjectErrorCode::InvalidValue, project_path, "A text clip contains an unsupported alignment.");
    }
    return text;
}

void validateDocument(const ProjectDocument& document,
                      const std::filesystem::path& project_path) {
    if (document.canvas_width != 1920 || document.canvas_height != 1080) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported canvas size; only 1920x1080 is supported.");
    }
    if (!std::isfinite(document.timeline_zoom) ||
        document.timeline_zoom < timeline::kMinTimelineZoomFactor ||
        document.timeline_zoom > timeline::kMaxTimelineZoomFactor) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid timeline zoom; expected a value from 0.25 to 512.0.");
    }
    if (!std::isfinite(document.timeline_row_height) ||
        document.timeline_row_height < timeline::kMinimumTrackRowHeight ||
        document.timeline_row_height > timeline::kMaximumTrackRowHeight) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid timeline row height; expected a value from 30.0 to 180.0.");
    }
    std::vector<std::filesystem::path> media_paths;
    for (const auto& media : document.media) {
        if (media.source_path.empty()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an empty media path.");
        }
        if (!media::MediaLibrary::validBinPath(media.bin_path)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media bin path.");
        }
        const auto canonical = media::MediaLibrary::canonicalPath(media.source_path);
        if (std::find(media_paths.begin(), media_paths.end(), canonical) != media_paths.end()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains duplicate media paths.");
        }
        media_paths.push_back(canonical);
    }

    for (const auto& bin : document.bins) {
        if (!media::MediaLibrary::validBinPath(bin)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid bin path.");
        }
    }

    auto validate_clip = [&project_path](const ProjectClip& clip) {
        if (timeline::isMediaClipKind(clip.kind) && clip.source_path.empty()) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains a media clip without a source.");
        }
        if (clip.timeline_start_frame < 0 ||
            clip.source_start_frame < 0 || clip.duration_frames <= 0) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains an invalid timeline segment.");
        }
        if (clip.duration_frames >
            std::numeric_limits<std::int64_t>::max() -
                clip.timeline_start_frame) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains an overflowing timeline range.");
        }
        if (!validAudioGain(clip.audio_gain)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip audio gain.");
        }
        if (clip.kind == timeline::ClipKind::Text && !validTextStyle(clip.text)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid text clip styling.");
        }
        if (!timeline::validTransform(clip.transform) ||
            !validKeyframeList(clip.keyframes.position_x,
                               timeline::TransformProperty::PositionX,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.position_y,
                               timeline::TransformProperty::PositionY,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.scale,
                               timeline::TransformProperty::Scale,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.rotation,
                               timeline::TransformProperty::Rotation,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.opacity,
                               timeline::TransformProperty::Opacity,
                               clip.duration_frames)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid clip transform or keyframes.");
        }
    };
    for (const auto& track : document.timeline_tracks) {
        if (track.name.empty()) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains a track without a name.");
        }
        if (!validAudioGain(track.audio_gain)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid track audio gain.");
        }
        for (const auto& clip : track.clips) validate_clip(clip);
        for (std::size_t left = 0; left < track.clips.size(); ++left) {
            const auto& first = track.clips[left];
            const auto first_end =
                first.timeline_start_frame + first.duration_frames;
            for (std::size_t right = left + 1; right < track.clips.size(); ++right) {
                const auto& second = track.clips[right];
                const auto second_end =
                    second.timeline_start_frame + second.duration_frames;
                if (first.kind == second.kind &&
                    second.timeline_start_frame < first_end &&
                    first.timeline_start_frame <
                        second_end) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains overlapping clips on one track.");
                }
            }
        }
        std::vector<std::pair<std::size_t, std::size_t>> transition_pairs;
        for (const auto& transition : track.transitions) {
            if (!validTransitionKind(transition.kind) ||
                transition.from_clip_index >= track.clips.size() ||
                transition.to_clip_index >= track.clips.size()) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with invalid clip indexes.");
            }
            if (transition.from_clip_index + 1 != transition.to_clip_index) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition between non-consecutive clips.");
            }
            const auto& from = track.clips[transition.from_clip_index];
            const auto& to = track.clips[transition.to_clip_index];
            if (from.timeline_start_frame >
                    std::numeric_limits<std::int64_t>::max() - from.duration_frames ||
                from.timeline_start_frame + from.duration_frames !=
                    to.timeline_start_frame) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition across a gap.");
            }
            const auto maximum = std::min(from.duration_frames, to.duration_frames);
            if (transition.duration_frames <= 0 ||
                transition.duration_frames > maximum) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with an invalid duration.");
            }
            const auto pair = std::make_pair(
                transition.from_clip_index, transition.to_clip_index);
            if (std::find(transition_pairs.begin(), transition_pairs.end(), pair) !=
                transition_pairs.end()) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains duplicate transitions.");
            }
            transition_pairs.push_back(pair);
        }
    }
    for (const auto& clip : document.timeline_clips) validate_clip(clip);
}

} // namespace

ProjectDocument load(const std::filesystem::path& project_path) {
    const QString file_name = pathToQString(project_path);
    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not open the project file for reading.",
            file.error(),
            project_path);
    }

    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not read the project file.",
            file.error(),
            project_path);
    }

    QJsonParseError parse_error;
    const auto json = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !json.isObject()) {
        throw ProjectError(
            ProjectErrorCode::InvalidFormat,
            "The project file is not valid JSON.",
            static_cast<int>(parse_error.error),
            project_path);
    }

    const auto root = json.object();
    if (requiredString(root, "format", project_path) !=
        QString::fromLatin1(format_identifier)) {
        throw ProjectError(
            ProjectErrorCode::InvalidFormat,
            "The project file has an unsupported format identifier.",
            std::nullopt,
            project_path);
    }

    const auto version = requiredInteger(root, "version", project_path);
    if (version < legacy_format_version || version > current_format_version) {
        throw ProjectError(
            ProjectErrorCode::UnsupportedVersion,
            "The project file version is not supported.",
            static_cast<int>(version),
            project_path);
    }

    const auto media_value = root.value("media");
    if (!media_value.isArray()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the media array.");
    }

    ProjectDocument document;
    if (version >= canvas_format_version) {
        const auto canvas_value = root.value("canvas");
        if (!canvas_value.isObject()) {
            throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the canvas object.");
        }
        const auto canvas = canvas_value.toObject();
        const auto width = requiredInteger(canvas, "width", project_path);
        const auto height = requiredInteger(canvas, "height", project_path);
        if (width != 1920 || height != 1080) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported canvas size; only 1920x1080 is supported.");
        }
        document.canvas_width = static_cast<int>(width);
        document.canvas_height = static_cast<int>(height);
    }
    const auto bins_value = root.value("bins");
    if (!bins_value.isUndefined()) {
        if (!bins_value.isArray()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid bins array.");
        }
        for (const auto& bin_value : bins_value.toArray()) {
            if (!bin_value.isString() || !media::MediaLibrary::validBinPath(
                    bin_value.toString().toUtf8().toStdString())) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid bin path.");
            }
            document.bins.push_back(bin_value.toString().toUtf8().toStdString());
        }
    }
    for (const auto& media_value_item : media_value.toArray()) {
        if (!media_value_item.isObject()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media item.");
        }
        const auto object = media_value_item.toObject();
        ProjectMedia media;
        media.kind = parseMediaKind(object, project_path, static_cast<int>(version));
        media.source_path = resolvedPath(
            project_path,
            requiredString(object, "path", project_path));
        if (object.contains("name")) {
            if (!object.value("name").isString()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media name.");
            }
            media.display_name = object.value("name").toString().toUtf8().toStdString();
        }
        if (object.contains("bin")) {
            if (!object.value("bin").isString()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media bin.");
            }
            media.bin_path = object.value("bin").toString().toUtf8().toStdString();
        }
        if (object.contains("offline")) {
            if (!object.value("offline").isBool()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid offline media flag.");
            }
            media.offline = object.value("offline").toBool();
        }
        document.media.push_back(std::move(media));
    }

    const auto timeline_value = root.value("timeline");
    if (!timeline_value.isObject()) {
        throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the timeline object.");
    }
    const auto timeline_object = timeline_value.toObject();
    if (version >= timeline_zoom_format_version) {
        const auto zoom_value = timeline_object.value("zoom");
        if (zoom_value.isUndefined()) {
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "Project JSON is missing the timeline zoom value.");
        }
        if (!zoom_value.isDouble() || !std::isfinite(zoom_value.toDouble()) ||
            zoom_value.toDouble() < timeline::kMinTimelineZoomFactor ||
            zoom_value.toDouble() > timeline::kMaxTimelineZoomFactor) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid timeline zoom; expected a value from 0.25 to 512.0.");
        }
        document.timeline_zoom = zoom_value.toDouble();
    }
    if (version >= timeline_row_height_format_version) {
        const auto row_height_value = timeline_object.value("row_height");
        if (row_height_value.isUndefined()) {
            throwJson(ProjectErrorCode::MissingField, project_path,
                      "Project JSON is missing the timeline row height value.");
        }
        if (!row_height_value.isDouble() || !std::isfinite(row_height_value.toDouble()) ||
            row_height_value.toDouble() < timeline::kMinimumTrackRowHeight ||
            row_height_value.toDouble() > timeline::kMaximumTrackRowHeight) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid timeline row height; expected a value from 30.0 to 180.0.");
        }
        document.timeline_row_height = row_height_value.toDouble();
    }
    if (version == legacy_format_version) {
        const auto clips_value = timeline_object.value("clips");
        if (!clips_value.isArray()) {
            throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the timeline clips array.");
        }
        ProjectTrack track{"Video 1", 1.0, false, {}};
        std::int64_t timeline_start = 0;
        for (const auto& clip_value : clips_value.toArray()) {
            if (!clip_value.isObject()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid timeline clip.");
            }
            const auto clip_object = clip_value.toObject();
            ProjectClip clip;
            clip.source_path = resolvedPath(project_path, requiredString(clip_object, "source", project_path));
            clip.timeline_start_frame = timeline_start;
            clip.source_start_frame = requiredInteger(clip_object, "source_start_frame", project_path);
            clip.duration_frames = requiredInteger(clip_object, "duration_frames", project_path);
            if (clip.duration_frames > 0 && timeline_start <=
                std::numeric_limits<std::int64_t>::max() - clip.duration_frames) {
                timeline_start += clip.duration_frames;
            }
            document.timeline_clips.push_back(clip);
            track.clips.push_back(std::move(clip));
        }
        document.timeline_tracks.push_back(std::move(track));
    } else {
        const auto tracks_value = timeline_object.value("tracks");
        if (!tracks_value.isArray()) {
            throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing the timeline tracks array.");
        }
        for (const auto& track_value : tracks_value.toArray()) {
            if (!track_value.isObject()) {
                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid timeline track.");
            }
            const auto track_object = track_value.toObject();
            ProjectTrack track;
            track.name = requiredString(track_object, "name", project_path).toUtf8().toStdString();
            if (track_object.contains("audio_gain")) {
                if (!track_object.value("audio_gain").isDouble()) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid track audio gain.");
                }
                track.audio_gain = track_object.value("audio_gain").toDouble();
            }
            if (track_object.contains("audio_muted")) {
                if (!track_object.value("audio_muted").isBool()) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid track mute flag.");
                }
                track.audio_muted = track_object.value("audio_muted").toBool();
            }
            const auto track_clips = track_object.value("clips");
            if (!track_clips.isArray()) {
                throwJson(ProjectErrorCode::MissingField, project_path, "Project JSON is missing a track clips array.");
            }
            for (const auto& clip_value : track_clips.toArray()) {
                if (!clip_value.isObject()) {
                    throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid timeline clip.");
                }
                const auto clip_object = clip_value.toObject();
                ProjectClip clip;
                if (version >= clip_kind_format_version && clip_object.contains("kind")) {
                    const auto kind = clip_object.value("kind");
                    if (!kind.isString()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip kind.");
                    }
                    if (kind.toString() == QLatin1String("video")) {
                        clip.kind = timeline::ClipKind::Video;
                    } else if (kind.toString() == QLatin1String("image")) {
                        clip.kind = timeline::ClipKind::Image;
                    } else if (kind.toString() == QLatin1String("text")) {
                        clip.kind = timeline::ClipKind::Text;
                    } else {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported clip kind.");
                    }
                }
                if (timeline::isMediaClipKind(clip.kind)) {
                    clip.source_path = resolvedPath(
                        project_path, requiredString(clip_object, "source", project_path));
                } else {
                    clip.text = parseTextStyle(clip_object, project_path);
                }
                clip.timeline_start_frame = requiredInteger(clip_object, "timeline_start_frame", project_path);
                clip.source_start_frame = requiredInteger(clip_object, "source_start_frame", project_path);
                clip.duration_frames = requiredInteger(clip_object, "duration_frames", project_path);
                if (clip_object.contains("audio_gain")) {
                    if (!clip_object.value("audio_gain").isDouble()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip audio gain.");
                    }
                    clip.audio_gain = clip_object.value("audio_gain").toDouble();
                }
                if (clip_object.contains("audio_muted")) {
                    if (!clip_object.value("audio_muted").isBool()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip mute flag.");
                    }
                    clip.audio_muted = clip_object.value("audio_muted").toBool();
                }
                if (version >= canvas_format_version && clip_object.contains("transform")) {
                    const auto transform = clip_object.value("transform");
                    if (!transform.isObject()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip transform.");
                    }
                    const auto transform_object = transform.toObject();
                    const auto position = transform_object.value("position");
                    if (!position.isObject() ||
                        !position.toObject().value("x").isDouble() ||
                        !position.toObject().value("y").isDouble() ||
                        !transform_object.value("scale").isDouble() ||
                        !transform_object.value("rotation").isDouble() ||
                        !transform_object.value("opacity").isDouble()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains incomplete clip transform data.");
                    }
                    clip.transform.position_x = position.toObject().value("x").toDouble();
                    clip.transform.position_y = position.toObject().value("y").toDouble();
                    clip.transform.scale = transform_object.value("scale").toDouble();
                    clip.transform.rotation_degrees = transform_object.value("rotation").toDouble();
                    clip.transform.opacity = transform_object.value("opacity").toDouble();
                }
                if (version >= canvas_format_version && clip_object.contains("keyframes")) {
                    const auto keyframes = clip_object.value("keyframes");
                    if (!keyframes.isObject()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid clip keyframes.");
                    }
                    const auto parse_keyframes = [&](const char* key,
                                                     timeline::TransformProperty property,
                                                     std::vector<timeline::Keyframe>& output) {
                        const auto value = keyframes.toObject().value(QLatin1String(key));
                        if (value.isUndefined()) return;
                        if (!value.isArray()) {
                            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid keyframe list.");
                        }
                        for (const auto& keyframe_value : value.toArray()) {
                            if (!keyframe_value.isObject()) {
                                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid keyframe.");
                            }
                            const auto keyframe_object = keyframe_value.toObject();
                            const auto frame = requiredInteger(keyframe_object, "frame", project_path);
                            const auto numeric = keyframe_object.value("value");
                            if (!numeric.isDouble() || !timeline::validKeyframeValue(property, numeric.toDouble())) {
                                throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid keyframe value.");
                            }
                            output.push_back({frame, numeric.toDouble()});
                        }
                    };
                    parse_keyframes("position_x", timeline::TransformProperty::PositionX, clip.keyframes.position_x);
                    parse_keyframes("position_y", timeline::TransformProperty::PositionY, clip.keyframes.position_y);
                    parse_keyframes("scale", timeline::TransformProperty::Scale, clip.keyframes.scale);
                    parse_keyframes("rotation", timeline::TransformProperty::Rotation, clip.keyframes.rotation);
                    parse_keyframes("opacity", timeline::TransformProperty::Opacity, clip.keyframes.opacity);
                }
                track.clips.push_back(clip);
                document.timeline_clips.push_back(std::move(clip));
            }
            if (version >= transitions_format_version) {
                const auto transitions_value = track_object.value("transitions");
                if (!transitions_value.isArray()) {
                    throwJson(ProjectErrorCode::MissingField, project_path,
                              "Project JSON is missing a track transitions array.");
                }
                for (const auto& transition_value : transitions_value.toArray()) {
                    if (!transition_value.isObject()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "Project JSON contains an invalid transition.");
                    }
                    const auto transition_object = transition_value.toObject();
                    const auto from_clip = requiredInteger(
                        transition_object, "from_clip", project_path);
                    const auto to_clip = requiredInteger(
                        transition_object, "to_clip", project_path);
                    if (from_clip < 0 || to_clip < 0 ||
                        static_cast<std::uint64_t>(from_clip) >
                            std::numeric_limits<std::size_t>::max() ||
                        static_cast<std::uint64_t>(to_clip) >
                            std::numeric_limits<std::size_t>::max()) {
                        throwJson(ProjectErrorCode::InvalidValue, project_path,
                                  "Project JSON contains invalid transition indexes.");
                    }
                    ProjectTransition transition;
                    transition.from_clip_index = static_cast<std::size_t>(from_clip);
                    transition.to_clip_index = static_cast<std::size_t>(to_clip);
                    transition.kind = parseTransitionKind(transition_object, project_path);
                    transition.duration_frames = requiredInteger(
                        transition_object, "duration_frames", project_path);
                    track.transitions.push_back(std::move(transition));
                }
            }
            document.timeline_tracks.push_back(std::move(track));
        }
    }

    validateDocument(document, project_path);
    return document;
}

void save(const std::filesystem::path& project_path, const ProjectDocument& document) {
    if (project_path.empty()) {
        throw ProjectError(ProjectErrorCode::Io, "The project path is empty.");
    }
    validateDocument(document, project_path);

    QJsonArray media;
    for (const auto& media_source : document.media) {
        QJsonObject item;
        item.insert("path", storedPath(project_path, media_source.source_path));
        if (!media_source.display_name.empty()) {
            item.insert("name", QString::fromUtf8(media_source.display_name.data(),
                                                    static_cast<int>(media_source.display_name.size())));
        }
        item.insert("bin", QString::fromUtf8(media_source.bin_path.data(),
                                               static_cast<int>(media_source.bin_path.size())));
        item.insert("offline", media_source.offline);
        item.insert("kind", mediaKindName(media_source.kind));
        media.append(item);
    }

    std::vector<ProjectTrack> tracks = document.timeline_tracks;
    if (tracks.empty() && !document.timeline_clips.empty()) {
        tracks.push_back({"Video 1", 1.0, false, document.timeline_clips});
        std::int64_t start = 0;
        for (auto& clip : tracks.front().clips) {
            clip.timeline_start_frame = start;
            if (clip.duration_frames > 0 && start <=
                std::numeric_limits<std::int64_t>::max() - clip.duration_frames) {
                start += clip.duration_frames;
            }
        }
    }

    QJsonArray track_array;
    for (const auto& track_source : tracks) {
        QJsonObject track;
        track.insert("name", QString::fromStdString(track_source.name));
        track.insert("audio_gain", track_source.audio_gain);
        track.insert("audio_muted", track_source.audio_muted);
        QJsonArray clips;
        for (const auto& clip : track_source.clips) {
            QJsonObject item;
            const char* clip_kind = clip.kind == timeline::ClipKind::Text
                ? "text"
                : clip.kind == timeline::ClipKind::Image ? "image" : "video";
            item.insert("kind", clip_kind);
            if (timeline::isMediaClipKind(clip.kind)) {
                item.insert("source", storedPath(project_path, clip.source_path));
            } else {
                QJsonObject text;
                text.insert("content", QString::fromUtf8(
                    clip.text.content.data(), static_cast<int>(clip.text.content.size())));
                text.insert("font_family", QString::fromUtf8(
                    clip.text.font_family.data(), static_cast<int>(clip.text.font_family.size())));
                text.insert("font_size_pixels", clip.text.font_size_pixels);
                QJsonObject color;
                color.insert("r", clip.text.color[0]);
                color.insert("g", clip.text.color[1]);
                color.insert("b", clip.text.color[2]);
                color.insert("a", clip.text.color[3]);
                text.insert("color", color);
                const char* alignment = "center";
                if (clip.text.alignment == timeline::TextAlignment::Left) alignment = "left";
                else if (clip.text.alignment == timeline::TextAlignment::Right) alignment = "right";
                text.insert("alignment", alignment);
                item.insert("text", text);
            }
            item.insert("timeline_start_frame", static_cast<qint64>(clip.timeline_start_frame));
            item.insert("source_start_frame", static_cast<qint64>(clip.source_start_frame));
            item.insert("duration_frames", static_cast<qint64>(clip.duration_frames));
            item.insert("audio_gain", clip.audio_gain);
            item.insert("audio_muted", clip.audio_muted);
            QJsonObject transform;
            QJsonObject position;
            position.insert("x", clip.transform.position_x);
            position.insert("y", clip.transform.position_y);
            transform.insert("position", position);
            transform.insert("scale", clip.transform.scale);
            transform.insert("rotation", clip.transform.rotation_degrees);
            transform.insert("opacity", clip.transform.opacity);
            item.insert("transform", transform);

            QJsonObject keyframes;
            const auto write_keyframes = [](const std::vector<timeline::Keyframe>& values) {
                QJsonArray output;
                for (const auto& keyframe : values) {
                    QJsonObject item;
                    item.insert("frame", static_cast<qint64>(keyframe.frame));
                    item.insert("value", keyframe.value);
                    output.append(item);
                }
                return output;
            };
            keyframes.insert("position_x", write_keyframes(clip.keyframes.position_x));
            keyframes.insert("position_y", write_keyframes(clip.keyframes.position_y));
            keyframes.insert("scale", write_keyframes(clip.keyframes.scale));
            keyframes.insert("rotation", write_keyframes(clip.keyframes.rotation));
            keyframes.insert("opacity", write_keyframes(clip.keyframes.opacity));
            item.insert("keyframes", keyframes);
            clips.append(item);
        }
        track.insert("clips", clips);
        QJsonArray transitions;
        for (const auto& transition : track_source.transitions) {
            QJsonObject item;
            item.insert("from_clip", static_cast<qint64>(transition.from_clip_index));
            item.insert("to_clip", static_cast<qint64>(transition.to_clip_index));
            item.insert(
                "kind",
                transition.kind == timeline::TransitionKind::FadeToBlack
                    ? "fade_to_black"
                    : "cross_dissolve");
            item.insert("duration_frames", static_cast<qint64>(transition.duration_frames));
            transitions.append(item);
        }
        track.insert("transitions", transitions);
        track_array.append(track);
    }

    QJsonObject timeline;
    timeline.insert("tracks", track_array);
    timeline.insert("zoom", document.timeline_zoom);
    timeline.insert("row_height", document.timeline_row_height);
    QJsonObject root;
    root.insert("format", QString::fromLatin1(format_identifier));
    root.insert("version", current_format_version);
    QJsonObject canvas;
    canvas.insert("width", document.canvas_width);
    canvas.insert("height", document.canvas_height);
    root.insert("canvas", canvas);
    root.insert("media", media);
    if (!document.bins.empty()) {
        QJsonArray bins;
        for (const auto& bin : document.bins) bins.append(QString::fromStdString(bin));
        root.insert("bins", bins);
    }
    root.insert("timeline", timeline);

    QSaveFile file(pathToQString(project_path));
    if (!file.open(QIODevice::WriteOnly)) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not open the project file for atomic writing.",
            file.error(),
            project_path);
    }

    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not write the complete project file.",
            file.error(),
            project_path);
    }
    if (!file.commit()) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not commit the project file atomically.",
            file.error(),
            project_path);
    }
}

} // namespace project
