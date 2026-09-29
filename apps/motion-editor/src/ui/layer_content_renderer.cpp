#include "layer_content_renderer.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QString>

#include <cstring>
#include <algorithm>
#include <variant>

namespace motion::ui {
namespace {

constexpr std::uint64_t kMaximumRasterBytes = 64ULL * 1024ULL * 1024ULL;

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QColor toColor(const model::ColorRgba& color)
{
    return QColor(color[0], color[1], color[2], color[3]);
}

std::optional<QImage> createImage(int width, int height)
{
    if (width <= 0 || height <= 0) return std::nullopt;
    if (static_cast<std::uint64_t>(width) >
        kMaximumRasterBytes / 4ULL / static_cast<std::uint64_t>(height)) {
        return std::nullopt;
    }
    QImage image(width, height, QImage::Format_RGBA8888);
    if (image.isNull()) return std::nullopt;
    image.fill(Qt::transparent);
    return image;
}

std::optional<creative_suite::media::RgbaFrame> copyToFrame(const QImage& image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        image.bytesPerLine() <= 0 ||
        static_cast<std::uint64_t>(image.bytesPerLine()) *
                static_cast<std::uint64_t>(image.height()) > kMaximumRasterBytes) {
        return std::nullopt;
    }
    creative_suite::media::RgbaFrame frame;
    frame.width = image.width();
    frame.height = image.height();
    frame.stride = image.bytesPerLine();
    frame.rgba_pixels.resize(static_cast<std::size_t>(frame.stride) *
                              static_cast<std::size_t>(frame.height));
    for (int row = 0; row < frame.height; ++row) {
        std::memcpy(frame.rgba_pixels.data() +
                        static_cast<std::size_t>(row) *
                            static_cast<std::size_t>(frame.stride),
                    image.constScanLine(row),
                    static_cast<std::size_t>(frame.stride));
    }
    return frame;
}

std::optional<QImage> rasterizeText(const model::TextLayerContent& content)
{
    auto image = createImage(content.box_width, content.box_height);
    if (!image.has_value()) return std::nullopt;

    const QString text = fromUtf8(content.text);
    QFont font(fromUtf8(content.font_family));
    font.setPixelSize(content.font_size_pixels);

    int flags = Qt::TextWordWrap | Qt::AlignVCenter;
    switch (content.alignment) {
    case model::TextAlignment::Left: flags |= Qt::AlignLeft; break;
    case model::TextAlignment::Center: flags |= Qt::AlignHCenter; break;
    case model::TextAlignment::Right: flags |= Qt::AlignRight; break;
    }

    QPainter painter(&*image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(toColor(content.color));
    painter.drawText(image->rect(), flags, text);
    painter.end();
    return image;
}

std::optional<QImage> rasterizeShape(const model::ShapeLayerContent& content)
{
    if (content.stroke_width_pixels < 0 || content.stroke_width_pixels > 4096 ||
        content.stroke_width_pixels > std::min(content.width, content.height)) {
        return std::nullopt;
    }
    auto image = createImage(content.width, content.height);
    if (!image.has_value()) return std::nullopt;

    QPainter painter(&*image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(toColor(content.fill_color));
    if (content.stroke_width_pixels > 0) {
        painter.setPen(QPen(toColor(content.stroke_color),
                            static_cast<qreal>(content.stroke_width_pixels)));
    } else {
        painter.setPen(Qt::NoPen);
    }
    const qreal inset = content.stroke_width_pixels > 0
        ? static_cast<qreal>(content.stroke_width_pixels) / 2.0 : 0.0;
    const QRectF bounds(inset, inset,
                        static_cast<qreal>(content.width) - inset * 2.0,
                        static_cast<qreal>(content.height) - inset * 2.0);
    if (content.shape == model::ShapeKind::Rectangle) {
        painter.drawRect(bounds);
    } else if (content.shape == model::ShapeKind::Ellipse) {
        painter.drawEllipse(bounds);
    } else {
        return std::nullopt;
    }
    painter.end();
    return image;
}

} // namespace

std::optional<creative_suite::media::RgbaFrame> rasterizeLayerContent(
    const model::LayerContent& content)
{
    try {
        if (const auto* text = std::get_if<model::TextLayerContent>(&content)) {
            const auto image = rasterizeText(*text);
            return image.has_value() ? copyToFrame(*image) : std::nullopt;
        }
        if (const auto* shape = std::get_if<model::ShapeLayerContent>(&content)) {
            const auto image = rasterizeShape(*shape);
            return image.has_value() ? copyToFrame(*image) : std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace motion::ui
