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
#include <QPointer>
#include <QProgressDialog>
#include <QRunnable>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QStringList>
#include <QStatusBar>
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

constexpr int kMediaThumbnailMaximumWidth = 192;
constexpr int kMediaThumbnailMaximumHeight = 108;

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
        kMediaThumbnailMaximumWidth,
        kMediaThumbnailMaximumHeight,
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

QWidget* MainWindow::createMediaBins() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    bin_tree_ = new MediaBrowserBinTreeWidget(container);
    bin_tree_->setHeaderHidden(true);
    bin_tree_->setMinimumHeight(80);
    bin_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bin_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (current == nullptr) return;
                populateMediaBrowser(
                    {},
                    current->data(0, Qt::UserRole).toString().toStdString());
            });
    connect(bin_tree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
    connect(bin_tree_, &MediaBrowserBinTreeWidget::mediaDropRequested,
            this, &MainWindow::handleMediaBrowserMediaDrop);
    connect(bin_tree_, &MediaBrowserBinTreeWidget::binDropRequested,
            this, &MainWindow::handleMediaBrowserBinDrop);
    connect(bin_tree_, &QTreeWidget::itemChanged, this,
            &MainWindow::handleMediaBrowserBinItemChanged);

    layout->addWidget(bin_tree_, 1);
    return container;
}

QWidget* MainWindow::createMediaPanel() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title_row = new QHBoxLayout;
    title_row->addStretch();

    auto* icon_size_label = new QLabel("Icon size", container);
    icon_size_label->setToolTip("Adjust the Media Browser icon size.");
    title_row->addWidget(icon_size_label);

    auto* icon_size_slider = new QSlider(Qt::Horizontal, container);
    icon_size_slider->setRange(
        MediaBrowserListWidget::kMinimumIconScalePercent,
        MediaBrowserListWidget::kMaximumIconScalePercent);
    icon_size_slider->setSingleStep(10);
    icon_size_slider->setPageStep(20);
    icon_size_slider->setFixedWidth(96);
    icon_size_slider->setAccessibleName("Media Browser icon size");
    title_row->addWidget(icon_size_slider);

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

    media_list_ = new MediaBrowserListWidget(container);
    media_list_->setMinimumHeight(140);
    media_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    icon_size_slider->setValue(media_list_->iconScalePercent());
    const auto updateIconSizeTooltip = [icon_size_slider](int value) {
        const auto tooltip = QString("Icon size: %1%").arg(value);
        icon_size_slider->setToolTip(tooltip);
        icon_size_slider->setAccessibleDescription(tooltip);
    };
    updateIconSizeTooltip(icon_size_slider->value());
    connect(icon_size_slider, &QSlider::valueChanged, this,
            [this, updateIconSizeTooltip](int value) {
                updateIconSizeTooltip(value);
                if (media_list_ != nullptr) {
                    media_list_->setIconScalePercent(value);
                }
            });
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

    layout->addWidget(media_list_, 1);

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

    std::vector<std::string> expanded_bins;
    std::function<void(QTreeWidgetItem*)> capture_expansion =
        [&capture_expansion, &expanded_bins](QTreeWidgetItem* item) {
            if (item == nullptr) return;
            if (item->isExpanded()) {
                expanded_bins.push_back(
                    item->data(0, Qt::UserRole).toString().toStdString());
            }
            for (int index = 0; index < item->childCount(); ++index) {
                capture_expansion(item->child(index));
            }
        };
    for (int index = 0; index < bin_tree_->topLevelItemCount(); ++index) {
        capture_expansion(bin_tree_->topLevelItem(index));
    }

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

        std::function<void(QTreeWidgetItem*)> restore_expansion =
            [&restore_expansion, &expanded_bins](QTreeWidgetItem* item) {
                if (item == nullptr) return;
                const auto path = item->data(0, Qt::UserRole).toString().toStdString();
                if (std::find(expanded_bins.begin(), expanded_bins.end(), path) !=
                    expanded_bins.end()) {
                    item->setExpanded(true);
                }
                for (int index = 0; index < item->childCount(); ++index) {
                    restore_expansion(item->child(index));
                }
            };
        restore_expansion(all);
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
                compactMediaBrowserName(relative), media_list_);
            bin_item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
            bin_item->setData(
                media_browser_ui::kMediaFullDisplayNameRole,
                QString::fromStdString(relative));
            bin_item->setData(
                media_browser_ui::kMediaItemTypeRole,
                media_browser_ui::kMediaItemTypeBin);
            bin_item->setData(
                media_browser_ui::kMediaBinPathRole,
                QString::fromStdString(bin));
            bin_item->setFlags(bin_item->flags() | Qt::ItemIsDragEnabled);
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
            list_item->setData(
                media_browser_ui::kMediaFullDisplayNameRole,
                fromUtf8(item.display_name.empty()
                    ? item.metadata.display_name
                    : item.display_name));
            list_item->setData(Qt::UserRole, fromUtf8(pathToUtf8(item.metadata.source_path)));
            list_item->setData(
                media_browser_ui::kMediaItemTypeRole,
                media_browser_ui::kMediaItemTypeMedia);
            list_item->setFlags(
                list_item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
            list_item->setData(
                media_browser_ui::kMediaIndexRole,
                static_cast<qint64>(index));
            if (item.metadata.frame_count.has_value()) {
                list_item->setData(
                    media_browser_ui::kMediaFrameCountRole,
                    static_cast<qlonglong>(*item.metadata.frame_count));
            }
            if (item.metadata.frame_rate.has_value()) {
                list_item->setData(
                    media_browser_ui::kMediaFrameRateRole,
                    *item.metadata.frame_rate);
            }
            if (item.metadata.duration_seconds.has_value()) {
                list_item->setData(
                    media_browser_ui::kMediaDurationSecondsRole,
                    *item.metadata.duration_seconds);
            }
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

void MainWindow::createBin() {
    const auto parent_bin = selectedBinPath();
    std::string new_path;
    for (std::size_t suffix = 1; suffix < 100000; ++suffix) {
        const std::string name = suffix == 1
            ? "New Bin"
            : "New Bin " + std::to_string(suffix);
        const auto candidate = parent_bin.empty()
            ? name
            : parent_bin + "/" + name;
        if (media_controller_.createBin(candidate).changed()) {
            new_path = candidate;
            break;
        }
    }
    if (new_path.empty()) {
        statusBar()->showMessage("Could not create a new bin.");
        return;
    }
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
            if (!media_controller_.renameBin(old_path, new_path).changed()) {
                restore();
                statusBar()->showMessage("That bin name is already in use or invalid.");
                return;
            }
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

        const auto rename_result = media_controller_.rename(
            media_items_[index].metadata.source_path, new_name.toStdString());
        if (!rename_result.changed()) {
            restore();
            statusBar()->showMessage("The media name could not be changed.");
            return;
        }
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
        if (!media_controller_.renameBin(old_path, new_path).changed()) {
            restore();
            statusBar()->showMessage("That bin name is already in use or invalid.");
            return;
        }
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
    if (!media_controller_.moveToBin(
            media_items_[*index].metadata.source_path, destination_bin).changed()) {
        QMessageBox::warning(this, "Could not move media", "The destination bin is invalid.");
        return;
    }
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

    if (!media_controller_.moveToBin(item->metadata.source_path, destination).changed()) {
        statusBar()->showMessage("Could not move media to that bin.");
        return;
    }

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

    if (!media_controller_.moveBin(source, new_path).changed()) {
        statusBar()->showMessage("That bin cannot be moved to this location.");
        return;
    }

    updateProjectDirtyState();
    populateMediaBrowser({}, new_path);
    statusBar()->showMessage(
        QString("Bin moved to %1.").arg(QString::fromStdString(destination)));
}

void MainWindow::removeSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value() || media_items_[*index].offline) return;
    if (!media_controller_.markOffline(media_items_[*index].metadata.source_path).changed()) return;
    if (playback_controller_ != nullptr) {
        playback_controller_->invalidate(true);
    }
    playback_is_playing_ = false;
    preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
    statusBar()->showMessage("Media marked offline. Timeline clips were preserved.");
}

void MainWindow::restoreSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value() || !media_items_[*index].offline) return;
    const auto path = media_items_[*index].metadata.source_path;
    static_cast<void>(startMediaImport({path}));
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
    const QStringList selected_files = QFileDialog::getOpenFileNames(
        this,
        "Open Media",
        QString(),
        "Video Files (*.avi *.mkv *.mov *.mp4 *.mxf *.webm);;"
        "Image Files (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;"
        "All Files (*)");
    if (selected_files.isEmpty()) return;
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<std::size_t>(selected_files.size()));
    for (const auto& selected_file : selected_files) {
        paths.push_back(normalizedPath(QFileInfo(selected_file).filesystemFilePath()));
    }
    static_cast<void>(startMediaImport(std::move(paths)));
}

bool MainWindow::startMediaImport(std::vector<std::filesystem::path> paths) {
    if (paths.empty()) return false;
    if (active_media_import_cancel_) {
        statusBar()->showMessage("A media import batch is already running or cancelling.");
        return false;
    }

    const auto work_id = next_media_work_id_++;
    active_media_work_id_ = work_id;
    const auto project_generation = project_generation_;
    const auto selection_generation = selection_generation_;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    active_media_import_cancel_ = cancel;
    auto* progress = new QProgressDialog(
        "Preparing media...", "Cancel", 0, static_cast<int>(paths.size()), this);
    progress->setWindowTitle("Import Media");
    progress->setWindowModality(Qt::NonModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(250);
    connect(progress, &QProgressDialog::canceled, this, [cancel, progress]() {
        cancel->store(true, std::memory_order_relaxed);
        progress->setLabelText("Finishing the current media operation...");
        progress->setCancelButton(nullptr);
    });
    media_import_progress_ = progress;
    progress->show();

    QPointer<MainWindow> guard(this);
    media_task_pool_.start(QRunnable::create(
        [guard, work_id, project_generation, selection_generation,
         paths = std::move(paths), cancel]() mutable {
            application::MediaImportService service;
            auto result = service.process(
                work_id,
                project_generation,
                selection_generation,
                paths,
                *cancel,
                [guard, work_id](std::size_t current,
                                 std::size_t total,
                                 const std::filesystem::path& path) {
                    if (guard.isNull()) return;
                    const auto path_text = pathToUtf8(path.filename());
                    QMetaObject::invokeMethod(
                        guard.data(),
                        [guard, work_id, current, total, path_text]() {
                            if (guard.isNull() || guard->media_import_progress_ == nullptr ||
                                guard->active_media_work_id_ != work_id) return;
                            guard->media_import_progress_->setValue(static_cast<int>(current));
                            guard->media_import_progress_->setLabelText(
                                QString("Processing %1 of %2: %3")
                                    .arg(current + 1).arg(total)
                                    .arg(fromUtf8(path_text)));
                        },
                        Qt::QueuedConnection);
                });
            if (guard.isNull()) return;
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, result = std::move(result)]() mutable {
                    if (!guard.isNull()) guard->finishMediaImport(std::move(result));
                },
                Qt::QueuedConnection);
        }));
    return true;
}

void MainWindow::finishMediaImport(application::MediaImportBatchResult result) {
    if (result.work_id != active_media_work_id_) return;
    if (media_import_progress_ != nullptr) {
        media_import_progress_->setValue(media_import_progress_->maximum());
        media_import_progress_->hide();
        media_import_progress_->deleteLater();
        media_import_progress_ = nullptr;
    }
    active_media_import_cancel_.reset();

    if (result.project_generation != project_generation_) return;

    int imported_count = 0;
    int restored_count = 0;
    int duplicate_count = 0;
    int failed_count = 0;
    bool changed = false;
    std::filesystem::path path_to_select;
    QStringList failures;
    const bool selection_is_current = result.selection_generation == selection_generation_;

    for (auto& file : result.files) {
        if (file.status == application::MediaImportFileStatus::Discarded) continue;
        if (file.status == application::MediaImportFileStatus::Failed) {
            ++failed_count;
            logging::Context context{{"path", pathToUtf8(file.path)}, {"cause", file.cause}};
            if (file.error_code.has_value()) {
                context.emplace_back("error_code", std::to_string(*file.error_code));
            }
            logging::Logger::instance().log(
                logging::Level::Error, "media", "import", file.cause, context);
            failures.push_back(
                fromUtf8(pathToUtf8(file.path.filename())) + ": " + fromUtf8(file.cause));
            continue;
        }
        if (!file.item.has_value()) continue;

        const auto existing_index = media_controller_.library().indexForPath(file.path);
        const bool was_offline = existing_index != media_controller_.library().size() &&
            media_controller_.library().items()[existing_index].offline;
        const auto committed = media_controller_.commitImported(std::move(*file.item));
        if (committed.status == application::MediaCommandStatus::Rejected) {
            if (committed.code == application::MediaCommandCode::Duplicate) {
                ++duplicate_count;
                if (selection_is_current) path_to_select = file.path;
            }
            continue;
        }
        if (committed.changed()) {
            changed = true;
            if (was_offline) ++restored_count;
            else ++imported_count;
            if (selection_is_current) path_to_select = file.path;
        } else {
            ++duplicate_count;
            if (selection_is_current) path_to_select = file.path;
        }
    }

    if (changed) updateProjectDirtyState();
    if (!path_to_select.empty()) populateMediaBrowser(path_to_select);
    statusBar()->showMessage(
        QString("Media import %1: %2 imported, %3 restored, %4 duplicate(s), %5 failed.")
            .arg(result.cancelled ? "cancelled" : "complete")
            .arg(imported_count)
            .arg(restored_count)
            .arg(duplicate_count)
            .arg(failed_count));
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, "Some media could not be imported", failures.join('\n'));
    } else if (imported_count + restored_count == 1 && duplicate_count == 0) {
        statusBar()->showMessage("Media imported with preview frame.");
    }
}

void MainWindow::updateMediaDetails(int row) {
    ++selection_generation_;
    if (playback_controller_ != nullptr) {
        playback_controller_->invalidate(true);
    }
    playback_is_playing_ = false;
    playback_frame_index_ = 0;

    if (row < 0 || media_list_ == nullptr || media_list_->currentItem() == nullptr) {
        active_timeline_clip_index_cache_.reset();
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
        setActiveTimelineSelection(*selected_location);
    } else {
        clearActiveTimelineSelection();
    }
    if (item.offline) {
        preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    } else {
        preview_widget_->setFrame(item.first_frame);
    }
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();

    if (playback_controller_ != nullptr && canPlaybackSelectedMedia() &&
        active_timeline_clip_id_.has_value()) {
        (void)playback_controller_->activateClip(
            *active_timeline_clip_id_, 0, false);
    }
}
