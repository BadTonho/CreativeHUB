#pragma once
#include "image_layer_blending.h"
namespace image_editor {
// Neutral Normal adjustments must retain the legacy accumulator byte path.
[[nodiscard]] inline bool needsAdjustmentPrecision(const ImageLayerData& layer) {
    return layer.adjustment && (std::holds_alternative<creative_suite::effects::GradientMapParameters>(*layer.adjustment) ||
        (layer.visible && layer.opacity>0 && (!creative_suite::effects::identityAdjustment(*layer.adjustment) || layer.blend_mode!=creative_suite::composition::BlendMode::Normal)));
}
// Borrowed mask, copy-on-write accumulated pixels; preserves each alpha byte.
[[nodiscard]] inline bool applyLayerAdjustment(QImage* pixels,
    const creative_suite::effects::AdjustmentParameters& parameters,const QImage& mask,
    creative_suite::composition::BlendMode mode,int opacity,const std::atomic_bool* cancel=nullptr) {
    using namespace creative_suite;
    if(!pixels || pixels->isNull() || !effects::validAdjustment(parameters) || !composition::validBlendMode(mode) ||
       opacity<0 || opacity>100 || (!mask.isNull() && mask.size()!=pixels->size()) || exportWasCancelled(cancel))return false;
    if(!opacity)return true;
    if(effects::identityAdjustment(parameters) && mode==composition::BlendMode::Normal)return true;
    *pixels=pixels->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(pixels->isNull())return false;
    for(int y=0;y<pixels->height();++y){
        if(exportWasCancelled(cancel))return false;
        auto* row=reinterpret_cast<QRgb*>(pixels->scanLine(y));
        for(int x=0;x<pixels->width();++x){
            const QRgb b=row[x];const int alpha=qAlpha(b);if(!alpha)continue;
            const std::array<double,3> rgb{double(qRed(b))/alpha,double(qGreen(b))/alpha,double(qBlue(b))/alpha};
            const auto mapped=effects::adjustmentRgb(parameters,rgb);
            const int coverage=mask.isNull()?255:mask.format()==QImage::Format_Grayscale8?mask.constScanLine(y)[x]:qGray(mask.pixel(x,y));
            const double strength=opacity/100.*coverage/255.;
            const auto channel=[&](int c){return int(std::lround(std::clamp((1-strength)*rgb[c]+strength*composition::blendChannel(mode,rgb[c],mapped[c]/255.),0.,1.)*alpha));};
            row[x]=qRgba(channel(0),channel(1),channel(2),alpha);
        }
    }
    return !exportWasCancelled(cancel);
}
inline QImage initialMaskPixels(const QSize& size,const std::optional<QPainterPath>& selection) {
    QImage mask(size,QImage::Format_ARGB32_Premultiplied);if(mask.isNull())return {};
    mask.fill(selection?Qt::black:Qt::white);
    if(selection){QPainter p(&mask);p.setRenderHint(QPainter::Antialiasing);p.fillPath(*selection,Qt::white);}
    return mask;
}
} // namespace image_editor
