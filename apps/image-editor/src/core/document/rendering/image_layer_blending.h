#pragma once
#include "image_document_utils.h"
#include <QPainter>

namespace image_editor {
// Borrowed source, copy-on-write destination. Both are document-sized. Normal
// retains Qt's existing rounding; non-Normal uses the shared transparent math.
[[nodiscard]] inline bool blendLayerPixels(QImage* destination, const QImage& source,
    creative_suite::composition::BlendMode mode, int opacity,
    const std::atomic_bool* cancel = nullptr, bool source_atop = false) {
    using namespace creative_suite::composition;
    if (!destination || destination->isNull() || source.isNull() ||
        destination->size() != source.size() || !validBlendMode(mode) ||
        opacity < 0 || opacity > 100 || exportWasCancelled(cancel)) return false;
    if (opacity == 0) return true;
    if (mode == BlendMode::Normal) {
        QPainter painter(destination);
        painter.setCompositionMode(source_atop ? QPainter::CompositionMode_SourceAtop : QPainter::CompositionMode_SourceOver);
        painter.setOpacity(opacity/100.0);
        if (!source_atop) painter.drawImage(0,0,source);
        else for (int y=0;y<source.height();++y) {
            if (exportWasCancelled(cancel)) return false;
            painter.drawImage(QPoint(0,y),source,QRect(0,y,source.width(),1));
        }
        return !exportWasCancelled(cancel);
    }
    *destination=destination->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto pixels=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (destination->isNull() || pixels.isNull()) return false;
    for (int y=0;y<pixels.height();++y) {
        if (exportWasCancelled(cancel)) return false;
        auto* target=reinterpret_cast<QRgb*>(destination->scanLine(y));
        const auto* input=reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        for (int x=0;x<pixels.width();++x) {
            const auto b=target[x],s=input[x];
            const double alpha=qAlpha(s)/255.;
            if (alpha==0) continue;
            const double as=alpha*opacity/100.,ab=qAlpha(b)/255.;
            const auto channel=[&](int backdrop,int src) {
                return int(std::lround(std::clamp(composeBlendChannel(mode,backdrop/255.,ab,
                    std::clamp(src/(255.*alpha),0.,1.),as,source_atop),0.,1.)*255));
            };
            target[x]=qRgba(channel(qRed(b),qRed(s)),channel(qGreen(b),qGreen(s)),channel(qBlue(b),qBlue(s)),
                int(std::lround(composeBlendAlpha(ab,as,source_atop)*255)));
        }
    }
    return !exportWasCancelled(cancel);
}
} // namespace image_editor
