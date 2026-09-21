#include "main_window.h"

#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"
#include "rendering/preview_performance_metrics.h"

#include <QCloseEvent>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include <memory>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      shortcut_manager_(std::make_unique<settings::ShortcutManager>()) {
    setWindowTitle("Main Editor");
    QSettings settings;
    const auto saved_geometry = settings.value(
        "workspace/window_geometry").toByteArray();
    const bool restored_geometry = !saved_geometry.isEmpty() &&
        restoreGeometry(saved_geometry);
    if (!restored_geometry) {
        resize(1280, 720);
        initial_window_layout_pending_ = true;
        setWindowState(windowState() | Qt::WindowMaximized);
    } else if (settings.value(
                   "workspace/window_maximized", false).toBool()) {
        setWindowState(windowState() | Qt::WindowMaximized);
    }
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    setStyleSheet(
        "QMainWindow::separator {"
        " background: #4d4d4d;"
        " width: 5px;"
        " height: 5px;"
        "}"
        "QMainWindow::separator:hover {"
        " background: #66b7ed;"
        "}");

    createWorkspace();
    createMenus();
    initializePlayback();
    configurePreviewPerformanceMetrics(
        settings::previewPerformanceMetricsEnabled());

    saved_project_document_ = currentProjectDocument();
    updateProjectDirtyState();

    statusBar()->showMessage("Ready");

    if (initial_window_layout_pending_) {
        QTimer::singleShot(0, this, [this]() { applyInitialWindowLayout(); });
    }
}

MainWindow::~MainWindow() {
    configurePreviewPerformanceMetrics(false);
    shutdownPlayback();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmProjectChange()) {
        event->ignore();
        return;
    }

    saveWorkspaceLayout();
    saveWindowGeometry();
    event->accept();
}
