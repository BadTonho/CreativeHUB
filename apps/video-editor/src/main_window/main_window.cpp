#include "main_window/main_window.h"

#include "logging/logger.h"
#include "main_window/main_window_support.h"
#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"
#include "rendering/preview_performance_metrics.h"
#include "ui/workspace/workspace_host.h"

#include <QDateTime>
#include <QDialog>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace {

QString snapshotDateText(const project::AutosaveSnapshot& snapshot) {
    const auto modified = QFileInfo(
        main_window_detail::fromUtf8(
            main_window_detail::pathToUtf8(snapshot.path))).lastModified();
    return modified.isValid()
        ? modified.toLocalTime().toString(Qt::TextDate)
        : QStringLiteral("Unknown time");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      shortcut_manager_(std::make_unique<settings::ShortcutManager>()) {
    media_task_pool_.setMaxThreadCount(1);
    media_task_pool_.setExpiryTimeout(-1);
    setWindowTitle("Video Editor");
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
    initializeLinkedImageCompatibility();
    configurePreviewPerformanceMetrics(
        settings::previewPerformanceMetricsEnabled());

    project_controller_.establishBaseline(currentProjectDocument());
    updateProjectDirtyState();

    configureProjectAutosave(
        settings::projectAutosaveEnabled(),
        settings::projectAutosaveIntervalSeconds());

    statusBar()->showMessage("Ready");

    if (initial_window_layout_pending_) {
        QTimer::singleShot(0, this, [this]() { applyInitialWindowLayout(); });
    }
    QTimer::singleShot(0, this, [this]() { offerUnsavedProjectRecovery(); });
}

MainWindow::~MainWindow() {
    if (autosave_timer_ != nullptr) autosave_timer_->stop();
    if (linked_image_poll_timer_ != nullptr) linked_image_poll_timer_->stop();
    if (active_media_import_cancel_) {
        active_media_import_cancel_->store(true, std::memory_order_relaxed);
    }
    if (project_load_cancel_) {
        project_load_cancel_->store(true, std::memory_order_relaxed);
    }
    media_task_pool_.waitForDone();
    configurePreviewPerformanceMetrics(false);
    shutdownPlayback();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmProjectChange()) {
        event->ignore();
        return;
    }

    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() == ui::WorkspacePageId::Render) {
        setWorkspacePage(ui::WorkspacePageId::Edit);
    }

    try {
        project_controller_.removeCurrentUnsavedSnapshots();
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_cleanup",
            error.what(),
            { {"cause", error.what()} });
    }
    saveWorkspaceLayout();
    saveWindowGeometry();
    event->accept();
}

void MainWindow::configureProjectAutosave(bool enabled, int interval_seconds) {
    if (autosave_timer_ == nullptr) {
        autosave_timer_ = new QTimer(this);
        connect(autosave_timer_, &QTimer::timeout,
                this, &MainWindow::autosaveProject);
    }

    autosave_timer_->setInterval(std::max(1, interval_seconds) * 1000);
    if (enabled) autosave_timer_->start();
    else autosave_timer_->stop();
}

std::optional<std::filesystem::path> MainWindow::chooseRecoverySnapshot(
    const std::vector<project::AutosaveSnapshot>& snapshots,
    const QString& project_name) {
    if (snapshots.empty()) return std::nullopt;

    QDialog dialog(this);
    dialog.setWindowTitle("Project Recovery");
    dialog.setModal(true);
    dialog.resize(620, 360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(
        QString("Recovery snapshots were found for %1. Select one to restore, "
                "or ignore them for now.").arg(project_name),
        &dialog);
    description->setWordWrap(true);
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);

    auto available_snapshots = snapshots;
    for (const auto& snapshot : available_snapshots) {
        new QListWidgetItem(
            QString("%1 — %2")
                .arg(snapshotDateText(snapshot),
                     main_window_detail::fromUtf8(
                         main_window_detail::pathToUtf8(snapshot.path.filename()))),
            list);
    }
    if (list->count() > 0) list->setCurrentRow(0);

    auto* buttons = new QHBoxLayout();
    auto* restore = new QPushButton("Restore", &dialog);
    auto* ignore = new QPushButton("Ignore", &dialog);
    auto* remove = new QPushButton("Delete", &dialog);
    buttons->addWidget(restore);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(ignore);
    layout->addWidget(description);
    layout->addWidget(list, 1);
    layout->addLayout(buttons);

    std::optional<std::filesystem::path> selected_path;
    connect(restore, &QPushButton::clicked, &dialog, [&]() {
        const auto row = list->currentRow();
        if (row < 0 || row >= static_cast<int>(available_snapshots.size())) return;
        selected_path = available_snapshots[static_cast<std::size_t>(row)].path;
        dialog.accept();
    });
    connect(ignore, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(remove, &QPushButton::clicked, &dialog, [&]() {
        auto* item = list->currentItem();
        if (item == nullptr) return;
        const auto row = list->row(item);
        if (row < 0 || row >= static_cast<int>(available_snapshots.size())) return;
        const auto path = available_snapshots[static_cast<std::size_t>(row)].path;
        try {
            project_controller_.removeSnapshot(path);
            available_snapshots.erase(
                available_snapshots.begin() + row);
            delete list->takeItem(row);
            if (list->count() == 0) dialog.reject();
            else list->setCurrentRow(0);
        } catch (const project::ProjectError& error) {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "project",
                "autosave_remove",
                error.what(),
                {{"snapshot_path", main_window_detail::pathToUtf8(path)}});
        }
    });

    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return selected_path;
}

void MainWindow::offerUnsavedProjectRecovery() {
    if (!settings::projectAutosaveEnabled()) return;
    const auto snapshots = project_controller_.unsavedSnapshots();
    if (snapshots.empty()) return;

    const auto selected = chooseRecoverySnapshot(
        snapshots, QStringLiteral("an unsaved project"));
    if (!selected.has_value()) return;

    project::ProjectDocument blank_document;
    blank_document.bins = {"Unsorted"};
    static_cast<void>(openProjectPath(
        *selected,
        std::nullopt,
        std::move(blank_document),
        [this, snapshot = *selected](bool succeeded) {
            if (!succeeded) return;
            try {
                project_controller_.removeUnsavedSnapshotsForSession(snapshot);
            } catch (const project::ProjectError& error) {
                logging::Logger::instance().log(
                    logging::Level::Warning,
                    "project",
                    "autosave_cleanup",
                    error.what(),
                    {{"cause", error.what()}});
            }
        }));
}
