#include "main_window.h"

#include <QAction>
#include <QDockWidget>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSizePolicy>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QWidget* createPlaceholder(const QString& title, const QString& description) {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* title_label = new QLabel(title, container);
    title_label->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title_label);

    auto* description_label = new QLabel(description, container);
    description_label->setWordWrap(true);
    description_label->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(description_label);
    layout->addStretch();

    return container;
}

QDockWidget* createDock(const QString& title, const QString& object_name, QWidget* content) {
    auto* dock = new QDockWidget(title);
    dock->setObjectName(object_name);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setWidget(content);
    return dock;
}

} // namespace

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

    statusBar()->showMessage("Ready");
}

void MainWindow::createWorkspace() {
    auto* preview = new QFrame(this);
    preview->setFrameShape(QFrame::StyledPanel);
    preview->setStyleSheet("background-color: #1c2028;");
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* preview_layout = new QVBoxLayout(preview);
    preview_layout->setContentsMargins(24, 24, 24, 24);

    auto* preview_label = new QLabel("Preview area", preview);
    preview_label->setAlignment(Qt::AlignCenter);
    preview_label->setStyleSheet("color: #c7d0dc; font-size: 20px;");
    preview_layout->addWidget(preview_label);

    auto* preview_note = new QLabel(
        "Media playback and GPU rendering will be added in a later milestone.",
        preview);
    preview_note->setAlignment(Qt::AlignCenter);
    preview_note->setStyleSheet("color: #8994a3;");
    preview_layout->addWidget(preview_note);
    setCentralWidget(preview);

    media_browser_dock_ = createDock(
        "Media Browser",
        "mediaBrowserDock",
        createPlaceholder("Media Browser", "Imported media will appear here."));
    addDockWidget(Qt::LeftDockWidgetArea, media_browser_dock_);

    inspector_dock_ = createDock(
        "Inspector",
        "inspectorDock",
        createPlaceholder("Inspector", "Selected item properties will appear here."));
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

    timeline_dock_ = createDock(
        "Timeline",
        "timelineDock",
        createPlaceholder("Timeline", "Editing tracks and clips will appear here."));
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);
}

void MainWindow::createMenus() {
    auto* file_menu = menuBar()->addMenu("&File");
    auto* new_project_action = file_menu->addAction("&New Project");
    new_project_action->setEnabled(false);
    auto* open_project_action = file_menu->addAction("&Open Project...");
    open_project_action->setEnabled(false);
    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction("E&xit");
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu("&Edit");
    auto* undo_action = edit_menu->addAction("&Undo");
    undo_action->setEnabled(false);
    auto* redo_action = edit_menu->addAction("&Redo");
    redo_action->setEnabled(false);

    auto* view_menu = menuBar()->addMenu("&View");
    view_menu->addAction(media_browser_dock_->toggleViewAction());
    view_menu->addAction(inspector_dock_->toggleViewAction());
    view_menu->addAction(timeline_dock_->toggleViewAction());
    view_menu->addSeparator();
    auto* restore_layout_action = view_menu->addAction("Restore &Default Layout");
    connect(restore_layout_action, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

    auto* help_menu = menuBar()->addMenu("&Help");
    auto* about_action = help_menu->addAction("&About Main Editor");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About Main Editor",
            "Main Editor application shell\n\n"
            "This is an early open-source creative suite workspace.");
    });
}

void MainWindow::restoreDefaultLayout() {
    media_browser_dock_->setFloating(false);
    inspector_dock_->setFloating(false);
    timeline_dock_->setFloating(false);

    addDockWidget(Qt::LeftDockWidgetArea, media_browser_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    media_browser_dock_->show();
    inspector_dock_->show();
    timeline_dock_->show();
}
