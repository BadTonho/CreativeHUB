#include "magic_wand_tool.h"

#include "magic_wand_config.h"
#include "image_document_store.h"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace image_editor {
namespace {

struct Span {
    int y = 0;
    int left = 0;
    int right = 0;
};

constexpr int kRectangleElementCount = 5;

QString geometryLimitMessage() {
    return QStringLiteral("The selected region exceeds the supported selection geometry limit.");
}

} // namespace

std::optional<QPoint> MagicWandTool::seedAt(
    const QPointF& document_position, const QSize& document_size) noexcept {
    if (document_size.width() <= 0 || document_size.height() <= 0 ||
        !std::isfinite(document_position.x()) || !std::isfinite(document_position.y()) ||
        document_position.x() < 0.0 || document_position.y() < 0.0 ||
        document_position.x() >= document_size.width() ||
        document_position.y() >= document_size.height()) {
        return std::nullopt;
    }
    return QPoint(static_cast<int>(std::floor(document_position.x())),
                  static_cast<int>(std::floor(document_position.y())));
}

MagicWandTool::Result MagicWandTool::select(
    const QImage& target, const QPoint& seed, int tolerance) const {
    if (target.isNull() || target.width() <= 0 || target.height() <= 0 ||
        seed.x() < 0 || seed.y() < 0 ||
        seed.x() >= target.width() || seed.y() >= target.height()) {
        return {Status::Rejected, {}, QStringLiteral("The active layer has no sampleable pixels at that position.")};
    }
    if (tolerance < MagicWandConfig::kMinimumTolerance ||
        tolerance > MagicWandConfig::kMaximumTolerance) {
        return {Status::Rejected, {}, QStringLiteral("The Magic Wand tolerance is outside its supported range.")};
    }

    const QColor reference = target.pixelColor(seed);
    const auto matches = [&target, &reference, tolerance](int x, int y) {
        const QColor pixel = target.pixelColor(x, y);
        return std::max({std::abs(pixel.red() - reference.red()),
                         std::abs(pixel.green() - reference.green()),
                         std::abs(pixel.blue() - reference.blue()),
                         std::abs(pixel.alpha() - reference.alpha())}) <= tolerance;
    };

    QImage visited(target.size(), QImage::Format_MonoLSB);
    if (visited.isNull()) {
        return {Status::Rejected, {}, QStringLiteral("The Magic Wand could not allocate its temporary region map.")};
    }
    visited.fill(0);
    const qsizetype maximum_rectangles =
        ImageDocumentStore::kMaximumStrokeClipPathElements / kRectangleElementCount;
    QVector<Span> pending;
    QVector<Span> spans;
    pending.reserve(std::min<qsizetype>(maximum_rectangles, 4096));
    spans.reserve(std::min<qsizetype>(maximum_rectangles, 4096));

    const auto isVisited = [&visited](int x, int y) {
        return (visited.constScanLine(y)[x >> 3] & (1U << (x & 7))) != 0;
    };
    const auto markVisited = [&visited](int x, int y) {
        visited.scanLine(y)[x >> 3] |= static_cast<uchar>(1U << (x & 7));
    };
    const auto scheduleRun = [&](int y, int x) -> bool {
        if (isVisited(x, y) || !matches(x, y)) return true;
        int left = x;
        while (left > 0 && !isVisited(left - 1, y) && matches(left - 1, y)) --left;
        int right = x;
        while (right + 1 < target.width() && !isVisited(right + 1, y) &&
               matches(right + 1, y)) ++right;
        for (int column = left; column <= right; ++column) markVisited(column, y);
        if (spans.size() + pending.size() >= maximum_rectangles) return false;
        pending.append({y, left, right});
        return true;
    };

    if (!scheduleRun(seed.y(), seed.x())) {
        return {Status::Rejected, {}, geometryLimitMessage()};
    }

    constexpr std::array<int, MagicWandConfig::kVerticalNeighborCount> kNeighborRows{-1, 1};
    static_assert(MagicWandConfig::kConnectivity == 4);
    while (!pending.isEmpty()) {
        const Span current = pending.takeLast();
        spans.append(current);

        for (int neighbor_index = 0; neighbor_index < 2; ++neighbor_index) {
            const int neighbor_y = current.y + kNeighborRows[neighbor_index];
            if (neighbor_y < 0 || neighbor_y >= target.height()) continue;
            int x = current.left;
            while (x <= current.right) {
                if (isVisited(x, neighbor_y) || !matches(x, neighbor_y)) {
                    ++x;
                    continue;
                }
                if (!scheduleRun(neighbor_y, x)) {
                    return {Status::Rejected, {}, geometryLimitMessage()};
                }
                while (x <= current.right && isVisited(x, neighbor_y)) ++x;
            }
        }
    }

    QPainterPath region;
    for (const Span& span : spans) {
        region.addRect(QRectF(span.left, span.y, span.right - span.left + 1, 1));
        if (region.elementCount() > ImageDocumentStore::kMaximumStrokeClipPathElements) {
            return {Status::Rejected, {}, geometryLimitMessage()};
        }
    }
    if (region.isEmpty()) return {};
    return {Status::Selected, std::move(region), {}};
}

} // namespace image_editor
