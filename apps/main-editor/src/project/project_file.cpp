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

#include "../media/media_library.h"

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

void validateDocument(const ProjectDocument& document,
                      const std::filesystem::path& project_path) {
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
        if (clip.source_path.empty() || clip.timeline_start_frame < 0 ||
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
                if (second.timeline_start_frame < first_end &&
                    first.timeline_start_frame <
                        second_end) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains overlapping clips on one track.");
                }
            }
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
    if (version != current_format_version && version != legacy_format_version) {
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
                clip.source_path = resolvedPath(project_path, requiredString(clip_object, "source", project_path));
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
                track.clips.push_back(clip);
                document.timeline_clips.push_back(std::move(clip));
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
            item.insert("source", storedPath(project_path, clip.source_path));
            item.insert("timeline_start_frame", static_cast<qint64>(clip.timeline_start_frame));
            item.insert("source_start_frame", static_cast<qint64>(clip.source_start_frame));
            item.insert("duration_frames", static_cast<qint64>(clip.duration_frames));
            item.insert("audio_gain", clip.audio_gain);
            item.insert("audio_muted", clip.audio_muted);
            clips.append(item);
        }
        track.insert("clips", clips);
        track_array.append(track);
    }

    QJsonObject timeline;
    timeline.insert("tracks", track_array);
    QJsonObject root;
    root.insert("format", QString::fromLatin1(format_identifier));
    root.insert("version", current_format_version);
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
