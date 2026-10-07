#pragma once

#include "image_document_store.h"

#include <QToolBar>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QWidget;
class QWidgetAction;

namespace image_editor {

class ImageToolOptionsBar final : public QToolBar {
    Q_OBJECT

public:
    explicit ImageToolOptionsBar(QWidget* parent = nullptr);

    void setBrushOptionsState(bool visible, int diameter, bool eraser_active,
                              bool mask_editing, bool eraser_preview_enabled);
    void setBrushDiameter(int diameter);
    void setShapeOptionsVisible(bool visible);
    void setShapeOptionsState(const ImageShapeData& style,
                              bool stroke_fill_enabled, bool stroke_color_enabled,
                              bool fill_color_enabled, bool stroke_width_enabled);
    void setTextOptionsVisible(bool visible);
    void setTextOptionsState(const ImageTextData& style);
    void setSelectionOptionsVisible(bool visible);
    void setDeleteSelectedObjectsEnabled(bool enabled);
    void setAreaSelectionOptionsState(bool visible, int shape, int mode);
    void setBucketFillOptionsState(bool visible, int tolerance);
    void setBlurOptionsState(bool visible, int diameter, int radius);
    void setBlurDiameter(int diameter);
    void hideAllOptions();

signals:
    void brushDiameterChanged(int diameter);
    void eraserPreviewToggled(bool enabled);
    void shapeStrokeToggled(bool enabled);
    void shapeFillToggled(bool enabled);
    void shapeStrokeColorRequested();
    void shapeFillColorRequested();
    void shapeStrokeWidthChanged(int width);
    void textFontChanged(const QString& family);
    void textSizeChanged(int size);
    void textColorRequested();
    void textAlignmentChanged(int alignment);
    void areaSelectionOptionsChanged(int shape, int mode);
    void deleteSelectedObjectsRequested();
    void bucketFillToleranceChanged(int tolerance);
    void blurDiameterChanged(int diameter);
    void blurRadiusChanged(int radius);

private:
    QWidgetAction* paint_options_action_ = nullptr;
    QWidgetAction* shape_options_action_ = nullptr;
    QWidgetAction* text_options_action_ = nullptr;
    QWidgetAction* selection_options_action_ = nullptr;
    QWidgetAction* area_selection_options_action_ = nullptr;
    QWidgetAction* bucket_fill_options_action_ = nullptr;
    QWidgetAction* blur_options_action_ = nullptr;
    QWidget* paint_size_options_ = nullptr;
    QWidget* shape_options_widget_ = nullptr;
    QWidget* text_options_widget_ = nullptr;
    QWidget* selection_options_widget_ = nullptr;
    QWidget* area_selection_options_widget_ = nullptr;
    QWidget* bucket_fill_options_widget_ = nullptr;
    QWidget* blur_options_widget_ = nullptr;
    QLabel* tool_size_label_ = nullptr;
    QSlider* brush_size_slider_ = nullptr;
    QSpinBox* brush_size_spin_ = nullptr;
    QCheckBox* eraser_preview_check_ = nullptr;
    QCheckBox* shape_stroke_check_ = nullptr;
    QCheckBox* shape_fill_check_ = nullptr;
    QPushButton* shape_stroke_color_button_ = nullptr;
    QPushButton* shape_fill_color_button_ = nullptr;
    QSpinBox* shape_stroke_width_spin_ = nullptr;
    QPushButton* delete_selected_objects_button_ = nullptr;
    QFontComboBox* text_font_combo_ = nullptr;
    QSpinBox* text_size_spin_ = nullptr;
    QPushButton* text_color_button_ = nullptr;
    QComboBox* text_alignment_combo_ = nullptr;
    QComboBox* area_selection_shape_combo_ = nullptr;
    QComboBox* area_selection_mode_combo_ = nullptr;
    QSpinBox* bucket_fill_tolerance_spin_ = nullptr;
    QSpinBox* blur_diameter_spin_ = nullptr;
    QSpinBox* blur_radius_spin_ = nullptr;
};

} // namespace image_editor
