#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_bin_tree_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QImage>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>


using namespace main_window_detail;

namespace {

QIcon mediaThumbnailIcon(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() <
            static_cast<std::size_t>(frame.stride) * frame.height) {
        return {};
    }

    const QImage image(
        frame.rgba_pixels.data(),
        frame.width,
        frame.height,
        frame.stride,
        QImage::Format_RGBA8888);
    const auto thumbnail = QPixmap::fromImage(image.copy()).scaled(
        128,
        72,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    return QIcon(thumbnail);
}

std::string binParentPath(std::string_view path) {
    const auto separator = path.rfind('/');
    return separator == std::string_view::npos
        ? std::string()
        : std::string(path.substr(0, separator));
}

std::string binLeafName(std::string_view path) {
    const auto separator = path.rfind('/');
    return std::string(path.substr(
        separator == std::string_view::npos ? 0 : separator + 1));
}

bool validInlineBinName(const QString& value) {
    const auto name = value.trimmed();
    return !name.isEmpty() &&
        name != QStringLiteral(".") &&
        name != QStringLiteral("..") &&
        !name.contains('/') &&
        !name.contains('\\');
}

} // namespace

QWidget* MainWindow::createMediaBrowser() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title_row = new QHBoxLayout;
    title_row->addStretch();

    auto* view_group = new QButtonGroup(container);
    view_group->setExclusive(true);
    auto* list_view_button = new QToolButton(container);
    list_view_button->setCheckable(true);
    list_view_button->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogListView));
    list_view_button->setToolTip("List view");
    view_group->addButton(list_view_button);
    title_row->addWidget(list_view_button);

    auto* grid_view_button = new QToolButton(container);
    grid_view_button->setCheckable(true);
    grid_view_button->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    grid_view_button->setToolTip("Blocks view");
    view_group->addButton(grid_view_button);
    title_row->addWidget(grid_view_button);
    layout->addLayout(title_row);

    auto* browser_splitter = new QSplitter(Qt::Vertical, container);
    browser_splitter->setChildrenCollapsible(false);
    browser_splitter->setHandleWidth(6);

    bin_tree_ = new MediaBrowserBinTreeWidget(browser_splitter);
    bin_tree_->setHeaderHidden(true);
    bin_tree_->setMinimumHeight(80);
    bin_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bin_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateMediaBrowserFilter(); });
    connect(bin_tree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
    connect(bin_tree_, &MediaBrowserBinTreeWidget::mediaDropRequested,
            this, &MainWindow::handleMediaBrowserMediaDrop);
    connect(bin_tree_, &MediaBrowserBinTreeWidget::binDropRequested,
            this, &MainWindow::handleMediaBrowserBinDrop);
    connect(bin_tree_, &QTreeWidget::itemChanged, this,
            &MainWindow::handleMediaBrowserBinItemChanged);
    media_list_ = new MediaBrowserListWidget(browser_splitter);
    media_list_->setMinimumHeight(140);
    media_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_view_button->setChecked(
        media_list_->displayMode() == MediaBrowserListWidget::DisplayMode::List);
    grid_view_button->setChecked(
        media_list_->displayMode() == MediaBrowserListWidget::DisplayMode::Grid);
    connect(list_view_button, &QToolButton::clicked, this, [this]() {
        if (media_list_ != nullptr) {
            media_list_->setDisplayMode(MediaBrowserListWidget::DisplayMode::List);
        }
    });
    connect(grid_view_button, &QToolButton::clicked, this, [this]() {
        if (media_list_ != nullptr) {
            media_list_->setDisplayMode(MediaBrowserListWidget::DisplayMode::Grid);
        }
    });
    connect(media_list_, &QListWidget::currentRowChanged, this, &MainWindow::updateMediaDetails);
    media_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(media_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
    connect(media_list_, &QListWidget::itemChanged, this,
            &MainWindow::handleMediaBrowserListItemChanged);

    browser_splitter->addWidget(bin_tree_);
    browser_splitter->addWidget(media_list_);
    browser_splitter->setStretchFactor(0, 0);
    browser_splitter->setStretchFactor(1, 1);

    QSettings settings;
    const auto saved_splitter_state = settings.value(
        "media_browser/bin_splitter_state").toByteArray();
    if (saved_splitter_state.isEmpty() ||
        !browser_splitter->restoreState(saved_splitter_state)) {
        browser_splitter->setSizes({300, 700});
    }
    connect(browser_splitter, &QSplitter::splitterMoved,
            container, [browser_splitter](int, int) {
                QSettings splitter_settings;
                splitter_settings.setValue(
                    "media_browser/bin_splitter_state",
                    browser_splitter->saveState());
                splitter_settings.sync();
            });
    layout->addWidget(browser_splitter, 1);

    populateMediaBrowser();
    updateTimelineState();

    return container;
}
std::optional<std::size_t> MainWindow::selectedMediaIndex() const noexcept {
    if (media_list_ == nullptr || media_list_->currentItem() == nullptr) return std::nullopt;
    const auto* current_item = media_list_->currentItem();
    if (current_item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
        media_browser_ui::kMediaItemTypeBin) {
        return std::nullopt;
    }
    bool ok = false;
    const auto value = current_item->data(media_browser_ui::kMediaIndexRole)
                           .toLongLong(&ok);
    if (!ok || value < 0 || value >= static_cast<qint64>(media_items_.size())) return std::nullopt;
    return static_cast<std::size_t>(value);
}

std::string MainWindow::selectedBinPath() const {
    if (bin_tree_ == nullptr || bin_tree_->currentItem() == nullptr) return {};
    return bin_tree_->currentItem()->data(0, Qt::UserRole).toString().toStdString();
}

void MainWindow::populateMediaBrowser(
    const std::filesystem::path& selected_path,
    std::optional<std::string> selected_bin_override) {
    if (media_list_ == nullptr || bin_tree_ == nullptr) return;

    std::filesystem::path path_to_select = selected_path;
    if (path_to_select.empty()) {
        const auto selected = selectedMediaIndex();
        if (selected.has_value()) path_to_select = media_items_[*selected].metadata.source_path;
    }

    const std::string selected_bin = selected_bin_override.has_value()
        ? *selected_bin_override
        : selectedBinPath();

    std::vector<std::string> bins;
    for (const auto& bin : bin_paths_) {
        if (std::find(bins.begin(), bins.end(), bin) == bins.end()) bins.push_back(bin);
    }
    for (const auto& item : media_items_) {
        std::string current;
        std::size_t start = 0;
        while (start < item.bin_path.size()) {
            const auto separator = item.bin_path.find('/', start);
            const auto part = item.bin_path.substr(
                start,
                separator == std::string::npos ? item.bin_path.size() - start : separator - start);
            if (!current.empty()) current += '/';
            current += part;
            if (std::find(bins.begin(), bins.end(), current) == bins.end()) bins.push_back(current);
            start = separator == std::string::npos ? item.bin_path.size() : separator + 1;
        }
    }
    if (std::find(bins.begin(), bins.end(), "Unsorted") == bins.end()) bins.emplace_back("Unsorted");
    std::sort(bins.begin(), bins.end());

    {
        const QSignalBlocker tree_blocker(bin_tree_);
        bin_tree_->clear();
        auto* all = new QTreeWidgetItem(bin_tree_, {"All Media"});
        all->setData(0, Qt::UserRole, QString());
        all->setIcon(0, style()->standardIcon(QStyle::SP_DirHomeIcon));
        all->setFlags(all->flags() & ~Qt::ItemIsEditable);
        bin_tree_->setCurrentItem(all);
        for (const auto& bin : bins) {
            QTreeWidgetItem* parent = all;
            std::string current;
            std::size_t start = 0;
            while (start < bin.size()) {
                const auto separator = bin.find('/', start);
                const auto part = bin.substr(
                    start,
                    separator == std::string::npos ? bin.size() - start : separator - start);
                if (!current.empty()) current += '/';
                current += part;
                QTreeWidgetItem* child = nullptr;
                for (int index = 0; index < parent->childCount(); ++index) {
                    if (parent->child(index)->data(0, Qt::UserRole).toString().toStdString() == current) {
                        child = parent->child(index);
                        break;
                    }
                }
                if (child == nullptr) {
                    child = new QTreeWidgetItem(parent, {QString::fromStdString(part)});
                    child->setData(0, Qt::UserRole, QString::fromStdString(current));
                    child->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                    if (current != "Unsorted") {
                        child->setFlags(child->flags() | Qt::ItemIsEditable);
                    } else {
                        child->setFlags(child->flags() & ~Qt::ItemIsEditable);
                    }
                }
                parent = child;
                start = separator == std::string::npos ? bin.size() : separator + 1;
            }
        }
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find_bin =
            [&find_bin, &selected_bin](QTreeWidgetItem* item) -> QTreeWidgetItem* {
                if (item->data(0, Qt::UserRole).toString().toStdString() == selected_bin) return item;
                for (int index = 0; index < item->childCount(); ++index) {
                    if (auto* found = find_bin(item->child(index)); found != nullptr) return found;
                }
                return nullptr;
            };
        if (!selected_bin.empty()) {
            if (auto* found = find_bin(all); found != nullptr) bin_tree_->setCurrentItem(found);
        }
    }

    {
        const QSignalBlocker list_blocker(media_list_);
        media_list_->clear();
        const auto active_bin = selectedBinPath();

        for (const auto& bin : bins) {
            const std::string prefix = active_bin.empty() ? std::string() : active_bin + '/';
            if (!active_bin.empty() && bin.rfind(prefix, 0) != 0) continue;

            const auto relative = active_bin.empty() ? bin : bin.substr(prefix.size());
            if (relative.empty() || relative.find('/') != std::string::npos) continue;

            auto* bin_item = new QListWidgetItem(
                QString::fromStdString(relative), media_list_);
            bin_item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
            bin_item->setData(
                media_browser_ui::kMediaItemTypeRole,
                media_browser_ui::kMediaItemTypeBin);
            bin_item->setData(
                media_browser_ui::kMediaBinPathRole,
                QString::fromStdString(bin));
            bin_item->setFlags(bin_item->flags() & ~Qt::ItemIsDragEnabled);
            if (bin != "Unsorted") {
                bin_item->setFlags(bin_item->flags() | Qt::ItemIsEditable);
            } else {
                bin_item->setFlags(bin_item->flags() & ~Qt::ItemIsEditable);
            }
        }

        for (std::size_t index = 0; index < media_items_.size(); ++index) {
            const auto& item = media_items_[index];
            if (!active_bin.empty() &&
                item.bin_path != active_bin &&
                item.bin_path.rfind(active_bin + '/', 0) != 0) {
                continue;
            }
            auto* list_item = new QListWidgetItem(
                compactMediaItemListText(
                    item.metadata, item.display_name, item.offline),
                media_list_);
            if (!item.offline) list_item->setIcon(mediaThumbnailIcon(item.first_frame));
            list_item->setData(Qt::UserRole, fromUtf8(pathToUtf8(item.metadata.source_path)));
            list_item->setData(
                media_browser_ui::kMediaItemTypeRole,
                media_browser_ui::kMediaItemTypeMedia);
            list_item->setFlags(list_item->flags() | Qt::ItemIsEditable);
            list_item->setData(
                media_browser_ui::kMediaIndexRole,
                static_cast<qint64>(index));
            list_item->setData(
                media_browser_ui::kMediaInfoRole,
                item.offline
                    ? QString("Name: %1\n"
                              "Bin: %2\n"
                              "Status: Offline\n"
                              "Path: %3")
                        .arg(fromUtf8(item.display_name))
                        .arg(fromUtf8(item.bin_path))
                        .arg(fromUtf8(pathToUtf8(item.metadata.source_path)))
                    : mediaDetailsText(item.metadata));
            if (!path_to_select.empty() &&
                normalizedPath(item.metadata.source_path) == normalizedPath(path_to_select)) {
                media_list_->setCurrentItem(list_item);
            }
        }
    }

    if (media_list_->currentRow() >= 0) {
        updateMediaDetails(media_list_->currentRow());
    } else {
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
    }
}

void MainWindow::selectMediaBrowserBin(const QString& path) {
    if (bin_tree_ == nullptr) return;

    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find_bin =
        [&find_bin, &path](QTreeWidgetItem* item) -> QTreeWidgetItem* {
        if (item != nullptr && item->data(0, Qt::UserRole).toString() == path) {
            return item;
        }
        if (item == nullptr) return nullptr;
        for (int index = 0; index < item->childCount(); ++index) {
            if (auto* found = find_bin(item->child(index)); found != nullptr) {
                return found;
            }
        }
        return nullptr;
    };

    if (auto* root = bin_tree_->topLevelItem(0); root != nullptr) {
        if (auto* found = find_bin(root); found != nullptr) {
            bin_tree_->setCurrentItem(found);
        }
    }
}

void MainWindow::selectMediaBrowserListBin(const QString& path) {
    if (media_list_ == nullptr) return;
    for (int row = 0; row < media_list_->count(); ++row) {
        auto* item = media_list_->item(row);
        if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                media_browser_ui::kMediaItemTypeBin &&
            item->data(media_browser_ui::kMediaBinPathRole).toString() == path) {
            media_list_->setCurrentItem(item);
            return;
        }
    }
}

void MainWindow::beginMediaBrowserBinEdit(const QString& path) {
    QTimer::singleShot(0, this, [this, path]() {
        if (media_list_ == nullptr) return;
        for (int row = 0; row < media_list_->count(); ++row) {
            auto* item = media_list_->item(row);
            if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                    media_browser_ui::kMediaItemTypeBin &&
                item->data(media_browser_ui::kMediaBinPathRole).toString() == path) {
                media_list_->setCurrentItem(item);
                media_list_->editItem(item);
                return;
            }
        }
    });
}

void MainWindow::updateMediaBrowserFilter() {
    populateMediaBrowser();
}

media::MediaLibrary MainWindow::buildMediaLibrary() const {
    media::MediaLibrary library;
    for (const auto& bin : bin_paths_) {
        if (bin != media::default_bin && media::MediaLibrary::validBinPath(bin)) {
            static_cast<void>(library.createBin(bin));
        }
    }
    for (const auto& item : media_items_) {
        static_cast<void>(library.addOffline(
            item.metadata.source_path,
            item.display_name,
            item.bin_path));
    }
    return library;
}

void MainWindow::applyMediaLibrary(const media::MediaLibrary& library) {
    bin_paths_ = library.bins();
    if (std::find(bin_paths_.begin(), bin_paths_.end(), media::default_bin) ==
        bin_paths_.end()) {
        bin_paths_.emplace_back(media::default_bin);
    }

    for (auto& item : media_items_) {
        const auto index = library.indexForPath(item.metadata.source_path);
        if (index != library.size()) {
            item.bin_path = library.items()[index].bin_path;
        }
    }
}

void MainWindow::createBin() {
    const auto parent_bin = selectedBinPath();
    auto library = buildMediaLibrary();
    std::string new_path;
    for (std::size_t suffix = 1; suffix < 100000; ++suffix) {
        const std::string name = suffix == 1
            ? "New Bin"
            : "New Bin " + std::to_string(suffix);
        const auto candidate = parent_bin.empty()
            ? name
            : parent_bin + "/" + name;
        if (library.createBin(candidate) == media::MediaMutationResult::Changed) {
            new_path = candidate;
            break;
        }
    }
    if (new_path.empty()) {
        statusBar()->showMessage("Could not create a new bin.");
        return;
    }
    applyMediaLibrary(library);
    updateProjectDirtyState();
    populateMediaBrowser({}, parent_bin);
    beginMediaBrowserBinEdit(QString::fromStdString(new_path));
}

void MainWindow::handleMediaBrowserListItemChanged(QListWidgetItem* item) {
    if (item == nullptr || media_browser_inline_rename_pending_) return;

    const auto item_type = item->data(
        media_browser_ui::kMediaItemTypeRole).toInt();
    const auto new_name = item->text().trimmed();
    if (item_type == media_browser_ui::kMediaItemTypeBin) {
        const auto old_path = item->data(
            media_browser_ui::kMediaBinPathRole).toString().toStdString();
        const auto old_name = QString::fromStdString(binLeafName(old_path));
        if (new_name == old_name) return;

        const auto active_bin = selectedBinPath();
        media_browser_inline_rename_pending_ = true;
        QMetaObject::invokeMethod(this, [this, item, old_path, old_name,
                                         new_name, active_bin]() {
            media_browser_inline_rename_pending_ = false;
            const auto restore = [this, item, &old_name]() {
                if (media_list_ == nullptr || item->listWidget() != media_list_) return;
                const QSignalBlocker blocker(media_list_);
                item->setText(old_name);
            };
            if (old_path.empty() || old_path == "Unsorted" ||
                !validInlineBinName(new_name)) {
                restore();
                statusBar()->showMessage("That bin name is not valid.");
                return;
            }

            const auto parent = binParentPath(old_path);
            const auto new_path = parent.empty()
                ? new_name.toStdString()
                : parent + "/" + new_name.toStdString();
            auto library = buildMediaLibrary();
            if (library.renameBin(old_path, new_path) !=
                media::MediaMutationResult::Changed) {
                restore();
                statusBar()->showMessage("That bin name is already in use or invalid.");
                return;
            }
            applyMediaLibrary(library);
            updateProjectDirtyState();
            populateMediaBrowser({}, active_bin);
            selectMediaBrowserListBin(QString::fromStdString(new_path));
        }, Qt::QueuedConnection);
        return;
    }

    if (item_type != media_browser_ui::kMediaItemTypeMedia) return;
    bool ok = false;
    const auto value = item->data(media_browser_ui::kMediaIndexRole)
                           .toLongLong(&ok);
    if (!ok || value < 0 || value >= static_cast<qint64>(media_items_.size())) return;
    const auto index = static_cast<std::size_t>(value);
    const auto old_name = QString::fromStdString(media_items_[index].display_name);
    if (new_name == old_name) return;

    media_browser_inline_rename_pending_ = true;
    QMetaObject::invokeMethod(this, [this, item, index, old_name, new_name]() {
        media_browser_inline_rename_pending_ = false;
        const auto restore = [this, item, &old_name]() {
            if (media_list_ == nullptr || item->listWidget() != media_list_) return;
            const QSignalBlocker blocker(media_list_);
            item->setText(old_name);
        };
        if (index >= media_items_.size() || new_name.isEmpty()) {
            restore();
            statusBar()->showMessage("The media name cannot be empty.");
            return;
        }

        media_items_[index].display_name = new_name.toStdString();
        media_items_[index].metadata.display_name = media_items_[index].display_name;
        timeline_model_.updateDisplayNameForSource(
            media_items_[index].metadata.source_path,
            media_items_[index].display_name);
        const auto source_path = media_items_[index].metadata.source_path;
        updateProjectDirtyState();
        populateMediaBrowser(source_path);
    }, Qt::QueuedConnection);
}

void MainWindow::handleMediaBrowserBinItemChanged(
    QTreeWidgetItem* item,
    int column) {
    if (item == nullptr || column != 0 || media_browser_inline_rename_pending_) return;

    const auto old_path = item->data(0, Qt::UserRole).toString().toStdString();
    const auto old_name = QString::fromStdString(binLeafName(old_path));
    const auto new_name = item->text(0).trimmed();
    if (new_name == old_name) return;

    media_browser_inline_rename_pending_ = true;
    QMetaObject::invokeMethod(this, [this, item, old_path, old_name, new_name]() {
        media_browser_inline_rename_pending_ = false;
        const auto restore = [this, item, &old_name]() {
            if (bin_tree_ == nullptr || item->treeWidget() != bin_tree_) return;
            const QSignalBlocker blocker(bin_tree_);
            item->setText(0, old_name);
        };
        if (old_path.empty() || old_path == "Unsorted" ||
            !validInlineBinName(new_name)) {
            restore();
            statusBar()->showMessage("That bin name is not valid.");
            return;
        }

        const auto parent = binParentPath(old_path);
        const auto new_path = parent.empty()
            ? new_name.toStdString()
            : parent + "/" + new_name.toStdString();
        auto library = buildMediaLibrary();
        if (library.renameBin(old_path, new_path) !=
            media::MediaMutationResult::Changed) {
            restore();
            statusBar()->showMessage("That bin name is already in use or invalid.");
            return;
        }
        applyMediaLibrary(library);
        updateProjectDirtyState();
        populateMediaBrowser({}, new_path);
    }, Qt::QueuedConnection);
}

void MainWindow::moveSelectedMediaToBin() {
    const auto index = selectedMediaIndex();
    if (!index.has_value()) return;
    QStringList choices;
    for (const auto& bin : bin_paths_) {
        if (choices.indexOf(QString::fromStdString(bin)) < 0) {
            choices.push_back(QString::fromStdString(bin));
        }
    }
    if (choices.isEmpty()) choices << "Unsorted";
    bool accepted = false;
    const QString value = QInputDialog::getItem(
        this, "Move to Bin", "Bin:", choices,
        choices.indexOf(QString::fromStdString(media_items_[*index].bin_path)), false, &accepted);
    if (!accepted) return;
    const auto destination_bin = value.toStdString();
    if (destination_bin == media_items_[*index].bin_path) return;
    auto library = buildMediaLibrary();
    const auto library_index = library.indexForPath(media_items_[*index].metadata.source_path);
    if (library_index == library.size() ||
        library.moveToBin(library_index, destination_bin) !=
            media::MediaMutationResult::Changed) {
        QMessageBox::warning(this, "Could not move media", "The destination bin is invalid.");
        return;
    }
    applyMediaLibrary(library);
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path, destination_bin);
}

void MainWindow::handleMediaBrowserMediaDrop(
    const QString& source_path,
    const QString& destination_bin) {
    if (source_path.isEmpty() || destination_bin.isEmpty()) return;

    const auto normalized_source = normalizedPath(
        QFileInfo(source_path).filesystemFilePath());
    const auto item = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&normalized_source](const ImportedMedia& media) {
            return normalizedPath(media.metadata.source_path) == normalized_source;
        });
    if (item == media_items_.end()) {
        statusBar()->showMessage("The dragged media is no longer available.");
        return;
    }

    const auto destination = destination_bin.toStdString();
    if (item->bin_path == destination) return;

    auto library = buildMediaLibrary();
    const auto library_index = library.indexForPath(item->metadata.source_path);
    if (library_index == library.size() ||
        library.moveToBin(library_index, destination) !=
            media::MediaMutationResult::Changed) {
        statusBar()->showMessage("Could not move media to that bin.");
        return;
    }

    applyMediaLibrary(library);
    updateProjectDirtyState();
    populateMediaBrowser(normalized_source, destination);
    statusBar()->showMessage(
        QString("Media moved to %1.").arg(QString::fromStdString(destination)));
}

void MainWindow::handleMediaBrowserBinDrop(
    const QString& source_bin,
    const QString& destination_bin) {
    const auto source = source_bin.toStdString();
    const auto destination = destination_bin.toStdString();
    if (source.empty() || destination.empty() || source == media::default_bin) {
        statusBar()->showMessage("That bin cannot be moved.");
        return;
    }

    const auto separator = source.rfind('/');
    const auto leaf = source.substr(
        separator == std::string::npos ? 0 : separator + 1);
    const auto new_path = destination + "/" + leaf;

    auto library = buildMediaLibrary();
    if (library.moveBin(source, new_path) != media::MediaMutationResult::Changed) {
        statusBar()->showMessage("That bin cannot be moved to this location.");
        return;
    }

    applyMediaLibrary(library);
    updateProjectDirtyState();
    populateMediaBrowser({}, new_path);
    statusBar()->showMessage(
        QString("Bin moved to %1.").arg(QString::fromStdString(destination)));
}

void MainWindow::removeSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value() || media_items_[*index].offline) return;
    media_items_[*index].offline = true;
    media_items_[*index].first_frame = {};
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
    statusBar()->showMessage("Media marked offline. Timeline clips were preserved.");
}

void MainWindow::restoreSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value() || !media_items_[*index].offline) return;
    const auto path = media_items_[*index].metadata.source_path;
    try {
        auto metadata = video_probe_.probe(path);
        auto frame = video_decoder_.decode_first_frame(path);
        metadata.display_name = media_items_[*index].display_name;
        media_items_[*index].metadata = std::move(metadata);
        media_items_[*index].first_frame = std::move(frame);
        media_items_[*index].offline = false;
        updateProjectDirtyState();
        populateMediaBrowser(path);
        statusBar()->showMessage("Media restored.");
    } catch (const media::MediaError& error) {
        logging::Context context{{"path", pathToUtf8(path)}, {"cause", error.what()}};
        if (error.error_code().has_value()) context.emplace_back("error_code", std::to_string(*error.error_code()));
        logging::Logger::instance().log(logging::Level::Error, "media", "restore", error.what(), context);
        QMessageBox::warning(this, "Could not restore media", fromUtf8(error.what()));
    }
}

void MainWindow::showMediaContextMenu(const QPoint& position) {
    QMenu menu(this);
    auto* new_bin = menu.addAction("New Bin");
    connect(new_bin, &QAction::triggered, this, &MainWindow::createBin);
    const bool from_media_list = sender() == media_list_;
    if (from_media_list) {
        if (auto* item = media_list_->itemAt(position); item != nullptr) {
            if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                media_browser_ui::kMediaItemTypeBin) {
                selectMediaBrowserBin(
                    item->data(media_browser_ui::kMediaBinPathRole).toString());
            } else {
                media_list_->setCurrentItem(item);
            }
        }
    } else if (bin_tree_ != nullptr) {
        if (auto* item = bin_tree_->itemAt(position); item != nullptr) {
            bin_tree_->setCurrentItem(item);
        }
    }
    const auto media_index = from_media_list ? selectedMediaIndex() : std::nullopt;
    if (media_index.has_value()) {
        menu.addSeparator();
        auto* move = menu.addAction("Move to Bin");
        connect(move, &QAction::triggered, this, &MainWindow::moveSelectedMediaToBin);
        auto* remove = menu.addAction(media_items_[*media_index].offline
            ? "Restore Media" : "Remove from Browser");
        connect(remove, &QAction::triggered, this,
                media_items_[*media_index].offline
                    ? &MainWindow::restoreSelectedMedia
                    : &MainWindow::removeSelectedMedia);
    }
    menu.exec((sender() == media_list_ ? media_list_->viewport()->mapToGlobal(position)
                                       : bin_tree_->viewport()->mapToGlobal(position)));
}

void MainWindow::openMedia() {
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Open Media",
        QString(),
        "Video Files (*.avi *.mkv *.mov *.mp4 *.mxf *.webm);;All Files (*)");
    if (selected_file.isEmpty()) return;

    const std::filesystem::path source_path = normalizedPath(
        QFileInfo(selected_file).filesystemFilePath());

    const auto existing = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&source_path](const ImportedMedia& item) {
            return item.metadata.source_path == source_path;
        });
    if (existing != media_items_.end()) {
        const auto index = static_cast<std::size_t>(std::distance(media_items_.begin(), existing));
        if (existing->offline) {
            try {
                auto metadata = video_probe_.probe(source_path);
                auto first_frame = video_decoder_.decode_first_frame(source_path);
                existing->metadata = std::move(metadata);
                existing->metadata.display_name = existing->display_name;
                existing->first_frame = std::move(first_frame);
                existing->offline = false;
                updateProjectDirtyState();
                populateMediaBrowser(source_path);
                statusBar()->showMessage("Offline media restored.");
            } catch (const media::MediaError& error) {
                logging::Context context{{"path", pathToUtf8(source_path)}, {"cause", error.what()}};
                if (error.error_code().has_value()) context.emplace_back("error_code", std::to_string(*error.error_code()));
                logging::Logger::instance().log(logging::Level::Error, "media", "restore", error.what(), context);
                QMessageBox::warning(this, "Could not restore media", fromUtf8(error.what()));
            }
        } else {
            populateMediaBrowser(source_path);
            static_cast<void>(index);
            statusBar()->showMessage("Media is already imported.");
        }
        return;
    }

    try {
        auto metadata = video_probe_.probe(source_path);
        auto first_frame = video_decoder_.decode_first_frame(source_path);
        addMediaItem(std::move(metadata), std::move(first_frame));
        logging::Logger::instance().log(
            logging::Level::Info,
            "media",
            "import",
            "Media metadata and first preview frame imported.",
            {{"path", pathToUtf8(source_path)},
             {"width", std::to_string(media_items_.back().first_frame.width)},
             {"height", std::to_string(media_items_.back().first_frame.height)}});
        statusBar()->showMessage("Media imported with preview frame.");
    } catch (const media::MediaError& error) {
        logging::Context context{{"path", pathToUtf8(source_path)}, {"cause", error.what()}};
        if (error.error_code().has_value()) context.emplace_back("error_code", std::to_string(*error.error_code()));
        logging::Logger::instance().log(logging::Level::Error, "media", "import", error.what(), context);
        const QString message = fromUtf8(error.what());
        QMessageBox::warning(this, "Could not open media", message);
        statusBar()->showMessage("Could not import media.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "ui",
            "media_import",
            error.what(),
            {{"path", pathToUtf8(source_path)}});
        const QString message = fromUtf8(error.what());
        QMessageBox::warning(this, "Could not open media", message);
        statusBar()->showMessage("Could not import media.");
    }
}

void MainWindow::updateMediaDetails(int row) {
    pending_clip_activation_.reset();
    ++playback_generation_;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    }
    playback_is_playing_ = false;
    playback_frame_index_ = 0;

    if (row < 0 || media_list_ == nullptr || media_list_->currentItem() == nullptr) {
        active_timeline_clip_index_.reset();
        preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }

    const auto item_index = selectedMediaIndex();
    if (!item_index.has_value()) {
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }
    const auto& item = media_items_[*item_index];
    const auto selected_location = selectedTimelineClipLocation();
    if (selected_location.has_value()) {
        active_timeline_track_index_ = selected_location->track_index;
        active_timeline_clip_index_ = selected_location->clip_index;
    } else {
        active_timeline_track_index_.reset();
        active_timeline_clip_index_.reset();
    }
    if (item.offline) {
        preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    } else {
        preview_widget_->setFrame(item.first_frame);
    }
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();

    if (playback_worker_ != nullptr && canPlaybackSelectedMedia()) {
        const QString source_path = fromUtf8(pathToUtf8(item.metadata.source_path));
        const double frame_rate = item.metadata.frame_rate.value_or(30.0);
        std::int64_t source_start_frame = 0;
        std::int64_t segment_frame_count = item.metadata.frame_count.value_or(0);
        if (active_timeline_track_index_.has_value() &&
            active_timeline_clip_index_.has_value() &&
            *active_timeline_track_index_ < timeline_model_.trackCount() &&
            *active_timeline_clip_index_ < timeline_model_.clipCount(
                *active_timeline_track_index_)) {
            const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_]
                .clips[*active_timeline_clip_index_];
            source_start_frame = clip.source_start_frame;
            segment_frame_count = clip.timeline_duration_frames;
        }
        QMetaObject::invokeMethod(
            playback_worker_,
            "setMedia",
            Qt::QueuedConnection,
            Q_ARG(QString, source_path),
            Q_ARG(double, frame_rate),
             Q_ARG(qint64, static_cast<qint64>(source_start_frame)),
             Q_ARG(qint64, static_cast<qint64>(segment_frame_count)),
             Q_ARG(double, active_timeline_track_index_.has_value()
                 ? timeline_model_.tracks()[*active_timeline_track_index_].audio_gain : 1.0),
             Q_ARG(bool, active_timeline_track_index_.has_value()
                 ? timeline_model_.tracks()[*active_timeline_track_index_].audio_muted : false),
             Q_ARG(double, active_timeline_track_index_.has_value() &&
                 active_timeline_clip_index_.has_value()
                 ? timeline_model_.tracks()[*active_timeline_track_index_]
                     .clips[*active_timeline_clip_index_].audio_gain : 1.0),
             Q_ARG(bool, active_timeline_track_index_.has_value() &&
                 active_timeline_clip_index_.has_value()
                 ? timeline_model_.tracks()[*active_timeline_track_index_]
                     .clips[*active_timeline_clip_index_].audio_muted : false),
             Q_ARG(qint64, active_timeline_track_index_.has_value()
                 ? static_cast<qint64>(*active_timeline_track_index_) : -1),
             Q_ARG(qint64, active_timeline_clip_index_.has_value()
                 ? static_cast<qint64>(*active_timeline_clip_index_) : -1),
             Q_ARG(quint64, playback_generation_));
        sendCompositionToWorker();
    }
}

void MainWindow::addMediaItem(
    media::VideoMetadata metadata,
    media::VideoFrame first_frame,
    std::string display_name,
    std::string bin_path,
    bool offline,
    bool mark_dirty) {
    if (display_name.empty()) display_name = metadata.display_name;
    if (display_name.empty()) display_name = media::MediaLibrary::defaultDisplayName(metadata.source_path);
    metadata.display_name = display_name;
    if (std::find(bin_paths_.begin(), bin_paths_.end(), bin_path) == bin_paths_.end()) {
        bin_paths_.push_back(bin_path);
    }
    media_items_.push_back({std::move(metadata), std::move(first_frame),
                            std::move(display_name), std::move(bin_path), offline});
    auto& item = media_items_.back();
    populateMediaBrowser(item.metadata.source_path);
    if (mark_dirty) updateProjectDirtyState();
}
