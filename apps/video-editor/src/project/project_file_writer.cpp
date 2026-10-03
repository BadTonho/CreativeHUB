#include "project_file_detail.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

#include <system_error>

namespace project::detail {
namespace {

QString pathToQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()),
                             static_cast<qsizetype>(value.size()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
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

const char* mediaKindName(media::MediaKind kind) {
    if (kind == media::MediaKind::Image) return "image";
    if (kind == media::MediaKind::Audio) return "audio";
    return "video";
}

const char* trackKindName(timeline::TrackKind kind) {
    return kind == timeline::TrackKind::Audio ? "audio" : "video";
}

QJsonObject linkedImageJson(
    const std::filesystem::path& project_path,
    const media::LinkedImageReference& link) {
    QJsonObject object;
    object.insert("id", QString::fromUtf8(link.id.data(),
                                            static_cast<qsizetype>(link.id.size())));
    object.insert("document", storedPath(project_path, link.document_path));
    object.insert("output", storedPath(project_path, link.published_output_path));
    return object;
}

} // namespace

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
        if (media_source.image_editor_link.has_value()) {
            item.insert("image_editor_link",
                        linkedImageJson(project_path, *media_source.image_editor_link));
        }
        media.append(item);
    }

    const auto& tracks = document.timeline_tracks;

    QJsonArray track_array;
    for (const auto& track_source : tracks) {
        QJsonObject track;
        track.insert("track_id", static_cast<qint64>(track_source.track_id));
        track.insert("name", QString::fromStdString(track_source.name));
        track.insert("audio_gain", track_source.audio_gain);
        track.insert("audio_muted", track_source.audio_muted);
        track.insert("kind", trackKindName(track_source.kind));
        QJsonArray clips;
        for (const auto& clip : track_source.clips) {
            QJsonObject item;
            item.insert("clip_id", static_cast<qint64>(clip.clip_id));
            const char* clip_kind = clip.kind == timeline::ClipKind::Text
                ? "text"
                : clip.kind == timeline::ClipKind::Image ? "image"
                : clip.kind == timeline::ClipKind::Audio ? "audio" : "video";
            item.insert("kind", clip_kind);
            if (clip.image_editor_variant.has_value()) {
                item.insert("image_editor_variant",
                            linkedImageJson(project_path, *clip.image_editor_variant));
            }
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
            if (clip.kind != timeline::ClipKind::Audio) {
                item.insert("source_start_frame",
                            static_cast<qint64>(clip.source_start_frame));
            }
            item.insert("duration_frames", static_cast<qint64>(clip.duration_frames));
            if (timeline::isFrameTimedMediaClipKind(clip.kind)) {
                item.insert("source_duration_frames",
                            static_cast<qint64>(clip.source_duration_frames));
                item.insert("source_duration_migration_pending",
                            clip.source_duration_migration_pending);
            }
            if (clip.kind == timeline::ClipKind::Audio) {
                item.insert("source_start_time_us",
                            static_cast<qint64>(clip.source_start_time_us));
                item.insert("source_duration_time_us",
                            static_cast<qint64>(clip.source_duration_time_us));
            }
            if (clip.linked_clip_id.has_value()) {
                item.insert("linked_clip_id",
                            static_cast<qint64>(*clip.linked_clip_id));
            }
            item.insert("audio_extracted", clip.audio_extracted);
            item.insert("audio_companion_pending", clip.audio_companion_pending);
            item.insert("audio_gain", clip.audio_gain);
            item.insert("audio_muted", clip.audio_muted);
            if (clip.kind == timeline::ClipKind::Audio) {
                QJsonArray audio_gain_keyframes;
                for (const auto& keyframe : clip.audio_gain_keyframes) {
                    QJsonObject point;
                    point.insert("frame", static_cast<qint64>(keyframe.frame));
                    point.insert("gain", keyframe.gain);
                    audio_gain_keyframes.append(point);
                }
                item.insert("audio_gain_keyframes", audio_gain_keyframes);
            }
            if (!clip.effects.empty()) {
                QJsonArray effect_stack;
                for (const auto& effect : clip.effects) {
                    QJsonObject effect_object;
                    effect_object.insert("id", QString::fromUtf8(
                        effect.id.data(), static_cast<qsizetype>(effect.id.size())));
                    QJsonArray parameters;
                    for (const auto& parameter : effect.parameters) {
                        QJsonObject parameter_object;
                        parameter_object.insert("id", QString::fromUtf8(
                            parameter.id.data(),
                            static_cast<qsizetype>(parameter.id.size())));
                        parameter_object.insert("value", parameter.value);
                        parameters.append(parameter_object);
                    }
                    effect_object.insert("parameters", parameters);
                    effect_stack.append(effect_object);
                }
                item.insert("effects", effect_stack);
            }
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
                    : transition.kind == timeline::TransitionKind::AudioCrossfade
                        ? "audio_crossfade"
                        : "cross_dissolve");
            item.insert("duration_frames", static_cast<qint64>(transition.duration_frames));
            transitions.append(item);
        }
        track.insert("transitions", transitions);
        track_array.append(track);
    }

    QJsonObject timeline;
    timeline.insert("tracks", track_array);
    QJsonObject frame_rate;
    frame_rate.insert("numerator",
                      static_cast<qint64>(document.timeline_frame_rate.numerator));
    frame_rate.insert("denominator",
                      static_cast<qint64>(document.timeline_frame_rate.denominator));
    timeline.insert("frame_rate", frame_rate);
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

} // namespace project::detail
