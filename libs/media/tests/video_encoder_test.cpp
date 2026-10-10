#include <creative_suite/media/video_encoder.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/media/video_probe.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("creative-suite-video-encoder-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool softwareEncoder(const std::string& name)
{
    constexpr const char* hardware_names[]{
        "_mf", "nvenc", "_qsv", "_amf", "vaapi", "videotoolbox", "_v4l2m2m"};
    return std::none_of(std::begin(hardware_names), std::end(hardware_names),
        [&name](const char* marker) { return name.find(marker) != std::string::npos; });
}

std::pair<creative_suite::media::VideoContainerOption,
          creative_suite::media::VideoEncoderOption> chooseOutput()
{
    auto containers = creative_suite::media::availableVideoContainers();
    std::stable_sort(containers.begin(), containers.end(), [](const auto& left, const auto& right) {
        return left.name == "matroska" && right.name != "matroska";
    });
    for (const auto& container : containers) {
        for (const auto& encoder : container.video_encoders) {
            if (softwareEncoder(encoder.name) &&
                creative_suite::media::supportsVideoEncoder(container, encoder.name)) {
                return {container, encoder};
            }
        }
    }
    throw std::runtime_error("FFmpeg exposes no compatible software video encoder.");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc > 2 && std::string(argv[1]) == "--hardware-encoder") {
            TemporaryDirectory temporary;
            creative_suite::media::VideoEncodingSettings settings;
            settings.output_path = temporary.path() / "native-encoder.mkv";
            settings.container_name = "matroska"; settings.video_encoder_name = argv[2];
            settings.width = 320; settings.height = 180; settings.frame_rate_numerator = 30;
            if (argc > 3) settings.hardware_device_name = argv[3];
            settings.audio = creative_suite::media::AudioEncodingSettings{"aac"};
            creative_suite::media::RgbaFrame frame{320, 180, 1280, std::vector<std::uint8_t>(1280 * 180, 255)};
            std::vector<float> silent_audio(3200, 0);
            const auto path = settings.output_path;
            { creative_suite::media::VideoEncoder writer(settings);
              for (int i = 0; i < 12; ++i) {
                  for (std::size_t pixel = 0; pixel < frame.rgba_pixels.size(); pixel += 4) {
                      frame.rgba_pixels[pixel] = i % 2 ? 20 : 220;
                      frame.rgba_pixels[pixel + 1] = i % 2 ? 210 : 30;
                      frame.rgba_pixels[pixel + 2] = 25;
                  }
                  writer.writeVideo(frame, i);
              }
              std::int64_t samples = 0;
              while (samples < 19200) {
                  const auto count = writer.nextAudioInputSampleCount();
                  silent_audio.resize(std::size_t(count) * 2);
                  writer.writeAudio(silent_audio, count); samples += count;
              }
              writer.finish(); }
            auto decoder = creative_suite::media::VideoPlaybackSession::open(path);
            for (int i = 0; i < 12; ++i) {
                const auto decoded = decoder->decode_next_frame();
                require(decoded && *decoded && (*decoded)->width == 320 && (*decoded)->height == 180,
                    "Hardware output lost a frame or changed its geometry.");
                const auto center = 90 * (*decoded)->stride + 160 * 4;
                require(((*decoded)->rgba_pixels[center + 1] > (*decoded)->rgba_pixels[center]) == bool(i % 2),
                    "Hardware output changed frame order or encoded color.");
            }
            require(!decoder->decode_next_frame(), "Hardware output added an unexpected video frame.");
            const auto metadata = creative_suite::media::VideoProbe{}.probe(path);
            require(metadata.frame_rate && std::abs(*metadata.frame_rate - 30) < .001 &&
                metadata.audio && metadata.audio->codec == "aac" && metadata.audio->sample_rate == 48000 &&
                metadata.duration_seconds && std::abs(*metadata.duration_seconds - .4) < .08,
                "Hardware export did not preserve frame rate, duration, and configured audio.");
            std::cout << "Hardware encoder " << settings.video_encoder_name << " passed 12 ordered RGBA frames with AAC.\n";
            return EXIT_SUCCESS;
        }
        const creative_suite::media::VideoEncodingError encoding_error(
            "controlled encoder failure", -734);
        require(std::string(encoding_error.what()) == "controlled encoder failure" &&
                    encoding_error.errorCode() == -734,
                "video encoding errors preserve their message and FFmpeg error code");

        const auto containers = creative_suite::media::availableVideoContainers();
        require(!containers.empty(), "FFmpeg discovers at least one video container");
        for (const auto& container : containers) {
            require(!container.video_encoders.empty(), "each discovered container has a video encoder");
            for (const auto& encoder : container.video_encoders) {
                if (encoder.name.ends_with("_nvenc") || encoder.name.ends_with("_amf") ||
                    encoder.name.ends_with("_qsv") || encoder.name.ends_with("_vaapi") ||
                    encoder.name.ends_with("_videotoolbox"))
                    require(encoder.hardware && encoder.experimental &&
                        encoder.display_name.find("Hardware, Experimental") != std::string::npos,
                        "hardware encoding capabilities retain their experimental label");
                require(creative_suite::media::supportsVideoEncoder(container, encoder.name),
                        "discovered video encoders pass the shared compatibility check");
            }
            for (const auto& encoder : container.audio_encoders) {
                require(creative_suite::media::supportsAudioEncoder(container, encoder.name),
                        "discovered audio encoders pass the shared compatibility check");
            }
        }
        require(!creative_suite::media::supportsVideoEncoder(
                    containers.front(), "motion-test-encoder-does-not-exist") &&
                    !creative_suite::media::supportsAudioEncoder(
                        containers.front(), "motion-test-encoder-does-not-exist"),
                "capability checks reject unavailable encoders");

        TemporaryDirectory temporary_directory;
        const auto [container, codec] = chooseOutput();
        auto extension = container.extensions.substr(0, container.extensions.find(','));
        if (extension.empty()) extension = "video";
        const auto output = temporary_directory.path() / ("shared-encoder." + extension);

        creative_suite::media::VideoEncodingSettings settings;
        settings.output_path = output;
        settings.container_name = container.name;
        settings.video_encoder_name = codec.name;
        settings.width = 32;
        settings.height = 24;
        settings.frame_rate_numerator = 24;
        settings.frame_rate_denominator = 1;
        settings.video_bitrate_mbps = 1.0;
        creative_suite::media::RgbaFrame source;
        source.width = 32;
        source.height = 24;
        source.stride = 32 * 4;
        source.rgba_pixels.resize(static_cast<std::size_t>(source.stride) * source.height);
        {
            creative_suite::media::VideoEncoder encoder(std::move(settings));
            for (int frame_index = 0; frame_index < 3; ++frame_index) {
                for (int y = 0; y < source.height; ++y) {
                    for (int x = 0; x < source.width; ++x) {
                        const auto offset = static_cast<std::size_t>(y) * source.stride +
                            static_cast<std::size_t>(x) * 4U;
                        source.rgba_pixels[offset] = frame_index == 1 ? 20 : 220;
                        source.rgba_pixels[offset + 1] = frame_index == 1 ? 210 : 30;
                        source.rgba_pixels[offset + 2] = 25;
                        source.rgba_pixels[offset + 3] = 255;
                    }
                }
                encoder.writeVideo(source, frame_index);
            }
            encoder.finish();
            require(encoder.uploadedVideoBytes() == 0, "Software encoding must not report a hardware upload.");
        }

        require(std::filesystem::is_regular_file(output), "the shared encoder writes a file");
        auto decoder = creative_suite::media::VideoPlaybackSession::open(output);
        int decoded_count = 0;
        bool saw_green = false;
        while (const auto frame = decoder->decode_next_frame()) {
            require(*frame != nullptr, "decoded RGBA frames are owned");
            require((*frame)->width == 32 && (*frame)->height == 24,
                    "the round trip retains output dimensions");
            const auto center = static_cast<std::size_t>(12) * (*frame)->stride + 16U * 4U;
            if ((*frame)->rgba_pixels[center + 1] > (*frame)->rgba_pixels[center]) {
                saw_green = true;
            }
            ++decoded_count;
        }
        require(decoded_count == 3, "the shared encoder round-trips every video frame");
        require(saw_green, "the decoded output contains the changed source frame");

        std::cout << "Shared video encoder tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Shared video encoder test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
