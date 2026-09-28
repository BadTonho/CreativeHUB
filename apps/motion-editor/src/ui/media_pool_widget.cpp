#include "media_pool_widget.h"

#include <creative_suite/diagnostics/logger.h>

#include <QAbstractItemView>
#include <QAction>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QMetaObject>
#include <QPixmap>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRunnable>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QPalette>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <map>
#include <utility>

namespace motion::ui {
namespace {

using creative_suite::media::MediaItem;
using creative_suite::media::MediaKind;
using creative_suite::media::MediaLibrary;

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kAllMediaRole = Qt::UserRole + 2;
const QString kMotionMediaMimeType = QStringLiteral("application/x-creative-suite-motion-media");

QString pathText(const std::filesystem::path& path)
{
    const auto utf8 = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()),
                             static_cast<qsizetype>(utf8.size()));
}

QString utf8Text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::filesystem::path filePath(const QString& path)
{
    const auto utf8 = path.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first, first + utf8.size()));
}

QString mediaKindText(MediaKind kind)
{
    return kind == MediaKind::Image ? QStringLiteral("Image") : QStringLiteral("Video");
}

QString optionalNumber(const std::optional<double>& value, const QString& suffix = {})
{
    return value.has_value()
        ? QString::number(*value, 'g', 8) + suffix
        : QStringLiteral("—");
}

QString optionalInteger(const std::optional<std::int64_t>& value)
{
    return value.has_value() ? QString::number(*value) : QStringLiteral("—");
}

QPixmap thumbnailFor(const MediaItem& item, bool large)
{
    if (item.offline || item.first_frame.width <= 0 || item.first_frame.height <= 0 ||
        item.first_frame.rgba_pixels.empty()) {
        return {};
    }
    const QImage image(item.first_frame.rgba_pixels.data(),
                       item.first_frame.width,
                       item.first_frame.height,
                       item.first_frame.stride,
                       QImage::Format_RGBA8888);
    return QPixmap::fromImage(image.copy()).scaled(
        large ? QSize(144, 82) : QSize(96, 54),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
}

class DraggableMediaListWidget final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override
    {
        return {kMotionMediaMimeType};
    }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        auto* payload = new QMimeData();
        if (!items.empty() && items.front() != nullptr) {
            payload->setData(kMotionMediaMimeType,
                             items.front()->data(kPathRole).toString().toUtf8());
        }
        return payload;
    }

    void startDrag(Qt::DropActions) override
    {
        QListWidget::startDrag(Qt::CopyAction);
    }
};

} // namespace

class ImportTask final : public QRunnable {
public:
    ImportTask(MediaPoolWidget* owner,
               std::vector<std::filesystem::path> paths,
               std::shared_ptr<std::atomic_bool> cancel,
               std::uint64_t generation)
        : owner_(owner), paths_(std::move(paths)), cancel_(std::move(cancel)),
          generation_(generation)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QPointer<MediaPoolWidget> owner = owner_;
        const auto generation = generation_;
        const auto result = creative_suite::media::MediaImporter{}.process(
            paths_, *cancel_, [owner, generation](std::size_t completed,
                                      std::size_t total,
                                      const std::filesystem::path& path) {
                if (owner.isNull()) return;
                QMetaObject::invokeMethod(
                    owner.data(),
                    [owner, completed, total, path, generation] {
                        if (!owner.isNull())
                            owner->updateProgress(generation, completed, total, path);
                    },
                    Qt::QueuedConnection);
            });
        if (owner.isNull()) return;
        QMetaObject::invokeMethod(
            owner.data(),
            [owner, result = std::move(result), generation]() mutable {
                if (!owner.isNull()) owner->finishImport(generation, std::move(result));
            },
            Qt::QueuedConnection);
    }

private:
    QPointer<MediaPoolWidget> owner_;
    std::vector<std::filesystem::path> paths_;
    std::shared_ptr<std::atomic_bool> cancel_;
    std::uint64_t generation_ = 0;
};

MediaPoolWidget::MediaPoolWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-media-pool"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* title = new QLabel(QStringLiteral("Media Pool"), this);
    title->setObjectName(QStringLiteral("motion-media-pool-title"));
    layout->addWidget(title);

    auto* actions = new QWidget(this);
    auto* actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(0, 0, 0, 0);
    auto* import_button = new QPushButton(QStringLiteral("Import Media..."), actions);
    import_button->setObjectName(QStringLiteral("motion-media-import-button"));
    auto* new_bin_button = new QPushButton(QStringLiteral("New Bin"), actions);
    new_bin_button->setObjectName(QStringLiteral("motion-media-new-bin-button"));
    actions_layout->addWidget(import_button);
    actions_layout->addWidget(new_bin_button);
    layout->addWidget(actions);

    auto* view_actions = new QWidget(this);
    auto* view_layout = new QHBoxLayout(view_actions);
    view_layout->setContentsMargins(0, 0, 0, 0);
    auto* bins_title = new QLabel(QStringLiteral("Bins"), view_actions);
    list_mode_button_ = new QToolButton(view_actions);
    list_mode_button_->setObjectName(QStringLiteral("motion-media-list-mode"));
    list_mode_button_->setText(QStringLiteral("List"));
    list_mode_button_->setCheckable(true);
    list_mode_button_->setChecked(true);
    thumbnail_mode_button_ = new QToolButton(view_actions);
    thumbnail_mode_button_->setObjectName(QStringLiteral("motion-media-thumbnail-mode"));
    thumbnail_mode_button_->setText(QStringLiteral("Thumbnails"));
    thumbnail_mode_button_->setCheckable(true);
    view_layout->addWidget(bins_title);
    view_layout->addStretch(1);
    view_layout->addWidget(list_mode_button_);
    view_layout->addWidget(thumbnail_mode_button_);
    layout->addWidget(view_actions);

    bins_tree_ = new QTreeWidget(this);
    bins_tree_->setObjectName(QStringLiteral("motion-media-bins"));
    bins_tree_->setHeaderHidden(true);
    bins_tree_->setMinimumHeight(100);
    bins_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(bins_tree_, 1);

    media_list_ = new DraggableMediaListWidget(this);
    media_list_->setObjectName(QStringLiteral("motion-media-items"));
    media_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    media_list_->setDragEnabled(true);
    media_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    media_list_->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                 QAbstractItemView::SelectedClicked);
    media_list_->setViewMode(QListView::ListMode);
    layout->addWidget(media_list_, 3);

    status_label_ = new QLabel(QStringLiteral("No media imported"), this);
    status_label_->setObjectName(QStringLiteral("motion-media-status"));
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);

    connect(import_button, &QPushButton::clicked, this, [this] {
        if (import_requested_handler_) import_requested_handler_();
    });
    connect(new_bin_button, &QPushButton::clicked, this, [this] { createBin(); });
    connect(list_mode_button_, &QToolButton::clicked, this, [this] { setThumbnailMode(false); });
    connect(thumbnail_mode_button_, &QToolButton::clicked, this,
            [this] { setThumbnailMode(true); });
    connect(media_list_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) { handleMediaItemChanged(item); });
    connect(media_list_, &QListWidget::currentItemChanged, this,
            [this] {
                if (selection_changed_handler_) selection_changed_handler_();
            });
    connect(media_list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& position) { showMediaContextMenu(position); });
    connect(bins_tree_, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) { handleBinItemChanged(item, column); });
    connect(bins_tree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& position) { showBinContextMenu(position); });
    connect(bins_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { refreshMedia(); });

    refresh();
}

MediaPoolWidget::~MediaPoolWidget()
{
    if (cancel_requested_) cancel_requested_->store(true, std::memory_order_relaxed);
    ++import_generation_;
}

const MediaLibrary& MediaPoolWidget::library() const noexcept
{
    return library_;
}

QListWidget* MediaPoolWidget::mediaListWidget() const noexcept
{
    return media_list_;
}

const MediaItem* MediaPoolWidget::selectedMedia() const noexcept
{
    if (media_list_ == nullptr || media_list_->currentItem() == nullptr) return nullptr;
    const auto index = library_.indexForPath(
        filePath(media_list_->currentItem()->data(kPathRole).toString()));
    return index < library_.size() ? &library_.items()[index] : nullptr;
}

creative_suite::media::RgbaFramePtr MediaPoolWidget::sharedFirstFrameForPath(
    const std::filesystem::path& path) const
{
    const auto canonical = MediaLibrary::canonicalPath(path);
    const auto cached = shared_frame_cache_.find(canonical);
    if (cached != shared_frame_cache_.end()) return cached->second;
    const auto index = library_.indexForPath(canonical);
    if (index >= library_.size()) return {};
    const auto& frame = library_.items()[index].first_frame;
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba_pixels.empty()) return {};
    auto shared = std::make_shared<const creative_suite::media::VideoFrame>(frame);
    shared_frame_cache_.emplace(canonical, shared);
    return shared;
}

void MediaPoolWidget::setImportRequestedHandler(std::function<void()> handler)
{
    import_requested_handler_ = std::move(handler);
}

void MediaPoolWidget::setSelectionChangedHandler(std::function<void()> handler)
{
    selection_changed_handler_ = std::move(handler);
}

void MediaPoolWidget::clear()
{
    if (cancel_requested_) cancel_requested_->store(true, std::memory_order_relaxed);
    cancel_requested_.reset();
    ++import_generation_;
    if (progress_ != nullptr) progress_->hide();
    library_.clear();
    shared_frame_cache_.clear();
    refresh();
    status_label_->setText(QStringLiteral("No media imported"));
}

void MediaPoolWidget::importFiles(std::vector<std::filesystem::path> paths)
{
    std::erase_if(paths, [](const auto& path) { return path.empty(); });
    if (paths.empty() || cancel_requested_) return;

    if (progress_ == nullptr) {
        progress_ = new QProgressDialog(QStringLiteral("Importing media..."),
                                        QStringLiteral("Cancel"), 0, 0, this);
        progress_->setObjectName(QStringLiteral("motion-media-import-progress"));
        progress_->setWindowTitle(QStringLiteral("Import Media"));
        progress_->setWindowModality(Qt::WindowModal);
        progress_->setMinimumDuration(250);
        connect(progress_, &QProgressDialog::canceled, this, [this] {
            if (cancel_requested_) cancel_requested_->store(true, std::memory_order_relaxed);
        });
    }

    cancel_requested_ = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++import_generation_;
    progress_->setRange(0, static_cast<int>(std::min<std::size_t>(
        paths.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    progress_->setValue(0);
    progress_->setLabelText(QStringLiteral("Preparing media import..."));
    progress_->show();
    QThreadPool::globalInstance()->start(
        new ImportTask(this, std::move(paths), cancel_requested_, generation));
}

void MediaPoolWidget::refresh(const QString& selected_path)
{
    refreshBins(QString{});
    refreshMedia(selected_path);
}

void MediaPoolWidget::refreshBins(const QString& selected_bin)
{
    refreshing_ = true;
    const QSignalBlocker blocker(bins_tree_);
    bins_tree_->clear();
    auto* all = new QTreeWidgetItem(bins_tree_, {QStringLiteral("All Media")});
    all->setData(0, kPathRole, QString{});
    all->setData(0, kAllMediaRole, true);

    std::map<std::string, QTreeWidgetItem*> nodes;
    for (const auto& bin : library_.bins()) {
        QStringList components = QString::fromUtf8(
            bin.data(), static_cast<qsizetype>(bin.size())).split(QLatin1Char('/'));
        QString full_path;
        QTreeWidgetItem* parent = all;
        std::string accumulated;
        for (const auto& component : components) {
            if (!accumulated.empty()) accumulated += '/';
            accumulated += component.toUtf8().toStdString();
            full_path += (full_path.isEmpty() ? QString{} : QStringLiteral("/")) + component;
            auto found = nodes.find(accumulated);
            if (found == nodes.end()) {
                auto* child = new QTreeWidgetItem(parent, {component});
                child->setData(0, kPathRole, full_path);
                child->setFlags(child->flags() | Qt::ItemIsEditable);
                found = nodes.emplace(accumulated, child).first;
            }
            parent = found->second;
        }
    }
    all->setExpanded(true);

    QTreeWidgetItem* selected = all;
    if (!selected_bin.isEmpty()) {
        for (auto it = nodes.begin(); it != nodes.end(); ++it) {
            if (it->second->data(0, kPathRole).toString() == selected_bin) {
                selected = it->second;
                break;
            }
        }
    }
    bins_tree_->setCurrentItem(selected);
    refreshing_ = false;
    if (selection_changed_handler_) selection_changed_handler_();
}

void MediaPoolWidget::refreshMedia(const QString& selected_path)
{
    if (media_list_ == nullptr) return;
    refreshing_ = true;
    const QSignalBlocker blocker(media_list_);
    media_list_->clear();
    const auto* bin_item = bins_tree_->currentItem();
    const QString bin = bin_item == nullptr ? QString{} : bin_item->data(0, kPathRole).toString();
    const bool thumbnails = media_list_->viewMode() == QListView::IconMode;
    int selected_row = -1;
    int row = 0;
    for (const auto& item : library_.items()) {
        const auto index = library_.indexForPath(item.metadata.source_path);
        if (!library_.isInBin(index, bin.toUtf8().toStdString())) continue;
        auto* entry = new QListWidgetItem(utf8Text(item.display_name), media_list_);
        entry->setData(kPathRole, pathText(item.metadata.source_path));
        entry->setToolTip(pathText(item.metadata.source_path));
        entry->setFlags(entry->flags() | Qt::ItemIsEditable);
        if (thumbnails) entry->setIcon(thumbnailFor(item, true));
        else if (!item.offline) entry->setIcon(thumbnailFor(item, false));
        if (item.offline) {
            entry->setText(utf8Text(item.display_name) + QStringLiteral("  (Offline)"));
            entry->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        if (pathText(item.metadata.source_path) == selected_path) selected_row = row;
        ++row;
    }
    if (selected_row >= 0) media_list_->setCurrentRow(selected_row);
    refreshing_ = false;
    if (selection_changed_handler_) selection_changed_handler_();
}

void MediaPoolWidget::handleMediaItemChanged(QListWidgetItem* item)
{
    if (refreshing_ || item == nullptr) return;
    const auto path = filePath(item->data(kPathRole).toString());
    const auto index = library_.indexForPath(path);
    if (index >= library_.size()) return;
    const auto name = item->text().remove(QStringLiteral("  (Offline)"));
    const auto result = library_.rename(index, name.toUtf8().toStdString());
    if (result != creative_suite::media::MediaMutationResult::Changed &&
        result != creative_suite::media::MediaMutationResult::NoChange) {
        refreshMedia(pathText(path));
    }
    if (selection_changed_handler_) selection_changed_handler_();
}

void MediaPoolWidget::handleBinItemChanged(QTreeWidgetItem* item, int column)
{
    if (refreshing_ || item == nullptr || column != 0 ||
        item->data(0, kAllMediaRole).toBool()) return;
    const auto old_path = item->data(0, kPathRole).toString();
    const auto parent = item->parent();
    const QString parent_path = parent == nullptr || parent->data(0, kAllMediaRole).toBool()
        ? QString{} : parent->data(0, kPathRole).toString();
    const QString new_path = parent_path.isEmpty()
        ? item->text(0) : parent_path + QStringLiteral("/") + item->text(0);
    if (new_path == old_path) return;
    const auto result = library_.renameBin(old_path.toUtf8().toStdString(),
                                           new_path.toUtf8().toStdString());
    const QString refresh_path = result == creative_suite::media::MediaMutationResult::Changed
        ? new_path : old_path;
    QTimer::singleShot(0, this, [this, refresh_path] {
        refresh(refresh_path);
        if (selection_changed_handler_) selection_changed_handler_();
    });
}

void MediaPoolWidget::showMediaContextMenu(const QPoint& position)
{
    auto* item = media_list_->itemAt(position);
    if (item == nullptr) return;
    media_list_->setCurrentItem(item);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* rename = menu->addAction(QStringLiteral("Rename"));
    connect(rename, &QAction::triggered, this, [this, item] { media_list_->editItem(item); });
    const auto path = filePath(item->data(kPathRole).toString());
    const auto index = library_.indexForPath(path);
    if (index >= library_.size()) {
        menu->deleteLater();
        return;
    }
    auto* move = menu->addMenu(QStringLiteral("Move to Bin"));
    for (const auto& bin : library_.bins()) {
        const auto bin_text = QString::fromUtf8(
            bin.data(), static_cast<qsizetype>(bin.size()));
        QAction* destination = move->addAction(bin_text);
        destination->setEnabled(bin_text != QString::fromUtf8(
            library_.items()[index].bin_path.data(),
            static_cast<qsizetype>(library_.items()[index].bin_path.size())));
        connect(destination, &QAction::triggered, this, [this, path, bin] {
            const auto media_index = library_.indexForPath(path);
            if (media_index >= library_.size()) return;
            const auto result = library_.moveToBin(media_index, bin);
            if (result == creative_suite::media::MediaMutationResult::Changed ||
                result == creative_suite::media::MediaMutationResult::NoChange) {
                QTimer::singleShot(0, this, [this, path, bin] {
                    refreshMedia(pathText(path));
                    status_label_->setText(QStringLiteral("Moved to %1")
                        .arg(QString::fromUtf8(bin.data(), static_cast<qsizetype>(bin.size()))));
                });
            }
        });
    }
    menu->addSeparator();
    auto* offline = menu->addAction(library_.items()[index].offline
        ? QStringLiteral("Restore Media") : QStringLiteral("Mark Offline"));
    connect(offline, &QAction::triggered, this, [this, path, was_offline = library_.items()[index].offline] {
        const auto current_index = library_.indexForPath(path);
        if (current_index >= library_.size()) return;
        if (was_offline) restoreSelectedMedia();
        else markSelectedMediaOffline();
    });
    menu->popup(media_list_->viewport()->mapToGlobal(position));
}

void MediaPoolWidget::showBinContextMenu(const QPoint& position)
{
    auto* item = bins_tree_->itemAt(position);
    if (item == nullptr) return;
    bins_tree_->setCurrentItem(item);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* create = menu->addAction(QStringLiteral("New Bin"));
    connect(create, &QAction::triggered, this, [this] { createBin(); });
    if (!item->data(0, kAllMediaRole).toBool()) {
        auto* rename = menu->addAction(QStringLiteral("Rename Bin"));
        auto* move = menu->addAction(QStringLiteral("Move Bin..."));
        connect(rename, &QAction::triggered, this, [this] { renameSelectedBin(); });
        connect(move, &QAction::triggered, this, [this] { moveSelectedBin(); });
    }
    menu->popup(bins_tree_->viewport()->mapToGlobal(position));
}

void MediaPoolWidget::createBin()
{
    bool accepted = false;
    const QString current = bins_tree_->currentItem() == nullptr
        ? QString{} : bins_tree_->currentItem()->data(0, kPathRole).toString();
    const auto name = QInputDialog::getText(
        this, QStringLiteral("New Bin"),
        QStringLiteral("Bin name (use / for nested bins):"),
        QLineEdit::Normal, QString{}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    QString path = name;
    if (!current.isEmpty() && current != QString::fromUtf8(
            creative_suite::media::default_bin.data(),
            static_cast<qsizetype>(creative_suite::media::default_bin.size())))
        path = current + QStringLiteral("/") + name;
    const auto result = library_.createBin(path.toUtf8().toStdString());
    if (result == creative_suite::media::MediaMutationResult::Changed ||
        result == creative_suite::media::MediaMutationResult::NoChange) {
        refresh(path);
        status_label_->setText(QStringLiteral("Bin ready: %1").arg(path));
    } else {
        status_label_->setText(QStringLiteral("That bin name is not valid."));
    }
}

void MediaPoolWidget::renameSelectedBin()
{
    auto* item = bins_tree_->currentItem();
    if (item == nullptr || item->data(0, kAllMediaRole).toBool()) return;
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    bins_tree_->editItem(item, 0);
}

void MediaPoolWidget::moveSelectedBin()
{
    auto* item = bins_tree_->currentItem();
    if (item == nullptr || item->data(0, kAllMediaRole).toBool()) return;
    bool accepted = false;
    const auto old_path = item->data(0, kPathRole).toString();
    const QString new_path = QInputDialog::getText(
        this, QStringLiteral("Move Bin"), QStringLiteral("New bin path:"),
        QLineEdit::Normal, old_path, &accepted).trimmed();
    if (!accepted || new_path.isEmpty()) return;
    const auto result = library_.moveBin(old_path.toUtf8().toStdString(),
                                         new_path.toUtf8().toStdString());
    if (result == creative_suite::media::MediaMutationResult::Changed) refresh(new_path);
    else status_label_->setText(QStringLiteral("That bin move is not valid."));
}

void MediaPoolWidget::markSelectedMediaOffline()
{
    auto* item = media_list_->currentItem();
    if (item == nullptr) return;
    const auto path = filePath(item->data(kPathRole).toString());
    const auto index = library_.indexForPath(path);
    if (index >= library_.size()) return;
    const auto result = library_.markOffline(index);
    if (result == creative_suite::media::MediaMutationResult::Changed) {
        status_label_->setText(QStringLiteral("Media marked offline; its source reference was kept."));
        QTimer::singleShot(0, this, [this, path] { refreshMedia(pathText(path)); });
    }
}

void MediaPoolWidget::restoreSelectedMedia()
{
    auto* item = media_list_->currentItem();
    if (item == nullptr) return;
    importFiles({filePath(item->data(kPathRole).toString())});
}

void MediaPoolWidget::setThumbnailMode(bool enabled)
{
    const QSignalBlocker list_blocker(list_mode_button_);
    const QSignalBlocker thumbnail_blocker(thumbnail_mode_button_);
    list_mode_button_->setChecked(!enabled);
    thumbnail_mode_button_->setChecked(enabled);
    const QString selected = media_list_->currentItem() == nullptr
        ? QString{} : media_list_->currentItem()->data(kPathRole).toString();
    media_list_->setViewMode(enabled ? QListView::IconMode : QListView::ListMode);
    media_list_->setResizeMode(enabled ? QListView::Adjust : QListView::Fixed);
    media_list_->setMovement(enabled ? QListView::Static : QListView::Free);
    media_list_->setWrapping(enabled);
    media_list_->setGridSize(enabled ? QSize(168, 116) : QSize{});
    media_list_->setIconSize(enabled ? QSize(144, 82) : QSize(96, 54));
    refreshMedia(selected);
}

void MediaPoolWidget::updateProgress(std::uint64_t generation,
                                     std::size_t completed,
                                     std::size_t total,
                                     const std::filesystem::path& path)
{
    if (generation != import_generation_ || progress_ == nullptr || !cancel_requested_) return;
    progress_->setRange(0, static_cast<int>(std::min<std::size_t>(
        total, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    progress_->setValue(static_cast<int>(std::min<std::size_t>(
        completed, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    progress_->setLabelText(QStringLiteral("Importing %1").arg(QFileInfo(pathText(path)).fileName()));
}

void MediaPoolWidget::finishImport(
    std::uint64_t generation,
    creative_suite::media::MediaImportBatchResult result)
{
    if (generation != import_generation_) return;
    const auto selection = media_list_->currentItem() == nullptr
        ? QString{} : media_list_->currentItem()->data(kPathRole).toString();
    std::size_t imported = 0;
    std::size_t failed = 0;
    std::size_t duplicates = 0;
    QString last_imported;
    auto& logger = creative_suite::diagnostics::Logger::instance();
    for (auto& file : result.files) {
        if (file.status == creative_suite::media::MediaImportFileStatus::Imported && file.item) {
            const auto path = MediaLibrary::canonicalPath(file.path);
            const auto index = library_.indexForPath(path);
            if (index < library_.size()) {
                if (library_.items()[index].offline) {
                    auto item = std::move(*file.item);
                    const auto mutation = library_.restore(
                        index, std::move(item.metadata), std::move(item.first_frame));
                    if (mutation == creative_suite::media::MediaMutationResult::Changed) {
                        ++imported;
                        last_imported = pathText(path);
                    }
                } else {
                    ++duplicates;
                }
            } else {
                auto item = std::move(*file.item);
                const auto mutation = library_.addOnline(
                    std::move(item.metadata), std::move(item.first_frame),
                    std::move(item.display_name), std::move(item.bin_path));
                if (mutation == creative_suite::media::MediaMutationResult::Changed) {
                    ++imported;
                    last_imported = pathText(path);
                } else if (mutation == creative_suite::media::MediaMutationResult::Duplicate) {
                    ++duplicates;
                }
            }
        } else if (file.status == creative_suite::media::MediaImportFileStatus::Failed) {
            ++failed;
            logger.log(creative_suite::diagnostics::Level::Error,
                       "motion_media_import", "import_file", file.cause,
                       {{"path", pathText(file.path).toUtf8().toStdString()},
                        {"error_code", file.error_code.has_value()
                            ? std::to_string(*file.error_code) : std::string{}}});
        }
    }
    if (imported > 0) shared_frame_cache_.clear();
    cancel_requested_.reset();
    if (progress_ != nullptr) progress_->hide();
    refresh(!last_imported.isEmpty() ? last_imported : selection);
    status_label_->setText(QStringLiteral("Imported %1; %2 failed; %3 duplicates%4.")
        .arg(imported).arg(failed).arg(duplicates)
        .arg(result.cancelled ? QStringLiteral("; cancelled") : QString{}));
}

MediaDetailsWidget::MediaDetailsWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-media-details"));
    setMinimumWidth(250);
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(QStringLiteral("Media Details"), this);
    title->setObjectName(QStringLiteral("motion-media-details-title"));
    layout->addWidget(title);
    auto* form = new QFormLayout();
    const std::array<std::pair<QString, QString>, 11> rows{{
        {QStringLiteral("Name"), QStringLiteral("motion-media-detail-name")},
        {QStringLiteral("Type"), QStringLiteral("motion-media-detail-type")},
        {QStringLiteral("Format"), QStringLiteral("motion-media-detail-format")},
        {QStringLiteral("Codec"), QStringLiteral("motion-media-detail-codec")},
        {QStringLiteral("Resolution"), QStringLiteral("motion-media-detail-resolution")},
        {QStringLiteral("Frame rate"), QStringLiteral("motion-media-detail-frame-rate")},
        {QStringLiteral("Duration"), QStringLiteral("motion-media-detail-duration")},
        {QStringLiteral("Frames"), QStringLiteral("motion-media-detail-frame-count")},
        {QStringLiteral("Audio"), QStringLiteral("motion-media-detail-audio")},
        {QStringLiteral("Bin"), QStringLiteral("motion-media-detail-bin")},
        {QStringLiteral("Status"), QStringLiteral("motion-media-detail-status")},
    }};
    for (const auto& [label, object_name] : rows) {
        auto* value = new QLabel(QStringLiteral("—"), this);
        value->setObjectName(object_name);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, value);
        values_.push_back(value);
    }
    auto* path = new QLabel(QStringLiteral("—"), this);
    path->setObjectName(QStringLiteral("motion-media-detail-path"));
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Source"), path);
    values_.push_back(path);
    layout->addLayout(form);
    layout->addStretch(1);
    setMedia(nullptr);
}

void MediaDetailsWidget::setMedia(const MediaItem* item)
{
    if (item == nullptr) {
        for (auto* value : values_) value->setText(QStringLiteral("—"));
        return;
    }
    const auto& metadata = item->metadata;
    values_[0]->setText(utf8Text(item->display_name));
    values_[1]->setText(mediaKindText(metadata.kind));
    values_[2]->setText(utf8Text(metadata.container_format));
    values_[3]->setText(utf8Text(metadata.video_codec));
    values_[4]->setText(metadata.width > 0 && metadata.height > 0
        ? QStringLiteral("%1 × %2").arg(metadata.width).arg(metadata.height)
        : QStringLiteral("—"));
    values_[5]->setText(optionalNumber(metadata.frame_rate, QStringLiteral(" fps")));
    values_[6]->setText(optionalNumber(metadata.duration_seconds, QStringLiteral(" s")));
    values_[7]->setText(optionalInteger(metadata.frame_count));
    if (metadata.audio.has_value()) {
        values_[8]->setText(QStringLiteral("%1, %2 Hz, %3 ch")
            .arg(utf8Text(metadata.audio->codec))
            .arg(metadata.audio->sample_rate)
            .arg(metadata.audio->channel_count));
    } else {
        values_[8]->setText(QStringLiteral("None"));
    }
    values_[9]->setText(utf8Text(item->bin_path));
    values_[10]->setText(item->offline ? QStringLiteral("Offline") : QStringLiteral("Online"));
    values_[11]->setText(pathText(metadata.source_path));
}

} // namespace motion::ui
