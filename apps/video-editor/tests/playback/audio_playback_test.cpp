#include "media/audio_playback.h"
#include "media/video_metadata.h"
#include "playback/audio_output.h"

#include <QCoreApplication>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void writeLittleEndian(std::ofstream& output, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        output.put(static_cast<char>((value >> (index * 8)) & 0xffU));
    }
}

void writeLittleEndian(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8) & 0xffU));
}

std::filesystem::path createWav() {
    const auto path = std::filesystem::temp_directory_path() /
        "creative-suite-audio-playback-test.wav";
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);

    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t sample_count = 8000;
    constexpr std::uint16_t block_align = channels * (bits_per_sample / 8);
    constexpr std::uint32_t data_size = sample_count * block_align;

    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not create temporary WAV file.");
    output.write("RIFF", 4);
    writeLittleEndian(output, 36U + data_size);
    output.write("WAVEfmt ", 8);
    writeLittleEndian(output, 16U);
    writeLittleEndian(output, static_cast<std::uint16_t>(1));
    writeLittleEndian(output, channels);
    writeLittleEndian(output, sample_rate);
    writeLittleEndian(output, sample_rate * block_align);
    writeLittleEndian(output, block_align);
    writeLittleEndian(output, bits_per_sample);
    output.write("data", 4);
    writeLittleEndian(output, data_size);
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        const auto sample = static_cast<std::int16_t>(
            static_cast<int>(index % 256U) * 128 - 16384);
        writeLittleEndian(output, static_cast<std::uint16_t>(sample));
    }
    output.close();
    return path;
}

void testAudioDecodeAndSeek(const std::filesystem::path& path) {
    auto session = media::AudioPlaybackSession::open(
        path,
        media::AudioPlaybackSession::OutputSpec{16000, 2});
    assert(session->has_audio());
    assert(session->output_spec().sample_rate == 16000);
    assert(session->output_spec().channel_count == 2);

    const auto first = session->decode_samples(512);
    assert(first.has_value());
    assert(first->sample_rate == 16000);
    assert(first->channel_count == 2);
    assert(first->sampleCount() > 0);
    assert(first->samples.size() == first->sampleCount() * 2);

    session->seek_to_source_frame(15, 30.0);
    const auto middle = session->decode_samples(512);
    assert(middle.has_value());
    assert(middle->first_sample_index >= 8000);
    assert(middle->sampleCount() > 0);

    std::size_t decoded_samples = middle->sampleCount();
    for (int attempt = 0; attempt < 100 && !session->at_end(); ++attempt) {
        const auto chunk = session->decode_samples(512);
        if (chunk.has_value()) decoded_samples += chunk->sampleCount();
    }
    assert(decoded_samples > 0);
    assert(session->at_end());

    session->reset();
    session->seek_to_sample_index(1234);
    const auto exact_sample = session->decode_samples(512);
    assert(exact_sample.has_value());
    assert(exact_sample->first_sample_index >= 1234);
}

void testMissingAudioDoesNotBecomeAnError(const std::filesystem::path& path) {
    auto session = media::AudioPlaybackSession::open(path);
    assert(!session->has_audio());
    assert(!session->decode_samples().has_value());
}

void testDisabledAudioOutputFallback() {
    qputenv("CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT", "1");
    playback::AudioOutput output;
    QString error;
    assert(!output.initialize(&error, nullptr));
    assert(output.disabledByEnvironment());
    assert(!output.bufferedUsecs().has_value());
    qunsetenv("CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT");
}

void testMonitorVolumeNormalization() {
    assert(playback::AudioOutput::normalizeVolume(0.0) == 0.0);
    assert(playback::AudioOutput::normalizeVolume(1.0) == 1.0);
    assert(playback::AudioOutput::normalizeVolume(2.0) == 2.0);
    assert(playback::AudioOutput::normalizeVolume(-0.1) == 1.0);
    assert(playback::AudioOutput::normalizeVolume(2.1) == 1.0);
    assert(playback::AudioOutput::normalizeVolume(std::numeric_limits<double>::quiet_NaN()) == 1.0);
    assert(playback::AudioOutput::normalizeVolume(
               std::numeric_limits<double>::infinity()) == 1.0);

    assert(playback::AudioOutput::outputVolume(0.0) == 0.0);
    assert(playback::AudioOutput::outputVolume(1.5) == 1.0);
    assert(playback::AudioOutput::sampleBoost(0.5) == 1.0);
    assert(playback::AudioOutput::sampleBoost(1.5) == 1.5);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto wav_path = createWav();
    try {
        testAudioDecodeAndSeek(wav_path);
        testDisabledAudioOutputFallback();
        testMonitorVolumeNormalization();
        if (argc > 1) {
            testMissingAudioDoesNotBecomeAnError(argv[1]);
        }
        std::error_code remove_error;
        std::filesystem::remove(wav_path, remove_error);
        std::cout << "audio playback tests passed\n";
        return 0;
    } catch (...) {
        std::error_code remove_error;
        std::filesystem::remove(wav_path, remove_error);
        throw;
    }
}
