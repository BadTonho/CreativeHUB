#include "text_renderer.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>

#include <algorithm>
#include <cstring>

namespace rendering {

std::optional<media::VideoFrame> renderText(
    const timeline::TextStyle& text_style,
    int canvas_width,
    int canvas_height) {
    if (!timeline::TimelineModel::validTextStyle(text_style) ||
        canvas_width <= 0 || canvas_height <= 0) {
        return std::nullopt;
    }

    try {
        const auto content = QString::fromUtf8(
            text_style.content.data(), static_cast<qsizetype>(text_style.content.size()));
        QFont font(QString::fromUtf8(
            text_style.font_family.data(),
            static_cast<qsizetype>(text_style.font_family.size())));
        font.setPixelSize(static_cast<int>(text_style.font_size_pixels));
        const QFontMetrics metrics(font);
        const auto flags = Qt::TextWordWrap |
            (text_style.alignment == timeline::TextAlignment::Left
                 ? Qt::AlignLeft
                 : text_style.alignment == timeline::TextAlignment::Right
                     ? Qt::AlignRight
                     : Qt::AlignHCenter) |
            Qt::AlignVCenter;
        const QRect available(
            0, 0, std::max(1, canvas_width - 120), canvas_height);
        const QRect measured = metrics.boundingRect(
            available, flags, content.isEmpty() ? QStringLiteral(" ") : content);
        const int width = std::clamp(measured.width() + 24, 1, canvas_width);
        const int height = std::clamp(measured.height() + 24, 1, canvas_height);

        QImage image(width, height, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font);
        painter.setPen(QColor(
            text_style.color[0], text_style.color[1],
            text_style.color[2], text_style.color[3]));
        painter.drawText(
            QRect(12, 12, width - 24, height - 24),
            flags,
            content.isEmpty() ? QStringLiteral(" ") : content);
        painter.end();

        media::VideoFrame frame;
        frame.width = image.width();
        frame.height = image.height();
        frame.stride = image.bytesPerLine();
        frame.rgba_pixels.resize(
            static_cast<std::size_t>(frame.stride) * frame.height);
        for (int row = 0; row < frame.height; ++row) {
            std::memcpy(
                frame.rgba_pixels.data() +
                    static_cast<std::size_t>(row) * frame.stride,
                image.constScanLine(row),
                static_cast<std::size_t>(frame.stride));
        }
        return frame;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace rendering
