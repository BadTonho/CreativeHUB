#pragma once

#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>

#include <QWidget>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QProgressDialog;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace motion::ui {

class ImportTask;

class MediaPoolWidget final : public QWidget {
public:
    explicit MediaPoolWidget(QWidget* parent = nullptr);
    ~MediaPoolWidget() override;

    [[nodiscard]] const creative_suite::media::MediaLibrary& library() const noexcept;
    [[nodiscard]] QListWidget* mediaListWidget() const noexcept;
    [[nodiscard]] const creative_suite::media::MediaItem* selectedMedia() const noexcept;
    [[nodiscard]] creative_suite::media::RgbaFramePtr sharedFirstFrameForPath(
        const std::filesystem::path& path) const;
    void setImportRequestedHandler(std::function<void()> handler);
    void setSelectionChangedHandler(std::function<void()> handler);
    void clear();
    void importFiles(std::vector<std::filesystem::path> paths);

private:
    friend class ImportTask;

    void updateProgress(std::uint64_t generation,
                        std::size_t completed, std::size_t total,
                        const std::filesystem::path& path);
    void finishImport(std::uint64_t generation,
                      creative_suite::media::MediaImportBatchResult result);
    void refresh(const QString& selected_path = {});
    void refreshBins(const QString& selected_bin = {});
    void refreshMedia(const QString& selected_path = {});
    void handleMediaItemChanged(QListWidgetItem* item);
    void handleBinItemChanged(QTreeWidgetItem* item, int column);
    void showMediaContextMenu(const QPoint& position);
    void showBinContextMenu(const QPoint& position);
    void createBin();
    void renameSelectedBin();
    void moveSelectedBin();
    void markSelectedMediaOffline();
    void restoreSelectedMedia();
    void setThumbnailMode(bool enabled);
    creative_suite::media::MediaLibrary library_;
    mutable std::map<std::filesystem::path, creative_suite::media::RgbaFramePtr>
        shared_frame_cache_;
    std::shared_ptr<std::atomic_bool> cancel_requested_;
    QProgressDialog* progress_ = nullptr;
    QLabel* status_label_ = nullptr;
    QListWidget* media_list_ = nullptr;
    QTreeWidget* bins_tree_ = nullptr;
    QToolButton* list_mode_button_ = nullptr;
    QToolButton* thumbnail_mode_button_ = nullptr;
    bool refreshing_ = false;
    std::uint64_t import_generation_ = 0;
    std::function<void()> import_requested_handler_;
    std::function<void()> selection_changed_handler_;
};

class MediaDetailsWidget final : public QWidget {
public:
    explicit MediaDetailsWidget(QWidget* parent = nullptr);
    void setMedia(const creative_suite::media::MediaItem* item);

private:
    std::vector<QLabel*> values_;
};

} // namespace motion::ui
