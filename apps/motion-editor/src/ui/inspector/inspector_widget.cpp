#include "ui/translation_helpers.h"
#include <QScrollArea>
#include "inspector_widget.h"
#include "mask_inspector_widget.h"

#include "ui/media_pool/media_pool_widget.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace motion::ui {
namespace {

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QColor toQColor(const model::ColorRgba& value)
{
    return QColor(value[0], value[1], value[2], value[3]);
}

void setColorButton(QPushButton* button, const model::ColorRgba& color)
{
    if (button == nullptr) return;
    const QColor value = toQColor(color);
    button->setText(value.name(QColor::HexArgb));
    button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; }")
                              .arg(value.name(QColor::HexArgb)));
}

class ReorderableEffectList final : public QListWidget {
public:
    using Callback = std::function<void(qulonglong, int)>;
    static constexpr auto kMime = "application/x-motion-studio-effect-index";

    explicit ReorderableEffectList(QWidget* parent) : QListWidget(parent) {}
    void setCallback(Callback callback) { callback_ = std::move(callback); }

protected:
    void startDrag(Qt::DropActions) override
    {
        const auto selected = selectedItems();
        if (selected.empty()) return;
        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(kMime),
                      selected.front()->data(Qt::UserRole).toString().toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        (void)drag->exec(Qt::MoveAction, Qt::MoveAction);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(QString::fromLatin1(kMime))) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else QListWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasFormat(QString::fromLatin1(kMime))) {
            QListWidget::dragMoveEvent(event);
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else QListWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!event->mimeData()->hasFormat(QString::fromLatin1(kMime))) {
            QListWidget::dropEvent(event);
            return;
        }
        bool valid = false;
        const auto source = event->mimeData()->data(QString::fromLatin1(kMime))
                                .toULongLong(&valid);
        if (!valid) {
            event->ignore();
            return;
        }
        const auto point = event->position().toPoint();
        const auto target = indexAt(point);
        int insertion = count();
        if (target.isValid()) {
            const auto rect = visualRect(target);
            insertion = target.row() + (point.y() >= rect.center().y() ? 1 : 0);
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
        QTimer::singleShot(0, this, [this, source, insertion] {
            if (callback_) callback_(source, insertion);
        });
    }

private:
    Callback callback_;
};

constexpr std::array<const char*, 5> kTransformObjectNames{{
    "motion-transform-position-x", "motion-transform-position-y",
    "motion-transform-scale", "motion-transform-rotation", "motion-transform-opacity"}};
const std::array<QString, 5> kTransformLabels{{
    MotionUiTranslation::tr("Position X (normalized)"), MotionUiTranslation::tr("Position Y (normalized)"), MotionUiTranslation::tr("Scale X (%)"),
    MotionUiTranslation::tr("Rotation (degrees)"), MotionUiTranslation::tr("Opacity (0-1)")}};

double transformValue(const creative_suite::animation::Transform2D& transform,
                      creative_suite::animation::TransformProperty property)
{
    using creative_suite::animation::TransformProperty;
    switch (property) {
    case TransformProperty::PositionX: return transform.position_x;
    case TransformProperty::PositionY: return transform.position_y;
    case TransformProperty::Scale: return transform.scale;
    case TransformProperty::Rotation: return transform.rotation_degrees;
    case TransformProperty::Opacity: return transform.opacity;
    }
    return 0.0;
}

} // namespace

InspectorWidget::InspectorWidget(QWidget* parent) : QTabWidget(parent)
{
    setObjectName(QStringLiteral("motion-inspector-tabs"));
    setMinimumWidth(250);

    controls_.media_details = new MediaDetailsWidget(this);
    addTab(controls_.media_details, MotionUiTranslation::tr("Media"));

    controls_.layer_content_inspector = new QWidget(this);
    controls_.layer_content_inspector->setObjectName(
        QStringLiteral("motion-layer-content-inspector"));
    auto* content_layout = new QVBoxLayout(controls_.layer_content_inspector);
    auto* content_title = new QLabel(MotionUiTranslation::tr("Selected Layer Content"),
                                     controls_.layer_content_inspector);
    content_title->setObjectName(QStringLiteral("motion-layer-content-title"));
    content_layout->addWidget(content_title);
    controls_.layer_content_pages = new QStackedWidget(controls_.layer_content_inspector);
    controls_.layer_content_pages->setObjectName(QStringLiteral("motion-layer-content-pages"));
    auto* empty_page = new QLabel(
        MotionUiTranslation::tr("Select a text or shape layer to edit its content."),
        controls_.layer_content_pages);
    empty_page->setObjectName(QStringLiteral("motion-layer-content-empty"));
    empty_page->setWordWrap(true);
    empty_page->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    controls_.layer_content_pages->addWidget(empty_page);

    controls_.text_content_page = new QWidget(controls_.layer_content_pages);
    controls_.text_content_page->setObjectName(QStringLiteral("motion-text-content-page"));
    auto* text_layout = new QVBoxLayout(controls_.text_content_page);
    auto* text_form = new QFormLayout;
    controls_.text_content_field = new QTextEdit(controls_.text_content_page);
    controls_.text_content_field->setObjectName(QStringLiteral("motion-text-content"));
    controls_.text_content_field->setAcceptRichText(false);
    controls_.text_content_field->setMaximumHeight(112);
    text_form->addRow(MotionUiTranslation::tr("Text"), controls_.text_content_field);
    controls_.text_font_field = new QFontComboBox(controls_.text_content_page);
    controls_.text_font_field->setObjectName(QStringLiteral("motion-text-font-family"));
    text_form->addRow(MotionUiTranslation::tr("Font"), controls_.text_font_field);
    controls_.text_font_size_field = new QDoubleSpinBox(controls_.text_content_page);
    controls_.text_font_size_field->setObjectName(QStringLiteral("motion-text-font-size"));
    controls_.text_font_size_field->setRange(1, 4096);
    controls_.text_font_size_field->setSuffix(QStringLiteral(" px"));
    text_form->addRow(MotionUiTranslation::tr("Size"), controls_.text_font_size_field);
    controls_.text_color_button = new QPushButton(controls_.text_content_page);
    controls_.text_color_button->setObjectName(QStringLiteral("motion-text-color"));
    text_form->addRow(MotionUiTranslation::tr("Color"), controls_.text_color_button);
    controls_.text_alignment_field = new QComboBox(controls_.text_content_page);
    controls_.text_alignment_field->setObjectName(QStringLiteral("motion-text-alignment"));
    controls_.text_alignment_field->addItem(MotionUiTranslation::tr("Left"),
        static_cast<int>(model::TextAlignment::Left));
    controls_.text_alignment_field->addItem(MotionUiTranslation::tr("Center"),
        static_cast<int>(model::TextAlignment::Center));
    controls_.text_alignment_field->addItem(MotionUiTranslation::tr("Right"),
        static_cast<int>(model::TextAlignment::Right));
    text_form->addRow(MotionUiTranslation::tr("Alignment"), controls_.text_alignment_field);
    controls_.text_box_width_field = new QSpinBox(controls_.text_content_page);
    controls_.text_box_width_field->setObjectName(QStringLiteral("motion-text-box-width"));
    controls_.text_box_width_field->setRange(1, 32768);
    controls_.text_box_width_field->setSuffix(QStringLiteral(" px"));
    text_form->addRow(MotionUiTranslation::tr("Box width"), controls_.text_box_width_field);
    controls_.text_box_height_field = new QSpinBox(controls_.text_content_page);
    controls_.text_box_height_field->setObjectName(QStringLiteral("motion-text-box-height"));
    controls_.text_box_height_field->setRange(1, 32768);
    controls_.text_box_height_field->setSuffix(QStringLiteral(" px"));
    text_form->addRow(MotionUiTranslation::tr("Box height"), controls_.text_box_height_field);
    text_layout->addLayout(text_form);
    text_layout->addStretch(1);
    controls_.layer_content_pages->addWidget(controls_.text_content_page);

    controls_.shape_content_page = new QWidget(controls_.layer_content_pages);
    controls_.shape_content_page->setObjectName(QStringLiteral("motion-shape-content-page"));
    auto* shape_layout = new QVBoxLayout(controls_.shape_content_page);
    auto* shape_form = new QFormLayout;
    controls_.shape_width_field = new QSpinBox(controls_.shape_content_page);
    controls_.shape_width_field->setObjectName(QStringLiteral("motion-shape-width"));
    controls_.shape_width_field->setRange(1, 32768);
    controls_.shape_width_field->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(MotionUiTranslation::tr("Width"), controls_.shape_width_field);
    controls_.shape_height_field = new QSpinBox(controls_.shape_content_page);
    controls_.shape_height_field->setObjectName(QStringLiteral("motion-shape-height"));
    controls_.shape_height_field->setRange(1, 32768);
    controls_.shape_height_field->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(MotionUiTranslation::tr("Height"), controls_.shape_height_field);
    controls_.shape_fill_button = new QPushButton(controls_.shape_content_page);
    controls_.shape_fill_button->setObjectName(QStringLiteral("motion-shape-fill-color"));
    shape_form->addRow(MotionUiTranslation::tr("Fill"), controls_.shape_fill_button);
    controls_.shape_stroke_button = new QPushButton(controls_.shape_content_page);
    controls_.shape_stroke_button->setObjectName(QStringLiteral("motion-shape-stroke-color"));
    shape_form->addRow(MotionUiTranslation::tr("Stroke"), controls_.shape_stroke_button);
    controls_.shape_stroke_width_field = new QSpinBox(controls_.shape_content_page);
    controls_.shape_stroke_width_field->setObjectName(
        QStringLiteral("motion-shape-stroke-width"));
    controls_.shape_stroke_width_field->setRange(0, 4096);
    controls_.shape_stroke_width_field->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(MotionUiTranslation::tr("Stroke width"), controls_.shape_stroke_width_field);
    shape_layout->addLayout(shape_form);
    shape_layout->addStretch(1);
    controls_.layer_content_pages->addWidget(controls_.shape_content_page);
    content_layout->addWidget(controls_.layer_content_pages, 1);
    controls_.layer_content_tab_index = addTab(
        controls_.layer_content_inspector, MotionUiTranslation::tr("Layer"));
    setTabEnabled(controls_.layer_content_tab_index, false);

    controls_.transform_inspector = new QWidget(this);
    controls_.transform_inspector->setObjectName(QStringLiteral("motion-transform-inspector"));
    auto* transform_layout = new QVBoxLayout(controls_.transform_inspector);
    auto* transform_title = new QLabel(MotionUiTranslation::tr("Selected Layer Transform"),
                                       controls_.transform_inspector);
    transform_title->setObjectName(QStringLiteral("motion-transform-inspector-title"));
    transform_layout->addWidget(transform_title);
    auto* transform_form = new QFormLayout;
    controls_.parent_layer = new QComboBox(controls_.transform_inspector);
    controls_.parent_layer->setObjectName(QStringLiteral("motion-parent-layer"));
    transform_form->addRow(MotionUiTranslation::tr("Parent & Link"), controls_.parent_layer);
    connect(controls_.parent_layer, &QComboBox::activated, this, [this](int index) {
        emit parentLayerChanged(controls_.parent_layer->itemData(index).toULongLong());
    });
    for (std::size_t index = 0; index < kTransformObjectNames.size(); ++index) {
        auto* row = new QWidget(controls_.transform_inspector);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);
        auto* field = new QDoubleSpinBox(row);
        field->setObjectName(QString::fromLatin1(kTransformObjectNames[index]));
        field->setDecimals(6);
        field->setKeyboardTracking(false);
        if (index < 2) field->setRange(-1'000'000.0, 1'000'000.0);
        else if (index == 2) field->setRange(0.0001, 100'000'000.0);
        else if (index == 3) field->setRange(-1'000'000'000.0, 1'000'000'000.0);
        else field->setRange(0.0, 1.0);
        controls_.transform_fields[index] = field;
        row_layout->addWidget(field, 1);
        auto* key_button = new QToolButton(row);
        const auto suffix = QString::fromLatin1(kTransformObjectNames[index])
            .mid(QStringLiteral("motion-transform-").size());
        key_button->setObjectName(QStringLiteral("motion-transform-keyframe-%1").arg(suffix));
        key_button->setText(QStringLiteral("◇"));
        key_button->setToolTip(MotionUiTranslation::tr("Add or remove a keyframe at the current frame"));
        controls_.transform_key_buttons[index] = key_button;
        row_layout->addWidget(key_button);
        transform_form->addRow(kTransformLabels[index], row);
        connect(field, &QDoubleSpinBox::valueChanged, this,
                [this, index](double) { if(index==2)emit scaleValueEdited(0);else emit transformValueEdited(static_cast<int>(index)); });
        connect(field, &QDoubleSpinBox::editingFinished,
                this, &InspectorWidget::transformEditFinished);
        connect(key_button, &QToolButton::clicked, this,
                [this, index] { if(index==2)emit scaleKeyframeToggleRequested(0);else emit keyframeToggleRequested(static_cast<int>(index)); });
        if(index==2) {
            controls_.scale_link=new QToolButton(row);controls_.scale_link->setCheckable(true);controls_.scale_link->setText(QStringLiteral("↔"));
            controls_.scale_link->setObjectName(QStringLiteral("motion-scale-link"));
            controls_.scale_link->setAccessibleName(MotionUiTranslation::tr("Link scale axes"));
            controls_.scale_link->setToolTip(MotionUiTranslation::tr("Link scale values while editing; keyframes remain independent."));
            row_layout->addWidget(controls_.scale_link);
            connect(controls_.scale_link,&QToolButton::toggled,this,&InspectorWidget::scaleLinkChanged);
            auto* y_row=new QWidget(controls_.transform_inspector);auto* y_layout=new QHBoxLayout(y_row);y_layout->setContentsMargins(0,0,0,0);
            controls_.scale_y_field=new QDoubleSpinBox(y_row);controls_.scale_y_field->setDecimals(6);controls_.scale_y_field->setRange(.0001,100'000'000.);
            controls_.scale_y_field->setKeyboardTracking(false);controls_.scale_y_field->setObjectName(QStringLiteral("motion-scale-y"));
            y_layout->addWidget(controls_.scale_y_field,1);controls_.scale_y_key=new QToolButton(y_row);controls_.scale_y_key->setText(QStringLiteral("◇"));
            controls_.scale_y_key->setObjectName(QStringLiteral("motion-scale-keyframe-y"));y_layout->addWidget(controls_.scale_y_key);
            transform_form->addRow(MotionUiTranslation::tr("Scale Y (%)"),y_row);
            connect(controls_.scale_y_field,&QDoubleSpinBox::valueChanged,this,[this]{emit scaleValueEdited(1);});
            connect(controls_.scale_y_field,&QDoubleSpinBox::editingFinished,this,&InspectorWidget::transformEditFinished);
            connect(controls_.scale_y_key,&QToolButton::clicked,this,[this]{emit scaleKeyframeToggleRequested(1);});
        }
    }
    for (std::size_t axis=0;axis<2;++axis) {
        auto* row=new QWidget(controls_.transform_inspector);
        auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        auto* field=new QDoubleSpinBox(row);field->setRange(-1e9,1e9);field->setDecimals(6);field->setKeyboardTracking(false);
        field->setObjectName(axis==0?QStringLiteral("motion-anchor-x"):QStringLiteral("motion-anchor-y"));
        field->setToolTip(MotionUiTranslation::tr("Pixels from the fitted layer center, before editable scale and rotation."));
        controls_.anchor_fields[axis]=field;layout->addWidget(field,1);
        auto* key=new QToolButton(row);key->setText(QStringLiteral("◇"));
        key->setObjectName(axis==0?QStringLiteral("motion-anchor-keyframe-x"):QStringLiteral("motion-anchor-keyframe-y"));
        controls_.anchor_key_buttons[axis]=key;layout->addWidget(key);
        transform_form->addRow(axis==0?MotionUiTranslation::tr("Anchor X (px)"):MotionUiTranslation::tr("Anchor Y (px)"),row);
        connect(field,&QDoubleSpinBox::valueChanged,this,[this,axis]{emit anchorValueEdited(static_cast<int>(axis));});
        connect(field,&QDoubleSpinBox::editingFinished,this,&InspectorWidget::transformEditFinished);
        connect(key,&QToolButton::clicked,this,[this,axis]{emit anchorKeyframeToggleRequested(static_cast<int>(axis));});
    }
    transform_layout->addLayout(transform_form);
    controls_.reposition_anchor=new QPushButton(MotionUiTranslation::tr("Reposition Anchor Without Moving..."),controls_.transform_inspector);
    controls_.reposition_anchor->setObjectName(QStringLiteral("motion-reposition-anchor"));
    controls_.reposition_anchor->setEnabled(false);
    connect(controls_.reposition_anchor,&QPushButton::clicked,this,&InspectorWidget::anchorRepositionRequested);
    transform_layout->addWidget(controls_.reposition_anchor);
    transform_layout->addStretch(1);
    addTab(controls_.transform_inspector, MotionUiTranslation::tr("Transform"));

    controls_.effects_inspector = new QWidget(this);
    controls_.effects_inspector->setObjectName(QStringLiteral("motion-effects-inspector"));
    auto* effects_layout = new QVBoxLayout(controls_.effects_inspector);
    auto* toolbar = new QHBoxLayout;
    controls_.add_layer_effect_button = new QPushButton(
        MotionUiTranslation::tr("Add Effect"), controls_.effects_inspector);
    controls_.add_layer_effect_button->setObjectName(QStringLiteral("motion-add-effect"));
    auto* add_menu = new QMenu(controls_.add_layer_effect_button);
    auto* blur_action = add_menu->addAction(MotionUiTranslation::tr("Gaussian Blur"));
    blur_action->setObjectName(QStringLiteral("motion-add-gaussian-blur"));
    auto* color_action = add_menu->addAction(MotionUiTranslation::tr("Color Adjustment"));
    color_action->setObjectName(QStringLiteral("motion-add-color-adjustment"));
    controls_.add_layer_effect_button->setMenu(add_menu);
    toolbar->addWidget(controls_.add_layer_effect_button);
    controls_.effect_up_button = new QPushButton(MotionUiTranslation::tr("Up"), controls_.effects_inspector);
    controls_.effect_up_button->setObjectName(QStringLiteral("motion-effect-up"));
    controls_.effect_down_button = new QPushButton(
        MotionUiTranslation::tr("Down"), controls_.effects_inspector);
    controls_.effect_down_button->setObjectName(QStringLiteral("motion-effect-down"));
    controls_.remove_effect_button = new QPushButton(
        MotionUiTranslation::tr("Remove"), controls_.effects_inspector);
    controls_.remove_effect_button->setObjectName(QStringLiteral("motion-effect-remove"));
    toolbar->addWidget(controls_.effect_up_button);
    toolbar->addWidget(controls_.effect_down_button);
    toolbar->addWidget(controls_.remove_effect_button);
    effects_layout->addLayout(toolbar);
    auto* effect_list = new ReorderableEffectList(controls_.effects_inspector);
    controls_.layer_effect_list = effect_list;
    controls_.layer_effect_list->setObjectName(QStringLiteral("motion-layer-effects"));
    controls_.layer_effect_list->setSelectionMode(QAbstractItemView::SingleSelection);
    controls_.layer_effect_list->setDragEnabled(true);
    controls_.layer_effect_list->setAcceptDrops(true);
    controls_.layer_effect_list->setDropIndicatorShown(true);
    controls_.layer_effect_list->setDragDropMode(QAbstractItemView::InternalMove);
    controls_.layer_effect_list->setDefaultDropAction(Qt::MoveAction);
    controls_.layer_effect_list->setDragDropOverwriteMode(false);
    controls_.layer_effect_list->setToolTip(
        MotionUiTranslation::tr("Drag effects to change their order. Effects apply from top to bottom."));
    effects_layout->addWidget(controls_.layer_effect_list, 1);
    controls_.effect_parameter_pages = new QStackedWidget(controls_.effects_inspector);
    controls_.effect_parameter_pages->setObjectName(QStringLiteral("motion-effect-parameters"));
    auto* no_effect = new QLabel(MotionUiTranslation::tr("Select an effect to edit its parameters."),
                                 controls_.effect_parameter_pages);
    no_effect->setObjectName(QStringLiteral("motion-effect-empty"));
    no_effect->setWordWrap(true);
    no_effect->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    controls_.effect_parameter_pages->addWidget(no_effect);
    auto* blur_page = new QWidget(controls_.effect_parameter_pages);
    blur_page->setObjectName(QStringLiteral("motion-gaussian-blur-parameters"));
    auto* blur_form = new QFormLayout(blur_page);
    const auto make_effect_key_button = [this](QWidget* parent, const QString& object_name,
                                               model::EffectParameterProperty property) {
        auto* button = new QToolButton(parent);
        button->setObjectName(object_name);
        button->setText(QStringLiteral("◇"));
        button->setFixedWidth(26);
        button->setToolTip(
            MotionUiTranslation::tr("Add or remove a keyframe at the current frame"));
        connect(button, &QToolButton::clicked, this, [this, property] {
            emit effectKeyframeToggleRequested(static_cast<int>(property));
        });
        return button;
    };
    const auto make_effect_row = [](QWidget* parent, QDoubleSpinBox* field, QToolButton* button) {
        auto* row = new QWidget(parent);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        layout->addWidget(field, 1);
        layout->addWidget(button);
        return row;
    };
    controls_.blur_radius_field = new QDoubleSpinBox(blur_page);
    controls_.blur_radius_field->setObjectName(QStringLiteral("motion-effect-blur-radius"));
    controls_.blur_radius_field->setRange(0.0, 100.0);
    controls_.blur_radius_field->setDecimals(1);
    controls_.blur_radius_field->setSingleStep(0.5);
    controls_.blur_radius_field->setSuffix(QStringLiteral(" px"));
    controls_.blur_radius_field->setKeyboardTracking(false);
    controls_.blur_radius_key_button = make_effect_key_button(
        blur_page, QStringLiteral("motion-effect-keyframe-blur-radius"),
        model::EffectParameterProperty::GaussianBlurRadius);
    blur_form->addRow(
        MotionUiTranslation::tr("Radius (sigma)"),
        make_effect_row(blur_page, controls_.blur_radius_field, controls_.blur_radius_key_button));
    controls_.effect_parameter_pages->addWidget(blur_page);
    auto* color_page = new QWidget(controls_.effect_parameter_pages);
    color_page->setObjectName(QStringLiteral("motion-color-adjustment-parameters"));
    auto* color_form = new QFormLayout(color_page);
    controls_.effect_brightness_field = new QDoubleSpinBox(color_page);
    controls_.effect_brightness_field->setObjectName(QStringLiteral("motion-effect-brightness"));
    controls_.effect_brightness_field->setRange(-100.0, 100.0);
    controls_.effect_brightness_field->setDecimals(1);
    controls_.effect_brightness_field->setKeyboardTracking(false);
    controls_.effect_brightness_key_button = make_effect_key_button(
        color_page, QStringLiteral("motion-effect-keyframe-brightness"),
        model::EffectParameterProperty::ColorBrightness);
    color_form->addRow(
        MotionUiTranslation::tr("Brightness"),
        make_effect_row(
            color_page, controls_.effect_brightness_field, controls_.effect_brightness_key_button));
    controls_.effect_contrast_field = new QDoubleSpinBox(color_page);
    controls_.effect_contrast_field->setObjectName(QStringLiteral("motion-effect-contrast"));
    controls_.effect_contrast_field->setRange(0.0, 200.0);
    controls_.effect_contrast_field->setDecimals(1);
    controls_.effect_contrast_field->setSuffix(QStringLiteral(" %"));
    controls_.effect_contrast_field->setKeyboardTracking(false);
    controls_.effect_contrast_key_button = make_effect_key_button(
        color_page, QStringLiteral("motion-effect-keyframe-contrast"),
        model::EffectParameterProperty::ColorContrastPercent);
    color_form->addRow(
        MotionUiTranslation::tr("Contrast"),
        make_effect_row(
            color_page, controls_.effect_contrast_field, controls_.effect_contrast_key_button));
    controls_.effect_saturation_field = new QDoubleSpinBox(color_page);
    controls_.effect_saturation_field->setObjectName(QStringLiteral("motion-effect-saturation"));
    controls_.effect_saturation_field->setRange(0.0, 200.0);
    controls_.effect_saturation_field->setDecimals(1);
    controls_.effect_saturation_field->setSuffix(QStringLiteral(" %"));
    controls_.effect_saturation_field->setKeyboardTracking(false);
    controls_.effect_saturation_key_button = make_effect_key_button(
        color_page, QStringLiteral("motion-effect-keyframe-saturation"),
        model::EffectParameterProperty::ColorSaturationPercent);
    color_form->addRow(
        MotionUiTranslation::tr("Saturation"),
        make_effect_row(
            color_page, controls_.effect_saturation_field, controls_.effect_saturation_key_button));
    controls_.effect_parameter_pages->addWidget(color_page);
    effects_layout->addWidget(controls_.effect_parameter_pages);
    controls_.effects_tab_index = addTab(controls_.effects_inspector, MotionUiTranslation::tr("Effects"));
    setTabEnabled(controls_.effects_tab_index, false);

    connect(blur_action, &QAction::triggered, this, [this] { emit effectAddRequested(0); });
    connect(color_action, &QAction::triggered, this, [this] { emit effectAddRequested(1); });
    connect(controls_.effect_up_button, &QPushButton::clicked, this,
            [this] { emit effectMoveRequested(-1); });
    connect(controls_.effect_down_button, &QPushButton::clicked, this,
            [this] { emit effectMoveRequested(1); });
    connect(controls_.remove_effect_button, &QPushButton::clicked, this,
            &InspectorWidget::effectRemoveRequested);
    connect(controls_.layer_effect_list, &QListWidget::currentRowChanged,
            this, &InspectorWidget::effectRowSelected);
    effect_list->setCallback([this](qulonglong source, int insertion) {
        emit effectReorderRequested(source, insertion);
    });
    connect(controls_.layer_effect_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                if (item == nullptr) return;
                emit effectEnabledChanged(controls_.layer_effect_list->row(item),
                                         item->checkState() == Qt::Checked);
            });

    const std::array<QWidget*, 13> focus_fields{{
        controls_.text_content_field, controls_.text_font_field,
        controls_.text_alignment_field, controls_.text_font_size_field,
        controls_.text_box_width_field, controls_.text_box_height_field,
        controls_.shape_width_field, controls_.shape_height_field,
        controls_.shape_stroke_width_field, controls_.blur_radius_field,
        controls_.effect_brightness_field, controls_.effect_contrast_field,
        controls_.effect_saturation_field}};
    for (auto* field : focus_fields) {
        field->installEventFilter(this);
    }
    connect(controls_.text_content_field, &QTextEdit::textChanged,
            this, &InspectorWidget::layerContentEdited);
    connect(controls_.text_font_field, &QFontComboBox::currentFontChanged,
            this, [this](const QFont&) { emit layerContentEdited(); });
    connect(controls_.text_alignment_field, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit layerContentEdited(); });
    connect(controls_.text_font_size_field, &QDoubleSpinBox::valueChanged, this,
        [this](double) { emit layerContentEdited(); });
    connect(controls_.text_font_size_field, &QDoubleSpinBox::editingFinished,
        this, &InspectorWidget::layerContentEditFinished);
    for (auto* field : {controls_.text_box_width_field,
                        controls_.text_box_height_field, controls_.shape_width_field,
                        controls_.shape_height_field, controls_.shape_stroke_width_field}) {
        connect(field, &QSpinBox::valueChanged, this,
                [this](int) { emit layerContentEdited(); });
        connect(field, &QSpinBox::editingFinished,
                this, &InspectorWidget::layerContentEditFinished);
    }
    connect(controls_.text_color_button, &QPushButton::clicked, this,
            [this] { emit layerColorSelectionRequested(true, false); });
    connect(controls_.shape_fill_button, &QPushButton::clicked, this,
            [this] { emit layerColorSelectionRequested(false, false); });
    connect(controls_.shape_stroke_button, &QPushButton::clicked, this,
            [this] { emit layerColorSelectionRequested(false, true); });
    for (auto* field : {controls_.blur_radius_field, controls_.effect_brightness_field,
                        controls_.effect_contrast_field, controls_.effect_saturation_field}) {
        connect(field, &QDoubleSpinBox::valueChanged, this,
                [this](double) { emit effectParametersEdited(); });
        connect(field, &QDoubleSpinBox::editingFinished,
                this, &InspectorWidget::effectParametersEditFinished);
    }
    auto* mask_scroll=new QScrollArea(this);
    mask_scroll->setWidgetResizable(true);mask_scroll->setFrameShape(QFrame::NoFrame);
    masks_=new MaskInspectorWidget(mask_scroll);mask_scroll->setWidget(masks_);
    addTab(mask_scroll,MotionUiTranslation::tr("Masks"));
}

void InspectorWidget::setMedia(const creative_suite::media::MediaItem* item)
{
    controls_.media_details->setMedia(item);
}

void InspectorWidget::selectMediaTab() { setCurrentWidget(controls_.media_details); }
void InspectorWidget::selectLayerTab() { setCurrentWidget(controls_.layer_content_inspector); }
void InspectorWidget::selectTransformTab() { setCurrentWidget(controls_.transform_inspector); }
void InspectorWidget::selectEffectsTab() { setCurrentWidget(controls_.effects_inspector); }
int InspectorWidget::effectCount() const noexcept { return controls_.layer_effect_list->count(); }
double InspectorWidget::transformFieldValue(std::size_t property_index) const noexcept
{
    return property_index < controls_.transform_fields.size()
        ? controls_.transform_fields[property_index]->value()/(property_index==2?100.:1.) : 0.0;
}
double InspectorWidget::effectFieldValue(model::EffectParameterProperty property) const noexcept
{
    switch (property) {
    case model::EffectParameterProperty::GaussianBlurRadius:
        return controls_.blur_radius_field->value();
    case model::EffectParameterProperty::ColorBrightness:
        return controls_.effect_brightness_field->value();
    case model::EffectParameterProperty::ColorContrastPercent:
        return controls_.effect_contrast_field->value();
    case model::EffectParameterProperty::ColorSaturationPercent:
        return controls_.effect_saturation_field->value();
    }
    return 0.0;
}

void InspectorWidget::syncParenting(const model::CompositionDocument* document, model::LayerId selected)
{
    // Playback changes the displayed values, not the parent menu. Avoid
    // rebuilding widgets and descendant maps on every playhead update.
    if (parenting_selected_ == selected && document && parenting_choices_.size() == document->layers().size() &&
        std::equal(parenting_choices_.begin(), parenting_choices_.end(), document->layers().begin(),
            [](const auto& choice, const auto& layer) {
                return choice.id == layer.id && choice.parent == layer.parent_id && choice.name == layer.name;
            })) return;
    parenting_selected_ = selected;
    parenting_choices_.clear();
    if (document) for (const auto& layer : document->layers())
        parenting_choices_.push_back({layer.id,layer.parent_id,layer.name});
    const QSignalBlocker blocker(controls_.parent_layer);
    controls_.parent_layer->clear();
    controls_.parent_layer->addItem(MotionUiTranslation::tr("No parent"), QVariant::fromValue<qulonglong>(0));
    controls_.parent_layer->setEnabled(document != nullptr && selected != 0);
    if (!document || !selected) return;
    const auto candidates = document->eligibleLayerParents(selected);
    const std::unordered_set<model::LayerId> eligible(candidates.begin(), candidates.end());
    model::LayerId parent = 0;
    for (const auto& layer : document->layers()) {
        if (layer.id == selected) parent = layer.parent_id;
        if (!eligible.contains(layer.id)) continue;
        controls_.parent_layer->addItem(MotionUiTranslation::tr("%1 [#%2]")
            .arg(fromUtf8(layer.name), QString::number(static_cast<qulonglong>(layer.id))),
            QVariant::fromValue<qulonglong>(layer.id));
    }
    controls_.parent_layer->setCurrentIndex(controls_.parent_layer->findData(QVariant::fromValue<qulonglong>(parent)));
}

double InspectorWidget::scaleFieldValue(std::size_t axis) const noexcept {
    return (axis==0?controls_.transform_fields[2]->value():controls_.scale_y_field->value())/100.;
}

double InspectorWidget::anchorFieldValue(std::size_t axis) const noexcept
{
    return axis<2?controls_.anchor_fields[axis]->value():0;
}

void InspectorWidget::previewAnchorReposition(const model::AnchorRepositionValues& values)
{
    const std::array anchors{values.anchor.x_pixels, values.anchor.y_pixels};
    const std::array positions{values.position_x, values.position_y};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const QSignalBlocker anchor_block(controls_.anchor_fields[axis]);
        const QSignalBlocker position_block(controls_.transform_fields[axis]);
        controls_.anchor_fields[axis]->setValue(anchors[axis]);
        controls_.transform_fields[axis]->setValue(positions[axis]);
        controls_.anchor_fields[axis]->setReadOnly(true);
        controls_.transform_fields[axis]->setReadOnly(true);
        controls_.anchor_key_buttons[axis]->setEnabled(false);
        controls_.transform_key_buttons[axis]->setEnabled(false);
    }
    controls_.reposition_anchor->setEnabled(false);
}

void InspectorWidget::syncLayer(const model::CompositionLayer* layer,
                                std::int64_t composition_frame,
                                int selected_effect_row)
{
    last_composition_frame_ = composition_frame;
    controls_.transform_inspector->setEnabled(layer != nullptr);
    const auto* text = layer ? std::get_if<model::TextLayerContent>(&layer->content) : nullptr;
    const auto* shape = layer ? std::get_if<model::ShapeLayerContent>(&layer->content) : nullptr;
    const bool content_enabled = text != nullptr || shape != nullptr;
    setTabEnabled(controls_.layer_content_tab_index, content_enabled);
    if (text != nullptr) {
        controls_.layer_content_pages->setCurrentIndex(1);
        const QSignalBlocker a(controls_.text_content_field);
        const QSignalBlocker b(controls_.text_font_field);
        const QSignalBlocker c(controls_.text_font_size_field);
        const QSignalBlocker d(controls_.text_alignment_field);
        const QSignalBlocker e(controls_.text_box_width_field);
        const QSignalBlocker f(controls_.text_box_height_field);
        controls_.text_content_field->setPlainText(fromUtf8(text->text));
        controls_.text_font_field->setCurrentFont(QFont(fromUtf8(text->font_family)));
        controls_.text_font_size_field->setValue(text->font_size_pixels);
        controls_.text_alignment_field->setCurrentIndex(
            controls_.text_alignment_field->findData(static_cast<int>(text->alignment)));
        controls_.text_box_width_field->setValue(text->box_width);
        controls_.text_box_height_field->setValue(text->box_height);
        setColorButton(controls_.text_color_button, text->color);
    } else if (shape != nullptr) {
        controls_.layer_content_pages->setCurrentIndex(2);
        const QSignalBlocker a(controls_.shape_width_field);
        const QSignalBlocker b(controls_.shape_height_field);
        const QSignalBlocker c(controls_.shape_stroke_width_field);
        controls_.shape_width_field->setValue(shape->width);
        controls_.shape_height_field->setValue(shape->height);
        controls_.shape_stroke_width_field->setValue(shape->stroke_width_pixels);
        setColorButton(controls_.shape_fill_button, shape->fill_color);
        setColorButton(controls_.shape_stroke_button, shape->stroke_color);
    } else {
        controls_.layer_content_pages->setCurrentIndex(0);
    }

    const bool has_layer = layer != nullptr && layer->kind != model::LayerKind::Null;
    controls_.effects_inspector->setEnabled(has_layer);
    setTabEnabled(controls_.effects_tab_index, has_layer);
    const QSignalBlocker list_blocker(controls_.layer_effect_list);
    controls_.layer_effect_list->clear();
    if (layer != nullptr) {
        for (std::size_t i = 0; i < layer->effects.size(); ++i) {
            const auto& effect = layer->effects[i];
            const bool enabled = std::visit([](const auto& value) { return value.enabled; }, effect);
            const QString name = std::holds_alternative<model::GaussianBlurEffect>(effect)
                ? MotionUiTranslation::tr("Gaussian Blur") : MotionUiTranslation::tr("Color Adjustment");
            auto* item = new QListWidgetItem(name, controls_.layer_effect_list);
            item->setData(Qt::UserRole, static_cast<qulonglong>(i));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
            item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        }
    }
    int row = selected_effect_row;
    if (layer == nullptr || row < 0 || static_cast<std::size_t>(row) >= layer->effects.size())
        row = -1;
    if (row >= 0) controls_.layer_effect_list->setCurrentRow(row);
    syncSelectedEffect(layer, row);

    if (layer == nullptr) {
        controls_.transform_inspector->setEnabled(false);
        controls_.reposition_anchor->setEnabled(false);
        return;
    }
    const auto raw_local_frame = composition_frame - layer->timeline_start_frame;
    const auto local_frame = layer->duration_frames > 0
        ? std::clamp(raw_local_frame, std::int64_t{0}, layer->duration_frames - 1)
        : std::int64_t{0};
    const auto evaluated = creative_suite::animation::evaluateTransform(
        layer->transform, layer->keyframes, local_frame);
    const bool inside = raw_local_frame >= 0 && raw_local_frame < layer->duration_frames;
    controls_.reposition_anchor->setEnabled(inside);
    const auto anchor=model::evaluateLayerAnchor(*layer,local_frame);
    for(std::size_t axis=0;axis<2;++axis) {
        const auto& keys=layer->anchor_keyframes[axis];
        const bool key_here=inside && std::any_of(keys.begin(),keys.end(),[raw_local_frame](const auto& key){return key.frame==raw_local_frame;});
        const QSignalBlocker blocker(controls_.anchor_fields[axis]);
        controls_.anchor_fields[axis]->setValue(axis==0?anchor.x_pixels:anchor.y_pixels);
        controls_.anchor_fields[axis]->setReadOnly(!keys.empty() && !key_here);
        controls_.anchor_key_buttons[axis]->setEnabled(inside);
        controls_.anchor_key_buttons[axis]->setText(key_here?QStringLiteral("◆"):QStringLiteral("◇"));
        controls_.anchor_key_buttons[axis]->setToolTip(key_here?MotionUiTranslation::tr("Remove the keyframe at the current frame"):MotionUiTranslation::tr("Add a keyframe at the current frame"));
    }
    constexpr std::array properties{
        creative_suite::animation::TransformProperty::PositionX,
        creative_suite::animation::TransformProperty::PositionY,
        creative_suite::animation::TransformProperty::Scale,
        creative_suite::animation::TransformProperty::Rotation,
        creative_suite::animation::TransformProperty::Opacity};
    const auto& y_keys=model::layerScaleKeys(*layer,model::ScaleAxis::Y);
    const bool y_key_here=inside && std::any_of(y_keys.begin(),y_keys.end(),[raw_local_frame](const auto& k){return k.frame==raw_local_frame;});
    const bool y_editable=y_keys.empty() || y_key_here;
    const bool x_key_here=inside && std::any_of(layer->keyframes.scale.begin(),layer->keyframes.scale.end(),[raw_local_frame](const auto& k){return k.frame==raw_local_frame;});
    const bool x_editable=layer->keyframes.scale.empty() || x_key_here;
    {const QSignalBlocker block(controls_.scale_y_field);controls_.scale_y_field->setValue(model::evaluateLayerScale(*layer,local_frame)[1]*100.);
        controls_.scale_y_field->setReadOnly(!y_editable || (layer->scale_linked&&!x_editable));}
    {const QSignalBlocker block(controls_.scale_link);controls_.scale_link->setChecked(layer->scale_linked);}
    controls_.scale_y_key->setEnabled(inside);controls_.scale_y_key->setText(y_key_here?QStringLiteral("◆"):QStringLiteral("◇"));
    controls_.scale_y_key->setToolTip(y_key_here?MotionUiTranslation::tr("Remove the keyframe at the current frame"):MotionUiTranslation::tr("Add a keyframe at the current frame"));
    const auto scale_help=layer->scale_linked && (!x_editable||!y_editable)
        ?MotionUiTranslation::tr("Add a keyframe on each animated axis or unlink the axes to edit this frame.")
        :MotionUiTranslation::tr("100% keeps the fitted size. Linked edits preserve the current proportion.");
    controls_.scale_y_field->setToolTip(scale_help);controls_.transform_fields[2]->setToolTip(scale_help);
    for (std::size_t i = 0; i < controls_.transform_fields.size(); ++i) {
        const auto& keys = creative_suite::animation::keyframesFor(layer->keyframes, properties[i]);
        const bool key_here = inside && std::any_of(keys.begin(), keys.end(),
            [raw_local_frame](const auto& key) { return key.frame == raw_local_frame; });
        const QSignalBlocker blocker(controls_.transform_fields[i]);
        controls_.transform_fields[i]->setValue(transformValue(evaluated, properties[i])*(i==2?100.:1.));
        controls_.transform_fields[i]->setReadOnly((!keys.empty() && !key_here) || (i==2 && layer->scale_linked && !y_editable));
        const bool supported = layer->kind != model::LayerKind::Null || i != 4;
        controls_.transform_fields[i]->setEnabled(supported);
        controls_.transform_key_buttons[i]->setEnabled(inside && supported);
        controls_.transform_key_buttons[i]->setText(key_here ? QStringLiteral("◆")
                                                               : QStringLiteral("◇"));
        controls_.transform_key_buttons[i]->setToolTip(key_here
            ? MotionUiTranslation::tr("Remove the keyframe at the current frame")
            : MotionUiTranslation::tr("Add a keyframe at the current frame"));
    }
}

void InspectorWidget::syncSelectedEffect(const model::CompositionLayer* layer, int row)
{
    const bool valid = layer != nullptr && row >= 0 &&
        static_cast<std::size_t>(row) < layer->effects.size();
    controls_.effect_up_button->setEnabled(valid && row > 0);
    controls_.effect_down_button->setEnabled(valid &&
        static_cast<std::size_t>(row + 1) < layer->effects.size());
    controls_.remove_effect_button->setEnabled(valid);
    controls_.effect_parameter_pages->setEnabled(valid);
    if (!valid) {
        controls_.effect_parameter_pages->setCurrentIndex(0);
        return;
    }
    const auto raw_local_frame = last_composition_frame_ - layer->timeline_start_frame;
    const auto local_frame = layer->duration_frames > 0
        ? std::clamp(raw_local_frame, std::int64_t{0}, layer->duration_frames - 1)
        : std::int64_t{0};
    const bool inside = raw_local_frame >= 0 && raw_local_frame < layer->duration_frames;
    const auto& effect = layer->effects[static_cast<std::size_t>(row)];
    const auto sync_parameter = [&](QDoubleSpinBox* field,
                                    QToolButton* key_button,
                                    model::EffectParameterProperty property) {
        const auto* keys = model::effectKeyframesFor(effect, property);
        const bool has_keys = keys != nullptr && !keys->empty();
        const bool key_here = inside &&
            model::hasEffectKeyframeAtFrame(effect, property, raw_local_frame);
        const QSignalBlocker blocker(field);
        field->setValue(model::evaluateEffectParameter(effect, property, local_frame));
        field->setReadOnly(has_keys && !key_here);
        key_button->setEnabled(inside);
        const QSignalBlocker key_blocker(key_button);
        key_button->setCheckable(true);
        key_button->setChecked(key_here);
        key_button->setText(key_here ? QStringLiteral("◆") : QStringLiteral("◇"));
        key_button->setToolTip(key_here
            ? MotionUiTranslation::tr("Remove the keyframe at the current frame")
            : MotionUiTranslation::tr("Add a keyframe at the current frame"));
    };
    if (std::holds_alternative<model::GaussianBlurEffect>(effect)) {
        controls_.effect_parameter_pages->setCurrentIndex(1);
        sync_parameter(controls_.blur_radius_field,
                       controls_.blur_radius_key_button,
                       model::EffectParameterProperty::GaussianBlurRadius);
    } else {
        controls_.effect_parameter_pages->setCurrentIndex(2);
        sync_parameter(controls_.effect_brightness_field,
                       controls_.effect_brightness_key_button,
                       model::EffectParameterProperty::ColorBrightness);
        sync_parameter(controls_.effect_contrast_field,
                       controls_.effect_contrast_key_button,
                       model::EffectParameterProperty::ColorContrastPercent);
        sync_parameter(controls_.effect_saturation_field,
                       controls_.effect_saturation_key_button,
                       model::EffectParameterProperty::ColorSaturationPercent);
    }
}

model::TextLayerContent InspectorWidget::editedTextContent(model::TextLayerContent value) const
{
    value.text = toUtf8(controls_.text_content_field->toPlainText());
    value.font_family = toUtf8(controls_.text_font_field->currentFont().family());
    value.font_size_pixels = controls_.text_font_size_field->value();
    value.alignment = static_cast<model::TextAlignment>(
        controls_.text_alignment_field->currentData().toInt());
    value.box_width = controls_.text_box_width_field->value();
    value.box_height = controls_.text_box_height_field->value();
    return value;
}

model::ShapeLayerContent InspectorWidget::editedShapeContent(model::ShapeLayerContent value) const
{
    value.width = controls_.shape_width_field->value();
    value.height = controls_.shape_height_field->value();
    value.stroke_width_pixels = controls_.shape_stroke_width_field->value();
    return value;
}

model::LayerEffect InspectorWidget::editedEffect(model::LayerEffect value) const
{
    if (auto* blur = std::get_if<model::GaussianBlurEffect>(&value)) {
        if (blur->radius_keyframes.empty()) {
            blur->radius_pixels = controls_.blur_radius_field->value();
        }
    } else if (auto* color = std::get_if<model::ColorAdjustmentEffect>(&value)) {
        if (color->brightness_keyframes.empty()) {
            color->brightness = controls_.effect_brightness_field->value();
        }
        if (color->contrast_keyframes.empty()) {
            color->contrast_percent = controls_.effect_contrast_field->value();
        }
        if (color->saturation_keyframes.empty()) {
            color->saturation_percent = controls_.effect_saturation_field->value();
        }
    }
    return value;
}

bool InspectorWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::FocusOut) {
        if (watched == controls_.text_content_field || watched == controls_.text_font_field ||
            watched == controls_.text_alignment_field ||
            watched == controls_.text_font_size_field || watched == controls_.text_box_width_field ||
            watched == controls_.text_box_height_field || watched == controls_.shape_width_field ||
            watched == controls_.shape_height_field || watched == controls_.shape_stroke_width_field)
            emit layerContentEditFinished();
        if (watched == controls_.blur_radius_field || watched == controls_.effect_brightness_field ||
            watched == controls_.effect_contrast_field || watched == controls_.effect_saturation_field)
            emit effectParametersEditFinished();
    }
    return QTabWidget::eventFilter(watched, event);
}

} // namespace motion::ui
