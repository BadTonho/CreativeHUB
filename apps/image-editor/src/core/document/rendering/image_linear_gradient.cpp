#include "rendering/image_linear_gradient.h"
#include "rendering/image_document_geometry.h"

#include <algorithm>
#include <cmath>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

QRgb sourceOver(QRgb destination, QRgb source) noexcept {
    const int source_alpha = qAlpha(source);
    const int inverse_alpha = 255 - source_alpha;
    return qRgba(
        qRed(source) + (qRed(destination) * inverse_alpha + 127) / 255,
        qGreen(source) + (qGreen(destination) * inverse_alpha + 127) / 255,
        qBlue(source) + (qBlue(destination) * inverse_alpha + 127) / 255,
        source_alpha + (qAlpha(destination) * inverse_alpha + 127) / 255);
}

int scaledChannel(int channel, qreal scale) noexcept {
    return std::clamp(qRound(channel * scale), 0, 255);
}

} // namespace

bool ImageLinearGradient::apply(QImage* image,
                                const ImageLinearGradientData& gradient,
                                bool* changed,
                                QString* error) {
    if (changed != nullptr) *changed = false;
    if (error != nullptr) error->clear();
    if (image == nullptr || image->isNull() || image->width() <= 0 ||
        image->height() <= 0 || !gradient.color.isValid() ||
        !std::isfinite(gradient.start.x()) || !std::isfinite(gradient.start.y()) ||
        !std::isfinite(gradient.end.x()) || !std::isfinite(gradient.end.y()) ||
        std::abs(gradient.start.x()) > 1'000'000.0 ||
        std::abs(gradient.start.y()) > 1'000'000.0 ||
        std::abs(gradient.end.x()) > 1'000'000.0 ||
        std::abs(gradient.end.y()) > 1'000'000.0) {
        assignError(error, QStringLiteral("The gradient image or geometry is invalid."));
        return false;
    }
    if (gradient.clipping_path.has_value() &&
        !ImageDocumentGeometry::isValidStrokeClipPath(*gradient.clipping_path)) {
        assignError(error, QStringLiteral("The gradient selection has invalid geometry."));
        return false;
    }

    const qreal dx = gradient.end.x() - gradient.start.x();
    const qreal dy = gradient.end.y() - gradient.start.y();
    const qreal length_squared = dx * dx + dy * dy;
    if (!(length_squared > 0.0) || !std::isfinite(length_squared) ||
        gradient.color.alpha() == 0) return true;

    const qint64 pixel_count = static_cast<qint64>(image->width()) * image->height();
    if (pixel_count <= 0 || pixel_count > ImageDocumentStore::kMaximumCanvasPixels) {
        assignError(error, QStringLiteral("The gradient image exceeds the supported canvas size."));
        return false;
    }
    *image = image->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image->isNull()) {
        assignError(error, QStringLiteral("There is not enough memory to render the gradient."));
        return false;
    }

    const QRgb start = qPremultiply(gradient.color.rgba());
    const QRect bounds = gradient.clipping_path.has_value()
        ? gradient.clipping_path->boundingRect().toAlignedRect().intersected(image->rect())
        : image->rect();
    bool any_changed = false;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image->scanLine(y));
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const QPointF pixel_center(x + 0.5, y + 0.5);
            if (gradient.clipping_path.has_value() &&
                !gradient.clipping_path->contains(pixel_center)) continue;
            const qreal projection = ((pixel_center.x() - gradient.start.x()) * dx +
                                      (pixel_center.y() - gradient.start.y()) * dy) /
                                     length_squared;
            const qreal remaining = 1.0 - std::clamp(projection, 0.0, 1.0);
            const QRgb source = qRgba(
                scaledChannel(qRed(start), remaining),
                scaledChannel(qGreen(start), remaining),
                scaledChannel(qBlue(start), remaining),
                scaledChannel(qAlpha(start), remaining));
            const QRgb updated = sourceOver(row[x], source);
            any_changed = any_changed || updated != row[x];
            row[x] = updated;
        }
    }
    if (changed != nullptr) *changed = any_changed;
    return true;
}

} // namespace image_editor
