#pragma once
#include "timeline/audio_effects.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <span>
#include <vector>

namespace media {
class AudioFilterError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
// Worker-owned, move-only FFmpeg filter graph. Input/output are interleaved
// floats at the same rate/layout. pull() removes intrinsic signal delay;
// callers supply bounded lookahead, including silence past the visible end.
class AudioFilterSession final {
public:
    AudioFilterSession(const timeline::AudioEffectInstance& effect, int rate, int channels);
    ~AudioFilterSession();
    AudioFilterSession(AudioFilterSession&&) noexcept;
    AudioFilterSession& operator=(AudioFilterSession&&) noexcept;
    AudioFilterSession(const AudioFilterSession&) = delete;
    AudioFilterSession& operator=(const AudioFilterSession&) = delete;
    void push(std::span<const float> samples);
    void drain();
    [[nodiscard]] std::vector<float> pull();
    [[nodiscard]] int latencySamples() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace media
