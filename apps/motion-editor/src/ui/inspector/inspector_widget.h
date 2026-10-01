#pragma once

#include "../../model/composition_document.h"

#include <QTabWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

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

class InspectorWidget final : public QTabWidget {
    Q_OBJECT

private:
    struct Controls {
        MediaDetailsWidget* media_details = nullptr;
        QWidget* transform_inspector = nullptr;
        QWidget* effects_inspector = nullptr;
        QListWidget* layer_effect_list = nullptr;
        QPushButton* add_layer_effect_button = nullptr;
        QPushButton* effect_up_button = nullptr;
        QPushButton* effect_down_button = nullptr;
        QPushButton* remove_effect_button = nullptr;
        QStackedWidget* effect_parameter_pages = nullptr;
        QDoubleSpinBox* blur_radius_field = nullptr;
        QDoubleSpinBox* effect_brightness_field = nullptr;
        QDoubleSpinBox* effect_contrast_field = nullptr;
        QDoubleSpinBox* effect_saturation_field = nullptr;
        int effects_tab_index = -1;
        QWidget* layer_content_inspector = nullptr;
        QStackedWidget* layer_content_pages = nullptr;
        QWidget* text_content_page = nullptr;
        QWidget* shape_content_page = nullptr;
        QTextEdit* text_content_field = nullptr;
        QFontComboBox* text_font_field = nullptr;
        QSpinBox* text_font_size_field = nullptr;
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
    };

public:
    explicit InspectorWidget(QWidget* parent = nullptr);

    void setMedia(const creative_suite::media::MediaItem* item);
    void selectMediaTab();
    void selectLayerTab();
    void selectTransformTab();
    void selectEffectsTab();
    void syncLayer(const model::CompositionLayer* layer,
                   std::int64_t composition_frame,
                   int selected_effect_row);
    void syncSelectedEffect(const model::CompositionLayer* layer, int row);
    [[nodiscard]] int effectCount() const noexcept;
    [[nodiscard]] double transformFieldValue(std::size_t property_index) const noexcept;
    [[nodiscard]] model::TextLayerContent editedTextContent(
        model::TextLayerContent value) const;
    [[nodiscard]] model::ShapeLayerContent editedShapeContent(
        model::ShapeLayerContent value) const;
    [[nodiscard]] model::LayerEffect editedEffect(model::LayerEffect value) const;

signals:
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
    void effectReorderRequested(qulonglong source_index, int insertion_row);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Controls controls_;
};

} // namespace motion::ui
