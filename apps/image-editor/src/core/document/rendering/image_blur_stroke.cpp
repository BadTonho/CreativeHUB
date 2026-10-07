#include "rendering/image_blur_stroke.h"

#include <QPainter>
#include <QPainterPathStroker>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

QPainterPath strokeCoveragePath(const ImageBlurStrokeData& stroke) {
    QPainterPath centerline;
    centerline.moveTo(stroke.points.front());
    for (qsizetype index = 1; index < stroke.points.size(); ++index)
        centerline.lineTo(stroke.points.at(index));

    if (stroke.points.size() == 1) {
        const qreal half = stroke.diameter / 2.0;
        QPainterPath dot;
        dot.addEllipse(QRectF(stroke.points.front().x() - half,
                              stroke.points.front().y() - half,
                              stroke.diameter, stroke.diameter));
        return dot;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(stroke.diameter);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(centerline);
}

void horizontalBoxBlur(const QImage& source, QImage* destination, int radius) {
    const int width = source.width();
    const int height = source.height();
    const std::uint64_t window = static_cast<std::uint64_t>(radius) * 2U + 1U;
    const std::uint64_t rounding = window / 2U;

    for (int y = 0; y < height; ++y) {
        const auto* source_row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        auto* destination_row = reinterpret_cast<QRgb*>(destination->scanLine(y));
        std::array<std::uint64_t, 4> sums{};
        for (int offset = -radius; offset <= radius; ++offset) {
            const QRgb pixel = source_row[std::clamp(offset, 0, width - 1)];
            sums[0] += static_cast<std::uint64_t>(qRed(pixel));
            sums[1] += static_cast<std::uint64_t>(qGreen(pixel));
            sums[2] += static_cast<std::uint64_t>(qBlue(pixel));
            sums[3] += static_cast<std::uint64_t>(qAlpha(pixel));
        }

        for (int x = 0; x < width; ++x) {
            destination_row[x] = qRgba(
                static_cast<int>((sums[0] + rounding) / window),
                static_cast<int>((sums[1] + rounding) / window),
                static_cast<int>((sums[2] + rounding) / window),
                static_cast<int>((sums[3] + rounding) / window));
            if (x + 1 < width) {
                const QRgb leaving = source_row[std::clamp(x - radius, 0, width - 1)];
                const QRgb entering = source_row[std::clamp(x + radius + 1, 0, width - 1)];
                sums[0] -= static_cast<std::uint64_t>(qRed(leaving));
                sums[1] -= static_cast<std::uint64_t>(qGreen(leaving));
                sums[2] -= static_cast<std::uint64_t>(qBlue(leaving));
                sums[3] -= static_cast<std::uint64_t>(qAlpha(leaving));
                sums[0] += static_cast<std::uint64_t>(qRed(entering));
                sums[1] += static_cast<std::uint64_t>(qGreen(entering));
                sums[2] += static_cast<std::uint64_t>(qBlue(entering));
                sums[3] += static_cast<std::uint64_t>(qAlpha(entering));
            }
        }
    }
}

void verticalBoxBlur(const QImage& source, QImage* destination, int radius) {
    const int width = source.width();
    const int height = source.height();
    const std::uint64_t window = static_cast<std::uint64_t>(radius) * 2U + 1U;
    const std::uint64_t rounding = window / 2U;

    for (int x = 0; x < width; ++x) {
        std::array<std::uint64_t, 4> sums{};
        for (int offset = -radius; offset <= radius; ++offset) {
            const auto* row = reinterpret_cast<const QRgb*>(source.constScanLine(
                std::clamp(offset, 0, height - 1)));
            const QRgb pixel = row[x];
            sums[0] += static_cast<std::uint64_t>(qRed(pixel));
            sums[1] += static_cast<std::uint64_t>(qGreen(pixel));
            sums[2] += static_cast<std::uint64_t>(qBlue(pixel));
            sums[3] += static_cast<std::uint64_t>(qAlpha(pixel));
        }

        for (int y = 0; y < height; ++y) {
            auto* destination_row = reinterpret_cast<QRgb*>(destination->scanLine(y));
            destination_row[x] = qRgba(
                static_cast<int>((sums[0] + rounding) / window),
                static_cast<int>((sums[1] + rounding) / window),
                static_cast<int>((sums[2] + rounding) / window),
                static_cast<int>((sums[3] + rounding) / window));
            if (y + 1 < height) {
                const auto* leaving_row = reinterpret_cast<const QRgb*>(source.constScanLine(
                    std::clamp(y - radius, 0, height - 1)));
                const auto* entering_row = reinterpret_cast<const QRgb*>(source.constScanLine(
                    std::clamp(y + radius + 1, 0, height - 1)));
                const QRgb leaving = leaving_row[x];
                const QRgb entering = entering_row[x];
                sums[0] -= static_cast<std::uint64_t>(qRed(leaving));
                sums[1] -= static_cast<std::uint64_t>(qGreen(leaving));
                sums[2] -= static_cast<std::uint64_t>(qBlue(leaving));
                sums[3] -= static_cast<std::uint64_t>(qAlpha(leaving));
                sums[0] += static_cast<std::uint64_t>(qRed(entering));
                sums[1] += static_cast<std::uint64_t>(qGreen(entering));
                sums[2] += static_cast<std::uint64_t>(qBlue(entering));
                sums[3] += static_cast<std::uint64_t>(qAlpha(entering));
            }
        }
    }
}

} // namespace

bool ImageBlurStrokeRenderer::apply(QImage* image,
                                    const ImageBlurStrokeData& stroke,
                                    bool* changed,
                                    QString* error) {
    if (changed != nullptr) *changed = false;
    if (error != nullptr) error->clear();
    if (image == nullptr || image->isNull()) {
        assignError(error, QStringLiteral("The blur target image is empty."));
        return false;
    }
    if (stroke.points.isEmpty() ||
        stroke.points.size() > ImageDocumentStore::kMaximumPaintStrokePoints ||
        stroke.diameter < 1 ||
        stroke.diameter > ImageDocumentStore::kMaximumPaintBrushDiameter ||
        stroke.radius < 0 || stroke.radius > ImageDocumentStore::kMaximumBlurRadius) {
        assignError(error, QStringLiteral("The blur stroke has invalid parameters."));
        return false;
    }
    if (stroke.radius == 0) return true;
    for (const QPointF& point : stroke.points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            assignError(error, QStringLiteral("The blur stroke contains a non-finite point."));
            return false;
        }
    }

    *image = image->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image->isNull()) {
        assignError(error, QStringLiteral("The blur target could not be converted to premultiplied RGBA."));
        return false;
    }

    const QPainterPath coverage_path = strokeCoveragePath(stroke);
    QRect output_rect = coverage_path.boundingRect().adjusted(-1.0, -1.0, 1.0, 1.0)
        .toAlignedRect().intersected(image->rect());
    if (stroke.clipping_path.has_value()) {
        output_rect = output_rect.intersected(
            stroke.clipping_path->boundingRect().toAlignedRect());
    }
    if (output_rect.isEmpty()) return true;

    QImage coverage(output_rect.size(), QImage::Format_ARGB32_Premultiplied);
    if (coverage.isNull()) {
        assignError(error, QStringLiteral("The blur coverage buffer could not be allocated."));
        return false;
    }
    coverage.fill(Qt::transparent);
    {
        QPainter painter(&coverage);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (stroke.clipping_path.has_value()) {
            QTransform to_local;
            to_local.translate(-output_rect.x(), -output_rect.y());
            painter.setClipPath(to_local.map(*stroke.clipping_path));
        }
        QTransform to_local;
        to_local.translate(-output_rect.x(), -output_rect.y());
        painter.fillPath(to_local.map(coverage_path), Qt::white);
    }

    bool has_coverage = false;
    for (int y = 0; y < coverage.height() && !has_coverage; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(coverage.constScanLine(y));
        for (int x = 0; x < coverage.width(); ++x) {
            if (qAlpha(row[x]) != 0) {
                has_coverage = true;
                break;
            }
        }
    }
    if (!has_coverage) return true;

    const int support = stroke.radius * 3 + 1;
    const QRect filter_rect = output_rect.adjusted(-support, -support, support, support)
        .intersected(image->rect());
    QImage filtered = image->copy(filter_rect);
    QImage scratch(filter_rect.size(), QImage::Format_ARGB32_Premultiplied);
    if (filtered.isNull() || scratch.isNull()) {
        assignError(error, QStringLiteral("The blur filter buffers could not be allocated."));
        return false;
    }
    for (int pass = 0; pass < 3; ++pass) {
        horizontalBoxBlur(filtered, &scratch, stroke.radius);
        verticalBoxBlur(scratch, &filtered, stroke.radius);
    }

    bool pixels_changed = false;
    for (int y = 0; y < output_rect.height(); ++y) {
        auto* destination_row = reinterpret_cast<QRgb*>(
            image->scanLine(output_rect.y() + y)) + output_rect.x();
        const auto* filtered_row = reinterpret_cast<const QRgb*>(
            filtered.constScanLine(output_rect.y() + y - filter_rect.y())) +
            output_rect.x() - filter_rect.x();
        const auto* coverage_row = reinterpret_cast<const QRgb*>(coverage.constScanLine(y));
        for (int x = 0; x < output_rect.width(); ++x) {
            const int amount = qAlpha(coverage_row[x]);
            if (amount == 0) continue;
            const QRgb original = destination_row[x];
            const QRgb blurred = filtered_row[x];
            const int inverse = 255 - amount;
            const QRgb result = qRgba(
                (qRed(original) * inverse + qRed(blurred) * amount + 127) / 255,
                (qGreen(original) * inverse + qGreen(blurred) * amount + 127) / 255,
                (qBlue(original) * inverse + qBlue(blurred) * amount + 127) / 255,
                (qAlpha(original) * inverse + qAlpha(blurred) * amount + 127) / 255);
            if (result != original) {
                destination_row[x] = result;
                pixels_changed = true;
            }
        }
    }
    if (changed != nullptr) *changed = pixels_changed;
    return true;
}

} // namespace image_editor
