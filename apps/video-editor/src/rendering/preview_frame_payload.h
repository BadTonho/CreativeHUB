#pragma once

#include "media/video_frame.h"
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <QtGlobal>
#include <QMetaType>
#include <utility>

namespace rendering {

// Transport-only metadata. The CPU media-frame contract remains unchanged.
struct PreviewFramePayload {
    PreviewFramePayload() = default;
    PreviewFramePayload(media::VideoFramePtr frame) : rgba(std::move(frame)) {}
    media::VideoFramePtr rgba;
    creative_suite::composition::OpenGlTextureFramePtr gpu;
    quint64 delivery_epoch = 0;
    quint64 composition_revision = 0;
    quint64 playback_generation = 0;
    qint64 timeline_frame = 0;
    [[nodiscard]] bool valid() const noexcept {
        return bool(rgba) != bool(gpu) && (!gpu || gpu->valid());
    }
    [[nodiscard]] int width() const noexcept { return rgba ? rgba->width : gpu ? gpu->width() : 0; }
    [[nodiscard]] int height() const noexcept { return rgba ? rgba->height : gpu ? gpu->height() : 0; }
};

} // namespace rendering
Q_DECLARE_METATYPE(rendering::PreviewFramePayload)
