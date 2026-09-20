#pragma once

#include "media/video_metadata.h"
#include "media/video_probe.h"

#include <QMainWindow>

#include <vector>

class QDockWidget;
class QLabel;
class QListWidget;
class QWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createMenus();
    void createWorkspace();
    void restoreDefaultLayout();
    QWidget* createMediaBrowser();
    void openMedia();
    void updateMediaDetails(int row);
    void addMediaItem(media::VideoMetadata metadata);

    QDockWidget* media_browser_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    QListWidget* media_list_ = nullptr;
    QLabel* media_details_ = nullptr;
    std::vector<media::VideoMetadata> media_items_;
    media::VideoProbe video_probe_;
};
