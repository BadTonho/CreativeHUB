#pragma once

#include "../../core/document/image_document_store.h"

#include <QHash>
#include <QImage>
#include <QWidget>
#include <array>
#include <cstdint>

class QAction;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QEvent;
class QLabel;
class QMenu;
class QObject;
class QPushButton;
class QSlider;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace image_editor {

class GradientMapEditor;
class BrightnessContrastEditor;
class ExposureOffsetGammaEditor;
class HueSaturationEditor;
class ColorBalanceEditor;
class LevelsEditor;
class CurvesEditor;
class LayerPanel final : public QWidget {
    Q_OBJECT

public:
    static constexpr int kThumbnailWidth = 48;
    static constexpr int kThumbnailHeight = 36;

    explicit LayerPanel(QWidget* parent = nullptr);

    void setDocument(const ImageDocumentData& document,
                     const QString& selected_layer_id,
                     const QString& selected_group_id,
                     const QHash<QString, QImage>& thumbnails,
                     const QHash<QString, QImage>& mask_thumbnails = {},
                     const QString& selected_mask_id = {},
                     const QHash<QString, QString>& source_problems = {},
                     const QVector<ImageStackItemData>& selected_stack_items = {});
    void setSelectedMask(const QString& layer_id);
    // Completed rendering must preserve items held by editors and context menus.
    void setThumbnails(const QHash<QString, QImage>& thumbnails,
                       const QHash<QString, QImage>& mask_thumbnails);
    void setQuickExportEnabled(bool enabled);
    void setLevelsHistogram(const std::array<std::uint64_t, 256>& bins);
    void setCurvesHistogram(const std::array<std::uint64_t, 256>& bins);
    [[nodiscard]] QVector<ImageStackItemData> selectedStackItems() const;
    void requestDeleteSelection();
    void finishOutlineEdit();
    [[nodiscard]] QAction* clippingAction() const { return clipping_action_; }

signals:
    void quickExportRequested();
    void addGradientMapRequested();
    void addHueSaturationRequested();
    void addColorBalanceRequested();
    void colorBalanceChanged(const QString&, const creative_suite::effects::ColorBalanceParameters&);
    void hueSaturationChanged(const QString&, const creative_suite::effects::HueSaturationParameters&);
    void addLevelsRequested();
    void levelsChanged(const QString&, const creative_suite::effects::LevelsParameters&);
    void addCurvesRequested();
    void curvesChanged(const QString&, const creative_suite::effects::CurvesParameters&);
    void addExposureOffsetGammaRequested();
    void exposureOffsetGammaChanged(const QString&, const creative_suite::effects::ExposureOffsetGammaParameters&);
    void addBrightnessContrastRequested();
    void brightnessContrastChanged(const QString&,const creative_suite::effects::BrightnessContrastParameters&);
    void adjustmentEditStarted();
    void adjustmentEditFinished();
    void gradientMapChanged(const QString&,const creative_suite::effects::GradientMapParameters&);
    void stackBlendModeChanged(const QString& id, bool group, int mode);
    void layerClippingChanged(const QString& id, bool enabled);
    void layerSelected(const QString& layer_id);
    void groupSelected(const QString& group_id);
    void layerMaskSelected(const QString& layer_id);
    void addLayerMaskRequested(const QString& layer_id);
    void removeLayerMaskRequested(const QString& layer_id);
    void layerMaskEnabledChanged(const QString& layer_id, bool enabled);
    void layerVisibilityChanged(const QString& layer_id, bool visible);
    void groupVisibilityChanged(const QString& group_id, bool visible);
    void layerRenamed(const QString& layer_id, const QString& name);
    void groupRenamed(const QString& group_id, const QString& name);
    void addLayerRequested();
    void addGroupRequested();
    void groupSelectedLayersRequested(const QStringList& layer_ids);
    void deleteStackItemsRequested(const QVector<image_editor::ImageStackItemData>& items);
    void deletionSelectionChanged();
    void ungroupRequested(const QString& group_id);
    void moveStackItemRequested(const QString& item_id,
                                bool is_group,
                                const QString& target_group_id,
                                qsizetype insertion_index);
    void outlineEditStarted();
    void outlineEditFinished();
    void layerOutlineChanged(const QString& id, const image_editor::ImageLayerOutlineData& outline, bool remove);
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
    QMenu* layer_context_menu_ = nullptr;
    HueSaturationEditor* hue_editor_ = nullptr;
    ColorBalanceEditor* color_balance_editor_ = nullptr;
    QAction* add_color_balance_action_ = nullptr;
    QAction* add_hue_action_ = nullptr;
    LevelsEditor* levels_editor_ = nullptr;
    QAction* add_levels_action_ = nullptr;
    CurvesEditor* curves_editor_ = nullptr;
    QAction* add_curves_action_ = nullptr;
    ExposureOffsetGammaEditor* exposure_editor_ = nullptr;
    QAction* add_exposure_action_ = nullptr;
    BrightnessContrastEditor* brightness_editor_ = nullptr;
    QAction* add_brightness_action_ = nullptr;
    GradientMapEditor* gradient_editor_ = nullptr;
    QAction* add_gradient_action_ = nullptr;
    QAction* clipping_action_ = nullptr;
    QLabel* edit_hint_ = nullptr;
    QSlider* opacity_slider_ = nullptr;
    QComboBox* blend_mode_ = nullptr;
    QToolButton* add_button_ = nullptr;
    QToolButton* group_selected_button_ = nullptr;
    QToolButton* delete_button_ = nullptr;
    QToolButton* ungroup_button_ = nullptr;
    QToolButton* rename_button_ = nullptr;
    QToolButton* move_up_button_ = nullptr;
    QToolButton* move_down_button_ = nullptr;
    ImageDocumentData document_;
    QCheckBox* outline_enabled_ = nullptr;
    QSpinBox* outline_thickness_ = nullptr;
    QComboBox* outline_position_ = nullptr;
    QPushButton* outline_color_ = nullptr;
    QPushButton* outline_remove_ = nullptr;
    bool outline_editing_ = false;
    bool refreshing_ = false;
    void beginOutlineEdit();
    void emitOutlineChange();
    bool eye_press_consumed_ = false;
};

} // namespace image_editor
