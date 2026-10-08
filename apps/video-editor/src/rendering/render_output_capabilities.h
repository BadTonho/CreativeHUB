#pragma once

#include <creative_suite/media/video_encoder.h>

namespace rendering {

using RenderEncoderOption = creative_suite::media::VideoEncoderOption;
using RenderContainerOption = creative_suite::media::VideoContainerOption;

class RenderOutputCapabilities final {
public:
    [[nodiscard]] static std::vector<RenderContainerOption> availableContainers();
    [[nodiscard]] static bool supportsVideoEncoder(
        const RenderContainerOption& container,
        const std::string& encoder_name);
    [[nodiscard]] static bool supportsAudioEncoder(
        const RenderContainerOption& container,
        const std::string& encoder_name);
};

}  // namespace rendering
