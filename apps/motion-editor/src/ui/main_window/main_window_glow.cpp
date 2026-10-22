#include "main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/translation_helpers.h"

#include <QColorDialog>

#include <algorithm>

namespace motion::ui {

void MainWindow::chooseGlowColor()
{
    if (!document_ || !selected_effect_ || export_worker_ || audio_keyframe_worker_) return;
    const auto identity = *selected_effect_;
    finishPendingTransformEdit();
    const auto layer = std::find_if(document_->layers().begin(), document_->layers().end(),
        [&](const auto& value) { return value.id == identity.first; });
    if (layer == document_->layers().end() || identity.second >= layer->effects.size()) return;
    const auto* glow = std::get_if<model::GlowEffect>(&layer->effects[identity.second]);
    if (!glow) return;
    const auto old = glow->parameters.color;
    const auto chosen = QColorDialog::getColor(QColor(old[0], old[1], old[2]), this,
        MotionUiTranslation::tr("Glow Color"));
    if (!chosen.isValid() || !document_ || selected_effect_ != identity) return;
    const std::array<std::uint8_t, 3> color{std::uint8_t(chosen.red()),
        std::uint8_t(chosen.green()), std::uint8_t(chosen.blue())};
    if (color == old) return;
    const auto fresh = std::find_if(document_->layers().begin(), document_->layers().end(),
        [&](const auto& value) { return value.id == identity.first; });
    if (fresh == document_->layers().end() || identity.second >= fresh->effects.size() ||
        !std::holds_alternative<model::GlowEffect>(fresh->effects[identity.second])) return;
    auto effects = fresh->effects;
    std::get<model::GlowEffect>(effects[identity.second]).parameters.color = color;
    applySelectedEffectStack(std::move(effects));
}

} // namespace motion::ui
