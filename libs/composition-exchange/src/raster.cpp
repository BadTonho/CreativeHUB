#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <creative_suite/composition_exchange/raster.h>
#include <cstring>
namespace creative_suite::composition_exchange {
namespace {
void validateFrame(const media::RgbaFrame &frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.width > 32768 || frame.height > 32768 ||
        frame.stride < frame.width * 4 ||
        std::uint64_t(frame.stride) * frame.height > frame.rgba_pixels.size())
        throw Error("Invalid source RGBA geometry.");
}
} // namespace
media::RgbaFrame transparent(int w, int h) {
    if (w <= 0 || h <= 0 || std::uint64_t(w) * h > 16777216)
        throw Error("Composition raster exceeds 64 MiB.");
    return {w, h, w * 4, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
}
media::RgbaFrame fit(const media::RgbaFrame &source, int w, int h, bool intrinsic, Cancel cancel) {
    validateFrame(source);
    auto result = transparent(w, h);
    const double scale =
        intrinsic ? 1. : std::min(double(w) / source.width, double(h) / source.height);
    const int fw = std::max(1, int(std::lround(source.width * scale))),
              fh = std::max(1, int(std::lround(source.height * scale)));
    const int left = (w - fw) / 2, top = (h - fh) / 2;
    for (int y = std::max(0, top); y < std::min(h, top + fh); ++y) {
        if (cancel && cancel())
            return result;
        for (int x = std::max(0, left); x < std::min(w, left + fw); ++x) {
            int sx = std::clamp(int((x - left) / scale), 0, source.width - 1),
                sy = std::clamp(int((y - top) / scale), 0, source.height - 1);
            std::copy_n(source.rgba_pixels.data() + std::size_t(sy) * source.stride + sx * 4, 4,
                        result.rgba_pixels.data() + std::size_t(y) * result.stride + x * 4);
        }
    }
    return result;
}
media::RgbaFrame textFrame(const Text &t, const Presentation &p, int w, int h) {
    QFont font(QString::fromUtf8(t.font));
    font.setPixelSize(int(t.size));
    int flags = Qt::TextWordWrap | Qt::AlignVCenter |
                (t.alignment == 0   ? Qt::AlignLeft
                 : t.alignment == 2 ? Qt::AlignRight
                                    : Qt::AlignHCenter);
    auto content = QString::fromUtf8(t.content);
    if (content.isEmpty())
        content = " ";
    int margin = 0;
    if (p.profile == Profile::FusionCanvas) {
        QFontMetrics metrics(font);
        auto measured = metrics.boundingRect(QRect(0, 0, std::max(1, w - 120), h), flags, content);
        w = std::clamp(measured.width() + 24, 1, w);
        h = std::clamp(measured.height() + 24, 1, h);
        margin = 12;
    } else {
        w = p.text_width;
        h = p.text_height;
    }
    auto result = transparent(w, h);
    QImage image(result.rgba_pixels.data(), w, h, result.stride, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(QColor(t.color[0], t.color[1], t.color[2], t.color[3]));
    painter.drawText(QRect(margin, margin, w - margin * 2, h - margin * 2), flags, content);
    painter.end();
    return result;
}
media::RgbaFrame transformFrame(const media::RgbaFrame &source, int w, int h,
                                const animation::Transform2D &t, Profile profile, bool intrinsic,
                                Cancel cancel) {
    validateFrame(source);
    if (!animation::validTransform(t))
        throw Error("Invalid raster transform.");
    auto result = transparent(w, h);
    double scale = t.scale;
    if (profile == Profile::MotionLayer && !intrinsic)
        scale *= std::min(double(w) / source.width, double(h) / source.height);
    const double c = std::cos(-t.rotation_degrees * 3.14159265358979323846 / 180.),
                 s = std::sin(-t.rotation_degrees * 3.14159265358979323846 / 180.);
    for (int y = 0; y < h; ++y) {
        if (cancel && cancel())
            return result;
        for (int x = 0; x < w; ++x) {
            double dx = x + .5 - t.position_x * w, dy = y + .5 - t.position_y * h;
            double fx = (c * dx - s * dy) / scale + source.width * .5,
                   fy = (s * dx + c * dy) / scale + source.height * .5;
            if (fx < 0 || fy < 0 || fx >= source.width || fy >= source.height)
                continue;
            int sx = int(std::floor(fx)), sy = int(std::floor(fy));
            auto *out = result.rgba_pixels.data() + std::size_t(y) * result.stride + x * 4;
            auto *in = source.rgba_pixels.data() + std::size_t(sy) * source.stride + sx * 4;
            std::copy_n(in, 4, out);
            out[3] = std::uint8_t(std::lround(in[3] * t.opacity));
        }
    }
    return result;
}
} // namespace creative_suite::composition_exchange
