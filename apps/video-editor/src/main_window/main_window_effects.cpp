#include "main_window/main_window.h"

#include "ui/effects/effects_favorites_widget.h"
#include "ui/effects/effects_list_widget.h"
#include "ui/effects/effects_toolbox_widget.h"
#include "ui/workspace/workspace_host.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "ui/functions/function_palette.h"
#include "workspaces/edit/ui/edit_workspace.h"

#include <QVBoxLayout>
#include <QWidget>

QWidget* MainWindow::createEffectsToolbox() {
    effects_toolbox_ = new EffectsToolboxWidget;
    return effects_toolbox_;
}

QWidget* MainWindow::createEffectsPanel() {
    effects_catalog_host_ = new QWidget;
    effects_catalog_host_->setObjectName(QStringLiteral("effectsCatalogHost"));
    effects_catalog_layout_ = new QVBoxLayout(effects_catalog_host_);
    effects_catalog_layout_->setContentsMargins(0, 0, 0, 0);
    effects_list_ = new EffectsListWidget(effects_catalog_host_);
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
    effects_catalog_layout_->addWidget(effects_list_);
    return effects_catalog_host_;
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
        : page == ui::WorkspacePageId::Color ? effects::Workspace::Color
        : page == ui::WorkspacePageId::Edit ? effects::Workspace::Edit : effects::Workspace::None;
    effects_list_->setWorkspace(workspace);
    effects_favorites_->setWorkspace(workspace);
    effects_toolbox_->setWorkspace(workspace);
    bool color_target_available = false;
    if (workspace == effects::Workspace::Color && active_timeline_clip_id_) {
        if (const auto location = timeline_model_.locateClip(*active_timeline_clip_id_)) {
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            color_target_available = timeline::supportsClipEffects(clip.kind);
        }
    }
    const bool enabled = workspace == effects::Workspace::Edit || color_target_available ||
        (workspace == effects::Workspace::Fusion && fusion_workspace_ &&
         fusion_workspace_->selectedClipId() != 0);
    effects_list_->setEnabled(enabled);
    effects_favorites_->setEnabled(enabled);
    if (function_palette_ != nullptr) {
        const auto functions_workspace = page == ui::WorkspacePageId::Color
            ? effects::Workspace::None : workspace;
        const bool target_available = workspace == effects::Workspace::Fusion
            ? enabled : workspace == effects::Workspace::Edit && edit_workspace_ &&
                edit_workspace_->controller()->selectedClipSupportsEffects();
        function_palette_->setContext(functions_workspace, target_available);
    }
}
