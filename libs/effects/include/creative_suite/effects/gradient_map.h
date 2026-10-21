#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace creative_suite::effects {
struct GradientMapStop {
    double position = 0;
    std::array<unsigned char,3> color{};
    bool operator==(const GradientMapStop&) const = default;
};
struct GradientMapParameters {
    std::vector<GradientMapStop> stops{{0,{0,0,0}},{1,{255,255,255}}};
    bool operator==(const GradientMapParameters&) const = default;
};
// Positions are normalized, on the 0.1 percent grid. RGB is encoded, opaque.
[[nodiscard]] inline bool validGradientMap(const GradientMapParameters& p) noexcept {
    if(p.stops.size()<2 || p.stops.size()>32 || p.stops.front().position!=0 || p.stops.back().position!=1) return false;
    double previous=-1;
    for(const auto& s:p.stops) {
        if(!std::isfinite(s.position) || s.position<0 || s.position>1 ||
            std::abs(s.position*1000-std::round(s.position*1000))>1e-7 ||
            s.position-previous<.001-1e-9) return false;
        previous=s.position;
    }
    return true;
}
// Borrowed validated points. No allocation, color conversion or alpha change.
[[nodiscard]] inline std::array<unsigned char,3> gradientMapColor(const GradientMapParameters& p,double t) noexcept {
    t=std::clamp(t,0.,1.);
    auto right=std::upper_bound(p.stops.begin(),p.stops.end(),t,[](double v,const GradientMapStop& s){return v<s.position;});
    if(right==p.stops.begin())return right->color;
    if(right==p.stops.end())return p.stops.back().color;
    const auto& left=*(right-1);const double f=(t-left.position)/(right->position-left.position);
    std::array<unsigned char,3> result{};
    for(int c=0;c<3;++c)result[c]=static_cast<unsigned char>(std::lround(left.color[c]+f*(right->color[c]-left.color[c])));
    return result;
}
} // namespace creative_suite::effects
