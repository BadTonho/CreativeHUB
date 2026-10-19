#pragma once

#include "../../model/composition_document.h"
#include "../../model/anchor_reposition.h"

#include <QTabWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QEvent;
class QObject;

class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSignalBlocker;
class QSpinBox;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QWidget;

namespace creative_suite::media { struct MediaItem; }

namespace motion::ui {

class MediaDetailsWidget;
class MaskInspectorWidget;

class InspectorWidget final : public QTabWidget {
    Q_OBJECT

private:
    struct Controls {
        MediaDetailsWidget* media_details = nullptr;
        QWidget* transform_inspector = nullptr;
        QComboBox* parent_layer = nullptr;
        QComboBox* blend_mode = nullptr;
        QComboBox* matte_source = nullptr;
        QComboBox* matte_mode = nullptr;
        QWidget* effects_inspector = nullptr;
        QListWidget* layer_effect_list = nullptr;
        QPushButton* add_layer_effect_button = nullptr;
        QPushButton* effect_up_button = nullptr;
        QPushButton* effect_down_button = nullptr;
        QPushButton* remove_effect_button = nullptr;
        QStackedWidget* effect_parameter_pages = nullptr;
        QDoubleSpinBox* blur_radius_field = nullptr;
        QToolButton* blur_radius_key_button = nullptr;
        QDoubleSpinBox* effect_brightness_field = nullptr;
        QToolButton* effect_brightness_key_button = nullptr;
        QDoubleSpinBox* effect_contrast_field = nullptr;
        QToolButton* effect_contrast_key_button = nullptr;
        QDoubleSpinBox* effect_saturation_field = nullptr;
        QToolButton* effect_saturation_key_button = nullptr;
        int effects_tab_index = -1;
        QWidget* layer_content_inspector = nullptr;
        QStackedWidget* layer_content_pages = nullptr;
        QWidget* text_content_page = nullptr;
        QWidget* shape_content_page = nullptr;
        QTextEdit* text_content_field = nullptr;
        QFontComboBox* text_font_field = nullptr;
        QDoubleSpinBox* text_font_size_field = nullptr;
        QPushButton* text_color_button = nullptr;
        QComboBox* text_alignment_field = nullptr;
        QSpinBox* text_box_width_field = nullptr;
        QSpinBox* text_box_height_field = nullptr;
        QSpinBox* shape_width_field = nullptr;
        QSpinBox* shape_height_field = nullptr;
        QPushButton* shape_fill_button = nullptr;
        QPushButton* shape_stroke_button = nullptr;
        QSpinBox* shape_stroke_width_field = nullptr;
        int layer_content_tab_index = -1;
        std::array<QDoubleSpinBox*, 5> transform_fields{};
        std::array<QToolButton*, 5> transform_key_buttons{};
        std::array<QDoubleSpinBox*, 2> anchor_fields{};
        std::array<QToolButton*, 2> anchor_key_buttons{};
        QPushButton* reposition_anchor = nullptr;
        QDoubleSpinBox* scale_y_field = nullptr;
        QToolButton* scale_y_key = nullptr;
        QToolButton* scale_link = nullptr;
    };

public:
    explicit InspectorWidget(QWidget* parent = nullptr);

    [[nodiscard]] MaskInspectorWidget* masksInspector() const noexcept { return masks_; }
    void setMedia(const creative_suite::media::MediaItem* item);
    void selectMediaTab();
    void selectLayerTab();
    void selectTransformTab();
    void selectEffectsTab();
    void syncLayer(const model::CompositionLayer* layer,
                   std::int64_t composition_frame,
                   int selected_effect_row);
    void syncSelectedEffect(const model::CompositionLayer* layer, int row);
    void setBlendModeBusy(bool);
    void syncTrackMatte(const model::CompositionDocument*,model::LayerId);
    void syncParenting(const model::CompositionDocument* document, model::LayerId selected);
    // Presentation only; signals are blocked and normal syncLayer restores editability.
    void previewAnchorReposition(const model::AnchorRepositionValues& values);
    void previewLayerPosition(double x,double y);
    void previewLayerRotation(double degrees);
    void previewLayerScale(double x,double y);
    [[nodiscard]] int effectCount() const noexcept;
    [[nodiscard]] double transformFieldValue(std::size_t property_index) const noexcept;
    [[nodiscard]] double anchorFieldValue(std::size_t axis) const noexcept;
    [[nodiscard]] double scaleFieldValue(std::size_t axis) const noexcept;
    [[nodiscard]] double effectFieldValue(model::EffectParameterProperty property) const noexcept;
    [[nodiscard]] model::TextLayerContent editedTextContent(
        model::TextLayerContent value) const;
    [[nodiscard]] model::ShapeLayerContent editedShapeContent(
        model::ShapeLayerContent value) const;
    [[nodiscard]] model::LayerEffect editedEffect(model::LayerEffect value) const;

signals:
    void blendModeChanged(int mode);
    void trackMatteChanged(qulonglong source,int mode);
    void scaleValueEdited(int axis);
    void scaleKeyframeToggleRequested(int axis);
    void scaleLinkChanged(bool linked);
    void anchorValueEdited(int axis);
    void anchorKeyframeToggleRequested(int axis);
    void anchorRepositionRequested();
    void parentLayerChanged(qulonglong parent);
    void layerContentEdited();
    void layerContentEditFinished();
    void layerColorSelectionRequested(bool text_color, bool stroke_color);
    void transformValueEdited(int property_index);
    void transformEditFinished();
    void keyframeToggleRequested(int property_index);
    void effectAddRequested(int kind);
    void effectMoveRequested(int direction);
    void effectRemoveRequested();
    void effectRowSelected(int row);
    void effectEnabledChanged(int row, bool enabled);
    void effectParametersEdited();
    void effectParametersEditFinished();
    void effectKeyframeToggleRequested(int property_index);
    void effectReorderRequested(qulonglong source_index, int insertion_row);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ParentChoice {
        model::LayerId id, parent;
        std::string name;
    };
    std::optional<model::LayerId> parenting_selected_;
    std::vector<ParentChoice> parenting_choices_;
    bool blend_mode_busy_=false, blend_layer_available_=false, matte_has_source_=false;
    Controls controls_;
    MaskInspectorWidget* masks_=nullptr;
    std::int64_t last_composition_frame_ = 0;
};

} // namespace motion::ui
