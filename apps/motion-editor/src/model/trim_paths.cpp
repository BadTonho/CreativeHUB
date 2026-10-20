#include "trim_paths.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace motion::model {
namespace a = creative_suite::animation;
bool validTrimProperty(TrimPathProperty p) noexcept {
    return static_cast<std::size_t>(p) < 3;
}
bool validTrimValue(TrimPathProperty p, double value) noexcept {
    return validTrimProperty(p) && std::isfinite(value) &&
           (p == TrimPathProperty::Offset ? std::abs(value) <= 1e9 : value >= 0 && value <= 100);
}
double trimValue(const TrimPathValues &v, TrimPathProperty p) noexcept {
    switch (p) {
    case TrimPathProperty::Start:
        return v.start_percent;
    case TrimPathProperty::End:
        return v.end_percent;
    case TrimPathProperty::Offset:
        return v.offset_degrees;
    }
    return 0;
}
void setTrimValue(TrimPathValues &v, TrimPathProperty p, double n) noexcept {
    switch (p) {
    case TrimPathProperty::Start:
        v.start_percent = n;
        break;
    case TrimPathProperty::End:
        v.end_percent = n;
        break;
    case TrimPathProperty::Offset:
        v.offset_degrees = n;
        break;
    }
}
std::size_t trimKeyframeCost(const TrimPathAnimation &v) noexcept {
    return v.keys[0].size() + v.keys[1].size() + v.keys[2].size();
}
bool validTrimAnimation(const TrimPathAnimation &v) noexcept {
    if (trimKeyframeCost(v) > 2'000'000)
        return false;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto p = static_cast<TrimPathProperty>(i);
        if (!validTrimValue(p, trimValue(v.base, p)))
            return false;
        std::int64_t previous = -1;
        for (const auto &key : v.keys[i]) {
            if (key.frame <= previous || key.frame >= (std::int64_t(1) << 50) ||
                !validTrimValue(p, key.value) ||
                !a::validKeyframeInterpolation(key.interpolation, key.easing))
                return false;
            previous = key.frame;
        }
    }
    return true;
}
struct TrimPathStorage::Data {
    TrimPathAnimation animation;
    bool valid;
    explicit Data(TrimPathAnimation v) : animation(std::move(v)), valid(validTrimAnimation(animation)) {
    }
};
TrimPathStorage::TrimPathStorage(TrimPathAnimation v) {
    if (v != TrimPathAnimation{})
        data_ = std::make_shared<const Data>(std::move(v));
}
const TrimPathAnimation &TrimPathStorage::get() const noexcept {
    static const TrimPathAnimation defaults;
    return data_ ? data_->animation : defaults;
}
bool TrimPathStorage::valid() const noexcept {
    return !data_ || data_->valid;
}
bool TrimPathStorage::isDefault() const noexcept {
    return !data_;
}
bool operator==(const TrimPathStorage &l, const TrimPathStorage &r) {
    return l.data_ == r.data_ || l.get() == r.get();
}
TrimPathValues evaluateTrimPaths(const TrimPathStorage &s, std::int64_t frame) {
    const auto &v = s.get();
    auto result = v.base;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto p = static_cast<TrimPathProperty>(i);
        setTrimValue(result, p, a::evaluateScalar(trimValue(v.base, p), v.keys[i], frame));
    }
    return result;
}
TrimPathRange trimPathRange(const TrimPathValues &v) {
    if (!validTrimValue(TrimPathProperty::Start, v.start_percent) ||
        !validTrimValue(TrimPathProperty::End, v.end_percent) ||
        !validTrimValue(TrimPathProperty::Offset, v.offset_degrees))
        throw std::invalid_argument("Invalid Trim Paths values");
    const auto difference = v.end_percent - v.start_percent;
    if (!v.enabled || std::abs(difference) == 100)
        return {};
    if (difference == 0)
        return {0, 0};
    const auto mod = [](double n, double d) {
        auto r = std::fmod(n, d);
        return r < 0 ? r + d : r;
    };
    auto start = mod(v.start_percent / 100 + std::fmod(v.offset_degrees, 360.) / 360., 1);
    // A tiny negative remainder can round to exactly one when wrapped. It is
    // the seam, not an invalid start outside the canonical half-open range.
    if (start == 1)
        start = 0;
    return {start, mod(difference, 100.) / 100};
}
} // namespace motion::model
