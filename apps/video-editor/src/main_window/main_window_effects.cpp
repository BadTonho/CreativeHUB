#include "main_window/main_window.h"

#include "ui/effects/effects_favorites_widget.h"
#include "ui/effects/effects_list_widget.h"
#include "ui/effects/effects_toolbox_widget.h"
#include "ui/workspace/workspace_host.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "ui/functions/function_palette.h"
#include "workspaces/edit/ui/edit_workspace.h"

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
    connect(effects_list_, &EffectsListWidget::creationRequested, this,
        [this](const QString& id) {
            if (fusion_workspace_) fusion_workspace_->addCatalogNode(id);
        });
    return effects_list_;
}

QWidget* MainWindow::createEffectsFavorites() {
    effects_favorites_ = new EffectsFavoritesWidget;
    connect(effects_list_, &EffectsListWidget::favoritesChanged, effects_favorites_, &EffectsFavoritesWidget::refresh);
    connect(effects_favorites_, &EffectsFavoritesWidget::creationRequested, this,
        [this](const QString& id) {
            if (fusion_workspace_) fusion_workspace_->addCatalogNode(id);
        });
    return effects_favorites_;
}

void MainWindow::refreshEffectsContext() {
    if (!effects_list_ || !effects_toolbox_ || !effects_favorites_) return;
    const auto page = workspace_host_ ? workspace_host_->currentPage() : ui::WorkspacePageId::Edit;
    const auto workspace = page == ui::WorkspacePageId::Fusion ? effects::Workspace::Fusion
        : page == ui::WorkspacePageId::Edit ? effects::Workspace::Edit : effects::Workspace::None;
    effects_list_->setWorkspace(workspace);
    effects_favorites_->setWorkspace(workspace);
    effects_toolbox_->setWorkspace(workspace);
    const bool enabled = workspace == effects::Workspace::Edit ||
        (workspace == effects::Workspace::Fusion && fusion_workspace_ &&
         fusion_workspace_->selectedClipId() != 0);
    effects_list_->setEnabled(enabled);
    effects_favorites_->setEnabled(enabled);
    if (function_palette_ != nullptr) {
        const bool target_available = workspace == effects::Workspace::Fusion
            ? enabled : workspace == effects::Workspace::Edit && edit_workspace_ &&
                edit_workspace_->controller()->selectedClipSupportsEffects();
        function_palette_->setContext(workspace, target_available);
    }
}
