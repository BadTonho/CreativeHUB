#include <creative_suite/media/video_encoder.h>
#include <creative_suite/media/video_playback.h>

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

int main()
{
    try {
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
