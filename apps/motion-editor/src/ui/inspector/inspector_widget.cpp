#include "inspector_widget.h"

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
constexpr std::array<const char*, 5> kTransformLabels{{
    "Position X (normalized)", "Position Y (normalized)", "Scale",
    "Rotation (degrees)", "Opacity (0-1)"}};

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
    addTab(controls_.media_details, QStringLiteral("Media"));

    controls_.layer_content_inspector = new QWidget(this);
    controls_.layer_content_inspector->setObjectName(
        QStringLiteral("motion-layer-content-inspector"));
    auto* content_layout = new QVBoxLayout(controls_.layer_content_inspector);
    auto* content_title = new QLabel(QStringLiteral("Selected Layer Content"),
                                     controls_.layer_content_inspector);
    content_title->setObjectName(QStringLiteral("motion-layer-content-title"));
    content_layout->addWidget(content_title);
    controls_.layer_content_pages = new QStackedWidget(controls_.layer_content_inspector);
    controls_.layer_content_pages->setObjectName(QStringLiteral("motion-layer-content-pages"));
    auto* empty_page = new QLabel(
        QStringLiteral("Select a text or shape layer to edit its content."),
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
    text_form->addRow(QStringLiteral("Text"), controls_.text_content_field);
    controls_.text_font_field = new QFontComboBox(controls_.text_content_page);
    controls_.text_font_field->setObjectName(QStringLiteral("motion-text-font-family"));
    text_form->addRow(QStringLiteral("Font"), controls_.text_font_field);
    controls_.text_font_size_field = new QSpinBox(controls_.text_content_page);
    controls_.text_font_size_field->setObjectName(QStringLiteral("motion-text-font-size"));
    controls_.text_font_size_field->setRange(1, 4096);
    controls_.text_font_size_field->setSuffix(QStringLiteral(" px"));
    text_form->addRow(QStringLiteral("Size"), controls_.text_font_size_field);
    controls_.text_color_button = new QPushButton(controls_.text_content_page);
    controls_.text_color_button->setObjectName(QStringLiteral("motion-text-color"));
    text_form->addRow(QStringLiteral("Color"), controls_.text_color_button);
    controls_.text_alignment_field = new QComboBox(controls_.text_content_page);
    controls_.text_alignment_field->setObjectName(QStringLiteral("motion-text-alignment"));
    controls_.text_alignment_field->addItem(QStringLiteral("Left"),
        static_cast<int>(model::TextAlignment::Left));
    controls_.text_alignment_field->addItem(QStringLiteral("Center"),
        static_cast<int>(model::TextAlignment::Center));
    controls_.text_alignment_field->addItem(QStringLiteral("Right"),
        static_cast<int>(model::TextAlignment::Right));
    text_form->addRow(QStringLiteral("Alignment"), controls_.text_alignment_field);
    controls_.text_box_width_field = new QSpinBox(controls_.text_content_page);
    controls_.text_box_width_field->setObjectName(QStringLiteral("motion-text-box-width"));
    controls_.text_box_width_field->setRange(1, 32768);
    controls_.text_box_width_field->setSuffix(QStringLiteral(" px"));
    text_form->addRow(QStringLiteral("Box width"), controls_.text_box_width_field);
    controls_.text_box_height_field = new QSpinBox(controls_.text_content_page);
    controls_.text_box_height_field->setObjectName(QStringLiteral("motion-text-box-height"));
    controls_.text_box_height_field->setRange(1, 32768);
    controls_.text_box_height_field->setSuffix(QStringLiteral(" px"));
    text_form->addRow(QStringLiteral("Box height"), controls_.text_box_height_field);
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
    shape_form->addRow(QStringLiteral("Width"), controls_.shape_width_field);
    controls_.shape_height_field = new QSpinBox(controls_.shape_content_page);
    controls_.shape_height_field->setObjectName(QStringLiteral("motion-shape-height"));
    controls_.shape_height_field->setRange(1, 32768);
    controls_.shape_height_field->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(QStringLiteral("Height"), controls_.shape_height_field);
    controls_.shape_fill_button = new QPushButton(controls_.shape_content_page);
    controls_.shape_fill_button->setObjectName(QStringLiteral("motion-shape-fill-color"));
    shape_form->addRow(QStringLiteral("Fill"), controls_.shape_fill_button);
    controls_.shape_stroke_button = new QPushButton(controls_.shape_content_page);
    controls_.shape_stroke_button->setObjectName(QStringLiteral("motion-shape-stroke-color"));
    shape_form->addRow(QStringLiteral("Stroke"), controls_.shape_stroke_button);
    controls_.shape_stroke_width_field = new QSpinBox(controls_.shape_content_page);
    controls_.shape_stroke_width_field->setObjectName(
        QStringLiteral("motion-shape-stroke-width"));
    controls_.shape_stroke_width_field->setRange(0, 4096);
    controls_.shape_stroke_width_field->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(QStringLiteral("Stroke width"), controls_.shape_stroke_width_field);
    shape_layout->addLayout(shape_form);
    shape_layout->addStretch(1);
    controls_.layer_content_pages->addWidget(controls_.shape_content_page);
    content_layout->addWidget(controls_.layer_content_pages, 1);
    controls_.layer_content_tab_index = addTab(
        controls_.layer_content_inspector, QStringLiteral("Layer"));
    setTabEnabled(controls_.layer_content_tab_index, false);

    controls_.transform_inspector = new QWidget(this);
    controls_.transform_inspector->setObjectName(QStringLiteral("motion-transform-inspector"));
    auto* transform_layout = new QVBoxLayout(controls_.transform_inspector);
    auto* transform_title = new QLabel(QStringLiteral("Selected Layer Transform"),
                                       controls_.transform_inspector);
    transform_title->setObjectName(QStringLiteral("motion-transform-inspector-title"));
    transform_layout->addWidget(transform_title);
    auto* transform_form = new QFormLayout;
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
        else if (index == 2) field->setRange(0.000001, 1'000'000.0);
        else if (index == 3) field->setRange(-1'000'000'000.0, 1'000'000'000.0);
        else field->setRange(0.0, 1.0);
        controls_.transform_fields[index] = field;
        row_layout->addWidget(field, 1);
        auto* key_button = new QToolButton(row);
        const auto suffix = QString::fromLatin1(kTransformObjectNames[index])
            .mid(QStringLiteral("motion-transform-").size());
        key_button->setObjectName(QStringLiteral("motion-transform-keyframe-%1").arg(suffix));
        key_button->setText(QStringLiteral("◇"));
        key_button->setToolTip(QStringLiteral("Add or remove a keyframe at the current frame"));
        controls_.transform_key_buttons[index] = key_button;
        row_layout->addWidget(key_button);
        transform_form->addRow(QString::fromLatin1(kTransformLabels[index]), row);
        connect(field, &QDoubleSpinBox::valueChanged, this,
                [this, index](double) { emit transformValueEdited(static_cast<int>(index)); });
        connect(field, &QDoubleSpinBox::editingFinished,
                this, &InspectorWidget::transformEditFinished);
        connect(key_button, &QToolButton::clicked, this,
                [this, index] { emit keyframeToggleRequested(static_cast<int>(index)); });
    }
    transform_layout->addLayout(transform_form);
    transform_layout->addStretch(1);
    addTab(controls_.transform_inspector, QStringLiteral("Transform"));

    controls_.effects_inspector = new QWidget(this);
    controls_.effects_inspector->setObjectName(QStringLiteral("motion-effects-inspector"));
    auto* effects_layout = new QVBoxLayout(controls_.effects_inspector);
    auto* toolbar = new QHBoxLayout;
    controls_.add_layer_effect_button = new QPushButton(
        QStringLiteral("Add Effect"), controls_.effects_inspector);
    controls_.add_layer_effect_button->setObjectName(QStringLiteral("motion-add-effect"));
    auto* add_menu = new QMenu(controls_.add_layer_effect_button);
    auto* blur_action = add_menu->addAction(QStringLiteral("Gaussian Blur"));
    blur_action->setObjectName(QStringLiteral("motion-add-gaussian-blur"));
    auto* color_action = add_menu->addAction(QStringLiteral("Color Adjustment"));
    color_action->setObjectName(QStringLiteral("motion-add-color-adjustment"));
    controls_.add_layer_effect_button->setMenu(add_menu);
    toolbar->addWidget(controls_.add_layer_effect_button);
    controls_.effect_up_button = new QPushButton(QStringLiteral("Up"), controls_.effects_inspector);
    controls_.effect_up_button->setObjectName(QStringLiteral("motion-effect-up"));
    controls_.effect_down_button = new QPushButton(
        QStringLiteral("Down"), controls_.effects_inspector);
    controls_.effect_down_button->setObjectName(QStringLiteral("motion-effect-down"));
    controls_.remove_effect_button = new QPushButton(
        QStringLiteral("Remove"), controls_.effects_inspector);
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
    controls_.layer_effect_list->setToolTip(QStringLiteral(
        "Drag effects to change their order. Effects apply from top to bottom."));
    effects_layout->addWidget(controls_.layer_effect_list, 1);
    controls_.effect_parameter_pages = new QStackedWidget(controls_.effects_inspector);
    controls_.effect_parameter_pages->setObjectName(QStringLiteral("motion-effect-parameters"));
    auto* no_effect = new QLabel(QStringLiteral("Select an effect to edit its parameters."),
                                 controls_.effect_parameter_pages);
    no_effect->setObjectName(QStringLiteral("motion-effect-empty"));
    no_effect->setWordWrap(true);
    no_effect->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    controls_.effect_parameter_pages->addWidget(no_effect);
    auto* blur_page = new QWidget(controls_.effect_parameter_pages);
    blur_page->setObjectName(QStringLiteral("motion-gaussian-blur-parameters"));
    auto* blur_form = new QFormLayout(blur_page);
    controls_.blur_radius_field = new QDoubleSpinBox(blur_page);
    controls_.blur_radius_field->setObjectName(QStringLiteral("motion-effect-blur-radius"));
    controls_.blur_radius_field->setRange(0.0, 100.0);
    controls_.blur_radius_field->setDecimals(1);
    controls_.blur_radius_field->setSingleStep(0.5);
    controls_.blur_radius_field->setSuffix(QStringLiteral(" px"));
    controls_.blur_radius_field->setKeyboardTracking(false);
    blur_form->addRow(QStringLiteral("Radius (sigma)"), controls_.blur_radius_field);
    controls_.effect_parameter_pages->addWidget(blur_page);
    auto* color_page = new QWidget(controls_.effect_parameter_pages);
    color_page->setObjectName(QStringLiteral("motion-color-adjustment-parameters"));
    auto* color_form = new QFormLayout(color_page);
    controls_.effect_brightness_field = new QDoubleSpinBox(color_page);
    controls_.effect_brightness_field->setObjectName(QStringLiteral("motion-effect-brightness"));
    controls_.effect_brightness_field->setRange(-100.0, 100.0);
    controls_.effect_brightness_field->setDecimals(1);
    controls_.effect_brightness_field->setKeyboardTracking(false);
    color_form->addRow(QStringLiteral("Brightness"), controls_.effect_brightness_field);
    controls_.effect_contrast_field = new QDoubleSpinBox(color_page);
    controls_.effect_contrast_field->setObjectName(QStringLiteral("motion-effect-contrast"));
    controls_.effect_contrast_field->setRange(0.0, 200.0);
    controls_.effect_contrast_field->setDecimals(1);
    controls_.effect_contrast_field->setSuffix(QStringLiteral(" %"));
    controls_.effect_contrast_field->setKeyboardTracking(false);
    color_form->addRow(QStringLiteral("Contrast"), controls_.effect_contrast_field);
    controls_.effect_saturation_field = new QDoubleSpinBox(color_page);
    controls_.effect_saturation_field->setObjectName(QStringLiteral("motion-effect-saturation"));
    controls_.effect_saturation_field->setRange(0.0, 200.0);
    controls_.effect_saturation_field->setDecimals(1);
    controls_.effect_saturation_field->setSuffix(QStringLiteral(" %"));
    controls_.effect_saturation_field->setKeyboardTracking(false);
    color_form->addRow(QStringLiteral("Saturation"), controls_.effect_saturation_field);
    controls_.effect_parameter_pages->addWidget(color_page);
    effects_layout->addWidget(controls_.effect_parameter_pages);
    controls_.effects_tab_index = addTab(controls_.effects_inspector, QStringLiteral("Effects"));
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
    for (auto* field : {controls_.text_font_size_field, controls_.text_box_width_field,
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
        ? controls_.transform_fields[property_index]->value() : 0.0;
}

void InspectorWidget::syncLayer(const model::CompositionLayer* layer,
                                std::int64_t composition_frame,
                                int selected_effect_row)
{
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

    const bool has_layer = layer != nullptr;
    controls_.effects_inspector->setEnabled(has_layer);
    setTabEnabled(controls_.effects_tab_index, has_layer);
    const QSignalBlocker list_blocker(controls_.layer_effect_list);
    controls_.layer_effect_list->clear();
    if (layer != nullptr) {
        for (std::size_t i = 0; i < layer->effects.size(); ++i) {
            const auto& effect = layer->effects[i];
            const bool enabled = std::visit([](const auto& value) { return value.enabled; }, effect);
            const QString name = std::holds_alternative<model::GaussianBlurEffect>(effect)
                ? QStringLiteral("Gaussian Blur") : QStringLiteral("Color Adjustment");
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
        return;
    }
    const auto raw_local_frame = composition_frame - layer->timeline_start_frame;
    const auto local_frame = layer->duration_frames > 0
        ? std::clamp(raw_local_frame, std::int64_t{0}, layer->duration_frames - 1)
        : std::int64_t{0};
    const auto evaluated = creative_suite::animation::evaluateTransform(
        layer->transform, layer->keyframes, local_frame);
    const bool inside = raw_local_frame >= 0 && raw_local_frame < layer->duration_frames;
    constexpr std::array properties{
        creative_suite::animation::TransformProperty::PositionX,
        creative_suite::animation::TransformProperty::PositionY,
        creative_suite::animation::TransformProperty::Scale,
        creative_suite::animation::TransformProperty::Rotation,
        creative_suite::animation::TransformProperty::Opacity};
    for (std::size_t i = 0; i < controls_.transform_fields.size(); ++i) {
        const auto& keys = creative_suite::animation::keyframesFor(layer->keyframes, properties[i]);
        const bool key_here = inside && std::any_of(keys.begin(), keys.end(),
            [raw_local_frame](const auto& key) { return key.frame == raw_local_frame; });
        const QSignalBlocker blocker(controls_.transform_fields[i]);
        controls_.transform_fields[i]->setValue(transformValue(evaluated, properties[i]));
        controls_.transform_fields[i]->setReadOnly(!keys.empty() && !key_here);
        controls_.transform_key_buttons[i]->setEnabled(inside);
        controls_.transform_key_buttons[i]->setText(key_here ? QStringLiteral("◆")
                                                               : QStringLiteral("◇"));
        controls_.transform_key_buttons[i]->setToolTip(key_here
            ? QStringLiteral("Remove the keyframe at the current frame")
            : QStringLiteral("Add a keyframe at the current frame"));
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
    const auto& effect = layer->effects[static_cast<std::size_t>(row)];
    if (const auto* blur = std::get_if<model::GaussianBlurEffect>(&effect)) {
        controls_.effect_parameter_pages->setCurrentIndex(1);
        const QSignalBlocker blocker(controls_.blur_radius_field);
        controls_.blur_radius_field->setValue(blur->radius_pixels);
    } else {
        const auto& color = std::get<model::ColorAdjustmentEffect>(effect);
        controls_.effect_parameter_pages->setCurrentIndex(2);
        const QSignalBlocker a(controls_.effect_brightness_field);
        const QSignalBlocker b(controls_.effect_contrast_field);
        const QSignalBlocker c(controls_.effect_saturation_field);
        controls_.effect_brightness_field->setValue(color.brightness);
        controls_.effect_contrast_field->setValue(color.contrast_percent);
        controls_.effect_saturation_field->setValue(color.saturation_percent);
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
        blur->radius_pixels = controls_.blur_radius_field->value();
    } else if (auto* color = std::get_if<model::ColorAdjustmentEffect>(&value)) {
        color->brightness = controls_.effect_brightness_field->value();
        color->contrast_percent = controls_.effect_contrast_field->value();
        color->saturation_percent = controls_.effect_saturation_field->value();
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
