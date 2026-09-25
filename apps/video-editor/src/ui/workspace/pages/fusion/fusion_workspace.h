#pragma once

#include <QObject>

class QWidget;

namespace ui {

// Builds the visual-only Fusion panels consumed by WorkspaceHost.
class FusionWorkspace final : public QObject {
public:
    explicit FusionWorkspace(QObject* parent = nullptr);

    void createPanels(QWidget* parent);

    [[nodiscard]] QWidget* viewerTitle() const noexcept { return viewer_title_; }
    [[nodiscard]] QWidget* nodeEditorPanel() const noexcept {
        return node_editor_panel_;
    }
    [[nodiscard]] QWidget* inspectorPanel() const noexcept {
        return inspector_panel_;
    }

private:
    QWidget* viewer_title_ = nullptr;
    QWidget* node_editor_panel_ = nullptr;
    QWidget* inspector_panel_ = nullptr;
};

}  // namespace ui
