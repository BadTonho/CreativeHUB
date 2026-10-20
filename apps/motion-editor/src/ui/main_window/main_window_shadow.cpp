#include "main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/translation_helpers.h"
#include <QColorDialog>
#include <algorithm>
namespace motion::ui {
void MainWindow::editSelectedShadowValue(int axis) {
    if (axis < 0 || axis >= 4 || !document_ || !selected_effect_ ||
        selected_effect_->first != selected_layer_id_ || export_worker_ || audio_keyframe_worker_)
        return;
    const auto at = std::find_if(document_->layers().begin(), document_->layers().end(),
                                 [&](const auto &l) { return l.id == selected_layer_id_; });
    if (at == document_->layers().end() || selected_effect_->second >= at->effects.size())
        return;
    auto effects = at->effects;
    auto *shadow = std::get_if<model::DropShadowEffect>(&effects[selected_effect_->second]);
    if (!shadow)
        return;
    const auto property = model::shadow_properties[axis];
    const double value = inspector_->effectFieldValue(property);
    auto &keys = shadow->keyframes[axis];
    if (keys.empty())
        model::setShadowBaseValue(*shadow, property, value);
    else {
        const auto frame = timeline_->currentFrame() - at->timeline_start_frame;
        auto key = std::find_if(keys.begin(), keys.end(), [&](const auto &k) { return k.frame == frame; });
        if (key == keys.end())
            return;
        key->value = value;
    }
    applySelectedEffectStack(std::move(effects), true);
}
void MainWindow::chooseShadowColor() {
    if (!document_ || !selected_effect_ || export_worker_ || audio_keyframe_worker_)
        return;
    const auto identity = *selected_effect_;
    finishPendingTransformEdit();
    const auto at = std::find_if(document_->layers().begin(), document_->layers().end(),
                                 [&](const auto &l) { return l.id == identity.first; });
    if (at == document_->layers().end() || identity.second >= at->effects.size())
        return;
    const auto *shadow = std::get_if<model::DropShadowEffect>(&at->effects[identity.second]);
    if (!shadow)
        return;
    const auto old = shadow->parameters.color;
    const auto chosen =
        QColorDialog::getColor(QColor(old[0], old[1], old[2]), this, MotionUiTranslation::tr("Shadow Color"));
    if (!chosen.isValid() || !document_ || selected_effect_ != identity)
        return;
    const std::array<std::uint8_t, 3> color{std::uint8_t(chosen.red()), std::uint8_t(chosen.green()),
                                            std::uint8_t(chosen.blue())};
    if (color == old)
        return;
    const auto fresh = std::find_if(document_->layers().begin(), document_->layers().end(),
                                    [&](const auto &l) { return l.id == identity.first; });
    if (fresh == document_->layers().end() || identity.second >= fresh->effects.size() ||
        !std::holds_alternative<model::DropShadowEffect>(fresh->effects[identity.second]))
        return;
    auto effects = fresh->effects;
    std::get<model::DropShadowEffect>(effects[identity.second]).parameters.color = color;
    applySelectedEffectStack(std::move(effects));
}
} // namespace motion::ui
