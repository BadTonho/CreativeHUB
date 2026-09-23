#include "video_decoder.h"

#include "../logging/logger.h"
#include "video_metadata.h"
#include "video_playback.h"

#include <filesystem>
#include <string>
#include <utility>

namespace media {
namespace {

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string safePathForLog(const std::filesystem::path& path) noexcept {
    try {
        return pathToUtf8(path);
    } catch (...) {
        return "<unavailable>";
    }
}

void logFailure(const std::filesystem::path& source_path,
                const media::MediaError& error) noexcept {
    try {
        logging::Context context{{"path", safePathForLog(source_path)}};
        if (error.error_code().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.error_code()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "media",
            "decode_first_frame",
            error.what(),
            context);
    } catch (...) {
        // Preserve the original media error even if diagnostic context allocation fails.
    }
}

} // namespace

VideoFrame VideoDecoder::decode_first_frame(const std::filesystem::path& source_path) const {
    try {
        auto session = VideoPlaybackSession::open(source_path);
        auto frame = session->decode_next_frame();
        if (!frame.has_value() || *frame == nullptr) {
            throw MediaError("The video ended before a frame could be decoded.");
        }
        return *(*frame);
    } catch (const MediaError& error) {
        logFailure(source_path, error);
        throw;
    }
}

} // namespace media
