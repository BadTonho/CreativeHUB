#pragma once
#include "project/project_document.h"
#include <functional>
namespace media {
// Snapshot-owned analysis; cancellation never returns a partial curve.
std::vector<timeline::AudioDuckingKeyframe> generateAudioDuckingCurve(
    const project::ProjectDocument& document, const timeline::AudioDuckingSettings& settings,
    const std::function<bool()>& canceled, const std::function<void(int)>& progress);
}
