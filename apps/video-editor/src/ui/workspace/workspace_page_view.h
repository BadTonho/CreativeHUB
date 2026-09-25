#pragma once

#include <QWidget>

class QLabel;
class QStackedWidget;

namespace ui {

class WorkspacePageView final : public QWidget {
public:
    explicit WorkspacePageView(
        QWidget* preview_widget,
        QWidget* edit_inspector,
        QWidget* timeline_panel,
        QWidget* parent = nullptr);

    void setFusionPageActive(bool active);

    [[nodiscard]] QWidget* previewWidget() const noexcept;
    [[nodiscard]] QWidget* timelinePanel() const noexcept;
    [[nodiscard]] QWidget* nodeEditorPanel() const noexcept;
    [[nodiscard]] QWidget* editInspectorPage() const noexcept;
    [[nodiscard]] QWidget* fusionInspectorPage() const noexcept;
    [[nodiscard]] QStackedWidget* inspectorPanel() const noexcept;
    [[nodiscard]] QStackedWidget* lowerWorkspacePanel() const noexcept;

private:
    QWidget* preview_widget_ = nullptr;
    QWidget* timeline_panel_ = nullptr;
    QWidget* node_editor_panel_ = nullptr;
    QWidget* edit_inspector_ = nullptr;
    QWidget* fusion_inspector_ = nullptr;
    QLabel* viewer_title_ = nullptr;
    QStackedWidget* lower_workspace_panel_ = nullptr;
    QStackedWidget* inspector_panel_ = nullptr;
};

}  // namespace ui
