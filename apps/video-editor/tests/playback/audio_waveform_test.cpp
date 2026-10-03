#include "logging/logger.h"
#include "media/audio_waveform.h"
#include "media/audio_waveform_cache.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void writeLittleEndian(std::ostream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

void writeLittleEndian(std::ostream& output, std::uint32_t value) {
    writeLittleEndian(output, static_cast<std::uint16_t>(value & 0xffffU));
    writeLittleEndian(output, static_cast<std::uint16_t>(value >> 16U));
}

std::filesystem::path uniquePath(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-audio-waveform-" + std::to_string(stamp) + suffix);
}

std::filesystem::path createStereoWav() {
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t block_align = channels * 2;
    constexpr std::uint32_t sample_frames = sample_rate;
    constexpr std::uint32_t data_size = sample_frames * block_align;
    const auto path = uniquePath(".wav");
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not create waveform WAV fixture.");

    output.write("RIFF", 4);
    writeLittleEndian(output, 36U + data_size);
    output.write("WAVEfmt ", 8);
    writeLittleEndian(output, 16U);
    writeLittleEndian(output, static_cast<std::uint16_t>(1));
    writeLittleEndian(output, channels);
    writeLittleEndian(output, sample_rate);
    writeLittleEndian(output, sample_rate * block_align);
    writeLittleEndian(output, block_align);
    writeLittleEndian(output, static_cast<std::uint16_t>(16));
    output.write("data", 4);
    writeLittleEndian(output, data_size);
    for (std::uint32_t index = 0; index < sample_frames; ++index) {
        std::int16_t left = 0;
        std::int16_t right = 0;
        if (index >= 800 && index < 1600) left = 8192;
        if (index >= 1600 && index < 2400) {
            left = 16384;
            right = -16384;
        }
        if (index >= 2400 && index < 3200) right = -8192;
        writeLittleEndian(output, static_cast<std::uint16_t>(left));
        writeLittleEndian(output, static_cast<std::uint16_t>(right));
    }
    output.close();
    return path;
}

std::filesystem::path createMonoWav() {
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t block_align = channels * 2;
    constexpr std::uint32_t sample_frames = sample_rate;
    constexpr std::uint32_t data_size = sample_frames * block_align;
    const auto path = uniquePath("-mono.wav");
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not create mono waveform WAV fixture.");

    output.write("RIFF", 4);
    writeLittleEndian(output, 36U + data_size);
    output.write("WAVEfmt ", 8);
    writeLittleEndian(output, 16U);
    writeLittleEndian(output, static_cast<std::uint16_t>(1));
    writeLittleEndian(output, channels);
    writeLittleEndian(output, sample_rate);
    writeLittleEndian(output, sample_rate * block_align);
    writeLittleEndian(output, block_align);
    writeLittleEndian(output, static_cast<std::uint16_t>(16));
    output.write("data", 4);
    writeLittleEndian(output, data_size);
    for (std::uint32_t index = 0; index < sample_frames; ++index) {
        const auto sample = index >= 800 && index < 1600
            ? static_cast<std::int16_t>(8192)
            : static_cast<std::int16_t>(0);
        writeLittleEndian(output, static_cast<std::uint16_t>(sample));
    }
    output.close();
    return path;
}

void testWaveformExtraction(const std::filesystem::path& path) {
    const auto waveform = media::decodeAudioWaveform(path);
    require(waveform.has_value(), "A valid WAV produced no waveform.");
    require(waveform->source_channel_count == 2,
            "The stereo WAV source channel count was not retained.");
    require(waveform->peaks.size() == 100, "The waveform did not use 10 ms buckets.");
    require(waveform->peaks[0] == media::AudioWaveformPeak{} &&
                waveform->peaks[9] == media::AudioWaveformPeak{} &&
                waveform->peaks[99] == media::AudioWaveformPeak{},
            "Silent WAV regions produced nonzero peaks.");
    require(waveform->peaks[10].left >= 63 && waveform->peaks[10].left <= 65 &&
                waveform->peaks[15].left >= 63 && waveform->peaks[15].left <= 65 &&
                waveform->peaks[10].right == 0 && waveform->peaks[15].right == 0,
            "The left-channel signal was mixed into the wrong waveform channel.");
    require(waveform->peaks[20].left >= 127 && waveform->peaks[20].left <= 129 &&
                waveform->peaks[25].left >= 127 && waveform->peaks[25].left <= 129 &&
                waveform->peaks[20].right >= 127 && waveform->peaks[20].right <= 129 &&
                waveform->peaks[25].right >= 127 && waveform->peaks[25].right <= 129,
            "Opposite-phase stereo samples cancelled instead of retaining channel peaks.");
    require(waveform->peaks[30].left == 0 &&
                waveform->peaks[30].right >= 63 && waveform->peaks[30].right <= 65,
            "The right-channel signal was mixed into the wrong waveform channel.");

    const auto cancelled = media::decodeAudioWaveform(path, [] { return true; });
    require(!cancelled.has_value(), "A cancelled waveform request returned data.");
}

void testMonoWaveformExtraction(const std::filesystem::path& path) {
    const auto waveform = media::decodeAudioWaveform(path);
    require(waveform.has_value(), "A valid mono WAV produced no waveform.");
    require(waveform->source_channel_count == 1,
            "The mono WAV source channel count was not retained.");
    require(waveform->peaks.size() == 100,
            "The mono WAV did not use 10 ms buckets.");
    require(waveform->peaks[10].left > 0 && waveform->peaks[10].right > 0,
            "The mono source did not produce a centered waveform signal.");
}

void testWaveformCache(const std::filesystem::path& path) {
    const auto modified = std::filesystem::file_time_type{};
    auto first = std::make_shared<media::AudioWaveform>();
    first->peaks = {{10, 11}, {20, 21}, {30, 31}, {40, 41}};
    auto second = std::make_shared<media::AudioWaveform>();
    second->peaks = {{50, 51}, {60, 61}, {70, 71}};
    const auto budget = std::max(first->memoryBytes(), second->memoryBytes());
    media::AudioWaveformCache cache(budget);
    const auto first_path = path.string() + ".first";
    const auto second_path = path.string() + ".second";
    const media::AudioWaveformSourceSignature signature{12, modified};

    require(cache.insert(first_path, signature, first),
            "The cache rejected a waveform within its byte budget.");
    require(cache.find(first_path, signature) == first,
            "The cache did not reuse a waveform for the same source signature.");
    require(cache.insert(second_path, signature, second),
            "The cache rejected a replacement waveform within its byte budget.");
    require(cache.size() == 1 && cache.retainedBytes() <= budget,
            "The cache exceeded its memory budget instead of evicting its least-recent item.");
    require(!cache.find(first_path, signature),
            "The least-recently-used waveform was not evicted.");
    require(cache.find(second_path, signature) == second,
            "The cache did not retain the newest waveform.");

    const media::AudioWaveformSourceSignature changed_signature{13, modified};
    require(!cache.find(second_path, changed_signature),
            "A stale file signature reused waveform data.");
    require(cache.size() == 0 && cache.retainedBytes() == 0,
            "Invalidating a file signature did not release the cached waveform.");

    require(cache.insert(first_path, signature, first),
            "The cache could not be populated before clearing.");
    cache.clear();
    require(cache.size() == 0 && cache.retainedBytes() == 0,
            "Clearing the cache retained waveform data.");

    media::AudioWaveformCache undersized_cache(1);
    require(!undersized_cache.insert(first_path, signature, first) &&
                undersized_cache.retainedBytes() == 0,
            "The cache retained a waveform larger than its configured byte limit.");
}

void testInvalidInput(const std::filesystem::path& path) {
    std::ofstream invalid(path, std::ios::binary);
    invalid << "not an audio file";
    invalid.close();

    bool threw = false;
    try {
        static_cast<void>(media::decodeAudioWaveform(path));
    } catch (const media::MediaError&) {
        threw = true;
    }
    require(threw, "An invalid media file did not return an actionable decode failure.");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto waveform_path = createStereoWav();
    const auto mono_waveform_path = createMonoWav();
    const auto invalid_path = uniquePath(".invalid");
    try {
        testWaveformExtraction(waveform_path);
        testMonoWaveformExtraction(mono_waveform_path);
        testWaveformCache(waveform_path);
        testInvalidInput(invalid_path);
        std::error_code remove_error;
        std::filesystem::remove(waveform_path, remove_error);
        std::filesystem::remove(mono_waveform_path, remove_error);
        std::filesystem::remove(invalid_path, remove_error);
        std::cout << "audio waveform tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code remove_error;
        std::filesystem::remove(waveform_path, remove_error);
        std::filesystem::remove(mono_waveform_path, remove_error);
        std::filesystem::remove(invalid_path, remove_error);
        std::cerr << error.what() << '\n';
        return 1;
    } catch (...) {
        std::error_code remove_error;
        std::filesystem::remove(waveform_path, remove_error);
        std::filesystem::remove(invalid_path, remove_error);
        std::cerr << "Unknown waveform test failure.\n";
        return 1;
    }
}
