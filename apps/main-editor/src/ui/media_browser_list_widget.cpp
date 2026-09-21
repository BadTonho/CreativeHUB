#include "media_browser_list_widget.h"

#include "media_drag_mime.h"

#include <QHelpEvent>
#include <QIcon>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QVariant>

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

void MediaBrowserListWidget::applyDisplayMode() {
    if (display_mode_ == DisplayMode::Grid) {
        QListWidget::setViewMode(QListView::IconMode);
        setIconSize(QSize(128, 72));
        setGridSize(QSize(168, 126));
        setSpacing(4);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setWordWrap(true);
        setUniformItemSizes(true);
        return;
    }

    QListWidget::setViewMode(QListView::ListMode);
    setIconSize(QSize(48, 32));
    setGridSize(QSize());
    setSpacing(2);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setWordWrap(true);
    setUniformItemSizes(false);
}

QMimeData* MediaBrowserListWidget::mimeData(
    const QList<QListWidgetItem*>& items) const {
    auto* mime_data = new QMimeData;
    if (items.isEmpty()) return mime_data;

    const auto* item = items.front();
    if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
        media_browser_ui::kMediaItemTypeBin) {
        return mime_data;
    }

    const QString source_path = item->data(Qt::UserRole).toString();
    if (!source_path.isEmpty()) {
        mime_data->setData(ui::kMediaPathMimeType, source_path.toUtf8());
    }
    return mime_data;
}
