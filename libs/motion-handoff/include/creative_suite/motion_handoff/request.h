#pragma once

#include <QJsonObject>
#include <QString>

#include <cmath>
#include <utility>

namespace creative_suite::motion_handoff {

inline constexpr int request_version = 1;

struct Request {
    QString origin_kind; // timeline_clip or media_item
    QString source_kind; // video or image
    QString source_path;
    QString document_path;
    QString published_output_path;
    QString container;
    QString codec;
    QString quality;
    double bitrate_mbps = 10.0;
    int canvas_width = 1920;
    int canvas_height = 1080;
    qint64 frame_rate_numerator = 30;
    qint64 frame_rate_denominator = 1;
    qint64 timeline_start_frame = 0;
    qint64 timeline_duration_frames = 0;
    qint64 source_start_frame = 0;
    qint64 source_duration_frames = 0;
    qint64 source_frame_count = 0;
    double source_frame_rate = 0.0;

    [[nodiscard]] QJsonObject toJson() const {
        QJsonObject object;
        object.insert(QStringLiteral("format"), QStringLiteral("creative-suite.motion-handoff"));
        object.insert(QStringLiteral("version"), request_version);
        object.insert(QStringLiteral("origin_kind"), origin_kind);
        object.insert(QStringLiteral("source_kind"), source_kind);
        object.insert(QStringLiteral("source_path"), source_path);
        object.insert(QStringLiteral("document_path"), document_path);
        object.insert(QStringLiteral("published_output_path"), published_output_path);
        object.insert(QStringLiteral("container"), container);
        object.insert(QStringLiteral("codec"), codec);
        object.insert(QStringLiteral("quality"), quality);
        object.insert(QStringLiteral("bitrate_mbps"), bitrate_mbps);
        object.insert(QStringLiteral("canvas_width"), canvas_width);
        object.insert(QStringLiteral("canvas_height"), canvas_height);
        object.insert(QStringLiteral("frame_rate_numerator"), frame_rate_numerator);
        object.insert(QStringLiteral("frame_rate_denominator"), frame_rate_denominator);
        object.insert(QStringLiteral("timeline_start_frame"), timeline_start_frame);
        object.insert(QStringLiteral("timeline_duration_frames"), timeline_duration_frames);
        object.insert(QStringLiteral("source_start_frame"), source_start_frame);
        object.insert(QStringLiteral("source_duration_frames"), source_duration_frames);
        object.insert(QStringLiteral("source_frame_count"), source_frame_count);
        object.insert(QStringLiteral("source_frame_rate"), source_frame_rate);
        return object;
    }

    [[nodiscard]] static bool parse(const QJsonObject& object, Request* request,
                                    QString* error = nullptr) {
        if (request == nullptr || object.value(QStringLiteral("format")).toString() !=
                QStringLiteral("creative-suite.motion-handoff") ||
            object.value(QStringLiteral("version")).toInt(-1) != request_version) {
            if (error) *error = QStringLiteral("Unsupported Motion handoff request.");
            return false;
        }
        Request parsed;
        parsed.origin_kind = object.value(QStringLiteral("origin_kind")).toString();
        parsed.source_kind = object.value(QStringLiteral("source_kind")).toString();
        parsed.source_path = object.value(QStringLiteral("source_path")).toString();
        parsed.document_path = object.value(QStringLiteral("document_path")).toString();
        parsed.published_output_path = object.value(QStringLiteral("published_output_path")).toString();
        parsed.container = object.value(QStringLiteral("container")).toString();
        parsed.codec = object.value(QStringLiteral("codec")).toString();
        parsed.quality = object.value(QStringLiteral("quality")).toString();
        parsed.bitrate_mbps = object.value(QStringLiteral("bitrate_mbps")).toDouble();
        parsed.canvas_width = object.value(QStringLiteral("canvas_width")).toInt();
        parsed.canvas_height = object.value(QStringLiteral("canvas_height")).toInt();
        parsed.frame_rate_numerator = object.value(QStringLiteral("frame_rate_numerator")).toInteger();
        parsed.frame_rate_denominator = object.value(QStringLiteral("frame_rate_denominator")).toInteger();
        parsed.timeline_start_frame = object.value(QStringLiteral("timeline_start_frame")).toInteger();
        parsed.timeline_duration_frames = object.value(QStringLiteral("timeline_duration_frames")).toInteger();
        parsed.source_start_frame = object.value(QStringLiteral("source_start_frame")).toInteger();
        parsed.source_duration_frames = object.value(QStringLiteral("source_duration_frames")).toInteger();
        parsed.source_frame_count = object.value(QStringLiteral("source_frame_count")).toInteger();
        parsed.source_frame_rate = object.value(QStringLiteral("source_frame_rate")).toDouble();
        const bool paths_valid = !parsed.source_path.isEmpty() && !parsed.document_path.isEmpty() &&
            !parsed.published_output_path.isEmpty();
        const bool profile_valid = !parsed.container.isEmpty() && !parsed.codec.isEmpty() &&
            !parsed.quality.isEmpty() && std::isfinite(parsed.bitrate_mbps) &&
            parsed.bitrate_mbps > 0.0;
        const bool source_valid = parsed.source_kind == QStringLiteral("video") ||
            parsed.source_kind == QStringLiteral("image");
        const bool origin_valid = parsed.origin_kind == QStringLiteral("timeline_clip") ||
            parsed.origin_kind == QStringLiteral("media_item");
        const bool origin_source_valid = parsed.origin_kind == QStringLiteral("media_item") ||
            parsed.source_kind == QStringLiteral("video");
        const bool timing_valid = parsed.canvas_width > 0 && parsed.canvas_height > 0 &&
            parsed.frame_rate_numerator > 0 && parsed.frame_rate_denominator > 0 &&
            parsed.timeline_start_frame >= 0 && parsed.timeline_duration_frames >= 0 &&
            parsed.source_start_frame >= 0 && parsed.source_duration_frames >= 0 &&
            parsed.source_frame_count >= 0 && std::isfinite(parsed.source_frame_rate) &&
            parsed.source_frame_rate >= 0.0 &&
            (parsed.origin_kind != QStringLiteral("timeline_clip") ||
             (parsed.timeline_duration_frames > 0 &&
              parsed.source_duration_frames > 0));
        const bool source_range_valid = parsed.source_frame_count == 0 ||
            parsed.source_start_frame < parsed.source_frame_count;
        if (!paths_valid || !profile_valid || !source_valid || !origin_valid ||
            !origin_source_valid || !timing_valid || !source_range_valid) {
            if (error) *error = QStringLiteral("The Motion handoff request contains invalid fields.");
            return false;
        }
        *request = std::move(parsed);
        return true;
    }
};

} // namespace creative_suite::motion_handoff
