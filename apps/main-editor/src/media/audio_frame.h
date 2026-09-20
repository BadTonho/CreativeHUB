#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace media {

struct AudioPcmChunk {
    int sample_rate = 0;
    int channel_count = 0;
    std::int64_t first_sample_index = 0;
    std::vector<std::int16_t> samples;

    [[nodiscard]] std::size_t sampleCount() const noexcept {
        if (channel_count <= 0) return 0;
        return samples.size() / static_cast<std::size_t>(channel_count);
    }
};

} // namespace media
