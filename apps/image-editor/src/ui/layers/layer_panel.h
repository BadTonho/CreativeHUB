#pragma once

#include "../../core/document/image_document_store.h"

#include <QHash>
#include <QImage>
#include <QWidget>

class QEvent;
class QLabel;
class QObject;
class QPushButton;
class QSlider;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace image_editor {

class LayerPanel final : public QWidget {
    Q_OBJECT

public:
    static constexpr int kThumbnailWidth = 48;
    static constexpr int kThumbnailHeight = 36;

    explicit LayerPanel(QWidget* parent = nullptr);

    void setDocument(const ImageDocumentData& document,
                     const QString& selected_layer_id,
                     const QString& selected_group_id,
                     const QHash<QString, QImage>& thumbnails);
    void setQuickExportEnabled(bool enabled);

signals:
    void quickExportRequested();
    void layerSelected(const QString& layer_id);
    void groupSelected(const QString& group_id);
    void layerVisibilityChanged(const QString& layer_id, bool visible);
    void groupVisibilityChanged(const QString& group_id, bool visible);
    void layerRenamed(const QString& layer_id, const QString& name);
    void groupRenamed(const QString& group_id, const QString& name);
    void addLayerRequested();
    void addGroupRequested();
    void groupSelectedLayersRequested(const QStringList& layer_ids);
    void deleteLayerRequested(const QString& layer_id);
    void deleteGroupRequested(const QString& group_id);
    void ungroupRequested(const QString& group_id);
    void moveStackItemRequested(const QString& item_id,
                                bool is_group,
                                const QString& target_group_id,
                                qsizetype insertion_index);
    void opacityEditStarted();
    void stackOpacityChanged(const QString& item_id, bool is_group, int opacity);
    void opacityEditFinished();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    [[nodiscard]] QString selectedItemId() const;
    [[nodiscard]] bool selectedItemIsGroup() const;
    [[nodiscard]] bool canGroupSelectedLayers(QStringList* layer_ids = nullptr) const;
    void updateControls();
    void requestMove(const QString& item_id, bool is_group, int direction);

    QPushButton* quick_export_button_ = nullptr;
    QTreeWidget* layer_tree_ = nullptr;
    QLabel* edit_hint_ = nullptr;
    QSlider* opacity_slider_ = nullptr;
    QToolButton* add_button_ = nullptr;
    QToolButton* group_selected_button_ = nullptr;
    QToolButton* delete_button_ = nullptr;
    QToolButton* ungroup_button_ = nullptr;
    QToolButton* rename_button_ = nullptr;
    QToolButton* move_up_button_ = nullptr;
    QToolButton* move_down_button_ = nullptr;
    ImageDocumentData document_;
    bool refreshing_ = false;
    bool eye_press_consumed_ = false;
};

} // namespace image_editor
