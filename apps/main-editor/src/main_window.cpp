#include "main_window.h"

#include "settings/shortcut_manager.h"

#include <QCloseEvent>
#include <QStatusBar>

#include <memory>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      shortcut_manager_(std::make_unique<settings::ShortcutManager>()) {
    setWindowTitle("Main Editor");
    resize(1280, 720);
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);

    createWorkspace();
    createMenus();
    initializePlayback();

    saved_project_document_ = currentProjectDocument();
    updateProjectDirtyState();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() {
    shutdownPlayback();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmProjectChange()) {
        event->ignore();
        return;
    }

    saveWorkspaceLayout();
    event->accept();
}
