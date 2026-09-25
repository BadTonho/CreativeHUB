#include "main_window/main_window.h"

#include "ui/effects/effects_favorites_widget.h"
#include "ui/effects/effects_list_widget.h"
#include "ui/effects/effects_toolbox_widget.h"

QWidget* MainWindow::createEffectsToolbox() {
    effects_toolbox_ = new EffectsToolboxWidget;
    return effects_toolbox_;
}

QWidget* MainWindow::createEffectsPanel() {
    effects_list_ = new EffectsListWidget;
    connect(
        effects_toolbox_,
        &EffectsToolboxWidget::categoryChanged,
        effects_list_,
        &EffectsListWidget::setCategory);
    effects_list_->setCategory(effects_toolbox_->currentCategoryId());
    return effects_list_;
}

QWidget* MainWindow::createEffectsFavorites() {
    effects_favorites_ = new EffectsFavoritesWidget;
    return effects_favorites_;
}
