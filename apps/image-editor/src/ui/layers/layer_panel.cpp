#include "layer_panel.h"

#include "../transparency_checkerboard.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
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
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace image_editor {
namespace {

constexpr int kItemIdRole = Qt::UserRole + 1;
constexpr int kGroupRole = Qt::UserRole + 2;
constexpr int kBackgroundRole = Qt::UserRole + 3;
constexpr int kVisibleRole = Qt::UserRole + 4;
constexpr int kThumbnailRole = Qt::UserRole + 5;
constexpr int kParentGroupRole = Qt::UserRole + 6;
constexpr int kLayerRowHeight = 46;

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

        const QRect text_rect(thumbnail_rect.right() + 9, row.top(),
                              eyeHitRect(row).left() - thumbnail_rect.right() - 15,
                              row.height());
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->setPen(option.palette.color(selected ? QPalette::HighlightedText
                                                       : QPalette::Text));
        const QString text = QFontMetrics(option.font).elidedText(
            index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
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
    using DropHandler = std::function<void(QTreeWidgetItem*, QTreeWidgetItem*,
                                           QAbstractItemView::DropIndicatorPosition)>;
    using QTreeWidget::QTreeWidget;
    DropHandler drop_handler;
    static bool isOnItem(QAbstractItemView::DropIndicatorPosition position) {
        return position == OnItem;
    }
    static bool isAboveItem(QAbstractItemView::DropIndicatorPosition position) {
        return position == AboveItem;
    }

protected:
    void dropEvent(QDropEvent* event) override {
        if (drop_handler) {
            drop_handler(currentItem(), itemAt(event->position().toPoint()),
                         dropIndicatorPosition());
            event->acceptProposedAction();
            return;
        }
        QTreeWidget::dropEvent(event);
    }
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
            ? QStringLiteral("Locked Background. Click the eye button to hide it.")
            : QStringLiteral("Locked Background. Click the eye button to show it."))
        : (layer.visible
            ? QStringLiteral("Visible — click the eye button to hide this layer.")
            : QStringLiteral("Hidden — click the eye button to show this layer.")));
    item->setData(0, Qt::AccessibleDescriptionRole, layer.visible
        ? QStringLiteral("Visible. Eye button hides this layer.")
        : QStringLiteral("Hidden. Eye button shows this layer."));
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

    quick_export_button_ = new QPushButton(QStringLiteral("Quick Export"), this);
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
    layout->addWidget(layer_tree_, 1);

    edit_hint_ = new QLabel(this);
    edit_hint_->setObjectName(QStringLiteral("layerEditingHint"));
    edit_hint_->setWordWrap(true);
    edit_hint_->setText(QStringLiteral(
        "Background is locked. Select an editable layer to paint or transform; "
        "Shapes creates a separate layer for each object."));
    layout->addWidget(edit_hint_);

    auto* actions = new QHBoxLayout;
    add_button_ = new QToolButton(this);
    add_button_->setObjectName(QStringLiteral("addImageLayerButton"));
    add_button_->setText(QStringLiteral("+"));
    add_button_->setToolTip(QStringLiteral("Add layer or group"));
    add_button_->setPopupMode(QToolButton::MenuButtonPopup);
    auto* add_menu = new QMenu(add_button_);
    QAction* add_layer_action = add_menu->addAction(QStringLiteral("Add Layer"));
    add_layer_action->setObjectName(QStringLiteral("addImageLayerAction"));
    QAction* add_group_action = add_menu->addAction(QStringLiteral("Add Group"));
    add_group_action->setObjectName(QStringLiteral("addImageGroupAction"));
    add_button_->setMenu(add_menu);
    actions->addWidget(add_button_);

    group_selected_button_ = new QToolButton(this);
    group_selected_button_->setObjectName(QStringLiteral("groupSelectedLayersButton"));
    group_selected_button_->setText(QStringLiteral("Group"));
    group_selected_button_->setToolTip(QStringLiteral(
        "Group contiguous selected root layers (Ctrl/Shift select)"));
    actions->addWidget(group_selected_button_);

    delete_button_ = new QToolButton(this);
    delete_button_->setObjectName(QStringLiteral("deleteImageLayerButton"));
    delete_button_->setText(QStringLiteral("−"));
    delete_button_->setToolTip(QStringLiteral("Delete layer or group"));
    actions->addWidget(delete_button_);

    ungroup_button_ = new QToolButton(this);
    ungroup_button_->setObjectName(QStringLiteral("ungroupImageLayersButton"));
    ungroup_button_->setText(QStringLiteral("Ungroup"));
    ungroup_button_->setToolTip(QStringLiteral("Ungroup and keep its layers"));
    actions->addWidget(ungroup_button_);

    rename_button_ = new QToolButton(this);
    rename_button_->setObjectName(QStringLiteral("renameImageLayerButton"));
    rename_button_->setText(QStringLiteral("Rename"));
    rename_button_->setToolTip(QStringLiteral("Rename layer or group"));
    actions->addWidget(rename_button_);

    move_up_button_ = new QToolButton(this);
    move_up_button_->setObjectName(QStringLiteral("moveImageLayerUpButton"));
    move_up_button_->setText(QStringLiteral("↑"));
    move_up_button_->setToolTip(QStringLiteral("Move item up"));
    actions->addWidget(move_up_button_);
    move_down_button_ = new QToolButton(this);
    move_down_button_->setObjectName(QStringLiteral("moveImageLayerDownButton"));
    move_down_button_->setText(QStringLiteral("↓"));
    move_down_button_->setToolTip(QStringLiteral("Move item down"));
    actions->addWidget(move_down_button_);
    layout->addLayout(actions);

    auto* opacity_row = new QHBoxLayout;
    auto* opacity_label = new QLabel(QStringLiteral("Opacity"), this);
    opacity_label->setObjectName(QStringLiteral("imageLayerOpacityLabel"));
    opacity_row->addWidget(opacity_label);
    opacity_slider_ = new QSlider(Qt::Horizontal, this);
    opacity_slider_->setObjectName(QStringLiteral("imageLayerOpacitySlider"));
    opacity_slider_->setRange(0, 100);
    opacity_slider_->setValue(100);
    opacity_slider_->setAccessibleName(QStringLiteral("Layer or group opacity"));
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
    connect(delete_button_, &QToolButton::clicked, this, [this]() {
        if (selectedItemIsGroup()) emit deleteGroupRequested(selectedItemId());
        else emit deleteLayerRequested(selectedItemId());
    });
    connect(ungroup_button_, &QToolButton::clicked, this,
            [this]() { emit ungroupRequested(selectedItemId()); });
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

    tree->drop_handler = [this](QTreeWidgetItem* source,
                                QTreeWidgetItem* target,
                                QAbstractItemView::DropIndicatorPosition indicator) {
        if (source == nullptr || source->data(0, kBackgroundRole).toBool()) return;
        const QString source_id = source->data(0, kItemIdRole).toString();
        const bool source_group = source->data(0, kGroupRole).toBool();
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

void LayerPanel::setDocument(const ImageDocumentData& document,
                             const QString& selected_layer_id,
                             const QString& selected_group_id,
                             const QHash<QString, QImage>& thumbnails) {
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
    layer_tree_->clear();
    QTreeWidgetItem* selected = nullptr;
    for (auto root = document.root_stack.crbegin(); root != document.root_stack.crend(); ++root) {
        if (!root->group) {
            const auto layer = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&root](const ImageLayerData& value) { return value.id == root->id; });
            if (layer == document.layers.cend()) continue;
            auto* item = makeLayerItem(*layer, thumbnails);
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
            ? QStringLiteral("Visible group. Click the eye button to hide its contents.")
            : QStringLiteral("Hidden group. Click the eye button to show its contents."));
        group_item->setData(0, Qt::AccessibleDescriptionRole, group->visible
            ? QStringLiteral("Visible group. Eye button hides its contents.")
            : QStringLiteral("Hidden group. Eye button shows its contents."));
        group_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable |
                             Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        layer_tree_->addTopLevelItem(group_item);
        for (auto child_id = group->layer_ids.crbegin();
             child_id != group->layer_ids.crend(); ++child_id) {
            const auto child = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&child_id](const ImageLayerData& value) { return value.id == *child_id; });
            if (child == document.layers.cend()) continue;
            auto* child_item = makeLayerItem(*child, thumbnails, group_item);
            if (child->id == selected_layer_id) selected = child_item;
        }
        group_item->setExpanded(expanded.isEmpty() || expanded.contains(group->id));
        if (group->id == selected_group_id) selected = group_item;
    }
    if (selected != nullptr) layer_tree_->setCurrentItem(selected);
    refreshing_ = false;
    updateControls();
}

void LayerPanel::setQuickExportEnabled(bool enabled) {
    quick_export_button_->setEnabled(enabled);
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* event) {
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
    if (direction != -1 && direction != 1 || item_id.isEmpty()) return;
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
    edit_hint_->setVisible(has_layer && layer->background);
    add_button_->setEnabled(!document_.root_stack.isEmpty());
    group_selected_button_->setEnabled(canGroupSelectedLayers());
    delete_button_->setEnabled(editable);
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
