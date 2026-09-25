#include "ui/workspace/pages/render/render_output_capabilities.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <set>
#include <string_view>

namespace ui {
namespace {

std::string firstName(std::string_view names) {
    const auto comma = names.find(',');
    return std::string(names.substr(0, comma));
}

bool formatHasName(const AVOutputFormat* format, std::string_view name) {
    if (format == nullptr || format->name == nullptr) return false;
    std::string_view names(format->name);
    while (!names.empty()) {
        const auto comma = names.find(',');
        const auto alias = names.substr(0, comma);
        if (alias == name) return true;
        if (comma == std::string_view::npos) break;
        names.remove_prefix(comma + 1);
    }
    return false;
}

const AVOutputFormat* findFormat(std::string_view name) {
    void* iterator = nullptr;
    const AVOutputFormat* format = nullptr;
    while ((format = av_muxer_iterate(&iterator)) != nullptr) {
        if (formatHasName(format, name)) return format;
    }
    return nullptr;
}

std::vector<RenderEncoderOption> compatibleEncoders(
    const AVOutputFormat* format,
    AVMediaType media_type) {
    std::vector<RenderEncoderOption> result;
    if (format == nullptr) return result;

    void* iterator = nullptr;
    const AVCodec* codec = nullptr;
    while ((codec = av_codec_iterate(&iterator)) != nullptr) {
        if (!av_codec_is_encoder(codec) || codec->type != media_type ||
            codec->id == AV_CODEC_ID_NONE || codec->name == nullptr ||
            avformat_query_codec(format, codec->id, FF_COMPLIANCE_NORMAL) <= 0) {
            continue;
        }

        result.push_back({
            codec->name,
            codec->long_name != nullptr ? codec->long_name : codec->name,
            static_cast<int>(codec->id)});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.display_name != right.display_name) {
            return left.display_name < right.display_name;
        }
        return left.name < right.name;
    });
    return result;
}

}  // namespace

std::vector<RenderContainerOption> RenderOutputCapabilities::availableContainers() {
    std::vector<RenderContainerOption> result;
    std::set<std::string> seen_names;

    void* iterator = nullptr;
    const AVOutputFormat* format = nullptr;
    while ((format = av_muxer_iterate(&iterator)) != nullptr) {
        if (format->name == nullptr) continue;
        auto name = firstName(format->name);
        if (name.empty() || seen_names.contains(name)) continue;

        auto video_encoders = compatibleEncoders(format, AVMEDIA_TYPE_VIDEO);
        if (video_encoders.empty()) continue;
        seen_names.insert(name);

        result.push_back({
            std::move(name),
            format->long_name != nullptr ? format->long_name : firstName(format->name),
            format->extensions != nullptr ? format->extensions : "",
            std::move(video_encoders),
            compatibleEncoders(format, AVMEDIA_TYPE_AUDIO)});
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.display_name != right.display_name) {
            return left.display_name < right.display_name;
        }
        return left.name < right.name;
    });
    return result;
}

bool RenderOutputCapabilities::supportsVideoEncoder(
    const RenderContainerOption& container,
    const std::string& encoder_name) {
    const auto* format = findFormat(container.name);
    const auto* encoder = avcodec_find_encoder_by_name(encoder_name.c_str());
    return format != nullptr && encoder != nullptr &&
        encoder->type == AVMEDIA_TYPE_VIDEO &&
        avformat_query_codec(format, encoder->id, FF_COMPLIANCE_NORMAL) > 0;
}

bool RenderOutputCapabilities::supportsAudioEncoder(
    const RenderContainerOption& container,
    const std::string& encoder_name) {
    const auto* format = findFormat(container.name);
    const auto* encoder = avcodec_find_encoder_by_name(encoder_name.c_str());
    return format != nullptr && encoder != nullptr &&
        encoder->type == AVMEDIA_TYPE_AUDIO &&
        avformat_query_codec(format, encoder->id, FF_COMPLIANCE_NORMAL) > 0;
}

}  // namespace ui
