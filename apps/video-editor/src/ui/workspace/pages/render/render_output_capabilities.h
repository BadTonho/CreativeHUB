#pragma once

#include <string>
#include <vector>

namespace ui {

struct RenderEncoderOption {
    std::string name;
    std::string display_name;
    int codec_id = 0;
};

struct RenderContainerOption {
    std::string name;
    std::string display_name;
    std::string extensions;
    std::vector<RenderEncoderOption> video_encoders;
    std::vector<RenderEncoderOption> audio_encoders;
};

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

}  // namespace ui
