#pragma once

#include "audio_waveform.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>

namespace media {

struct AudioWaveformSourceSignature {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};

    friend bool operator==(
        const AudioWaveformSourceSignature&,
        const AudioWaveformSourceSignature&) = default;
};

[[nodiscard]] std::optional<AudioWaveformSourceSignature>
audioWaveformSourceSignature(const std::filesystem::path& source_path) noexcept;

class AudioWaveformCache final {
public:
    static constexpr std::size_t kDefaultByteLimit = 64U * 1024U * 1024U;

    explicit AudioWaveformCache(
        std::size_t byte_limit = kDefaultByteLimit) noexcept;

    [[nodiscard]] std::shared_ptr<const AudioWaveform> find(
        const std::filesystem::path& source_path,
        AudioWaveformSourceSignature signature);
    [[nodiscard]] bool insert(
        const std::filesystem::path& source_path,
        AudioWaveformSourceSignature signature,
        std::shared_ptr<const AudioWaveform> waveform) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t retainedBytes() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry {
        AudioWaveformSourceSignature signature;
        std::shared_ptr<const AudioWaveform> waveform;
        std::size_t bytes = 0;
        std::list<std::filesystem::path>::iterator recency;
    };

    void erase(std::unordered_map<std::filesystem::path, Entry>::iterator entry) noexcept;

    std::size_t byte_limit_ = kDefaultByteLimit;
    std::size_t retained_bytes_ = 0;
    std::list<std::filesystem::path> recency_;
    std::unordered_map<std::filesystem::path, Entry> entries_;
};

} // namespace media
