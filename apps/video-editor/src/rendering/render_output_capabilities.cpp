#include "rendering/render_output_capabilities.h"

namespace rendering {

std::vector<RenderContainerOption> RenderOutputCapabilities::availableContainers() {
    return creative_suite::media::availableVideoContainers();
}

bool RenderOutputCapabilities::supportsVideoEncoder(
    const RenderContainerOption& container,
    const std::string& encoder_name) {
    return creative_suite::media::supportsVideoEncoder(container, encoder_name);
}

bool RenderOutputCapabilities::supportsAudioEncoder(
    const RenderContainerOption& container,
    const std::string& encoder_name) {
    return creative_suite::media::supportsAudioEncoder(container, encoder_name);
}

}  // namespace rendering
