#include "rendering/image_layer_outline.h"
#include <QRect>
#include <algorithm>
#include <cmath>
#include <vector>

namespace image_editor {
QImage applyLayerOutline(const QImage& source, const ImageLayerOutlineData& outline,
                         const std::atomic_bool* cancel) {
    const auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (cancelled() || source.isNull() || !validLayerOutline(outline)) return {};
    if (!outline.enabled || outline.color.alpha() == 0) return source;
    QImage pixels = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixels.isNull()) return {};
    QRect bounds;
    for (int y = 0; y < pixels.height(); ++y) {
        if (cancelled()) return {};
        const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        int first = pixels.width(), last = -1;
        for (int x = 0; x < pixels.width(); ++x) if (qAlpha(row[x])) {
            first = std::min(first, x); last = x;
        }
        if (last >= first) bounds = bounds.united(QRect(first, y, last-first+1, 1));
    }
    if (bounds.isEmpty()) return source;
    const bool inside = outline.position == ImageOutlinePosition::Inside;
    const int radius = outline.thickness;
    const QRect roi = (inside ? bounds : bounds.adjusted(-radius-1, -radius-1, radius+1, radius+1))
        .intersected(pixels.rect());
    QImage alpha(roi.size(), QImage::Format_Grayscale8);
    QImage dilated(roi.size(), QImage::Format_Grayscale8);
    if (alpha.isNull() || dilated.isNull()) return {};
    dilated.fill(0);
    for (int y = 0; y < roi.height(); ++y) {
        if (cancelled()) return {};
        auto* dst = alpha.scanLine(y);
        const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y+roi.y()));
        for (int x=0; x<roi.width(); ++x) {
            const int a=qAlpha(row[x+roi.x()]); dst[x]=static_cast<uchar>(inside ? 255-a : a);
        }
    }
    // Disk morphology: horizontal sliding maxima for the fully covered core,
    // plus fractional weights on the one-pixel circular fringe. O(radius*pixels),
    // independent of disk area; only two byte planes and one scanline deque.
    std::vector<int> queue(static_cast<std::size_t>(roi.width()));
    const double inner2 = (radius-.5)*(radius-.5), outer2=(radius+.5)*(radius+.5);
    const auto sample = [&](const uchar* row, int x) {
        return row && x>=0 && x<roi.width() ? int(row[x]) : (inside ? 255 : 0);
    };
    for (int dy=-radius; dy<=radius; ++dy) {
        const double yy=double(dy)*dy;
        const int core=yy<=inner2 ? int(std::floor(std::sqrt(inner2-yy))) : -1;
        const int fringe=int(std::floor(std::sqrt(outer2-yy)));
        std::vector<std::pair<int,double>> weights;
        for(int dx=core+1; dx<=fringe; ++dx)
            weights.emplace_back(dx, std::clamp(radius+.5-std::hypot(double(dx),double(dy)),0.,1.));
        for(int y=0; y<roi.height(); ++y) {
            if(cancelled()) return {};
            const int sy=y+dy;
            const auto* row=sy>=0 && sy<roi.height() ? alpha.constScanLine(sy) : nullptr;
            auto* dst=dilated.scanLine(y);
            if (!row) {
                if (inside) {
                    int edge = core >= 0 ? 255 : 0;
                    for (const auto& weight : weights) edge = std::max(edge, int(std::lround(255*weight.second)));
                    for (int x=0; x<roi.width(); ++x) dst[x] = static_cast<uchar>(std::max(int(dst[x]), edge));
                }
                continue;
            }
            int head=0, tail=0, next=0;
            for(int x=0; x<roi.width(); ++x) {
                int value=0;
                if(core>=0) {
                    const int right=std::min(roi.width()-1,x+core);
                    while(next<=right) {
                        while(tail>head && sample(row,queue[tail-1])<=sample(row,next)) --tail;
                        queue[tail++]=next++;
                    }
                    while(tail>head && queue[head]<x-core) ++head;
                    value=tail>head ? sample(row,queue[head]) : 0;
                    if(inside && (!row || x-core<0 || x+core>=roi.width())) value=255;
                }
                for(const auto& [dx,weight]:weights) {
                    value=std::max(value,int(std::lround(sample(row,x-dx)*weight)));
                    value=std::max(value,int(std::lround(sample(row,x+dx)*weight)));
                }
                dst[x]=static_cast<uchar>(std::max(int(dst[x]),value));
            }
        }
    }
    for(int y=0; y<roi.height(); ++y) {
        if(cancelled()) return {};
        auto* row=reinterpret_cast<QRgb*>(pixels.scanLine(y+roi.y()));
        const auto* coverage=dilated.constScanLine(y);
        for(int x=0; x<roi.width(); ++x) {
            const QRgb p=row[x+roi.x()]; const int a=qAlpha(p);
            const int eroded=255-coverage[x];
            const int ring=inside ? std::max(0,a-eroded) : std::max(0,int(coverage[x])-a);
            const double opacity=outline.color.alphaF();
            if(inside) {
                const double mix=a ? ring*opacity/a : 0.;
                const auto channel=[&](int original,int color) { return int(std::lround(original*(1-mix)+color*a/255.*mix)); };
                row[x+roi.x()]=qRgba(channel(qRed(p),outline.color.red()),channel(qGreen(p),outline.color.green()),
                    channel(qBlue(p),outline.color.blue()),a);
            } else {
                const double behind=ring*opacity*(255-a)/255.;
                const auto channel=[&](int original,int color) { return std::min(255,original+int(std::lround(color*behind/255.))); };
                row[x+roi.x()]=qRgba(channel(qRed(p),outline.color.red()),channel(qGreen(p),outline.color.green()),
                    channel(qBlue(p),outline.color.blue()),std::min(255,a+int(std::lround(behind))));
            }
        }
    }
    return pixels;
}
}
