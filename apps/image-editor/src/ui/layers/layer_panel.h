#pragma once

#include "image_document_store.h"

#include <QWidget>

class QListWidget;
class QLabel;
class QSlider;
class QToolButton;

namespace image_editor {

class LayerPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void setLayers(const QVector<ImageLayerData>& layers, const QString& selected_layer_id);

signals:
    void layerSelected(const QString& layer_id);
    void layerVisibilityChanged(const QString& layer_id, bool visible);
    void layerRenamed(const QString& layer_id, const QString& name);
    void addLayerRequested();
    void deleteLayerRequested(const QString& layer_id);
    void moveLayerRequested(const QString& layer_id, int direction);
    void opacityEditStarted();
    void layerOpacityChanged(const QString& layer_id, int opacity);
    void opacityEditFinished();

private:
    [[nodiscard]] QString selectedLayerId() const;
    void updateControls();

    QListWidget* layer_list_ = nullptr;
    QLabel* edit_hint_ = nullptr;
    QSlider* opacity_slider_ = nullptr;
    QToolButton* add_button_ = nullptr;
    QToolButton* delete_button_ = nullptr;
    QToolButton* rename_button_ = nullptr;
    QToolButton* move_up_button_ = nullptr;
    QToolButton* move_down_button_ = nullptr;
    QVector<ImageLayerData> layers_;
    bool refreshing_ = false;
};

} // namespace image_editor
