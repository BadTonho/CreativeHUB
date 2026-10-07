#include "magic_wand_tool.h"

#include "magic_wand_config.h"
#include "image_document_store.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QRect>

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
constexpr qsizetype kMaximumFloodSpans = 250'000;

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

    // Read straight RGBA bytes directly in the flood loop. pixelColor() builds
    // a QColor for every visited pixel and made large uniform regions needlessly
    // expensive to inspect.
    const QImage samples = target.convertToFormat(QImage::Format_RGBA8888);
    if (samples.isNull()) {
        return {Status::Rejected, {}, QStringLiteral("The Magic Wand could not prepare the active pixels.")};
    }
    const uchar* reference = samples.constScanLine(seed.y()) + seed.x() * 4;
    const auto matches = [&samples, reference, tolerance](int x, int y) {
        const uchar* pixel = samples.constScanLine(y) + x * 4;
        return std::max({std::abs(static_cast<int>(pixel[0]) - reference[0]),
                         std::abs(static_cast<int>(pixel[1]) - reference[1]),
                         std::abs(static_cast<int>(pixel[2]) - reference[2]),
                         std::abs(static_cast<int>(pixel[3]) - reference[3])}) <= tolerance;
    };

    QImage visited(target.size(), QImage::Format_MonoLSB);
    if (visited.isNull()) {
        return {Status::Rejected, {}, QStringLiteral("The Magic Wand could not allocate its temporary region map.")};
    }
    visited.fill(0);
    QVector<Span> pending;
    QVector<Span> spans;
    pending.reserve(4096);
    spans.reserve(4096);

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
        if (spans.size() + pending.size() >= kMaximumFloodSpans) return false;
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

    // Adjacent rows with the same horizontal run form one rectangle. A fully
    // transparent canvas therefore becomes one selection rectangle instead of
    // thousands of per-row outlines in the canvas overlay.
    std::sort(spans.begin(), spans.end(), [](const Span& left, const Span& right) {
        if (left.y != right.y) return left.y < right.y;
        if (left.left != right.left) return left.left < right.left;
        return left.right < right.right;
    });

    QVector<QRect> rectangles;
    QHash<quint64, qsizetype> active_rectangles;
    qsizetype span_index = 0;
    while (span_index < spans.size()) {
        const int row = spans.at(span_index).y;
        QHash<quint64, qsizetype> next_active_rectangles;
        while (span_index < spans.size() && spans.at(span_index).y == row) {
            const Span& span = spans.at(span_index++);
            const quint64 key = (static_cast<quint64>(static_cast<quint32>(span.left)) << 32U) |
                                static_cast<quint32>(span.right);
            const auto previous = active_rectangles.constFind(key);
            qsizetype rectangle_index = -1;
            if (previous != active_rectangles.cend() &&
                rectangles.at(previous.value()).bottom() + 1 == row) {
                rectangle_index = previous.value();
                rectangles[rectangle_index].setHeight(rectangles.at(rectangle_index).height() + 1);
            } else {
                rectangle_index = rectangles.size();
                rectangles.append(QRect(span.left, span.y, span.right - span.left + 1, 1));
            }
            next_active_rectangles.insert(key, rectangle_index);
        }
        active_rectangles = std::move(next_active_rectangles);
    }

    QPainterPath region;
    for (const QRect& rectangle : rectangles) {
        if (region.elementCount() + kRectangleElementCount >
            ImageDocumentStore::kMaximumStrokeClipPathElements) {
            return {Status::Rejected, {}, geometryLimitMessage()};
        }
        region.addRect(QRectF(rectangle));
    }
    if (region.isEmpty()) return {};

    // The scanline rectangles describe one filled region, but their shared
    // edges are not selection boundaries. Merge the subpaths so the canvas
    // overlay draws only the exterior contour (and any real holes).
    region = region.simplified();
    if (region.elementCount() > ImageDocumentStore::kMaximumStrokeClipPathElements) {
        return {Status::Rejected, {}, geometryLimitMessage()};
    }
    return {Status::Selected, std::move(region), {}};
}

} // namespace image_editor
