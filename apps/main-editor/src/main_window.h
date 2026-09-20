#pragma once

#include <QMainWindow>

class QDockWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createMenus();
    void createWorkspace();
    void restoreDefaultLayout();

    QDockWidget* media_browser_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
};
