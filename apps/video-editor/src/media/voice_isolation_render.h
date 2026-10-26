#pragma once
#include "voice_isolation.h"
#include "project/project_document.h"
#include <set>

namespace media {
using VoiceIsolationTarget = std::pair<bool, std::uint64_t>; // true: clip; false: track
using VoiceIsolationJobProgress = std::function<void(VoiceIsolationTarget, int)>;
bool hasVoiceIsolation(const project::ProjectDocument& document);
// Worker-only snapshot preparation. Dependencies are processed before tracks.
// Cache hits do not run the neural model. force bypasses cache lookup once.
std::shared_ptr<const VoiceIsolationContext> prepareVoiceIsolation(
    const project::ProjectDocument& document, const IsolationCancelled& canceled,
    const VoiceIsolationJobProgress& progress = {},
    const std::set<VoiceIsolationTarget>& force = {}, int channels = 2);
QString voiceIsolationRequestKey(const project::ProjectDocument& document, int channels = 2, bool file_identity = true);
} // namespace media
