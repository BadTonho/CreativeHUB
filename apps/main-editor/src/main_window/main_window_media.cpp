#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
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
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
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

QWidget* MainWindow::createMediaBrowser() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* title = new QLabel("Media Browser", container);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title);

    auto* browser_controls = new QHBoxLayout;
    new_bin_button_ = new QPushButton("New Bin", container);
    connect(new_bin_button_, &QPushButton::clicked, this, &MainWindow::createBin);
    browser_controls->addWidget(new_bin_button_);
    browser_controls->addStretch();
    layout->addLayout(browser_controls);

    bin_tree_ = new QTreeWidget(container);
    bin_tree_->setHeaderHidden(true);
    bin_tree_->setMaximumHeight(150);
    bin_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bin_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateMediaBrowserFilter(); });
    connect(bin_tree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
    layout->addWidget(bin_tree_);

    media_list_ = new MediaBrowserListWidget(container);
    media_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    media_list_->setWordWrap(true);
    connect(media_list_, &QListWidget::currentRowChanged, this, &MainWindow::updateMediaDetails);
    media_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(media_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
    layout->addWidget(media_list_, 1);

    media_details_ = new QLabel("No media imported.", container);
    media_details_->setWordWrap(true);
    media_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    media_details_->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(media_details_);

    add_to_timeline_button_ = new QPushButton("Add to Timeline", container);
    connect(add_to_timeline_button_, &QPushButton::clicked, this, [this]() {
        addSelectedMediaToTimeline();
    });
    layout->addWidget(add_to_timeline_button_);

    populateMediaBrowser();
    updateTimelineState();

    return container;
}
std::optional<std::size_t> MainWindow::selectedMediaIndex() const noexcept {
    if (media_list_ == nullptr || media_list_->currentItem() == nullptr) return std::nullopt;
    bool ok = false;
    const auto value = media_list_->currentItem()->data(Qt::UserRole + 1).toLongLong(&ok);
    if (!ok || value < 0 || value >= static_cast<qint64>(media_items_.size())) return std::nullopt;
    return static_cast<std::size_t>(value);
}

std::string MainWindow::selectedBinPath() const {
    if (bin_tree_ == nullptr || bin_tree_->currentItem() == nullptr) return {};
    return bin_tree_->currentItem()->data(0, Qt::UserRole).toString().toStdString();
}

void MainWindow::populateMediaBrowser(const std::filesystem::path& selected_path) {
    if (media_list_ == nullptr || bin_tree_ == nullptr) return;

    std::filesystem::path path_to_select = selected_path;
    if (path_to_select.empty()) {
        const auto selected = selectedMediaIndex();
        if (selected.has_value()) path_to_select = media_items_[*selected].metadata.source_path;
    }

    std::string selected_bin = selectedBinPath();
    if (selected_bin.empty()) selected_bin = "";

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
        for (std::size_t index = 0; index < media_items_.size(); ++index) {
            const auto& item = media_items_[index];
            if (!active_bin.empty() &&
                item.bin_path != active_bin &&
                item.bin_path.rfind(active_bin + '/', 0) != 0) {
                continue;
            }
            auto* list_item = new QListWidgetItem(
                mediaItemListText(item.metadata, item.display_name, item.bin_path, item.offline),
                media_list_);
            list_item->setToolTip(fromUtf8(pathToUtf8(item.metadata.source_path)));
            list_item->setData(Qt::UserRole, fromUtf8(pathToUtf8(item.metadata.source_path)));
            list_item->setData(Qt::UserRole + 1, static_cast<qint64>(index));
            if (!path_to_select.empty() &&
                normalizedPath(item.metadata.source_path) == normalizedPath(path_to_select)) {
                media_list_->setCurrentItem(list_item);
            }
        }
    }

    if (media_list_->currentRow() >= 0) {
        updateMediaDetails(media_list_->currentRow());
    } else {
        media_details_->setText(media_items_.empty() ? "No media imported." : "No media in this bin.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
    }
}

void MainWindow::updateMediaBrowserFilter() {
    populateMediaBrowser();
}

void MainWindow::createBin() {
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, "New Bin", "Bin path (use / for sub-bins):", QLineEdit::Normal,
        "New Bin", &accepted);
    if (!accepted) return;
    if (!media::MediaLibrary::validBinPath(value.toStdString())) {
        QMessageBox::warning(this, "Invalid bin", "Use a non-empty bin path with / separators.");
        return;
    }
    const std::string current = value.toStdString();
    if (std::find(bin_paths_.begin(), bin_paths_.end(), current) != bin_paths_.end()) return;
    std::size_t start = 0;
    while (start < current.size()) {
        const auto separator = current.find('/', start);
        const auto path = current.substr(
            0, separator == std::string::npos ? current.size() : separator);
        if (std::find(bin_paths_.begin(), bin_paths_.end(), path) == bin_paths_.end()) {
            bin_paths_.push_back(path);
        }
        start = separator == std::string::npos ? current.size() : separator + 1;
    }
    updateProjectDirtyState();
    populateMediaBrowser();
}

void MainWindow::renameSelectedBin() {
    const auto old_path = selectedBinPath();
    if (old_path.empty() || old_path == "Unsorted") return;
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, "Rename Bin", "New bin path:", QLineEdit::Normal,
        QString::fromStdString(old_path), &accepted);
    if (!accepted) return;
    media::MediaLibrary library;
    for (const auto& item : media_items_) {
        library.addOffline(item.metadata.source_path, item.display_name, item.bin_path);
    }
    const auto result = library.renameBin(old_path, value.toStdString());
    if (result != media::MediaMutationResult::Changed) {
        QMessageBox::warning(this, "Could not rename bin", "The bin name is invalid or already exists.");
        return;
    }
    for (std::size_t index = 0; index < media_items_.size(); ++index) {
        const auto library_index = library.indexForPath(media_items_[index].metadata.source_path);
        if (library_index != library.size()) media_items_[index].bin_path = library.items()[library_index].bin_path;
    }
    bin_paths_.clear();
    bin_paths_.push_back("Unsorted");
    for (const auto& item : media_items_) {
        if (std::find(bin_paths_.begin(), bin_paths_.end(), item.bin_path) == bin_paths_.end()) {
            bin_paths_.push_back(item.bin_path);
        }
    }
    updateProjectDirtyState();
    populateMediaBrowser();
}

void MainWindow::renameSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value()) return;
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, "Rename Media", "Display name:", QLineEdit::Normal,
        QString::fromStdString(media_items_[*index].display_name), &accepted);
    if (!accepted) return;
    if (value.isEmpty()) {
        QMessageBox::warning(this, "Invalid name", "The display name cannot be empty.");
        return;
    }
    media_items_[*index].display_name = value.toStdString();
    media_items_[*index].metadata.display_name = media_items_[*index].display_name;
    timeline_model_.updateDisplayNameForSource(
        media_items_[*index].metadata.source_path,
        media_items_[*index].display_name);
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
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
    if (value.toStdString() == media_items_[*index].bin_path) return;
    media_items_[*index].bin_path = value.toStdString();
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
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
            media_list_->setCurrentItem(item);
        }
    } else if (bin_tree_ != nullptr) {
        if (auto* item = bin_tree_->itemAt(position); item != nullptr) {
            bin_tree_->setCurrentItem(item);
        }
    }
    const auto media_index = from_media_list ? selectedMediaIndex() : std::nullopt;
    if (media_index.has_value()) {
        menu.addSeparator();
        auto* rename = menu.addAction("Rename");
        connect(rename, &QAction::triggered, this, &MainWindow::renameSelectedMedia);
        auto* move = menu.addAction("Move to Bin");
        connect(move, &QAction::triggered, this, &MainWindow::moveSelectedMediaToBin);
        auto* remove = menu.addAction(media_items_[*media_index].offline
            ? "Restore Media" : "Remove from Browser");
        connect(remove, &QAction::triggered, this,
                media_items_[*media_index].offline
                    ? &MainWindow::restoreSelectedMedia
                    : &MainWindow::removeSelectedMedia);
    } else if (!selectedBinPath().empty()) {
        menu.addSeparator();
        auto* rename = menu.addAction("Rename");
        connect(rename, &QAction::triggered, this, &MainWindow::renameSelectedBin);
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
        media_details_->setText("No media imported.");
        preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }

    const auto item_index = selectedMediaIndex();
    if (!item_index.has_value()) {
        media_details_->setText("No media selected.");
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
    media_details_->setText(
        item.offline
            ? QString("Name: %1\nBin: %2\nStatus: Offline\nPath: %3")
                .arg(fromUtf8(item.display_name))
                .arg(fromUtf8(item.bin_path))
                .arg(fromUtf8(pathToUtf8(item.metadata.source_path)))
            : mediaDetailsText(item.metadata));
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
