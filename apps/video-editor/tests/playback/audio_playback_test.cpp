#include "media/audio_playback.h"
#include "media/video_metadata.h"
#include "playback/audio_output.h"

#include <QCoreApplication>

#include "../../../../cmake/test_support/test_check.h"
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
    CS_TEST_CHECK(session->has_audio());
    CS_TEST_CHECK(session->output_spec().sample_rate == 16000);
    CS_TEST_CHECK(session->output_spec().channel_count == 2);

    const auto first = session->decode_samples(512);
    CS_TEST_CHECK(first.has_value());
    CS_TEST_CHECK(first->sample_rate == 16000);
    CS_TEST_CHECK(first->channel_count == 2);
    CS_TEST_CHECK(first->sampleCount() > 0);
    CS_TEST_CHECK(first->samples.size() == first->sampleCount() * 2);

    session->seek_to_source_frame(15, 30.0);
    const auto middle = session->decode_samples(512);
    CS_TEST_CHECK(middle.has_value());
    CS_TEST_CHECK(middle->first_sample_index >= 8000);
    CS_TEST_CHECK(middle->sampleCount() > 0);

    std::size_t decoded_samples = middle->sampleCount();
    for (int attempt = 0; attempt < 100 && !session->at_end(); ++attempt) {
        const auto chunk = session->decode_samples(512);
        if (chunk.has_value()) decoded_samples += chunk->sampleCount();
    }
    CS_TEST_CHECK(decoded_samples > 0);
    CS_TEST_CHECK(session->at_end());

    session->reset();
    session->seek_to_sample_index(1234);
    const auto exact_sample = session->decode_samples(512);
    CS_TEST_CHECK(exact_sample.has_value());
    CS_TEST_CHECK(exact_sample->first_sample_index >= 1234);
}

std::vector<std::int16_t> decodeEntireAudio(
    const std::filesystem::path& path,
    media::AudioPlaybackSession::OutputSpec output,
    std::size_t block_size) {
    auto session = media::AudioPlaybackSession::open(path, output);
    std::vector<std::int16_t> pcm;
    std::int64_t next_sample = 0;
    while (auto chunk = session->decode_samples(block_size)) {
        CS_TEST_CHECK(chunk->first_sample_index == next_sample);
        CS_TEST_CHECK(chunk->sampleCount() > 0 && chunk->sampleCount() <= block_size);
        next_sample += static_cast<std::int64_t>(chunk->sampleCount());
        CS_TEST_CHECK(session->current_sample_index() == next_sample);
        pcm.insert(pcm.end(), chunk->samples.begin(), chunk->samples.end());
    }
    CS_TEST_CHECK(session->at_end());
    return pcm;
}

void testAudioBlockSizeAndResamplerDrain(const std::filesystem::path& path) {
    for (const auto output : {media::AudioPlaybackSession::OutputSpec{8000, 1},
                              media::AudioPlaybackSession::OutputSpec{16000, 2},
                              media::AudioPlaybackSession::OutputSpec{48000, 2}}) {
        const auto reference = decodeEntireAudio(path, output, 65536);
        CS_TEST_CHECK(reference.size() == static_cast<std::size_t>(output.sample_rate) *
                                            static_cast<std::size_t>(output.channel_count));
        for (const std::size_t block_size : {1U, 511U, 4096U}) {
            CS_TEST_CHECK(decodeEntireAudio(path, output, block_size) == reference);
        }
        auto session = media::AudioPlaybackSession::open(path, output);
        CS_TEST_CHECK(!session->decode_samples(0).has_value() && !session->at_end());
        CS_TEST_CHECK(!session->decode_samples(511, [] { return true; }).has_value());
        const auto first = session->decode_samples(511);
        CS_TEST_CHECK(first.has_value());
        const auto position = session->current_sample_index();
        CS_TEST_CHECK(!session->decode_samples(511, [] { return true; }).has_value() &&
                      session->current_sample_index() == position);
        const auto second = session->decode_samples(511);
        CS_TEST_CHECK(second.has_value());
        const auto channels = static_cast<std::size_t>(output.channel_count);
        const auto reference_begin = reference.begin() +
            static_cast<std::ptrdiff_t>(first->sampleCount() * channels);
        CS_TEST_CHECK(second->samples == std::vector<std::int16_t>(reference_begin,
            reference_begin + static_cast<std::ptrdiff_t>(second->sampleCount() * channels)));
        session->reset();
        const auto reset_first = session->decode_samples(511);
        CS_TEST_CHECK(reset_first.has_value() && reset_first->samples == first->samples);
    }
}

void testMissingAudioDoesNotBecomeAnError(const std::filesystem::path& path) {
    auto session = media::AudioPlaybackSession::open(path);
    CS_TEST_CHECK(!session->has_audio());
    const auto decoded = session->decode_samples();
    CS_TEST_CHECK(!decoded.has_value());
}

void testDisabledAudioOutputFallback() {
    qputenv("CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT", "1");
    playback::AudioOutput output;
    QString error;
    const bool initialized = output.initialize(&error, nullptr);
    CS_TEST_CHECK(!initialized);
    CS_TEST_CHECK(output.disabledByEnvironment());
    CS_TEST_CHECK(!output.bufferedUsecs().has_value());
    qunsetenv("CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT");
}

void testMonitorVolumeNormalization() {
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(0.0) == 0.0);
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(1.0) == 1.0);
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(2.0) == 2.0);
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(-0.1) == 1.0);
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(2.1) == 1.0);
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(std::numeric_limits<double>::quiet_NaN()) == 1.0);
    CS_TEST_CHECK(playback::AudioOutput::normalizeVolume(
               std::numeric_limits<double>::infinity()) == 1.0);

    CS_TEST_CHECK(playback::AudioOutput::outputVolume(0.0) == 0.0);
    CS_TEST_CHECK(playback::AudioOutput::outputVolume(1.5) == 1.0);
    CS_TEST_CHECK(playback::AudioOutput::sampleBoost(0.5) == 1.0);
    CS_TEST_CHECK(playback::AudioOutput::sampleBoost(1.5) == 1.5);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto wav_path = createWav();
    try {
        testAudioDecodeAndSeek(wav_path);
        testAudioBlockSizeAndResamplerDrain(wav_path);
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
