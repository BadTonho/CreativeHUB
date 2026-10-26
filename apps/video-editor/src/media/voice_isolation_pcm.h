#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace media {
// Immutable artifact; read() is thread-safe. Positions use the caller's rate
// and refer to source samples for clips, absolute Timeline samples for tracks.
class VoiceIsolationPcm {
public:
    virtual ~VoiceIsolationPcm() = default;
    virtual std::vector<float> read(std::int64_t start, std::size_t count, int rate, int channels) const = 0;
};
struct VoiceIsolationBinding {
    std::shared_ptr<const VoiceIsolationPcm> audio;
    double amount = 1;
};
struct VoiceIsolationContext {
    std::unordered_map<std::uint64_t, VoiceIsolationBinding> clips, tracks;
};
} // namespace media
