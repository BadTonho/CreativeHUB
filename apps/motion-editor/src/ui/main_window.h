#pragma once

#include "model/composition_document.h"

#include <QMainWindow>

#include <array>
#include <optional>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QToolButton;
class QWidget;

namespace motion::ui {

class CompositionViewer;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

    [[nodiscard]] const model::CompositionDocument* compositionDocument() const noexcept;
    [[nodiscard]] std::optional<model::LayerId> selectedLayerId() const noexcept;

private:
    void createNewComposition();
    void createWorkspace();
    void addLayer(model::LayerKind kind);
    void removeSelectedLayer();
    void moveSelectedLayer(int row_delta);
    void selectLayerAtRow(int row);
    void updateLayerVisibility(QListWidgetItem* item);
    void updateTransformField(
        QLineEdit* field,
        creative_suite::animation::TransformProperty property);
    void refreshLayerList();
    void refreshLayerControls();
    void refreshTransformInspector();
    void refreshViewer();
    [[nodiscard]] const model::CompositionLayer* selectedLayer() const noexcept;

    std::optional<model::CompositionDocument> document_;
    std::optional<model::LayerId> selected_layer_id_;
    QLabel* empty_state_ = nullptr;
    QSplitter* workspace_ = nullptr;
    QListWidget* layer_list_ = nullptr;
    QToolButton* add_layer_button_ = nullptr;
    QPushButton* remove_layer_button_ = nullptr;
    QPushButton* move_front_button_ = nullptr;
    QPushButton* move_back_button_ = nullptr;
    QWidget* transform_panel_ = nullptr;
    CompositionViewer* viewer_ = nullptr;
    std::array<QLineEdit*, 5> transform_fields_{};
};

} // namespace motion::ui
