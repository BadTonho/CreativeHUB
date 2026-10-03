#include "audio_waveform_cache.h"

#include <utility>

namespace media {

std::optional<AudioWaveformSourceSignature> audioWaveformSourceSignature(
    const std::filesystem::path& source_path) noexcept {
    if (source_path.empty()) return std::nullopt;
    std::error_code size_error;
    if (!std::filesystem::is_regular_file(source_path, size_error) || size_error) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(source_path, size_error);
    if (size_error) return std::nullopt;
    std::error_code modified_error;
    const auto modified = std::filesystem::last_write_time(
        source_path, modified_error);
    if (modified_error) return std::nullopt;
    return AudioWaveformSourceSignature{size, modified};
}

AudioWaveformCache::AudioWaveformCache(std::size_t byte_limit) noexcept
    : byte_limit_(byte_limit) {}

std::shared_ptr<const AudioWaveform> AudioWaveformCache::find(
    const std::filesystem::path& source_path,
    AudioWaveformSourceSignature signature) {
    const auto entry = entries_.find(source_path);
    if (entry == entries_.end()) return {};
    if (entry->second.signature != signature) {
        erase(entry);
        return {};
    }
    recency_.splice(recency_.end(), recency_, entry->second.recency);
    entry->second.recency = std::prev(recency_.end());
    return entry->second.waveform;
}

bool AudioWaveformCache::insert(
    const std::filesystem::path& source_path,
    AudioWaveformSourceSignature signature,
    std::shared_ptr<const AudioWaveform> waveform) noexcept {
    if (source_path.empty() || waveform == nullptr) return false;
    const auto bytes = waveform->memoryBytes();
    if (bytes == 0 || bytes > byte_limit_) return false;

    const auto existing = entries_.find(source_path);
    if (existing != entries_.end()) erase(existing);

    while (!recency_.empty() && retained_bytes_ > byte_limit_ - bytes) {
        const auto oldest = entries_.find(recency_.front());
        if (oldest == entries_.end()) {
            recency_.pop_front();
            continue;
        }
        erase(oldest);
    }

    try {
        recency_.push_back(source_path);
        const auto recency = std::prev(recency_.end());
        try {
            const auto [entry, inserted] = entries_.emplace(source_path, Entry{
                signature,
                std::move(waveform),
                bytes,
                recency});
            static_cast<void>(entry);
            if (!inserted) {
                recency_.erase(recency);
                return false;
            }
        } catch (...) {
            recency_.erase(recency);
            return false;
        }
        retained_bytes_ += bytes;
        return true;
    } catch (...) {
        return false;
    }
}

void AudioWaveformCache::clear() noexcept {
    entries_.clear();
    recency_.clear();
    retained_bytes_ = 0;
}

std::size_t AudioWaveformCache::retainedBytes() const noexcept {
    return retained_bytes_;
}

std::size_t AudioWaveformCache::size() const noexcept {
    return entries_.size();
}

void AudioWaveformCache::erase(
    std::unordered_map<std::filesystem::path, Entry>::iterator entry) noexcept {
    if (entry == entries_.end()) return;
    retained_bytes_ -= entry->second.bytes;
    recency_.erase(entry->second.recency);
    entries_.erase(entry);
}

} // namespace media
