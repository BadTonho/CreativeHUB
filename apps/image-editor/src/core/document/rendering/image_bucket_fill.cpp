#include "rendering/image_bucket_fill.h"
#include "rendering/image_document_geometry.h"

#include <QBitArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

int channelDistance(int lhs, int rhs) noexcept {
    return std::abs(lhs - rhs);
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

} // namespace

bool ImageBucketFill::apply(QImage* image, const ImageBucketFillData& fill,
                            bool* changed, QString* error) {
    if (changed != nullptr) *changed = false;
    if (error != nullptr) error->clear();
    if (image == nullptr || image->isNull() || image->width() <= 0 ||
        image->height() <= 0 || image->sizeInBytes() <= 0 ||
        !fill.color.isValid() || fill.tolerance < 0 || fill.tolerance > 255) {
        assignError(error, QStringLiteral("The fill image, seed, color, or tolerance is invalid."));
        return false;
    }
    if (fill.clipping_path.has_value() &&
        !ImageDocumentGeometry::isValidStrokeClipPath(*fill.clipping_path)) {
        assignError(error, QStringLiteral("The fill selection has invalid geometry."));
        return false;
    }
    // A later canvas resize may move a persisted seed outside the new canvas.
    // In that case the operation is a valid no-op, like a click outside it.
    if (fill.seed.x() < 0 || fill.seed.y() < 0 ||
        fill.seed.x() >= image->width() || fill.seed.y() >= image->height()) return true;
    if (fill.color.alpha() == 0) return true;

    *image = image->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image->isNull()) {
        assignError(error, QStringLiteral("There is not enough memory to render the fill."));
        return false;
    }

    const qint64 pixel_count = static_cast<qint64>(image->width()) * image->height();
    if (pixel_count <= 0 || pixel_count > ImageDocumentStore::kMaximumCanvasPixels ||
        pixel_count > std::numeric_limits<quint32>::max()) {
        assignError(error, QStringLiteral("The fill image exceeds the supported canvas size."));
        return false;
    }

    const auto inside_selection = [&fill](int x, int y) {
        return !fill.clipping_path.has_value() ||
            fill.clipping_path->contains(QPointF(x + 0.5, y + 0.5));
    };
    if (!inside_selection(fill.seed.x(), fill.seed.y())) return true;

    const int width = image->width();
    const int height = image->height();
    const auto* seed_row = reinterpret_cast<const QRgb*>(image->constScanLine(fill.seed.y()));
    const QRgb seed_pixel = qUnpremultiply(seed_row[fill.seed.x()]);
    const int seed_red = qRed(seed_pixel);
    const int seed_green = qGreen(seed_pixel);
    const int seed_blue = qBlue(seed_pixel);
    const int seed_alpha = qAlpha(seed_pixel);
    const auto pixel_matches_seed = [&](QRgb premultiplied) {
        const QRgb pixel = qUnpremultiply(premultiplied);
        const int difference = std::max({
            channelDistance(qRed(pixel), seed_red),
            channelDistance(qGreen(pixel), seed_green),
            channelDistance(qBlue(pixel), seed_blue),
            channelDistance(qAlpha(pixel), seed_alpha)});
        return difference <= fill.tolerance;
    };

    QBitArray visited(static_cast<qsizetype>(pixel_count), false);
    QVector<quint32> pending;
    constexpr qsizetype kMaximumPendingSeeds = 16 * 1024 * 1024;
    pending.reserve(std::min<qsizetype>(width * 2LL, 4096));

    const auto indexOf = [width](int x, int y) {
        return static_cast<qsizetype>(y) * width + x;
    };
    const auto candidate = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height ||
            !inside_selection(x, y)) return false;
        const qsizetype index = indexOf(x, y);
        if (visited.testBit(index)) return false;
        const auto* row = reinterpret_cast<const QRgb*>(image->constScanLine(y));
        return pixel_matches_seed(row[x]);
    };
    const auto pushSeed = [&](int x, int y) {
        if (!candidate(x, y)) return true;
        if (pending.size() >= kMaximumPendingSeeds) {
            assignError(error, QStringLiteral(
                "This fill region is too complex to process within the memory limit."));
            return false;
        }
        visited.setBit(indexOf(x, y), true);
        pending.append(static_cast<quint32>(indexOf(x, y)));
        return true;
    };

    if (!pushSeed(fill.seed.x(), fill.seed.y())) return false;
    const QRgb fill_pixel = qPremultiply(fill.color.rgba());
    bool any_changed = false;

    while (!pending.isEmpty()) {
        const quint32 seed_index = pending.takeLast();
        const int y = static_cast<int>(seed_index / static_cast<quint32>(width));
        const int seed_x = static_cast<int>(seed_index % static_cast<quint32>(width));
        int left = seed_x;
        int right = seed_x;
        while (left > 0 && candidate(left - 1, y)) --left;
        while (right + 1 < width && candidate(right + 1, y)) ++right;

        auto* row = reinterpret_cast<QRgb*>(image->scanLine(y));
        for (int x = left; x <= right; ++x) {
            const qsizetype index = indexOf(x, y);
            if (!visited.testBit(index)) visited.setBit(index, true);
            const QRgb updated = sourceOver(row[x], fill_pixel);
            any_changed = any_changed || updated != row[x];
            row[x] = updated;
        }

        for (const int adjacent_y : {y - 1, y + 1}) {
            if (adjacent_y < 0 || adjacent_y >= height) continue;
            int x = left;
            while (x <= right) {
                if (!candidate(x, adjacent_y)) {
                    ++x;
                    continue;
                }
                if (!pushSeed(x, adjacent_y)) return false;
                do {
                    ++x;
                } while (x <= right && candidate(x, adjacent_y));
            }
        }
    }

    if (changed != nullptr) *changed = any_changed;
    return true;
}

} // namespace image_editor
