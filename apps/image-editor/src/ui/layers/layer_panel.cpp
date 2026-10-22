#include "../../core/document/image_layer_clipping.h"
#include "layer_panel.h"
#include "gradient_map_editor.h"
#include "brightness_contrast_editor.h"
#include "exposure_offset_gamma_editor.h"
#include "hue_saturation_editor.h"
#include "levels_editor.h"
#include "../translation_helpers.h"

#include "../transparency_checkerboard.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include <QCoreApplication>
namespace image_editor {
namespace {

constexpr int kItemIdRole = Qt::UserRole + 1;
constexpr int kGroupRole = Qt::UserRole + 2;
constexpr int kBackgroundRole = Qt::UserRole + 3;
constexpr int kVisibleRole = Qt::UserRole + 4;
constexpr int kThumbnailRole = Qt::UserRole + 5;
constexpr int kParentGroupRole = Qt::UserRole + 6;
constexpr int kMaskThumbnailRole = Qt::UserRole + 7;
constexpr int kMaskSelectedRole = Qt::UserRole + 8;
constexpr int kMaskEnabledRole = Qt::UserRole + 9;
constexpr int kSourceProblemRole = Qt::UserRole + 10;
constexpr int kRasterObjectRole = Qt::UserRole + 11;
constexpr int kHasMaskRole = Qt::UserRole + 12;
constexpr int kClippedRole = Qt::UserRole + 13;
constexpr int kLayerRowHeight = 46;

QRect maskThumbnailRect(const QRect& row) {
    return QRect(row.left() + 60, row.center().y() - 18, 36, 36);
}

QRect eyeButtonRect(const QRect& row) {
    return QRect(row.right() - 32, row.center().y() - 13, 26, 26);
}

QRect eyeHitRect(const QRect& row) {
    return QRect(row.right() - 36, row.top(), 36, row.height());
}

void drawCheckerboard(QPainter* painter, const QRect& rect) {
    constexpr int cell_size = 8;
    const QColor light = QColor::fromRgba(ui::kTransparencyCheckerLight);
    const QColor dark = QColor::fromRgba(ui::kTransparencyCheckerDark);
    painter->fillRect(rect, dark);
    for (int y = 0; y < rect.height(); y += cell_size) {
        for (int x = 0; x < rect.width(); x += cell_size) {
            if (((x / cell_size) + (y / cell_size)) % 2 == 0) {
                painter->fillRect(QRect(rect.left() + x, rect.top() + y,
                                        cell_size, cell_size).intersected(rect), light);
            }
        }
    }
}

class LayerItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(kLayerRowHeight);
        return size;
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem background_option(option);
        initStyleOption(&background_option, index);
        background_option.text.clear();
        background_option.icon = {};
        const QStyle* style = option.widget != nullptr
            ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background_option, painter,
                           option.widget);

        painter->save();
        const QRect row = option.rect;
        const QRect thumbnail_rect(row.left() + 6,
                                   row.center().y() - LayerPanel::kThumbnailHeight / 2,
                                   LayerPanel::kThumbnailWidth,
                                   LayerPanel::kThumbnailHeight);
        drawCheckerboard(painter, thumbnail_rect);
        const QImage thumbnail = index.data(kThumbnailRole).value<QImage>();
        if (!thumbnail.isNull()) {
            const QSize fitted = thumbnail.size().scaled(thumbnail_rect.size(),
                                                          Qt::KeepAspectRatio);
            const QRect image_rect(QPoint(thumbnail_rect.center().x() - fitted.width() / 2,
                                          thumbnail_rect.center().y() - fitted.height() / 2),
                                   fitted);
            painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter->drawImage(image_rect, thumbnail);
        }
        painter->setPen(option.palette.color(QPalette::Mid));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(thumbnail_rect.adjusted(0, 0, -1, -1));

        const QImage mask_thumbnail = index.data(kMaskThumbnailRole).value<QImage>();
        int content_right = thumbnail_rect.right();
        if (index.data(kHasMaskRole).toBool()) {
            const QRect mask_rect = maskThumbnailRect(row);
            painter->fillRect(mask_rect, Qt::black);
            const QSize fitted = mask_thumbnail.size().scaled(mask_rect.size(), Qt::KeepAspectRatio);
            painter->drawImage(QRect(QPoint(mask_rect.center().x() - fitted.width() / 2,
                                           mask_rect.center().y() - fitted.height() / 2), fitted),
                               mask_thumbnail);
            painter->setPen(QPen(index.data(kMaskSelectedRole).toBool()
                ? option.palette.color(QPalette::Highlight) : option.palette.color(QPalette::Mid),
                index.data(kMaskSelectedRole).toBool() ? 3 : 1));
            painter->drawRect(mask_rect.adjusted(0, 0, -1, -1));
            if (!index.data(kMaskEnabledRole).toBool()) {
                painter->setPen(QPen(QColor(230, 70, 70), 2));
                painter->drawLine(mask_rect.topLeft(), mask_rect.bottomRight());
            }
            content_right = mask_rect.right();
        }
        const QRect text_rect(content_right + 9, row.top(),
                              eyeHitRect(row).left() - content_right - 15,
                              row.height());
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool source_problem = index.data(kSourceProblemRole).toBool();
        painter->setPen(source_problem ? QColor(230, 170, 80) : option.palette.color(selected ? QPalette::HighlightedText
                                                       : QPalette::Text));
        const QString text = QFontMetrics(option.font).elidedText(
            (index.data(kClippedRole).toBool() ? QString(QChar(0x21b3)) + " " : QString{}) + index.data(Qt::DisplayRole).toString() + (source_problem
                ? QCoreApplication::translate("ImageEditorUI", " [Image unavailable]")
                : QString{}), Qt::ElideRight,
            std::max(0, text_rect.width()));
        painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, text);

        const QRect button = eyeButtonRect(row);
        painter->setPen(QPen(option.palette.color(QPalette::Mid), 1));
        painter->setBrush(option.palette.color(QPalette::Button));
        painter->drawRoundedRect(button, 3, 3);
        const QPointF center = button.center();
        const bool visible = index.data(kVisibleRole).toBool();
        const QColor icon_color = visible
            ? option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text)
            : option.palette.color(QPalette::Mid);
        painter->setPen(QPen(icon_color, 1.6, Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        QPainterPath eye;
        eye.moveTo(center.x() - 8, center.y());
        eye.cubicTo(center.x() - 4, center.y() - 6,
                    center.x() + 4, center.y() - 6,
                    center.x() + 8, center.y());
        eye.cubicTo(center.x() + 4, center.y() + 6,
                    center.x() - 4, center.y() + 6,
                    center.x() - 8, center.y());
        painter->drawPath(eye);
        if (visible) {
            painter->setBrush(icon_color);
            painter->drawEllipse(center, 2.2, 2.2);
        } else {
            painter->drawLine(QPointF(center.x() - 7, center.y() + 8),
                              QPointF(center.x() + 7, center.y() - 8));
        }
        painter->restore();
    }
};

class LayerTreeWidget final : public QTreeWidget {
public:
    using DropIndicatorPosition = QAbstractItemView::DropIndicatorPosition;
    using DropHandler = std::function<void(const QString&, bool, QTreeWidgetItem*,
                                           DropIndicatorPosition)>;
    using QTreeWidget::QTreeWidget;
    DropHandler drop_handler;
    static bool isOnItem(DropIndicatorPosition position) {
        return position == OnItem;
    }
    static bool isAboveItem(DropIndicatorPosition position) {
        return position == AboveItem;
    }

protected:
    void startDrag(Qt::DropActions supported_actions) override {
        auto* source = currentItem();
        dragged_item_id_ = source == nullptr
            ? QString{} : source->data(0, kItemIdRole).toString();
        dragged_item_is_group_ = source != nullptr &&
            source->data(0, kGroupRole).toBool();
        drag_source_active_ = !dragged_item_id_.isEmpty();

        // The document/window handles the move and rebuilds this tree during
        // the drop callback. QAbstractItemView::startDrag() removes the source
        // row again after a MoveAction completes, which can then remove a row
        // from the freshly rebuilt tree. Run the drag without that second,
        // widget-level model mutation; the tree is always rebuilt from the
        // committed document instead.
        const Qt::DropActions move_actions = supported_actions & Qt::MoveAction;
        if (source != nullptr && !dragged_item_id_.isEmpty() &&
            move_actions.testFlag(Qt::MoveAction)) {
            QDrag drag(this);
            drag.setMimeData(mimeData({source}));
            const QRect source_rect = visualItemRect(source);
            if (source_rect.isValid()) {
                drag.setPixmap(viewport()->grab(source_rect));
                drag.setHotSpot(viewport()->mapFromGlobal(QCursor::pos()) -
                                source_rect.topLeft());
            }
            (void)drag.exec(move_actions, Qt::MoveAction);
        }

        drag_source_active_ = false;
        dragged_item_id_.clear();
        dragged_item_is_group_ = false;
    }

    void dropEvent(QDropEvent* event) override {
        if (drop_handler) {
            const auto* current = currentItem();
            const QString source_id = drag_source_active_
                ? dragged_item_id_
                : (current == nullptr ? QString{} : current->data(0, kItemIdRole).toString());
            const bool source_group = drag_source_active_
                ? dragged_item_is_group_
                : (current != nullptr && current->data(0, kGroupRole).toBool());
            drop_handler(source_id, source_group,
                         itemAt(event->position().toPoint()),
                         dropIndicatorPosition());
            event->acceptProposedAction();
            return;
        }
        QTreeWidget::dropEvent(event);
    }

private:
    QString dragged_item_id_;
    bool dragged_item_is_group_ = false;
    bool drag_source_active_ = false;
};

QTreeWidgetItem* makeLayerItem(const ImageLayerData& layer,
                               const QHash<QString, QImage>& thumbnails,
                               QTreeWidgetItem* parent = nullptr) {
    auto* item = parent == nullptr ? new QTreeWidgetItem() : new QTreeWidgetItem(parent);
    item->setText(0, layer.name);
    item->setData(0, kItemIdRole, layer.id);
    item->setData(0, kGroupRole, false);
    item->setData(0, kBackgroundRole, layer.background);
    item->setData(0, kVisibleRole, layer.visible);
    item->setData(0, kThumbnailRole, thumbnails.value(layer.id));
    item->setData(0, kParentGroupRole, layer.parent_group_id);
    item->setData(0, Qt::ToolTipRole, layer.background
        ? (layer.visible
            ? QCoreApplication::translate("ImageEditorUI", "Locked Background. Click the eye button to hide it.")
            : QCoreApplication::translate("ImageEditorUI", "Locked Background. Click the eye button to show it."))
        : (layer.visible
            ? QCoreApplication::translate("ImageEditorUI", "Visible â€” click the eye button to hide this layer.")
            : QCoreApplication::translate("ImageEditorUI", "Hidden â€” click the eye button to show this layer.")));
    item->setData(0, Qt::AccessibleDescriptionRole, layer.visible
        ? QCoreApplication::translate("ImageEditorUI", "Visible. Eye button hides this layer.")
        : QCoreApplication::translate("ImageEditorUI", "Hidden. Eye button shows this layer."));
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!layer.background) flags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    item->setFlags(flags);
    return item;
}

} // namespace

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("imageEditorLayerPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    quick_export_button_ = new QPushButton(QCoreApplication::translate("ImageEditorUI", "Quick Export"), this);
    quick_export_button_->setObjectName(QStringLiteral("quickExportLayerButton"));
    quick_export_button_->setEnabled(false);
    layout->addWidget(quick_export_button_);

    auto* tree = new LayerTreeWidget(this);
    layer_tree_ = tree;
    layer_tree_->setObjectName(QStringLiteral("imageLayerTree"));
    layer_tree_->setHeaderHidden(true);
    layer_tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layer_tree_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed |
                                 QAbstractItemView::SelectedClicked);
    layer_tree_->setItemDelegate(new LayerItemDelegate(layer_tree_));
    layer_tree_->setMouseTracking(true);
    layer_tree_->viewport()->setMouseTracking(true);
    layer_tree_->viewport()->installEventFilter(this);
    layer_tree_->setDragEnabled(true);
    layer_tree_->setAcceptDrops(true);
    layer_tree_->setDropIndicatorShown(true);
    layer_tree_->setDragDropMode(QAbstractItemView::InternalMove);
    layer_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(layer_tree_, 1);

    layer_context_menu_ = new QMenu(this);
    layer_context_menu_->setObjectName(QStringLiteral("imageLayerContextMenu"));
    auto* adjustments=layer_context_menu_->addMenu(QCoreApplication::translate("ImageEditorUI","New Adjustment Layer"));
    add_gradient_action_=adjustments->addAction(QCoreApplication::translate("ImageEditorUI","Gradient Map"));add_gradient_action_->setObjectName("addGradientMapAction");
    connect(add_gradient_action_,&QAction::triggered,this,[this]{finishOutlineEdit();emit addGradientMapRequested();});
    add_brightness_action_=adjustments->addAction(QCoreApplication::translate("ImageEditorUI","Brightness/Contrast"));add_brightness_action_->setObjectName("addBrightnessContrastAction");
    connect(add_brightness_action_,&QAction::triggered,this,[this]{finishOutlineEdit();emit addBrightnessContrastRequested();});
    add_exposure_action_ = adjustments->addAction(QCoreApplication::translate("ImageEditorUI", "Exposure / Offset / Gamma"));
    add_exposure_action_->setObjectName("addExposureOffsetGammaAction");
    connect(add_exposure_action_, &QAction::triggered, this, [this] { finishOutlineEdit(); emit addExposureOffsetGammaRequested(); });
    exposure_editor_ = new ExposureOffsetGammaEditor(this); layout->addWidget(exposure_editor_); exposure_editor_->hide();
    exposure_editor_->started = [this] { emit adjustmentEditStarted(); };
    exposure_editor_->finished = [this] { emit adjustmentEditFinished(); };
    exposure_editor_->changed = [this](const auto& value) { emit exposureOffsetGammaChanged(selectedItemId(), value); };
    add_hue_action_ = adjustments->addAction(QCoreApplication::translate("ImageEditorUI", "Hue/Saturation"));
    add_hue_action_->setObjectName("addHueSaturationAction");
    connect(add_hue_action_, &QAction::triggered, this, [this] { finishOutlineEdit(); emit addHueSaturationRequested(); });
    hue_editor_ = new HueSaturationEditor(this); layout->addWidget(hue_editor_); hue_editor_->hide();
    hue_editor_->started = [this] { emit adjustmentEditStarted(); };
    hue_editor_->finished = [this] { emit adjustmentEditFinished(); };
    hue_editor_->changed = [this](const auto& value) { emit hueSaturationChanged(selectedItemId(), value); };
    add_levels_action_ = adjustments->addAction(QCoreApplication::translate("ImageEditorUI", "Levels"));
    add_levels_action_->setObjectName("addLevelsAction");
    connect(add_levels_action_, &QAction::triggered, this,
        [this] { finishOutlineEdit(); emit addLevelsRequested(); });
    levels_editor_ = new LevelsEditor(this);
    layout->addWidget(levels_editor_);
    levels_editor_->hide();
    levels_editor_->started = [this] { emit adjustmentEditStarted(); };
    levels_editor_->finished = [this] { emit adjustmentEditFinished(); };
    levels_editor_->changed = [this](const auto& value) { emit levelsChanged(selectedItemId(), value); };
    brightness_editor_=new BrightnessContrastEditor(this);layout->addWidget(brightness_editor_);brightness_editor_->hide();
    brightness_editor_->started=[this]{emit adjustmentEditStarted();};brightness_editor_->finished=[this]{emit adjustmentEditFinished();};
    brightness_editor_->changed=[this](const auto& value){emit brightnessContrastChanged(selectedItemId(),value);};
    gradient_editor_=new GradientMapEditor(this);layout->addWidget(gradient_editor_);gradient_editor_->hide();
    gradient_editor_->started=[this]{emit adjustmentEditStarted();};gradient_editor_->finished=[this]{emit adjustmentEditFinished();};
    gradient_editor_->changed=[this](const auto& value){emit gradientMapChanged(selectedItemId(),value);};
    QAction* group_context_action = layer_context_menu_->addAction(
        QCoreApplication::translate("ImageEditorUI", "Group Selected"));
    group_context_action->setObjectName(QStringLiteral("groupSelectedLayersContextAction"));
    QAction* ungroup_context_action = layer_context_menu_->addAction(
        QCoreApplication::translate("ImageEditorUI", "Ungroup"));
    ungroup_context_action->setObjectName(QStringLiteral("ungroupLayerGroupContextAction"));
    QAction* delete_group_context_action = layer_context_menu_->addAction(
        QCoreApplication::translate("ImageEditorUI", "Delete Group"));
    delete_group_context_action->setObjectName(QStringLiteral("deleteLayerGroupContextAction"));
    QAction* delete_layer_context_action = layer_context_menu_->addAction(
        QCoreApplication::translate("ImageEditorUI", "Delete Layer"));
    delete_layer_context_action->setObjectName(QStringLiteral("deleteLayerContextAction"));
    layer_context_menu_->addSeparator();
    QAction* add_mask_action = layer_context_menu_->addAction(QCoreApplication::translate("ImageEditorUI", "Add Layer Mask"));
    add_mask_action->setObjectName(QStringLiteral("addLayerMaskAction"));
    QAction* enable_mask_action = layer_context_menu_->addAction(QCoreApplication::translate("ImageEditorUI", "Enable Layer Mask"));
    enable_mask_action->setObjectName(QStringLiteral("enableLayerMaskAction"));
    enable_mask_action->setCheckable(true);
    QAction* remove_mask_action = layer_context_menu_->addAction(QCoreApplication::translate("ImageEditorUI", "Remove Layer Mask"));
    remove_mask_action->setObjectName(QStringLiteral("removeLayerMaskAction"));

    clipping_action_ = layer_context_menu_->addAction(QCoreApplication::translate("ImageEditorUI", "Create Clipping Mask"));
    clipping_action_->setObjectName(QStringLiteral("toggleLayerClippingAction"));
    connect(clipping_action_, &QAction::triggered, this, [this] {
        if (!clipping_action_->isEnabled()) return;
        const auto* layer = findLayer(document_, selectedItemId());
        if (layer) { finishOutlineEdit(); emit layerClippingChanged(layer->id, !layer->clip_to_base); }
    });

    auto* outline_form = new QFormLayout;
    outline_enabled_ = new QCheckBox(QCoreApplication::translate("ImageEditorUI", "Outline"), this);
    outline_enabled_->setObjectName(QStringLiteral("layerOutlineEnabled"));
    outline_form->addRow(outline_enabled_);
    outline_thickness_ = new QSpinBox(this);
    outline_thickness_->setObjectName(QStringLiteral("layerOutlineThickness"));
    outline_thickness_->setRange(1, 1024); outline_thickness_->setValue(2);
    outline_thickness_->setSuffix(QStringLiteral(" px"));
    outline_thickness_->installEventFilter(this);
    outline_form->addRow(QCoreApplication::translate("ImageEditorUI", "Thickness"), outline_thickness_);
    outline_position_ = new QComboBox(this);
    outline_position_->setObjectName(QStringLiteral("layerOutlinePosition"));
    outline_position_->addItem(QCoreApplication::translate("ImageEditorUI", "Inside"));
    outline_position_->addItem(QCoreApplication::translate("ImageEditorUI", "Outside"));
    outline_position_->setCurrentIndex(1);
    outline_form->addRow(QCoreApplication::translate("ImageEditorUI", "Position"), outline_position_);
    outline_color_ = new QPushButton(QCoreApplication::translate("ImageEditorUI", "Color"), this);
    outline_color_->setObjectName(QStringLiteral("layerOutlineColor"));
    outline_form->addRow(outline_color_);
    outline_remove_ = new QPushButton(QCoreApplication::translate("ImageEditorUI", "Remove Outline"), this);
    outline_remove_->setObjectName(QStringLiteral("layerOutlineRemove"));
    outline_form->addRow(outline_remove_);
    layout->addLayout(outline_form);
    connect(outline_enabled_, &QCheckBox::toggled, this, [this] { if (!refreshing_) emitOutlineChange(); });
    connect(outline_position_, &QComboBox::currentIndexChanged, this, [this] { if (!refreshing_) emitOutlineChange(); });
    connect(outline_thickness_, &QSpinBox::valueChanged, this, [this] {
        if (refreshing_) return;
        beginOutlineEdit(); emitOutlineChange();
    });
    connect(outline_thickness_, &QSpinBox::editingFinished, this, &LayerPanel::finishOutlineEdit);
    connect(outline_remove_, &QPushButton::clicked, this, [this] {
        finishOutlineEdit(); emit layerOutlineChanged(selectedItemId(), {}, true);
    });
    connect(outline_color_, &QPushButton::clicked, this, [this] {
        finishOutlineEdit();
        const QString id = selectedItemId();
        const auto layer = std::find_if(document_.layers.cbegin(), document_.layers.cend(),
            [&id](const ImageLayerData& value) { return value.id == id; });
        if (layer == document_.layers.cend() || layer->background) return;
        const auto before = layer->outline;
        auto initial = before.value_or(ImageLayerOutlineData{});
        QColorDialog dialog(initial.color, this);
        dialog.setOption(QColorDialog::ShowAlphaChannel);
        dialog.setObjectName(QStringLiteral("layerOutlineColorDialog"));
        beginOutlineEdit();
        connect(&dialog, &QColorDialog::currentColorChanged, this, [this, id, initial](const QColor& color) mutable {
            initial.color = color; emit layerOutlineChanged(id, initial, false);
        });
        if (dialog.exec() != QDialog::Accepted) emit layerOutlineChanged(id, initial, !before.has_value());
        finishOutlineEdit();
    });

    edit_hint_ = new QLabel(this);
    edit_hint_->setObjectName(QStringLiteral("layerEditingHint"));
    edit_hint_->setWordWrap(true);
    edit_hint_->setText(QCoreApplication::translate("ImageEditorUI",
        "Background is locked. Select an editable layer to paint or transform; "
        "Shapes creates a separate layer for each object."));
    layout->addWidget(edit_hint_);

    auto* actions = new QHBoxLayout;
    add_button_ = new QToolButton(this);
    add_button_->setObjectName(QStringLiteral("addImageLayerButton"));
    add_button_->setText(QCoreApplication::translate("ImageEditorUI", "+"));
    add_button_->setToolTip(QCoreApplication::translate("ImageEditorUI", "Add layer or group"));
    add_button_->setPopupMode(QToolButton::MenuButtonPopup);
    auto* add_menu = new QMenu(add_button_);
    QAction* add_layer_action = add_menu->addAction(QCoreApplication::translate("ImageEditorUI", "Add Layer"));
    add_layer_action->setObjectName(QStringLiteral("addImageLayerAction"));
    QAction* add_group_action = add_menu->addAction(QCoreApplication::translate("ImageEditorUI", "Add Group"));
    add_group_action->setObjectName(QStringLiteral("addImageGroupAction"));
    add_button_->setMenu(add_menu);
    actions->addWidget(add_button_);

    group_selected_button_ = new QToolButton(this);
    group_selected_button_->setObjectName(QStringLiteral("groupSelectedLayersButton"));
    group_selected_button_->setText(QCoreApplication::translate("ImageEditorUI", "Group"));
    group_selected_button_->setToolTip(QCoreApplication::translate("ImageEditorUI",
        "Group contiguous selected root layers (Ctrl/Shift select)"));
    actions->addWidget(group_selected_button_);

    delete_button_ = new QToolButton(this);
    delete_button_->setObjectName(QStringLiteral("deleteImageLayerButton"));
    delete_button_->setText(QCoreApplication::translate("ImageEditorUI", "Delete"));
    delete_button_->setToolTip(QCoreApplication::translate("ImageEditorUI", "Delete selected layers or groups"));
    delete_button_->setAccessibleName(QCoreApplication::translate("ImageEditorUI", "Delete selected layers or groups"));
    actions->addWidget(delete_button_);

    ungroup_button_ = new QToolButton(this);
    ungroup_button_->setObjectName(QStringLiteral("ungroupImageLayersButton"));
    ungroup_button_->setText(QCoreApplication::translate("ImageEditorUI", "Ungroup"));
    ungroup_button_->setToolTip(QCoreApplication::translate("ImageEditorUI", "Ungroup and keep its layers"));
    actions->addWidget(ungroup_button_);

    rename_button_ = new QToolButton(this);
    rename_button_->setObjectName(QStringLiteral("renameImageLayerButton"));
    rename_button_->setText(QCoreApplication::translate("ImageEditorUI", "Rename"));
    rename_button_->setToolTip(QCoreApplication::translate("ImageEditorUI", "Rename layer or group"));
    actions->addWidget(rename_button_);

    move_up_button_ = new QToolButton(this);
    move_up_button_->setObjectName(QStringLiteral("moveImageLayerUpButton"));
    move_up_button_->setText(QCoreApplication::translate("ImageEditorUI", "â†‘"));
    move_up_button_->setToolTip(QCoreApplication::translate("ImageEditorUI", "Move item up"));
    actions->addWidget(move_up_button_);
    move_down_button_ = new QToolButton(this);
    move_down_button_->setObjectName(QStringLiteral("moveImageLayerDownButton"));
    move_down_button_->setText(QCoreApplication::translate("ImageEditorUI", "â†“"));
    move_down_button_->setToolTip(QCoreApplication::translate("ImageEditorUI", "Move item down"));
    actions->addWidget(move_down_button_);
    layout->addLayout(actions);

    auto* blend_row = new QHBoxLayout;
    blend_row->addWidget(new QLabel(QCoreApplication::translate("ImageEditorUI","Blend Mode"),this));
    blend_mode_=new QComboBox(this); blend_mode_->setObjectName(QStringLiteral("layerBlendMode"));
    blend_mode_->setAccessibleName(QCoreApplication::translate("ImageEditorUI","Blend Mode"));
    const char* blend_names[]{QT_TRANSLATE_NOOP("ImageEditorUI","Normal"),QT_TRANSLATE_NOOP("ImageEditorUI","Multiply"),
        QT_TRANSLATE_NOOP("ImageEditorUI","Screen"),QT_TRANSLATE_NOOP("ImageEditorUI","Overlay"),
        QT_TRANSLATE_NOOP("ImageEditorUI","Add"),QT_TRANSLATE_NOOP("ImageEditorUI","Difference")};
    for(int i=0;i<6;++i) blend_mode_->addItem(QCoreApplication::translate("ImageEditorUI",blend_names[i]),i);
    blend_row->addWidget(blend_mode_,1); layout->addLayout(blend_row);
    connect(blend_mode_,&QComboBox::currentIndexChanged,this,[this] {
        if (!refreshing_ && blend_mode_->isEnabled()) {
            const auto id=selectedItemId(); const bool group=selectedItemIsGroup();
            const int mode=blend_mode_->currentData().toInt();
            finishOutlineEdit(); emit stackBlendModeChanged(id,group,mode);
        }
    });

    auto* opacity_row = new QHBoxLayout;
    auto* opacity_label = new QLabel(QCoreApplication::translate("ImageEditorUI", "Opacity"), this);
    opacity_label->setObjectName(QStringLiteral("imageLayerOpacityLabel"));
    opacity_row->addWidget(opacity_label);
    opacity_slider_ = new QSlider(Qt::Horizontal, this);
    opacity_slider_->setObjectName(QStringLiteral("imageLayerOpacitySlider"));
    opacity_slider_->setRange(0, 100);
    opacity_slider_->setValue(100);
    opacity_slider_->setAccessibleName(QCoreApplication::translate("ImageEditorUI", "Layer or group opacity"));
    opacity_row->addWidget(opacity_slider_, 1);
    layout->addLayout(opacity_row);

    connect(layer_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current) {
                if (refreshing_ || current == nullptr) return;
                const QString id = current->data(0, kItemIdRole).toString();
                if (current->data(0, kGroupRole).toBool()) emit groupSelected(id);
                else emit layerSelected(id);
                updateControls();
            });
    connect(layer_tree_, &QTreeWidget::itemSelectionChanged, this,
            [this]() { updateControls(); });
    connect(layer_tree_, &QTreeWidget::customContextMenuRequested, this,
            [this, group_context_action, ungroup_context_action,
             delete_group_context_action, delete_layer_context_action,
             add_mask_action, enable_mask_action,
             remove_mask_action](const QPoint& position) {
                QTreeWidgetItem* item = layer_tree_->itemAt(position);
                if (item == nullptr) return;

                if (item->isSelected()) {
                    layer_tree_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
                } else {
                    layer_tree_->setCurrentItem(item, 0,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }

                const bool is_group = item->data(0, kGroupRole).toBool();
                const bool editable = !is_group && !item->data(0, kBackgroundRole).toBool();
                const bool has_mask = item->data(0, kHasMaskRole).toBool();
                add_mask_action->setVisible(editable && !has_mask);
                enable_mask_action->setVisible(editable && has_mask);
                remove_mask_action->setVisible(editable && has_mask);
                enable_mask_action->setChecked(item->data(0, kMaskEnabledRole).toBool());
                group_context_action->setVisible(!is_group);
                group_context_action->setEnabled(!is_group && canGroupSelectedLayers());
                ungroup_context_action->setVisible(is_group);
                delete_group_context_action->setVisible(is_group);
                delete_layer_context_action->setVisible(!is_group);
                delete_layer_context_action->setEnabled(!selectedStackItems().isEmpty());
                const bool multiple = selectedStackItems().size() > 1;
                delete_layer_context_action->setText(multiple
                    ? QCoreApplication::translate("ImageEditorUI", "Delete Selected Layers / Groups") : QCoreApplication::translate("ImageEditorUI", "Delete Layer"));
                delete_group_context_action->setText(multiple
                    ? QCoreApplication::translate("ImageEditorUI", "Delete Selected Layers / Groups") : QCoreApplication::translate("ImageEditorUI", "Delete Group"));
                ungroup_context_action->setEnabled(is_group);
                delete_group_context_action->setEnabled(is_group);
                layer_context_menu_->exec(layer_tree_->viewport()->mapToGlobal(position));
            });
    connect(layer_tree_, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
                if (refreshing_ || item == nullptr || column != 0 ||
                    item->data(0, kBackgroundRole).toBool()) return;
                const QString id = item->data(0, kItemIdRole).toString();
                if (item->data(0, kGroupRole).toBool()) emit groupRenamed(id, item->text(0));
                else emit layerRenamed(id, item->text(0));
            });
    connect(quick_export_button_, &QPushButton::clicked, this,
            &LayerPanel::quickExportRequested);
    connect(add_button_, &QToolButton::clicked, this, &LayerPanel::addLayerRequested);
    connect(add_layer_action, &QAction::triggered, this, &LayerPanel::addLayerRequested);
    connect(add_group_action, &QAction::triggered, this, &LayerPanel::addGroupRequested);
    connect(group_selected_button_, &QToolButton::clicked, this, [this]() {
        QStringList ids;
        if (canGroupSelectedLayers(&ids)) emit groupSelectedLayersRequested(ids);
    });
    connect(group_context_action, &QAction::triggered, this, [this]() {
        QStringList ids;
        if (canGroupSelectedLayers(&ids)) emit groupSelectedLayersRequested(ids);
    });
    connect(delete_button_, &QToolButton::clicked, this, &LayerPanel::requestDeleteSelection);
    connect(ungroup_button_, &QToolButton::clicked, this,
            [this]() { emit ungroupRequested(selectedItemId()); });
    connect(ungroup_context_action, &QAction::triggered, this,
            [this]() { emit ungroupRequested(selectedItemId()); });
    connect(delete_group_context_action, &QAction::triggered, this,
            &LayerPanel::requestDeleteSelection);
    connect(delete_layer_context_action, &QAction::triggered, this,
            &LayerPanel::requestDeleteSelection);
    connect(add_mask_action, &QAction::triggered, this,
            [this]() { emit addLayerMaskRequested(selectedItemId()); });
    connect(remove_mask_action, &QAction::triggered, this,
            [this]() { emit removeLayerMaskRequested(selectedItemId()); });
    connect(enable_mask_action, &QAction::triggered, this,
            [this](bool enabled) { emit layerMaskEnabledChanged(selectedItemId(), enabled); });
    connect(rename_button_, &QToolButton::clicked, this, [this]() {
        auto* item = layer_tree_->currentItem();
        if (item != nullptr && !item->data(0, kBackgroundRole).toBool()) {
            layer_tree_->editItem(item, 0);
        }
    });
    connect(move_up_button_, &QToolButton::clicked, this,
            [this]() { requestMove(selectedItemId(), selectedItemIsGroup(), 1); });
    connect(move_down_button_, &QToolButton::clicked, this,
            [this]() { requestMove(selectedItemId(), selectedItemIsGroup(), -1); });
    connect(opacity_slider_, &QSlider::sliderPressed, this,
            &LayerPanel::opacityEditStarted);
    connect(opacity_slider_, &QSlider::sliderReleased, this,
            &LayerPanel::opacityEditFinished);
    connect(opacity_slider_, &QSlider::valueChanged, this, [this](int value) {
        const QString id = selectedItemId();
        if (!refreshing_ && !id.isEmpty()) {
            emit stackOpacityChanged(id, selectedItemIsGroup(), value);
        }
    });

    tree->drop_handler = [this](const QString& source_id, bool source_group,
                                QTreeWidgetItem* target,
                                LayerTreeWidget::DropIndicatorPosition indicator) {
        if (source_id.isEmpty()) return;
        const auto source_layer = std::find_if(document_.layers.cbegin(),
            document_.layers.cend(), [&source_id](const ImageLayerData& layer) {
                return layer.id == source_id;
            });
        if (source_layer != document_.layers.cend() && source_layer->background) return;
        QString target_group_id;
        qsizetype insertion_index = document_.root_stack.size();
        if (target != nullptr && target->data(0, kGroupRole).toBool() &&
            LayerTreeWidget::isOnItem(indicator)) {
            if (source_group) return;
            target_group_id = target->data(0, kItemIdRole).toString();
            const auto group = std::find_if(document_.groups.cbegin(), document_.groups.cend(),
                [&target_group_id](const ImageGroupData& value) {
                    return value.id == target_group_id;
                });
            if (group == document_.groups.cend()) return;
            insertion_index = group->layer_ids.size();
        } else if (target != nullptr) {
            target_group_id = target->data(0, kParentGroupRole).toString();
            if (source_group) target_group_id.clear();
            if (target_group_id.isEmpty()) {
                const auto target_item = std::find_if(document_.root_stack.cbegin(),
                    document_.root_stack.cend(), [&target](const ImageStackItemData& value) {
                        return value.id == target->data(0, kItemIdRole).toString() &&
                            value.group == target->data(0, kGroupRole).toBool();
                    });
                if (target_item == document_.root_stack.cend()) return;
                const qsizetype model_index = std::distance(document_.root_stack.cbegin(), target_item);
                insertion_index = model_index +
                    (LayerTreeWidget::isAboveItem(indicator) ? 1 : 0);
            } else {
                const auto group = std::find_if(document_.groups.cbegin(), document_.groups.cend(),
                    [&target_group_id](const ImageGroupData& value) {
                        return value.id == target_group_id;
                    });
                if (group == document_.groups.cend()) return;
                const QString target_id = target->data(0, kItemIdRole).toString();
                const qsizetype model_index = group->layer_ids.indexOf(target_id);
                if (model_index < 0) return;
                insertion_index = model_index +
                    (LayerTreeWidget::isAboveItem(indicator) ? 1 : 0);
            }
        }
        emit moveStackItemRequested(source_id, source_group,
                                    target_group_id, insertion_index);
    };
    updateControls();
}

void LayerPanel::setThumbnails(const QHash<QString, QImage>& thumbnails,
                               const QHash<QString, QImage>& mask_thumbnails) {
    const QSignalBlocker blocker(layer_tree_);
    for (QTreeWidgetItemIterator item(layer_tree_); *item; ++item) {
        const auto id = (*item)->data(0, kItemIdRole).toString();
        (*item)->setData(0, kThumbnailRole, thumbnails.value(id));
        (*item)->setData(0, kMaskThumbnailRole, mask_thumbnails.value(id));
    }
}

void LayerPanel::setDocument(const ImageDocumentData& document,
                             const QString& selected_layer_id,
                             const QString& selected_group_id,
                             const QHash<QString, QImage>& thumbnails,
                             const QHash<QString, QImage>& mask_thumbnails,
                             const QString& selected_mask_id,
                             const QHash<QString, QString>& source_problems,
                             const QVector<ImageStackItemData>& selected_stack_items) {
    QSet<QString> expanded;
    for (int index = 0; index < layer_tree_->topLevelItemCount(); ++index) {
        auto* item = layer_tree_->topLevelItem(index);
        if (item->data(0, kGroupRole).toBool() && item->isExpanded()) {
            expanded.insert(item->data(0, kItemIdRole).toString());
        }
    }

    refreshing_ = true;
    const QSignalBlocker blocker(layer_tree_);
    document_ = document;
    const auto set_mask_data = [&](QTreeWidgetItem* item, const ImageLayerData& layer) {
        item->setData(0, kClippedRole, layer.clip_to_base);
        item->setData(0, kHasMaskRole, layer.mask.has_value());
        item->setData(0, kMaskThumbnailRole, mask_thumbnails.value(layer.id));
        item->setData(0, kMaskSelectedRole, layer.id == selected_mask_id);
        item->setData(0, kMaskEnabledRole, layer.mask.has_value() && layer.mask->enabled);
        if (layer.mask.has_value()) {
            item->setData(0, Qt::ToolTipRole, QCoreApplication::translate("ImageEditorUI",
                "Click the mask thumbnail to edit it. Black hides, white reveals. "
                "Click the image thumbnail to edit layer content."));
        }
        for (const auto& op : layer.operations) {
            if (op.kind == OperationKind::RasterImage) item->setData(0, kRasterObjectRole, op.raster.id);
            if (op.kind != OperationKind::RasterImage || !source_problems.contains(op.raster.id)) continue;
            item->setData(0, kSourceProblemRole, true);
            item->setData(0, Qt::ToolTipRole,
                ui::translateMessage(source_problems.value(op.raster.id)) +
                QCoreApplication::translate("ImageEditorUI", " Select this image and choose File > Relink Image."));
            item->setData(0, Qt::AccessibleDescriptionRole,
                ui::translateMessage(source_problems.value(op.raster.id)));
            item->setForeground(0, QColor(230, 170, 80));
        }
        if (layer.clip_to_base) {
            const auto* base = findLayer(document_, clippingBases(document_).value(layer.id));
            const auto hint = QCoreApplication::translate("ImageEditorUI", "Clipped to: %1").arg(base ? base->name : QString{});
            item->setToolTip(0, hint + "\n" + item->toolTip(0));
            item->setData(0, Qt::AccessibleDescriptionRole, hint);
        }
    };
    layer_tree_->clear();
    QTreeWidgetItem* selected = nullptr;
    for (auto root = document.root_stack.crbegin(); root != document.root_stack.crend(); ++root) {
        if (!root->group) {
            const auto layer = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&root](const ImageLayerData& value) { return value.id == root->id; });
            if (layer == document.layers.cend()) continue;
            auto* item = makeLayerItem(*layer, thumbnails);
            set_mask_data(item, *layer);
            layer_tree_->addTopLevelItem(item);
            if (layer->id == selected_layer_id) selected = item;
            continue;
        }
        const auto group = std::find_if(document.groups.cbegin(), document.groups.cend(),
            [&root](const ImageGroupData& value) { return value.id == root->id; });
        if (group == document.groups.cend()) continue;
        auto* group_item = new QTreeWidgetItem();
        group_item->setText(0, group->name);
        group_item->setData(0, kItemIdRole, group->id);
        group_item->setData(0, kGroupRole, true);
        group_item->setData(0, kBackgroundRole, false);
        group_item->setData(0, kVisibleRole, group->visible);
        group_item->setData(0, kThumbnailRole, thumbnails.value(group->id));
        group_item->setData(0, kParentGroupRole, QString{});
        group_item->setData(0, Qt::ToolTipRole, group->visible
            ? QCoreApplication::translate("ImageEditorUI", "Visible group. Click the eye button to hide its contents.")
            : QCoreApplication::translate("ImageEditorUI", "Hidden group. Click the eye button to show its contents."));
        group_item->setData(0, Qt::AccessibleDescriptionRole, group->visible
            ? QCoreApplication::translate("ImageEditorUI", "Visible group. Eye button hides its contents.")
            : QCoreApplication::translate("ImageEditorUI", "Hidden group. Eye button shows its contents."));
        group_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable |
                             Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        layer_tree_->addTopLevelItem(group_item);
        for (auto child_id = group->layer_ids.crbegin();
             child_id != group->layer_ids.crend(); ++child_id) {
            const auto child = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&child_id](const ImageLayerData& value) { return value.id == *child_id; });
            if (child == document.layers.cend()) continue;
            auto* child_item = makeLayerItem(*child, thumbnails, group_item);
            set_mask_data(child_item, *child);
            if (child->id == selected_layer_id) selected = child_item;
        }
        group_item->setExpanded(expanded.isEmpty() || expanded.contains(group->id));
        if (group->id == selected_group_id) selected = group_item;
    }
    if (selected != nullptr) layer_tree_->setCurrentItem(selected);
    if (!selected_stack_items.isEmpty()) {
        layer_tree_->clearSelection();
        for (const auto& selected_item : selected_stack_items) {
            for (QTreeWidgetItemIterator item(layer_tree_); *item != nullptr; ++item) {
                if ((*item)->data(0, kItemIdRole).toString() == selected_item.id &&
                    (*item)->data(0, kGroupRole).toBool() == selected_item.group) {
                    (*item)->setSelected(true);
                    break;
                }
            }
        }
    }
    refreshing_ = false;
    updateControls();
}

void LayerPanel::setSelectedMask(const QString& layer_id) {
    const QSignalBlocker blocker(layer_tree_);
    for (QTreeWidgetItemIterator item(layer_tree_); *item != nullptr; ++item) {
        (*item)->setData(0, kMaskSelectedRole,
                        (*item)->data(0, kItemIdRole).toString() == layer_id);
    }
    layer_tree_->viewport()->update();
}

void LayerPanel::setLevelsHistogram(const std::array<std::uint64_t, 256>& bins) {
    levels_editor_->setHistogram(bins);
}

void LayerPanel::setQuickExportEnabled(bool enabled) {
    quick_export_button_->setEnabled(enabled);
}

QVector<ImageStackItemData> LayerPanel::selectedStackItems() const {
    QVector<ImageStackItemData> items;
    for (auto* item : layer_tree_->selectedItems()) {
        if (item->data(0, kBackgroundRole).toBool()) continue;
        items.append({item->data(0, kItemIdRole).toString(), item->data(0, kGroupRole).toBool()});
    }
    return items;
}

void LayerPanel::requestDeleteSelection() {
    const auto items = selectedStackItems();
    if (!items.isEmpty()) emit deleteStackItemsRequested(items);
}

void LayerPanel::beginOutlineEdit() {
    outline_editing_ = true; emit outlineEditStarted();
}

void LayerPanel::finishOutlineEdit() {
    if(gradient_editor_)gradient_editor_->finish();
    if(brightness_editor_)brightness_editor_->finish();
    if(exposure_editor_)exposure_editor_->finish();
    if(hue_editor_)hue_editor_->finish();
    if(levels_editor_)levels_editor_->finish();
    if (!outline_editing_) return;
    outline_editing_ = false; emit outlineEditFinished();
}

void LayerPanel::emitOutlineChange() {
    const QString id = selectedItemId();
    const auto layer = std::find_if(document_.layers.cbegin(), document_.layers.cend(),
        [&id](const ImageLayerData& value) { return value.id == id; });
    if (layer == document_.layers.cend() || layer->background || layer_tree_->selectedItems().size() != 1) return;
    auto outline = layer->outline.value_or(ImageLayerOutlineData{});
    outline.enabled = outline_enabled_->isChecked();
    outline.thickness = outline_thickness_->value();
    outline.position = outline_position_->currentIndex() == 0 ? ImageOutlinePosition::Inside : ImageOutlinePosition::Outside;
    emit layerOutlineChanged(id, outline, false);
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == outline_thickness_ && event->type() == QEvent::FocusIn) beginOutlineEdit();
    if (watched == outline_thickness_ && event->type() == QEvent::FocusOut) finishOutlineEdit();
    if (watched == layer_tree_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            QTreeWidgetItem* item = layer_tree_->itemAt(mouse->pos());
            const bool over_eye = item != nullptr &&
                eyeHitRect(layer_tree_->visualItemRect(item)).contains(mouse->pos());
            layer_tree_->viewport()->setCursor(over_eye ? Qt::PointingHandCursor
                                                        : Qt::ArrowCursor);
        }
        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            QTreeWidgetItem* item = layer_tree_->itemAt(mouse->pos());
            if (mouse->button() == Qt::LeftButton && item != nullptr &&
                !item->data(0, kGroupRole).toBool() &&
                (item->data(0, kHasMaskRole).toBool() ||
                 !item->data(0, kRasterObjectRole).toString().isEmpty())) {
                const QRect row = layer_tree_->visualItemRect(item);
                const bool mask_hit = item->data(0, kHasMaskRole).toBool() &&
                    maskThumbnailRect(row).contains(mouse->pos());
                const QRect content_rect(row.left() + 6, row.center().y() - 18, 48, 36);
                if (mask_hit || content_rect.contains(mouse->pos())) {
                    eye_press_consumed_ = true;
                    if (event->type() == QEvent::MouseButtonPress) {
                        const QString id = item->data(0, kItemIdRole).toString();
                        layer_tree_->setCurrentItem(item);
                        if (mask_hit) emit layerMaskSelected(id);
                        else emit layerSelected(id);
                    }
                    return true;
                }
            }
            if (mouse->button() == Qt::LeftButton && item != nullptr &&
                eyeHitRect(layer_tree_->visualItemRect(item)).contains(mouse->pos())) {
                eye_press_consumed_ = true;
                if (event->type() == QEvent::MouseButtonPress) {
                    const QString id = item->data(0, kItemIdRole).toString();
                    const bool visible = !item->data(0, kVisibleRole).toBool();
                    if (item->data(0, kGroupRole).toBool()) {
                        emit groupVisibilityChanged(id, visible);
                    } else {
                        emit layerVisibilityChanged(id, visible);
                    }
                }
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease && eye_press_consumed_) {
            eye_press_consumed_ = false;
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString LayerPanel::selectedItemId() const {
    return layer_tree_->currentItem() == nullptr
        ? QString{} : layer_tree_->currentItem()->data(0, kItemIdRole).toString();
}

bool LayerPanel::selectedItemIsGroup() const {
    return layer_tree_->currentItem() != nullptr &&
        layer_tree_->currentItem()->data(0, kGroupRole).toBool();
}

bool LayerPanel::canGroupSelectedLayers(QStringList* layer_ids) const {
    const QList<QTreeWidgetItem*> selected = layer_tree_->selectedItems();
    if (selected.size() < 2) return false;
    QStringList ids;
    for (const auto* item : selected) {
        if (item->data(0, kGroupRole).toBool() ||
            item->data(0, kBackgroundRole).toBool() ||
            !item->data(0, kParentGroupRole).toString().isEmpty()) return false;
        ids.append(item->data(0, kItemIdRole).toString());
    }
    QVector<qsizetype> positions;
    for (qsizetype index = 0; index < document_.root_stack.size(); ++index) {
        if (!document_.root_stack.at(index).group && ids.contains(document_.root_stack.at(index).id)) {
            positions.append(index);
        }
    }
    std::sort(positions.begin(), positions.end());
    if (positions.size() != ids.size() ||
        positions.back() - positions.front() + 1 != positions.size()) return false;
    if (layer_ids != nullptr) *layer_ids = std::move(ids);
    return true;
}

void LayerPanel::requestMove(const QString& item_id, bool is_group, int direction) {
    if ((direction != -1 && direction != 1) || item_id.isEmpty()) return;
    QString group_id;
    qsizetype position = -1;
    qsizetype count = 0;
    if (is_group) {
        count = document_.root_stack.size();
        for (qsizetype index = 1; index < count; ++index) {
            if (document_.root_stack.at(index).group && document_.root_stack.at(index).id == item_id) {
                position = index;
                break;
            }
        }
    } else {
        const auto layer = std::find_if(document_.layers.cbegin(), document_.layers.cend(),
            [&item_id](const ImageLayerData& value) { return value.id == item_id; });
        if (layer == document_.layers.cend()) return;
        group_id = layer->parent_group_id;
        if (group_id.isEmpty()) {
            count = document_.root_stack.size();
            for (qsizetype index = 1; index < count; ++index) {
                if (!document_.root_stack.at(index).group &&
                    document_.root_stack.at(index).id == item_id) position = index;
            }
        } else {
            const auto group = std::find_if(document_.groups.cbegin(), document_.groups.cend(),
                [&group_id](const ImageGroupData& value) { return value.id == group_id; });
            if (group == document_.groups.cend()) return;
            count = group->layer_ids.size();
            position = group->layer_ids.indexOf(item_id);
        }
    }
    if (position < 0 || position + direction < (group_id.isEmpty() ? 1 : 0) ||
        position + direction >= count) return;
    const qsizetype insertion_index = position + direction + (direction > 0 ? 1 : 0);
    emit moveStackItemRequested(item_id, is_group, group_id, insertion_index);
}

void LayerPanel::updateControls() {
    const QString id = selectedItemId();
    const bool is_group = selectedItemIsGroup();
    const auto group = std::find_if(document_.groups.cbegin(), document_.groups.cend(),
        [&id](const ImageGroupData& value) { return value.id == id; });
    const auto layer = std::find_if(document_.layers.cbegin(), document_.layers.cend(),
        [&id](const ImageLayerData& value) { return value.id == id; });
    const bool has_group = is_group && group != document_.groups.cend();
    const bool has_layer = !is_group && layer != document_.layers.cend();
    const bool editable = has_group || (has_layer && !layer->background);
    {
        const QSignalBlocker block(blend_mode_);
        const auto mode=has_group ? group->blend_mode : has_layer ? layer->blend_mode : creative_suite::composition::BlendMode::Normal;
        blend_mode_->setCurrentIndex(blend_mode_->findData(static_cast<int>(mode)));
        blend_mode_->setEnabled(editable && layer_tree_->selectedItems().size()==1);
    }
    add_gradient_action_->setEnabled(!document_.root_stack.isEmpty() && layer_tree_->selectedItems().size()==1);
    add_brightness_action_->setEnabled(add_gradient_action_->isEnabled());
    add_exposure_action_->setEnabled(add_gradient_action_->isEnabled());
    add_hue_action_->setEnabled(add_gradient_action_->isEnabled());
    add_levels_action_->setEnabled(add_gradient_action_->isEnabled());
    const bool adjustment=has_layer && layer->adjustment.has_value();
    quick_export_button_->setToolTip(adjustment?QCoreApplication::translate("ImageEditorUI","Adjustment layers have no pixels to export. Export the document or its group."):QString{});
    const auto* gradient=adjustment?std::get_if<creative_suite::effects::GradientMapParameters>(&*layer->adjustment):nullptr;
    const auto* brightness=adjustment?std::get_if<creative_suite::effects::BrightnessContrastParameters>(&*layer->adjustment):nullptr;
    gradient_editor_->setVisible(gradient && layer_tree_->selectedItems().size()==1);
    brightness_editor_->setVisible(brightness && layer_tree_->selectedItems().size()==1);
    if(gradient)gradient_editor_->setValue(id,*gradient);
    if(brightness)brightness_editor_->setValue(id,*brightness);
    const auto* exposure = adjustment ? std::get_if<creative_suite::effects::ExposureOffsetGammaParameters>(&*layer->adjustment) : nullptr;
    exposure_editor_->setVisible(exposure && layer_tree_->selectedItems().size() == 1);
    if (exposure) exposure_editor_->setValue(id, *exposure);
    const auto* hue = adjustment ? std::get_if<creative_suite::effects::HueSaturationParameters>(&*layer->adjustment) : nullptr;
    hue_editor_->setVisible(hue && layer_tree_->selectedItems().size() == 1);
    if (hue) hue_editor_->setValue(id, *hue);
    const auto* levels = adjustment ? std::get_if<creative_suite::effects::LevelsParameters>(&*layer->adjustment) : nullptr;
    levels_editor_->setVisible(levels && layer_tree_->selectedItems().size() == 1);
    if (levels) levels_editor_->setValue(id, *levels);
    const bool outline_editable = has_layer && !adjustment && !layer->background && layer_tree_->selectedItems().size() == 1;
    clipping_action_->setEnabled(has_layer && !layer->background && layer_tree_->selectedItems().size()==1 && (layer->clip_to_base || clippingBases(document_).contains(id)));
    clipping_action_->setText(has_layer && layer->clip_to_base
        ? QCoreApplication::translate("ImageEditorUI", "Release Clipping Mask")
        : QCoreApplication::translate("ImageEditorUI", "Create Clipping Mask"));
    const auto outline = has_layer ? layer->outline.value_or(ImageLayerOutlineData{}) : ImageLayerOutlineData{};
    const QSignalBlocker enabled_block(outline_enabled_), thickness_block(outline_thickness_), position_block(outline_position_);
    outline_enabled_->setChecked(has_layer && layer->outline && outline.enabled);
    outline_thickness_->setValue(outline.thickness);
    outline_position_->setCurrentIndex(outline.position == ImageOutlinePosition::Inside ? 0 : 1);
    outline_color_->setStyleSheet(QStringLiteral("background-color: %1; color: %2").arg(
        outline.color.name(QColor::HexArgb), outline.color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
    outline_enabled_->setEnabled(outline_editable);
    outline_thickness_->setEnabled(outline_editable);
    outline_position_->setEnabled(outline_editable);
    outline_color_->setEnabled(outline_editable);
    outline_remove_->setEnabled(outline_editable && layer->outline.has_value());
    edit_hint_->setVisible(has_layer && layer->background);
    add_button_->setEnabled(!document_.root_stack.isEmpty());
    group_selected_button_->setEnabled(canGroupSelectedLayers());
    delete_button_->setEnabled(!selectedStackItems().isEmpty());
    emit deletionSelectionChanged();
    ungroup_button_->setVisible(has_group);
    ungroup_button_->setEnabled(has_group);
    rename_button_->setEnabled(editable);
    move_up_button_->setEnabled(false);
    move_down_button_->setEnabled(false);
    int opacity = 100;
    if (has_group) opacity = group->opacity;
    if (has_layer) opacity = layer->opacity;
    {
        const QSignalBlocker blocker(opacity_slider_);
        opacity_slider_->setValue(opacity);
    }
    opacity_slider_->setEnabled(editable);
    if (has_group) {
        qsizetype position = -1;
        for (qsizetype index = 1; index < document_.root_stack.size(); ++index) {
            if (document_.root_stack.at(index).group &&
                document_.root_stack.at(index).id == id) position = index;
        }
        move_up_button_->setEnabled(position >= 1 && position + 1 < document_.root_stack.size());
        move_down_button_->setEnabled(position > 1);
    } else if (has_layer && !layer->background) {
        if (layer->parent_group_id.isEmpty()) {
            qsizetype position = -1;
            for (qsizetype index = 1; index < document_.root_stack.size(); ++index) {
                if (!document_.root_stack.at(index).group &&
                    document_.root_stack.at(index).id == id) position = index;
            }
            move_up_button_->setEnabled(position >= 1 && position + 1 < document_.root_stack.size());
            move_down_button_->setEnabled(position > 1);
        } else {
            const auto parent = std::find_if(document_.groups.cbegin(), document_.groups.cend(),
                [&layer](const ImageGroupData& value) { return value.id == layer->parent_group_id; });
            if (parent != document_.groups.cend()) {
                const qsizetype position = parent->layer_ids.indexOf(id);
                move_up_button_->setEnabled(position >= 0 && position + 1 < parent->layer_ids.size());
                move_down_button_->setEnabled(position > 0);
            }
        }
    }
}

} // namespace image_editor
