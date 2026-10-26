#pragma once
#include "voice_isolation_pcm.h"
#include <QString>
#include <functional>
#include <exception>
#include <span>

namespace media {
class VoiceIsolationCancelled final : public std::exception {
public: const char* what() const noexcept override { return "Voice isolation cancelled."; }
};
using IsolationCancelled = std::function<bool()>;
using IsolationProgress = std::function<void(int)>;
using IsolationSource = std::function<std::vector<float>(std::int64_t, std::size_t)>;
// Worker-only operations. Cache files are validated before leasing; the model
// is extracted lazily from the linked resource and verified against its hash.
QString voiceIsolationModelPath();
QString voiceIsolationCacheRoot();
std::shared_ptr<const VoiceIsolationPcm> findVoiceIsolation(const QString& key);
std::shared_ptr<const VoiceIsolationPcm> generateVoiceIsolation(const QString& key,
    std::int64_t first, std::int64_t count, int channels, const IsolationSource& source,
    const IsolationCancelled& canceled, const IsolationProgress& progress = {});
void pruneVoiceIsolationCache(std::uint64_t budget = 2ULL * 1024 * 1024 * 1024);
void forgetVoiceIsolation(const QString& key);
} // namespace media
