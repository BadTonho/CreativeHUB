#include "layer_panel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace image_editor {
namespace {

constexpr int kLayerIdRole = Qt::UserRole + 1;
constexpr int kBackgroundRole = Qt::UserRole + 2;
constexpr int kVisibleRole = Qt::UserRole + 3;
constexpr int kThumbnailRole = Qt::UserRole + 4;
constexpr int kLayerRowHeight = 46;

QRect eyeButtonRect(const QRect& row) {
    return QRect(row.right() - 32, row.center().y() - 13, 26, 26);
}

QRect eyeHitRect(const QRect& row) {
    return QRect(row.right() - 36, row.top(), 36, row.height());
}

void drawCheckerboard(QPainter* painter, const QRect& rect, const QPalette& palette) {
    constexpr int cell_size = 8;
    const QColor light = palette.color(QPalette::Base).lighter(135);
    const QColor dark = palette.color(QPalette::Base).darker(125);
    painter->fillRect(rect, light);
    for (int y = 0; y < rect.height(); y += cell_size) {
        for (int x = 0; x < rect.width(); x += cell_size) {
            if (((x / cell_size) + (y / cell_size)) % 2 == 0) {
                painter->fillRect(QRect(rect.left() + x, rect.top() + y,
                                        cell_size, cell_size).intersected(rect), dark);
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
        drawCheckerboard(painter, thumbnail_rect, option.palette);

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

} // namespace

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("imageEditorLayerPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    layer_list_ = new QListWidget(this);
    layer_list_->setObjectName(QStringLiteral("imageLayerList"));
    layer_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layer_list_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed |
                                 QAbstractItemView::SelectedClicked);
    layer_list_->setItemDelegate(new LayerItemDelegate(layer_list_));
    layer_list_->setMouseTracking(true);
    layer_list_->viewport()->setMouseTracking(true);
    layer_list_->viewport()->installEventFilter(this);
    layout->addWidget(layer_list_, 1);

    edit_hint_ = new QLabel(this);
    edit_hint_->setObjectName(QStringLiteral("layerEditingHint"));
    edit_hint_->setWordWrap(true);
    edit_hint_->setText(QStringLiteral("Select or create an editable layer to paint or transform."));
    layout->addWidget(edit_hint_);

    auto* actions = new QHBoxLayout;
    add_button_ = new QToolButton(this);
    add_button_->setObjectName(QStringLiteral("addImageLayerButton"));
    add_button_->setText(QStringLiteral("+"));
    add_button_->setToolTip(QStringLiteral("Add layer"));
    actions->addWidget(add_button_);
    delete_button_ = new QToolButton(this);
    delete_button_->setObjectName(QStringLiteral("deleteImageLayerButton"));
    delete_button_->setText(QStringLiteral("−"));
    delete_button_->setToolTip(QStringLiteral("Delete layer"));
    actions->addWidget(delete_button_);
    rename_button_ = new QToolButton(this);
    rename_button_->setObjectName(QStringLiteral("renameImageLayerButton"));
    rename_button_->setText(QStringLiteral("Rename"));
    rename_button_->setToolTip(QStringLiteral("Rename layer"));
    actions->addWidget(rename_button_);
    move_up_button_ = new QToolButton(this);
    move_up_button_->setObjectName(QStringLiteral("moveImageLayerUpButton"));
    move_up_button_->setText(QStringLiteral("↑"));
    move_up_button_->setToolTip(QStringLiteral("Move layer up"));
    actions->addWidget(move_up_button_);
    move_down_button_ = new QToolButton(this);
    move_down_button_->setObjectName(QStringLiteral("moveImageLayerDownButton"));
    move_down_button_->setText(QStringLiteral("↓"));
    move_down_button_->setToolTip(QStringLiteral("Move layer down"));
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
    opacity_slider_->setAccessibleName(QStringLiteral("Layer opacity"));
    opacity_row->addWidget(opacity_slider_, 1);
    layout->addLayout(opacity_row);

    connect(layer_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current) {
                if (refreshing_ || current == nullptr) return;
                emit layerSelected(current->data(kLayerIdRole).toString());
                updateControls();
            });
    connect(layer_list_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                if (refreshing_ || item == nullptr) return;
                const QString id = item->data(kLayerIdRole).toString();
                const auto found = std::find_if(layers_.cbegin(), layers_.cend(),
                    [&id](const ImageLayerData& layer) { return layer.id == id; });
                if (found != layers_.cend() && item->text() != found->name &&
                    !found->background) {
                    emit layerRenamed(id, item->text());
                }
            });
    connect(add_button_, &QToolButton::clicked, this,
            &LayerPanel::addLayerRequested);
    connect(delete_button_, &QToolButton::clicked, this,
            [this]() { emit deleteLayerRequested(selectedLayerId()); });
    connect(rename_button_, &QToolButton::clicked, this, [this]() {
        if (layer_list_->currentItem() != nullptr &&
            !layer_list_->currentItem()->data(kBackgroundRole).toBool()) {
            layer_list_->editItem(layer_list_->currentItem());
        }
    });
    connect(move_up_button_, &QToolButton::clicked, this,
            [this]() { emit moveLayerRequested(selectedLayerId(), 1); });
    connect(move_down_button_, &QToolButton::clicked, this,
            [this]() { emit moveLayerRequested(selectedLayerId(), -1); });
    connect(opacity_slider_, &QSlider::sliderPressed, this,
            &LayerPanel::opacityEditStarted);
    connect(opacity_slider_, &QSlider::sliderReleased, this,
            &LayerPanel::opacityEditFinished);
    connect(opacity_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (!refreshing_ && !selectedLayerId().isEmpty()) {
            emit layerOpacityChanged(selectedLayerId(), value);
        }
    });
    updateControls();
}

void LayerPanel::setLayers(const QVector<ImageLayerData>& layers,
                           const QString& selected_layer_id,
                           const QHash<QString, QImage>& thumbnails) {
    refreshing_ = true;
    const QSignalBlocker blocker(layer_list_);
    layers_ = layers;
    layer_list_->clear();
    QListWidgetItem* selected = nullptr;
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        auto* item = new QListWidgetItem(it->name, layer_list_);
        item->setData(kLayerIdRole, it->id);
        item->setData(kBackgroundRole, it->background);
        item->setData(kVisibleRole, it->visible);
        item->setData(kThumbnailRole, thumbnails.value(it->id));
        item->setData(Qt::ToolTipRole, it->background
            ? (it->visible
                ? QStringLiteral("Locked Background. Click the eye button to hide it.")
                : QStringLiteral("Locked Background. Click the eye button to show it."))
            : (it->visible
                ? QStringLiteral("Visible — click the eye button to hide this layer.")
                : QStringLiteral("Hidden — click the eye button to show this layer.")));
        item->setData(Qt::AccessibleDescriptionRole, it->visible
            ? QStringLiteral("Visible. Eye button hides this layer.")
            : QStringLiteral("Hidden. Eye button shows this layer."));
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (!it->background) flags |= Qt::ItemIsEditable;
        item->setFlags(flags);
        if (it->id == selected_layer_id) selected = item;
    }
    if (selected != nullptr) layer_list_->setCurrentItem(selected);
    const auto active = std::find_if(layers.cbegin(), layers.cend(),
        [&selected_layer_id](const ImageLayerData& layer) {
            return layer.id == selected_layer_id;
        });
    {
        const QSignalBlocker opacity_blocker(opacity_slider_);
        opacity_slider_->setValue(active == layers.cend() ? 100 : active->opacity);
    }
    refreshing_ = false;
    updateControls();
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == layer_list_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            QListWidgetItem* item = layer_list_->itemAt(mouse->pos());
            const bool over_eye = item != nullptr &&
                eyeHitRect(layer_list_->visualItemRect(item)).contains(mouse->pos());
            layer_list_->viewport()->setCursor(over_eye ? Qt::PointingHandCursor
                                                         : Qt::ArrowCursor);
        }

        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            QListWidgetItem* item = layer_list_->itemAt(mouse->pos());
            if (mouse->button() == Qt::LeftButton && item != nullptr &&
                eyeHitRect(layer_list_->visualItemRect(item)).contains(mouse->pos())) {
                eye_press_consumed_ = true;
                if (event->type() == QEvent::MouseButtonPress) {
                    const QString id = item->data(kLayerIdRole).toString();
                    emit layerVisibilityChanged(id,
                        !item->data(kVisibleRole).toBool());
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

QString LayerPanel::selectedLayerId() const {
    return layer_list_->currentItem() == nullptr
        ? QString{} : layer_list_->currentItem()->data(kLayerIdRole).toString();
}

void LayerPanel::updateControls() {
    const QString id = selectedLayerId();
    const auto active = std::find_if(layers_.cbegin(), layers_.cend(),
        [&id](const ImageLayerData& layer) { return layer.id == id; });
    const bool selected = active != layers_.cend();
    const bool editable = selected && !active->background;
    edit_hint_->setVisible(selected && active->background);
    add_button_->setEnabled(!layers_.isEmpty());
    delete_button_->setEnabled(editable);
    rename_button_->setEnabled(editable);
    move_up_button_->setEnabled(editable && active + 1 != layers_.cend());
    move_down_button_->setEnabled(editable && active != layers_.cbegin() + 1);
    opacity_slider_->setEnabled(editable);
}

} // namespace image_editor
