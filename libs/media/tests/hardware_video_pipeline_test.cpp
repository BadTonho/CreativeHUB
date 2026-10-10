#include <creative_suite/media/video_encoder.h>
#include <creative_suite/media/video_playback.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace creative_suite::media;
namespace {
void require(bool value, const char* cause) {
    if (!value) throw std::runtime_error(cause);
}
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("creative-suite-hardware-video-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
void compare(const VideoFramePtr& a, const VideoFramePtr& b) {
    require(a && b && a->width == b->width && a->height == b->height && a->stride == b->stride,
        "Hardware decoding changed geometry.");
    require(a->rgba_pixels.size() == b->rgba_pixels.size(), "Hardware frame storage changed.");
    int maximum = 0;
    for (std::size_t i = 0; i < a->rgba_pixels.size(); ++i) {
        const int difference = std::abs(int(a->rgba_pixels[i]) - int(b->rgba_pixels[i]));
        require(i % 4 != 3 || difference == 0, "Hardware decoding changed alpha.");
        maximum = std::max(maximum, difference);
    }
    if (maximum > 2) throw std::runtime_error("Hardware/software conversion maximum RGB difference: " + std::to_string(maximum));
}
void verifyCodec(const std::filesystem::path& path, const char* name) {
    VideoEncodingSettings settings;
    settings.output_path = path;
    settings.container_name = "matroska";
    settings.video_encoder_name = name;
    settings.width = 320; settings.height = 180;
    settings.frame_rate_numerator = 30000; settings.frame_rate_denominator = 1001;
    settings.video_bitrate_mbps = 3;
    RgbaFrame source{320, 180, 1280, std::vector<std::uint8_t>(1280 * 180)};
    {
        VideoEncoder encoder(settings);
        for (int frame = 0; frame < 24; ++frame) {
            for (int y = 0; y < source.height; ++y) for (int x = 0; x < source.width; ++x) {
                const auto i = y * source.stride + x * 4;
                source.rgba_pixels[i] = static_cast<std::uint8_t>(x + frame * 7);
                source.rgba_pixels[i + 1] = static_cast<std::uint8_t>(y + frame * 11);
                source.rgba_pixels[i + 2] = static_cast<std::uint8_t>(x / 2 + y / 2 + frame * 3);
                source.rgba_pixels[i + 3] = 255;
            }
            encoder.writeVideo(source, frame);
        }
        encoder.finish();
    }
    auto cpu = VideoPlaybackSession::open(path);
    auto gpu = VideoPlaybackSession::open(path, DecodeOptions{DecodeAcceleration::PreferHardware});
    for (const auto index : {0, 17, 4, 5, 23, 0}) {
        const auto a = cpu->decode_frame_at(index), b = gpu->decode_frame_at(index);
        require(a && b, "Native decoder lost a requested frame during seeking.");
        compare(*a, *b);
        require(gpu->current_frame_index() == index, "Native decoder returned the wrong index.");
    }
    auto diagnostic = gpu->acceleration_diagnostics();
    require(diagnostic.backend == DecodeBackend::D3D11 && diagnostic.hardware_frames > 0 &&
        diagnostic.software_frames == 0 && diagnostic.downloaded_frames > 0 &&
        diagnostic.downloaded_bytes > 0 && diagnostic.recovery_count == 0,
        "Native test did not exercise D3D11 decoding and the RGBA download path.");
    const auto downloads = diagnostic.downloaded_frames;
    require(gpu->decode_frame_at(0).has_value() &&
        gpu->acceleration_diagnostics().downloaded_frames == downloads,
        "A native decode cache hit performed another GPU transfer.");
    require(!gpu->decode_frame_at(14, [] { return true; }) && gpu->current_frame_index() == 0,
        "Cancelled hardware seeking changed the visible frame.");
    gpu->reset();
    require(gpu->decode_frame_at(0).has_value(), "Native decoder did not recreate resources on reset.");
    require(gpu->decode_forward_to(13).has_value() && gpu->current_frame_index() == 13,
        "Native forward decode lost intermediate-frame scheduling.");

    DecodeOptions injected{DecodeAcceleration::PreferHardware};
    injected.hardware_frame_guard = [](std::int64_t index) {
        if (index == 3) throw MediaError("Injected lost D3D11 transfer resource.", -1);
    };
    auto failing = VideoPlaybackSession::open(path, injected);
    require(failing->decode_frame_at(0).has_value(), "Injected session could not start on hardware.");
    const auto recovered = failing->decode_frame_at(3);
    require(recovered.has_value(), "Hardware failure did not recover the requested frame.");
    compare(*cpu->decode_frame_at(3), *recovered);
    const auto fallback = failing->acceleration_diagnostics();
    require(fallback.backend == DecodeBackend::Software && fallback.recovery_count == 1 &&
        fallback.hardware_frames > 0 && fallback.software_frames > 0 && !fallback.fallback_reason.empty(),
        "Hardware recovery was not diagnosed accurately.");
    failing->reset();
    require(failing->decode_frame_at(3).has_value() &&
        failing->acceleration_diagnostics().recovery_count == 1,
        "Recovered session retried the failing hardware on reset.");
    gpu->set_decode_options(DecodeOptions{});
    require(gpu->decode_frame_at(3).has_value() &&
        gpu->acceleration_diagnostics().backend == DecodeBackend::Software,
        "Changing the decode preference did not reset video resources.");
    auto native_session = VideoPlaybackSession::open(path, DecodeOptions{DecodeAcceleration::PreferHardware});
    auto native = native_session->decode_frame_at_native(17);
    require(native && native->native && !native->rgba &&
        native_session->acceleration_diagnostics().downloaded_frames == 0,
        "Native frame delivery downloaded pixels to CPU.");
    const auto view = native->native->d3d11_view();
    require(view.device && view.texture && view.width == 320 && view.height == 180,
        "Native frame delivery lost the D3D11 lease.");
    const auto cached_native = native_session->decode_frame_at_native(17);
    require(cached_native && cached_native->native == native->native &&
        native_session->acceleration_diagnostics().downloaded_frames == 0,
        "Native cache did not retain the immutable surface without a transfer.");
    compare(*cpu->decode_frame_at(17), native->native->download_rgba());
    require(native_session->acceleration_diagnostics().downloaded_frames == 1,
        "Explicit native-frame recovery transfer was not counted.");
    native_session->reset();
    require(native_session->decode_frame_at_native(0).has_value() &&
        native_session->decode_forward_to_native(13).has_value(),
        "Native forward decode did not preserve the shared decoder scheduling.");
    native_session.reset();
    compare(*cpu->decode_frame_at(17), native->native->download_rgba());
    std::cout << name << ": NVENC encode, D3D11 decode, seeks, cache, cancellation, recovery passed.\n";
}
}
int main(int argc, char** argv) {
    const bool required = argc > 1 && std::string(argv[1]) == "--require-hardware";
    try {
        const auto containers = availableVideoContainers();
        const auto container = std::find_if(containers.begin(), containers.end(), [](const auto& item) {
            return item.name == "matroska";
        });
        const bool available = container != containers.end() && supportsVideoEncoder(*container, "h264_nvenc") &&
            supportsVideoEncoder(*container, "hevc_nvenc");
        if (!available) {
            if (required) throw std::runtime_error("NVENC is required but absent from the FFmpeg build.");
            return 77;
        }
        TemporaryDirectory temporary;
        verifyCodec(temporary.path / "h264.mkv", "h264_nvenc");
        verifyCodec(temporary.path / "hevc.mkv", "hevc_nvenc");
        return 0;
    } catch (const VideoEncodingError& error) {
        std::cerr << error.what() << '\n';
        // Optional developer runs can lack the NVIDIA runtime. Native acceptance
        // always uses --require-hardware and treats this as a failure.
        return required ? 1 : 77;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
