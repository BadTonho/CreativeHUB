#include "main_window.h"

#include <QCloseEvent>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
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
    if (confirmProjectChange()) {
        event->accept();
        return;
    }
    event->ignore();
}
