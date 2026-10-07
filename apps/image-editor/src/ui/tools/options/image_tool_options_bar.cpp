#include "image_tool_options_bar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>

namespace image_editor {

ImageToolOptionsBar::ImageToolOptionsBar(QWidget* parent)
    : QToolBar(QStringLiteral("Tool Options"), parent) {
    setObjectName(QStringLiteral("toolOptionsToolBar"));
    setMovable(false);
    setFloatable(false);
    setAllowedAreas(Qt::TopToolBarArea);
    setMinimumHeight(40);

    paint_size_options_ = new QWidget(this);
    paint_size_options_->setObjectName(QStringLiteral("paintBrushSizeOptions"));
    paint_size_options_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* options_layout = new QHBoxLayout(paint_size_options_);
    options_layout->setContentsMargins(8, 3, 8, 3);
    options_layout->setSpacing(8);

    tool_size_label_ = new QLabel(QStringLiteral("Brush Size"), paint_size_options_);
    tool_size_label_->setObjectName(QStringLiteral("paintBrushSizeLabel"));
    options_layout->addWidget(tool_size_label_);

    brush_size_slider_ = new QSlider(Qt::Horizontal, paint_size_options_);
    brush_size_slider_->setObjectName(QStringLiteral("paintBrushSizeSlider"));
    brush_size_slider_->setAccessibleName(QStringLiteral("Brush size"));
    brush_size_slider_->setRange(1, ImageDocumentStore::kMaximumPaintBrushDiameter);
    brush_size_slider_->setValue(12);
    brush_size_slider_->setMinimumWidth(140);
    brush_size_slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    options_layout->addWidget(brush_size_slider_, 1);

    brush_size_spin_ = new QSpinBox(paint_size_options_);
    brush_size_spin_->setObjectName(QStringLiteral("paintBrushSizeSpinBox"));
    brush_size_spin_->setAccessibleName(QStringLiteral("Brush size in pixels"));
    brush_size_spin_->setRange(1, ImageDocumentStore::kMaximumPaintBrushDiameter);
    brush_size_spin_->setValue(12);
    brush_size_spin_->setSuffix(QStringLiteral(" px"));
    brush_size_spin_->setFixedWidth(96);
    options_layout->addWidget(brush_size_spin_);

    eraser_preview_check_ = new QCheckBox(QStringLiteral("Preview"), paint_size_options_);
    eraser_preview_check_->setObjectName(QStringLiteral("eraserPreviewCheckBox"));
    eraser_preview_check_->setToolTip(
        QStringLiteral("Show a translucent erase preview and apply it when the stroke ends"));
    eraser_preview_check_->setChecked(false);
    eraser_preview_check_->setVisible(false);
    options_layout->addWidget(eraser_preview_check_);

    paint_options_action_ = new QWidgetAction(this);
    paint_options_action_->setObjectName(QStringLiteral("paintBrushSizeAction"));
    paint_options_action_->setDefaultWidget(paint_size_options_);
    addAction(paint_options_action_);
    paint_options_action_->setVisible(false);

    shape_options_widget_ = new QWidget(this);
    shape_options_widget_->setObjectName(QStringLiteral("shapeOptionsWidget"));
    auto* shape_layout = new QHBoxLayout(shape_options_widget_);
    shape_layout->setContentsMargins(8, 3, 8, 3);
    shape_layout->setSpacing(7);
    shape_stroke_check_ = new QCheckBox(QStringLiteral("Stroke"), shape_options_widget_);
    shape_stroke_check_->setObjectName(QStringLiteral("shapeStrokeCheckBox"));
    shape_stroke_check_->setChecked(true);
    shape_layout->addWidget(shape_stroke_check_);
    shape_stroke_color_button_ = new QPushButton(QStringLiteral("Color"), shape_options_widget_);
    shape_stroke_color_button_->setObjectName(QStringLiteral("shapeStrokeColorButton"));
    shape_stroke_color_button_->setFixedWidth(62);
    shape_layout->addWidget(shape_stroke_color_button_);
    shape_fill_check_ = new QCheckBox(QStringLiteral("Fill"), shape_options_widget_);
    shape_fill_check_->setObjectName(QStringLiteral("shapeFillCheckBox"));
    shape_fill_check_->setChecked(true);
    shape_layout->addWidget(shape_fill_check_);
    shape_fill_color_button_ = new QPushButton(QStringLiteral("Color"), shape_options_widget_);
    shape_fill_color_button_->setObjectName(QStringLiteral("shapeFillColorButton"));
    shape_fill_color_button_->setFixedWidth(62);
    shape_layout->addWidget(shape_fill_color_button_);
    shape_layout->addWidget(new QLabel(QStringLiteral("Width"), shape_options_widget_));
    shape_stroke_width_spin_ = new QSpinBox(shape_options_widget_);
    shape_stroke_width_spin_->setObjectName(QStringLiteral("shapeStrokeWidthSpinBox"));
    shape_stroke_width_spin_->setRange(1, ImageDocumentStore::kMaximumShapeStrokeWidth);
    shape_stroke_width_spin_->setValue(2);
    shape_stroke_width_spin_->setSuffix(QStringLiteral(" px"));
    shape_stroke_width_spin_->setFixedWidth(82);
    shape_layout->addWidget(shape_stroke_width_spin_);
    shape_options_action_ = new QWidgetAction(this);
    shape_options_action_->setObjectName(QStringLiteral("shapeOptionsAction"));
    shape_options_action_->setDefaultWidget(shape_options_widget_);
    addAction(shape_options_action_);
    shape_options_action_->setVisible(false);

    text_options_widget_ = new QWidget(this);
    text_options_widget_->setObjectName(QStringLiteral("textOptionsWidget"));
    auto* text_layout = new QHBoxLayout(text_options_widget_);
    text_layout->setContentsMargins(8, 3, 8, 3);
    text_layout->setSpacing(7);
    text_font_combo_ = new QFontComboBox(text_options_widget_);
    text_font_combo_->setObjectName(QStringLiteral("textFontComboBox"));
    text_font_combo_->setAccessibleName(QStringLiteral("Text font family"));
    text_font_combo_->setMinimumWidth(150);
    text_layout->addWidget(text_font_combo_);
    text_size_spin_ = new QSpinBox(text_options_widget_);
    text_size_spin_->setObjectName(QStringLiteral("textSizeSpinBox"));
    text_size_spin_->setAccessibleName(QStringLiteral("Text size in pixels"));
    text_size_spin_->setRange(1, ImageDocumentStore::kMaximumTextFontPixelSize);
    text_size_spin_->setValue(ImageTextData{}.font_pixel_size);
    text_size_spin_->setSuffix(QStringLiteral(" px"));
    text_size_spin_->setFixedWidth(88);
    text_layout->addWidget(text_size_spin_);
    text_color_button_ = new QPushButton(QStringLiteral("Color"), text_options_widget_);
    text_color_button_->setObjectName(QStringLiteral("textColorButton"));
    text_color_button_->setAccessibleName(QStringLiteral("Text color"));
    text_color_button_->setFixedWidth(70);
    text_layout->addWidget(text_color_button_);
    text_alignment_combo_ = new QComboBox(text_options_widget_);
    text_alignment_combo_->setObjectName(QStringLiteral("textAlignmentComboBox"));
    text_alignment_combo_->setAccessibleName(QStringLiteral("Text alignment"));
    text_alignment_combo_->addItem(QStringLiteral("Left"),
        static_cast<int>(ImageTextAlignment::Left));
    text_alignment_combo_->addItem(QStringLiteral("Center"),
        static_cast<int>(ImageTextAlignment::Center));
    text_alignment_combo_->addItem(QStringLiteral("Right"),
        static_cast<int>(ImageTextAlignment::Right));
    text_layout->addWidget(text_alignment_combo_);
    text_options_action_ = new QWidgetAction(this);
    text_options_action_->setObjectName(QStringLiteral("textOptionsAction"));
    text_options_action_->setDefaultWidget(text_options_widget_);
    addAction(text_options_action_);
    text_options_action_->setVisible(false);

    selection_options_widget_ = new QWidget(this);
    selection_options_widget_->setObjectName(QStringLiteral("selectionOptionsWidget"));
    auto* selection_layout = new QHBoxLayout(selection_options_widget_);
    selection_layout->setContentsMargins(8, 3, 8, 3);
    delete_selected_objects_button_ = new QPushButton(
        QStringLiteral("Delete Selected Objects"), selection_options_widget_);
    delete_selected_objects_button_->setObjectName(QStringLiteral("deleteSelectedShapeButton"));
    delete_selected_objects_button_->setAccessibleName(QStringLiteral("Delete selected objects"));
    selection_layout->addWidget(delete_selected_objects_button_);
    selection_options_action_ = new QWidgetAction(this);
    selection_options_action_->setObjectName(QStringLiteral("selectionOptionsAction"));
    selection_options_action_->setDefaultWidget(selection_options_widget_);
    addAction(selection_options_action_);
    selection_options_action_->setVisible(false);

    area_selection_options_widget_ = new QWidget(this);
    area_selection_options_widget_->setObjectName(QStringLiteral("areaSelectionOptionsWidget"));
    auto* area_layout = new QHBoxLayout(area_selection_options_widget_);
    area_layout->setContentsMargins(8, 3, 8, 3);
    area_layout->setSpacing(7);
    area_selection_shape_label_ = new QLabel(QStringLiteral("Shape"), area_selection_options_widget_);
    area_layout->addWidget(area_selection_shape_label_);
    area_selection_shape_combo_ = new QComboBox(area_selection_options_widget_);
    area_selection_shape_combo_->setObjectName(QStringLiteral("areaSelectionShapeComboBox"));
    area_selection_shape_combo_->setAccessibleName(QStringLiteral("Area selection shape"));
    area_selection_shape_combo_->addItem(QStringLiteral("Rectangle"), 0);
    area_selection_shape_combo_->addItem(QStringLiteral("Ellipse"), 1);
    area_layout->addWidget(area_selection_shape_combo_);
    area_layout->addWidget(new QLabel(QStringLiteral("Mode"), area_selection_options_widget_));
    area_selection_mode_combo_ = new QComboBox(area_selection_options_widget_);
    area_selection_mode_combo_->setObjectName(QStringLiteral("areaSelectionModeComboBox"));
    area_selection_mode_combo_->setAccessibleName(QStringLiteral("Area selection mode"));
    area_selection_mode_combo_->addItem(QStringLiteral("Replace"), 0);
    area_selection_mode_combo_->addItem(QStringLiteral("Add"), 1);
    area_selection_mode_combo_->addItem(QStringLiteral("Subtract"), 2);
    area_layout->addWidget(area_selection_mode_combo_);
    area_selection_options_action_ = new QWidgetAction(this);
    area_selection_options_action_->setObjectName(QStringLiteral("areaSelectionOptionsAction"));
    area_selection_options_action_->setDefaultWidget(area_selection_options_widget_);
    addAction(area_selection_options_action_);
    area_selection_options_action_->setVisible(false);

    bucket_fill_options_widget_ = new QWidget(this);
    bucket_fill_options_widget_->setObjectName(QStringLiteral("bucketFillOptionsWidget"));
    auto* bucket_fill_layout = new QHBoxLayout(bucket_fill_options_widget_);
    bucket_fill_layout->setContentsMargins(8, 3, 8, 3);
    bucket_fill_layout->setSpacing(7);
    bucket_fill_layout->addWidget(new QLabel(QStringLiteral("Tolerance"), bucket_fill_options_widget_));
    bucket_fill_tolerance_spin_ = new QSpinBox(bucket_fill_options_widget_);
    bucket_fill_tolerance_spin_->setObjectName(QStringLiteral("bucketFillToleranceSpinBox"));
    bucket_fill_tolerance_spin_->setAccessibleName(QStringLiteral("Bucket fill tolerance"));
    bucket_fill_tolerance_spin_->setRange(0, 255);
    bucket_fill_tolerance_spin_->setValue(0);
    bucket_fill_tolerance_spin_->setFixedWidth(72);
    bucket_fill_layout->addWidget(bucket_fill_tolerance_spin_);
    bucket_fill_options_action_ = new QWidgetAction(this);
    bucket_fill_options_action_->setObjectName(QStringLiteral("bucketFillOptionsAction"));
    bucket_fill_options_action_->setDefaultWidget(bucket_fill_options_widget_);
    addAction(bucket_fill_options_action_);
    bucket_fill_options_action_->setVisible(false);

    blur_options_widget_ = new QWidget(this);
    blur_options_widget_->setObjectName(QStringLiteral("blurOptionsWidget"));
    auto* blur_layout = new QHBoxLayout(blur_options_widget_);
    blur_layout->setContentsMargins(8, 3, 8, 3);
    blur_layout->setSpacing(7);
    blur_layout->addWidget(new QLabel(QStringLiteral("Brush Size"), blur_options_widget_));
    blur_diameter_spin_ = new QSpinBox(blur_options_widget_);
    blur_diameter_spin_->setObjectName(QStringLiteral("blurBrushSizeSpinBox"));
    blur_diameter_spin_->setAccessibleName(QStringLiteral("Blur brush size in pixels"));
    blur_diameter_spin_->setRange(1, ImageDocumentStore::kMaximumPaintBrushDiameter);
    blur_diameter_spin_->setValue(12);
    blur_diameter_spin_->setSuffix(QStringLiteral(" px"));
    blur_diameter_spin_->setFixedWidth(96);
    blur_layout->addWidget(blur_diameter_spin_);
    blur_layout->addWidget(new QLabel(QStringLiteral("Radius"), blur_options_widget_));
    blur_radius_spin_ = new QSpinBox(blur_options_widget_);
    blur_radius_spin_->setObjectName(QStringLiteral("blurRadiusSpinBox"));
    blur_radius_spin_->setAccessibleName(QStringLiteral("Blur radius in pixels"));
    blur_radius_spin_->setRange(0, ImageDocumentStore::kMaximumBlurRadius);
    blur_radius_spin_->setValue(10);
    blur_radius_spin_->setSuffix(QStringLiteral(" px"));
    blur_radius_spin_->setFixedWidth(88);
    blur_layout->addWidget(blur_radius_spin_);
    blur_options_action_ = new QWidgetAction(this);
    blur_options_action_->setObjectName(QStringLiteral("blurOptionsAction"));
    blur_options_action_->setDefaultWidget(blur_options_widget_);
    addAction(blur_options_action_);
    blur_options_action_->setVisible(false);

    connect(brush_size_slider_, &QSlider::valueChanged,
            brush_size_spin_, &QSpinBox::setValue);
    connect(brush_size_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int diameter) {
                if (brush_size_slider_->value() != diameter) brush_size_slider_->setValue(diameter);
                emit brushDiameterChanged(diameter);
            });
    connect(eraser_preview_check_, &QCheckBox::toggled,
            this, &ImageToolOptionsBar::eraserPreviewToggled);
    connect(shape_stroke_check_, &QCheckBox::toggled,
            this, &ImageToolOptionsBar::shapeStrokeToggled);
    connect(shape_fill_check_, &QCheckBox::toggled,
            this, &ImageToolOptionsBar::shapeFillToggled);
    connect(shape_stroke_color_button_, &QPushButton::clicked,
            this, &ImageToolOptionsBar::shapeStrokeColorRequested);
    connect(shape_fill_color_button_, &QPushButton::clicked,
            this, &ImageToolOptionsBar::shapeFillColorRequested);
    connect(shape_stroke_width_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ImageToolOptionsBar::shapeStrokeWidthChanged);
    connect(text_font_combo_, &QFontComboBox::currentFontChanged, this,
            [this](const QFont& font) { emit textFontChanged(font.family()); });
    connect(text_size_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ImageToolOptionsBar::textSizeChanged);
    connect(text_color_button_, &QPushButton::clicked,
            this, &ImageToolOptionsBar::textColorRequested);
    connect(text_alignment_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                emit textAlignmentChanged(text_alignment_combo_->itemData(index).toInt());
            });
    const auto emitAreaSelectionOptions = [this]() {
        emit areaSelectionOptionsChanged(area_selection_shape_combo_->currentData().toInt(),
                                         area_selection_mode_combo_->currentData().toInt());
    };
    connect(area_selection_shape_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [emitAreaSelectionOptions](int) { emitAreaSelectionOptions(); });
    connect(area_selection_mode_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [emitAreaSelectionOptions](int) { emitAreaSelectionOptions(); });
    connect(delete_selected_objects_button_, &QPushButton::clicked,
            this, &ImageToolOptionsBar::deleteSelectedObjectsRequested);
    connect(bucket_fill_tolerance_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ImageToolOptionsBar::bucketFillToleranceChanged);
    connect(blur_diameter_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ImageToolOptionsBar::blurDiameterChanged);
    connect(blur_radius_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ImageToolOptionsBar::blurRadiusChanged);
}

void ImageToolOptionsBar::setBrushOptionsState(bool visible, int diameter,
                                               bool eraser_active, bool mask_editing,
                                               bool eraser_preview_enabled) {
    paint_options_action_->setVisible(visible);
    paint_size_options_->setVisible(visible);
    paint_size_options_->setEnabled(visible);
    tool_size_label_->setText(mask_editing ? QStringLiteral("Mask Brush Size")
        : (eraser_active ? QStringLiteral("Eraser Size") : QStringLiteral("Brush Size")));
    brush_size_slider_->setAccessibleName(
        eraser_active ? QStringLiteral("Eraser size") : QStringLiteral("Brush size"));
    brush_size_spin_->setAccessibleName(
        eraser_active ? QStringLiteral("Eraser size in pixels")
                      : QStringLiteral("Brush size in pixels"));
    eraser_preview_check_->setVisible(visible && eraser_active && !mask_editing);
    {
        const QSignalBlocker slider_blocker(brush_size_slider_);
        const QSignalBlocker spin_blocker(brush_size_spin_);
        brush_size_slider_->setValue(diameter);
        brush_size_spin_->setValue(diameter);
    }
    {
        const QSignalBlocker preview_blocker(eraser_preview_check_);
        eraser_preview_check_->setChecked(eraser_preview_enabled);
    }
}

void ImageToolOptionsBar::setBrushDiameter(int diameter) {
    brush_size_spin_->setValue(diameter);
}

void ImageToolOptionsBar::setShapeOptionsVisible(bool visible) {
    shape_options_action_->setVisible(visible);
    shape_options_widget_->setVisible(visible);
}

void ImageToolOptionsBar::setShapeOptionsState(const ImageShapeData& style,
                                               bool stroke_fill_enabled,
                                               bool stroke_color_enabled,
                                               bool fill_color_enabled,
                                               bool stroke_width_enabled) {
    {
        const QSignalBlocker stroke_blocker(shape_stroke_check_);
        const QSignalBlocker fill_blocker(shape_fill_check_);
        const QSignalBlocker width_blocker(shape_stroke_width_spin_);
        shape_stroke_check_->setChecked(style.stroke_enabled);
        shape_fill_check_->setChecked(style.fill_enabled);
        shape_stroke_width_spin_->setValue(style.stroke_width);
    }
    shape_stroke_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
        style.stroke_color.name(QColor::HexArgb)));
    shape_stroke_color_button_->setToolTip(style.stroke_color.name(QColor::HexArgb));
    shape_fill_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
        style.fill_color.name(QColor::HexArgb)));
    shape_fill_color_button_->setToolTip(style.fill_color.name(QColor::HexArgb));
    shape_stroke_check_->setEnabled(stroke_fill_enabled);
    shape_fill_check_->setEnabled(stroke_fill_enabled);
    shape_stroke_color_button_->setEnabled(stroke_color_enabled);
    shape_fill_color_button_->setEnabled(fill_color_enabled);
    shape_stroke_width_spin_->setEnabled(stroke_width_enabled);
}

void ImageToolOptionsBar::setTextOptionsVisible(bool visible) {
    text_options_action_->setVisible(visible);
    text_options_widget_->setVisible(visible);
}

void ImageToolOptionsBar::setTextOptionsState(const ImageTextData& style) {
    {
        const QSignalBlocker font_blocker(text_font_combo_);
        const QSignalBlocker size_blocker(text_size_spin_);
        const QSignalBlocker alignment_blocker(text_alignment_combo_);
        text_font_combo_->setCurrentFont(QFont(style.font_family));
        text_size_spin_->setValue(style.font_pixel_size);
        const int alignment_index = text_alignment_combo_->findData(
            static_cast<int>(style.alignment));
        if (alignment_index >= 0) text_alignment_combo_->setCurrentIndex(alignment_index);
    }
    text_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
        style.color.name(QColor::HexArgb)));
    text_color_button_->setToolTip(style.color.name(QColor::HexArgb));
}

void ImageToolOptionsBar::setSelectionOptionsVisible(bool visible) {
    selection_options_action_->setVisible(visible);
    selection_options_widget_->setVisible(visible);
}

void ImageToolOptionsBar::setDeleteSelectedObjectsEnabled(bool enabled) {
    delete_selected_objects_button_->setEnabled(enabled);
}

void ImageToolOptionsBar::setAreaSelectionOptionsState(
    bool visible, int shape, int mode, bool show_shape) {
    area_selection_options_action_->setVisible(visible);
    area_selection_options_widget_->setVisible(visible);
    area_selection_shape_label_->setVisible(show_shape);
    area_selection_shape_combo_->setVisible(show_shape);
    {
        const QSignalBlocker shape_blocker(area_selection_shape_combo_);
        const QSignalBlocker mode_blocker(area_selection_mode_combo_);
        const int shape_index = area_selection_shape_combo_->findData(shape);
        const int mode_index = area_selection_mode_combo_->findData(mode);
        if (shape_index >= 0) area_selection_shape_combo_->setCurrentIndex(shape_index);
        if (mode_index >= 0) area_selection_mode_combo_->setCurrentIndex(mode_index);
    }
}

void ImageToolOptionsBar::setBucketFillOptionsState(bool visible, int tolerance) {
    bucket_fill_options_action_->setVisible(visible);
    bucket_fill_options_widget_->setVisible(visible);
    const QSignalBlocker blocker(bucket_fill_tolerance_spin_);
    bucket_fill_tolerance_spin_->setValue(std::clamp(tolerance, 0, 255));
}

void ImageToolOptionsBar::setBlurOptionsState(bool visible, int diameter, int radius) {
    blur_options_action_->setVisible(visible);
    blur_options_widget_->setVisible(visible);
    const QSignalBlocker diameter_blocker(blur_diameter_spin_);
    const QSignalBlocker radius_blocker(blur_radius_spin_);
    blur_diameter_spin_->setValue(std::clamp(
        diameter, 1, ImageDocumentStore::kMaximumPaintBrushDiameter));
    blur_radius_spin_->setValue(std::clamp(
        radius, 0, ImageDocumentStore::kMaximumBlurRadius));
}

void ImageToolOptionsBar::setBlurDiameter(int diameter) {
    blur_diameter_spin_->setValue(std::clamp(
        diameter, 1, ImageDocumentStore::kMaximumPaintBrushDiameter));
}

void ImageToolOptionsBar::hideAllOptions() {
    setBrushOptionsState(false, brush_size_spin_->value(), false, false,
                         eraser_preview_check_->isChecked());
    setShapeOptionsVisible(false);
    setShapeOptionsState(ImageShapeData{}, false, false, false, false);
    setTextOptionsVisible(false);
    setTextOptionsState(ImageTextData{});
    setSelectionOptionsVisible(false);
    setAreaSelectionOptionsState(false, 0, 0);
    setBucketFillOptionsState(false, bucket_fill_tolerance_spin_->value());
    setBlurOptionsState(false, blur_diameter_spin_->value(), blur_radius_spin_->value());
}

} // namespace image_editor
