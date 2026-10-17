#pragma once
#include "rendering/render_job.h"
#include <array>
#include <algorithm>
#include <cmath>
extern "C" {
#include <libavutil/rational.h>
}

namespace rendering {
// Qualified preset tuples only. Custom jobs with identical effective values match.
inline bool qualifiedVulkanNvenc(const RenderJobSettings& s) noexcept {
    if(s.graphics_backend!=creative_suite::composition::GraphicsBackend::Vulkan || !s.gpu_composition_enabled ||
       (s.video_encoder_name!="h264_nvenc" && s.video_encoder_name!="hevc_nvenc") ||
       !std::isfinite(s.frame_rate) || s.frame_rate<=0 || s.frame_rate>1000 ||
       !std::isfinite(s.video_bitrate_mbps) || s.video_bitrate_mbps<=0 || s.video_bitrate_mbps>500)return false;
    const auto rate=av_d2q(s.frame_rate,1001000);
    if(rate.den!=1 || (rate.num!=30 && rate.num!=60))return false;
    constexpr std::array<std::array<int,2>,5> sizes{{{1280,720},{1920,1080},{1080,1920},{2560,1440},{3840,2160}}};
    bool size=false;for(const auto& extent:sizes)size|=s.width==extent[0] && s.height==extent[1];if(!size)return false;
    for(const double baseline:{5.0,10.0,20.0}){
        const auto bitrate=std::clamp(std::round(baseline*(double(s.width)*s.height*rate.num)/(1920.0*1080*30)*10)/10,0.1,500.0);
        if(std::llround(s.video_bitrate_mbps*1000000)==std::llround(bitrate*1000000))return true;
    }
    return false;
}
}
