#pragma once

#include <array>
#include <creative_suite/composition/shape_mask.h>
#include <creative_suite/animation/animation.h>
#include <creative_suite/effects/effects.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <variant>
class QJsonObject;
class QJsonArray;

namespace creative_suite::composition_exchange {
enum class Kind { Video, Image, Background, Text };
enum class Profile { FusionCanvas, MotionLayer };
// App-owned records embed these portable presentation semantics. Zero duration
// means the enclosing clip's duration; offsets are in composition frames.
struct Presentation {
    Profile profile = Profile::FusionCanvas;
    std::string name;
    bool visible = true;
    std::int64_t start = 0, duration = 0, offset = 0, source_start = 0;
    int text_width = 0, text_height = 0;
    friend bool operator==(const Presentation &, const Presentation &) = default;
};
struct Text {
    std::string content = "Text", font = "Sans Serif";
    double size = 48;
    int alignment = 1;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    friend bool operator==(const Text &, const Text &) = default;
};
using MaskId = std::uint64_t;
struct Mask {
    MaskId id = 0;
    composition::ShapeMaskKind kind = composition::ShapeMaskKind::Rectangle;
    composition::ShapeMaskParameters parameters;
    composition::ShapeMaskKeyframes keyframes;
    friend bool operator==(const Mask&, const Mask&) = default;
};
[[nodiscard]] QJsonArray masksJson(const std::vector<Mask>&);
[[nodiscard]] std::vector<Mask> parseMasksJson(const QJsonArray&);
[[nodiscard]] bool validMask(const Mask&) noexcept;
struct ColorEffect {
    effects::ColorAdjustmentParameters parameters;
    bool enabled = true;
    MaskId mask = 0;
};
struct BlurEffect {
    double radius_pixels = 10;
    bool enabled = true;
    MaskId mask = 0;
};
using Effect = std::variant<ColorEffect, BlurEffect>;
struct Layer {
    std::uint64_t id = 0;
    Kind kind = Kind::Background;
    Presentation presentation;
    std::filesystem::path source;
    double source_rate = 0;
    std::int64_t source_count = 0;
    std::array<std::uint8_t, 4> color{0, 0, 0, 0};
    Text text;
    animation::Transform2D transform;
    animation::TransformKeyframes curves;
    std::vector<Effect> effects;
    MaskId source_mask = 0, transform_mask = 0;
};
struct Document {
    std::string name = "Composition";
    int width = 1920, height = 1080;
    std::int64_t fps_numerator = 30, fps_denominator = 1, duration = 150;
    std::vector<Layer> layers;
    std::vector<Mask> masks;
};
struct Issue {
    std::uint64_t id = 0;
    std::string reason;
};
class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class Unsupported : public Error {
  public:
    using Error::Error;
};
using Cancel = std::function<bool()>;
[[nodiscard]] std::vector<Issue> validate(const Document &, bool require_media = false);
[[nodiscard]] bool validPresentation(const Presentation &) noexcept;
[[nodiscard]] QJsonObject presentationJson(const Presentation &);
[[nodiscard]] Presentation parsePresentation(const QJsonObject &);
[[nodiscard]] QJsonObject toJson(const Document &, const std::filesystem::path &file);
[[nodiscard]] Document fromJson(const QJsonObject &, const std::filesystem::path &file);
[[nodiscard]] std::optional<Document> load(const std::filesystem::path &, Cancel = {});
// Atomic publication. False is cancellation; technical failures throw Error.
[[nodiscard]] bool save(const std::filesystem::path &, const Document &, Cancel = {});
[[nodiscard]] std::optional<std::int64_t> sourceFrame(const Presentation &, std::int64_t frame,
                                                      double source_rate, std::int64_t source_count,
                                                      double fps);
} // namespace creative_suite::composition_exchange
