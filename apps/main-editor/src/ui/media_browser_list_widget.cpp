#include "media_browser_list_widget.h"

#include "media_drag_mime.h"

#include <QApplication>
#include <QDrag>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QIcon>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QVariant>

#include <algorithm>

namespace {

class MediaBrowserItemDelegate final : public QStyledItemDelegate {
public:
    explicit MediaBrowserItemDelegate(
        const MediaBrowserListWidget* list,
        QObject* parent = nullptr)
        : QStyledItemDelegate(parent), list_(list) {}

    void paint(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);

        const auto info = index.data(
            media_browser_ui::kMediaInfoRole).toString();
        if (info.isEmpty() || option.widget == nullptr) return;

        const auto icon = option.widget->style()->standardIcon(
            QStyle::SP_MessageBoxInformation);
        icon.paint(
            painter,
            infoIconRect(option.rect),
            Qt::AlignCenter,
            (option.state & QStyle::State_MouseOver)
                ? QIcon::Active
                : QIcon::Normal);
    }

    bool helpEvent(
        QHelpEvent* event,
        QAbstractItemView* view,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) override {
        if (event != nullptr && view != nullptr &&
            infoIconRect(option.rect).contains(event->pos())) {
            const auto info = index.data(
                media_browser_ui::kMediaInfoRole).toString();
            if (!info.isEmpty()) {
                QToolTip::showText(event->globalPos(), info, view->viewport());
                return true;
            }
        }
        QToolTip::hideText();
        return false;
    }

    void setEditorData(
        QWidget* editor,
        const QModelIndex& index) const override {
        QStyledItemDelegate::setEditorData(editor, index);
        auto* line_edit = qobject_cast<QLineEdit*>(editor);
        if (line_edit == nullptr) return;

        const auto full_name = index.data(
            media_browser_ui::kMediaFullDisplayNameRole).toString();
        if (full_name.isEmpty()) return;
        line_edit->setText(full_name);
        line_edit->selectAll();
    }

private:
    [[nodiscard]] QRect infoIconRect(const QRect& item_rect) const noexcept {
        const int icon_size = list_->displayMode() ==
                MediaBrowserListWidget::DisplayMode::Grid
            ? 20
            : 18;
        const int margin = 4;
        return QRect(
            item_rect.right() - icon_size - margin,
            item_rect.top() + margin,
            icon_size,
            icon_size);
    }

    const MediaBrowserListWidget* list_ = nullptr;
};

} // namespace

namespace media_browser_ui {

QPixmap createDragPreview(const QIcon& icon, const QString& label) {
    constexpr int padding = 8;
    constexpr int corner_radius = 8;
    const QSize image_size(kDragPreviewImageWidth, kDragPreviewImageHeight);
    const QFontMetrics font_metrics(QApplication::font());
    const int text_height = std::max(1, font_metrics.height());
    const QSize preview_size(
        image_size.width() + padding * 2,
        image_size.height() + padding * 2 + text_height);

    QPixmap preview(preview_size);
    preview.fill(Qt::transparent);

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 255, 255, 70)));
    painter.setBrush(QColor(28, 28, 32, 235));
    painter.drawRoundedRect(
        preview.rect().adjusted(0, 0, -1, -1),
        corner_radius,
        corner_radius);

    const QPixmap source = icon.pixmap(image_size);
    if (!source.isNull()) {
        const QPixmap scaled = source.scaled(
            image_size,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        const QRect image_rect(
            padding + (image_size.width() - scaled.width()) / 2,
            padding + (image_size.height() - scaled.height()) / 2,
            scaled.width(),
            scaled.height());
        painter.drawPixmap(image_rect, scaled);
    }

    const QRect text_rect(
        padding,
        padding + image_size.height(),
        image_size.width(),
        text_height);
    painter.setPen(Qt::white);
    painter.drawText(
        text_rect,
        Qt::AlignCenter,
        font_metrics.elidedText(label, Qt::ElideRight, text_rect.width()));
    return preview;
}

QMimeData* createMediaBrowserDragMimeData(
    const QList<QListWidgetItem*>& items) {
    auto* mime_data = new QMimeData;
    if (items.isEmpty()) return mime_data;

    const auto* item = items.front();
    if (item->data(kMediaItemTypeRole).toInt() == kMediaItemTypeBin) {
        return mime_data;
    }

    const QString source_path = item->data(Qt::UserRole).toString();
    if (!source_path.isEmpty()) {
        mime_data->setData(ui::kMediaPathMimeType, source_path.toUtf8());
    }
    return mime_data;
}

} // namespace media_browser_ui

MediaBrowserListWidget::MediaBrowserListWidget(QWidget* parent)
    : QListWidget(parent) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
    setEditTriggers(
        QAbstractItemView::DoubleClicked |
        QAbstractItemView::EditKeyPressed);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setItemDelegate(new MediaBrowserItemDelegate(this, this));

    QSettings settings;
    const auto saved_mode = settings.value(
        "media_browser/view_mode", "list").toString();
    display_mode_ = saved_mode == "grid" ? DisplayMode::Grid : DisplayMode::List;
    icon_scale_percent_ = std::clamp(
        settings.value(
            "media_browser/icon_scale_percent",
            kDefaultIconScalePercent).toInt(),
        kMinimumIconScalePercent,
        kMaximumIconScalePercent);
    applyDisplayMode();
}

MediaBrowserListWidget::DisplayMode
MediaBrowserListWidget::displayMode() const noexcept {
    return display_mode_;
}

void MediaBrowserListWidget::setDisplayMode(DisplayMode mode) {
    if (display_mode_ == mode) {
        applyDisplayMode();
        return;
    }
    display_mode_ = mode;
    applyDisplayMode();

    QSettings settings;
    settings.setValue(
        "media_browser/view_mode",
        display_mode_ == DisplayMode::Grid ? "grid" : "list");
    settings.sync();
}

int MediaBrowserListWidget::iconScalePercent() const noexcept {
    return icon_scale_percent_;
}

void MediaBrowserListWidget::setIconScalePercent(int percent) {
    const auto normalized = std::clamp(
        percent,
        kMinimumIconScalePercent,
        kMaximumIconScalePercent);
    if (icon_scale_percent_ == normalized) {
        applyDisplayMode();
        return;
    }

    icon_scale_percent_ = normalized;
    applyDisplayMode();

    QSettings settings;
    settings.setValue("media_browser/icon_scale_percent", icon_scale_percent_);
    settings.sync();
}

void MediaBrowserListWidget::applyDisplayMode() {
    const auto scaledSize = [this](const QSize& base_size) {
        return QSize(
            std::max(1, (base_size.width() * icon_scale_percent_ + 50) / 100),
            std::max(1, (base_size.height() * icon_scale_percent_ + 50) / 100));
    };

    if (display_mode_ == DisplayMode::Grid) {
        QListWidget::setViewMode(QListView::IconMode);
        const auto icon_size = scaledSize(QSize(128, 72));
        setIconSize(icon_size);
        setGridSize(QSize(icon_size.width() + 40, icon_size.height() + 54));
        setSpacing(4);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setWordWrap(true);
        setUniformItemSizes(true);
        return;
    }

    QListWidget::setViewMode(QListView::ListMode);
    setIconSize(scaledSize(QSize(48, 32)));
    setGridSize(QSize());
    setSpacing(2);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setWordWrap(true);
    setUniformItemSizes(false);
}

void MediaBrowserListWidget::startDrag(Qt::DropActions supportedActions) {
    const auto items = selectedItems();
    if (items.isEmpty()) return;

    auto* drag = new QDrag(this);
    drag->setMimeData(media_browser_ui::createMediaBrowserDragMimeData(items));

    const auto* item = items.front();
    const auto preview = media_browser_ui::createDragPreview(
        item->icon(),
        item->text());
    if (!preview.isNull()) {
        drag->setPixmap(preview);
        drag->setHotSpot(QPoint(preview.width() / 2, preview.height() / 2));
    }

    const auto actions = supportedActions == Qt::IgnoreAction
        ? Qt::CopyAction
        : supportedActions;
    const auto default_action = defaultDropAction() == Qt::IgnoreAction
        ? Qt::CopyAction
        : defaultDropAction();
    drag->exec(actions, default_action);
}

QMimeData* MediaBrowserListWidget::mimeData(
    const QList<QListWidgetItem*>& items) const {
    return media_browser_ui::createMediaBrowserDragMimeData(items);
}
