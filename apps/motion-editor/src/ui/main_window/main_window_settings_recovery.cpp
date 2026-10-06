#include "main_window.h"
#include "main_window_support.h"

#include "persistence/motion_document_store.h"
#include "settings/autosave_preferences.h"
#include "ui/dialogs/autosave_recovery_dialog.h"
#include "ui/dialogs/general_settings_dialog.h"
#include "ui/dialogs/shortcut_settings_dialog.h"

#include <creative_suite/diagnostics/logger.h>

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace motion::ui {
using detail::pathFromQString;
using detail::pathForLog;
using detail::pathForDisplay;

void MainWindow::openShortcutSettings()
{
    ShortcutSettingsDialog dialog(shortcut_manager_.entries(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    QString error;
    if (!shortcut_manager_.applyShortcuts(dialog.assignments(), &error)) {
        const auto detail = error.toStdString();
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_settings", "save_shortcuts", detail,
            {{"settings_group", shortcut_manager_.settingsGroup().toStdString()}});
        QMessageBox::warning(
            this, QStringLiteral("Settings Error"),
            QStringLiteral("Keyboard shortcut preferences could not be saved."));
    }
}
void MainWindow::configureAutosaveTimer()
{
    if (autosave_timer_ == nullptr) return;
    autosave_timer_->setInterval(settings::autosaveIntervalSeconds() * 1000);
    if (settings::autosaveEnabled()) autosave_timer_->start();
    else autosave_timer_->stop();
}
void MainWindow::autosaveProject()
{
    if (!settings::autosaveEnabled() || !document_ || !documentIsDirty()) return;

    try {
        auto snapshot = projectData();
        if (last_autosaved_data_.has_value() &&
            *last_autosaved_data_ == snapshot) return;
        if (recovery_store_.containsSnapshotData(
                snapshot, document_path_.value_or(std::filesystem::path{}))) {
            last_autosaved_data_ = std::move(snapshot);
            return;
        }
        if (document_path_.has_value()) {
            recovery_store_.saveSnapshot(
                snapshot, *document_path_, settings::recoveryRetention());
        } else {
            recovery_store_.saveSnapshot(snapshot, settings::recoveryRetention());
        }
        last_autosaved_data_ = std::move(snapshot);
        if (recovered_untitled_snapshot_path_.has_value()) {
            cleanupRecoveredUnsavedSnapshot("autosave_recovered_snapshot_cleanup");
        }
        statusBar()->showMessage(QStringLiteral("Recovery snapshot saved."), 2500);
    } catch (const persistence::MotionDocumentError& error) {
        creative_suite::diagnostics::Context context{
            {"path", pathForLog(error.path())},
            {"error_code", std::to_string(static_cast<int>(error.code()))}};
        if (error.systemError().has_value())
            context.emplace_back("system_error", std::to_string(*error.systemError()));
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", "autosave", error.what(), context);
        statusBar()->showMessage(
            QStringLiteral("Autosave failed. Check the Motion Studio log."), 5000);
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", "autosave", error.what(),
            {{"document_path", document_path_.has_value()
                ? pathForLog(*document_path_) : std::string{}}});
        statusBar()->showMessage(
            QStringLiteral("Autosave failed. Check the Motion Studio log."), 5000);
    }
}
void MainWindow::cleanupCurrentUnsavedSnapshots(const char* operation) noexcept
{
    try {
        recovery_store_.removeCurrentUnsavedSnapshots();
    } catch (const persistence::MotionDocumentError& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"path", pathForLog(error.path())},
             {"error_code", std::to_string(static_cast<int>(error.code()))}});
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"session_id", recovery_store_.sessionId()}});
    }
}
void MainWindow::cleanupRecoveredUnsavedSnapshot(const char* operation) noexcept
{
    if (!recovered_untitled_snapshot_path_.has_value()) return;
    const auto snapshot_path = *recovered_untitled_snapshot_path_;
    try {
        const auto current_directory = recovery_store_.recoveryRoot() /
            "unsaved" / recovery_store_.sessionId();
        if (snapshot_path.parent_path() == current_directory)
            recovery_store_.removeSnapshot(snapshot_path);
        else
            recovery_store_.removeUnsavedSnapshotsForSession(snapshot_path);
        recovered_untitled_snapshot_path_.reset();
    } catch (const persistence::MotionDocumentError& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"path", pathForLog(error.path())},
             {"error_code", std::to_string(static_cast<int>(error.code()))}});
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"snapshot_path", pathForLog(snapshot_path)}});
    }
}
std::optional<std::filesystem::path> MainWindow::chooseRecoverySnapshot(
    std::vector<persistence::MotionRecoverySnapshot> snapshots,
    const QString& project_label)
{
    if (snapshots.empty()) return std::nullopt;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("motion-recovery-choice-dialog"));
    dialog.setWindowTitle(QStringLiteral("Composition Recovery"));
    dialog.setModal(true);
    dialog.resize(620, 360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(
        QStringLiteral("Recovery snapshots were found for %1. Restore one or continue without recovery.")
            .arg(project_label), &dialog);
    description->setWordWrap(true);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("motion-recovery-choice-list"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    const auto append_snapshot = [list](const persistence::MotionRecoverySnapshot& snapshot) {
        const QFileInfo info(pathForDisplay(snapshot.path));
        const auto modified = info.lastModified().isValid()
            ? info.lastModified().toLocalTime().toString(Qt::TextDate)
            : QStringLiteral("Unknown time");
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(modified, info.fileName()), list);
        item->setData(Qt::UserRole, pathForDisplay(snapshot.path));
    };
    for (const auto& snapshot : snapshots) append_snapshot(snapshot);
    if (list->count() > 0) list->setCurrentRow(0);

    auto* buttons = new QHBoxLayout();
    auto* restore = new QPushButton(QStringLiteral("Restore"), &dialog);
    restore->setObjectName(QStringLiteral("motion-recovery-choice-restore"));
    auto* remove = new QPushButton(QStringLiteral("Delete"), &dialog);
    remove->setObjectName(QStringLiteral("motion-recovery-choice-delete"));
    auto* ignore = new QPushButton(QStringLiteral("Ignore"), &dialog);
    ignore->setObjectName(QStringLiteral("motion-recovery-choice-ignore"));
    buttons->addWidget(restore);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(ignore);
    layout->addWidget(description);
    layout->addWidget(list, 1);
    layout->addLayout(buttons);

    std::optional<std::filesystem::path> selected_path;
    const auto current_path = [list]() -> std::filesystem::path {
        const auto* item = list->currentItem();
        return item == nullptr ? std::filesystem::path{}
                               : pathFromQString(item->data(Qt::UserRole).toString());
    };
    connect(restore, &QPushButton::clicked, &dialog, [&] {
        const auto path = current_path();
        if (path.empty()) return;
        selected_path = path;
        dialog.accept();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        auto* item = list->currentItem();
        if (item == nullptr) return;
        if (QMessageBox::question(
                &dialog, QStringLiteral("Delete Recovery Snapshot"),
                QStringLiteral("Delete the selected recovery snapshot?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto path = current_path();
        try {
            recovery_store_.removeSnapshot(path);
            delete list->takeItem(list->row(item));
            if (list->count() > 0) list->setCurrentRow(0);
        } catch (const persistence::MotionDocumentError& error) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Warning,
                "motion_recovery", "delete_snapshot", error.what(),
                {{"path", pathForLog(path)},
                 {"error_code", std::to_string(static_cast<int>(error.code()))},
                 {"system_error", error.systemError().has_value()
                     ? std::to_string(*error.systemError()) : std::string{}}});
            statusBar()->showMessage(
                QStringLiteral("Recovery snapshot could not be deleted."), 5000);
        }
    });
    connect(ignore, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&] {
        const auto path = current_path();
        if (path.empty()) return;
        selected_path = path;
        dialog.accept();
    });
    connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, restore, remove] {
        const bool selected = list->currentItem() != nullptr;
        restore->setEnabled(selected);
        remove->setEnabled(selected);
    });
    restore->setEnabled(list->currentItem() != nullptr);
    remove->setEnabled(list->currentItem() != nullptr);
    static_cast<void>(dialog.exec());
    return selected_path;
}
void MainWindow::maybeOfferUnsavedRecovery()
{
    auto snapshots = recovery_store_.unsavedSnapshots();
    if (snapshots.empty()) return;
    const auto selected = chooseRecoverySnapshot(
        std::move(snapshots), QStringLiteral("an untitled composition"));
    if (selected.has_value()) restoreRecoverySnapshot(*selected);
}
void MainWindow::restoreRecoverySnapshot(
    const std::filesystem::path& snapshot_path)
{
    try {
        const auto recovery = persistence::MotionDocumentStore::loadRecovery(snapshot_path);
        stageOpenProject(recovery.target_document_path, recovery.document,
                         true, snapshot_path);
    } catch (const persistence::MotionDocumentError& error) {
        reportDocumentError("restore_recovery", snapshot_path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
    } catch (const std::exception& error) {
        reportDocumentError("restore_recovery", snapshot_path, error);
    }
}
void MainWindow::refreshAutosaveRecoveryDialog(
    AutosaveRecoveryDialog& dialog) const
{
    std::vector<persistence::MotionRecoverySnapshot> snapshots;
    if (document_path_.has_value()) {
        snapshots = recovery_store_.validSnapshotsForProject(*document_path_);
    }
    auto unsaved = recovery_store_.unsavedSnapshots();
    snapshots.insert(snapshots.end(),
                     std::make_move_iterator(unsaved.begin()),
                     std::make_move_iterator(unsaved.end()));
    std::sort(snapshots.begin(), snapshots.end(),
        [](const auto& left, const auto& right) {
            if (left.modified_time != right.modified_time)
                return left.modified_time > right.modified_time;
            return left.path > right.path;
        });

    std::vector<AutosaveSnapshotRow> rows;
    rows.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        const QFileInfo file_info(pathForDisplay(snapshot.path));
        const bool has_project = !snapshot.target_document_path.empty();
        const QFileInfo project_info(pathForDisplay(snapshot.target_document_path));
        const auto modified = file_info.lastModified().isValid()
            ? file_info.lastModified().toLocalTime().toString(Qt::TextDate)
            : QStringLiteral("Unknown time");
        rows.push_back({
            has_project ? project_info.fileName() : QStringLiteral("Untitled composition"),
            has_project ? QStringLiteral("Saved project") : QStringLiteral("Untitled project"),
            modified,
            file_info.fileName(),
            pathForDisplay(snapshot.path),
            has_project ? pathForDisplay(snapshot.target_document_path) : QString{},
            pathForDisplay(snapshot.path.parent_path())});
    }
    dialog.setSnapshots(rows);
}
void MainWindow::openAutosaveRecoverySettings()
{
    AutosaveRecoveryDialog dialog(settings::autosaveEnabled(),
                                  settings::autosaveIntervalSeconds(),
                                  settings::recoveryRetention(), this);
    refreshAutosaveRecoveryDialog(dialog);
    connect(&dialog, &AutosaveRecoveryDialog::autosaveSettingsChanged,
            this, [this](bool enabled, int interval, int retention) {
        settings::setAutosaveEnabled(enabled);
        settings::setAutosaveIntervalSeconds(interval);
        settings::setRecoveryRetention(retention);
        configureAutosaveTimer();
    });
    connect(&dialog, &AutosaveRecoveryDialog::refreshRequested,
            this, [this, &dialog] { refreshAutosaveRecoveryDialog(dialog); });
    connect(&dialog, &AutosaveRecoveryDialog::deleteSnapshotRequested,
            this, [this, &dialog](const QString& encoded_path) {
        if (QMessageBox::question(
                this, QStringLiteral("Delete Recovery Snapshot"),
                QStringLiteral("Delete the selected recovery snapshot?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto path = pathFromQString(encoded_path);
        try {
            recovery_store_.removeSnapshot(path);
            refreshAutosaveRecoveryDialog(dialog);
        } catch (const persistence::MotionDocumentError& error) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Warning,
                "motion_recovery", "delete_snapshot", error.what(),
                {{"path", pathForLog(path)},
                 {"error_code", std::to_string(static_cast<int>(error.code()))}});
            statusBar()->showMessage(
                QStringLiteral("Recovery snapshot could not be deleted."), 5000);
        }
    });
    connect(&dialog, &AutosaveRecoveryDialog::openFolderRequested,
            this, [this](const QString& encoded_folder) {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(encoded_folder))) return;
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", "open_snapshot_folder",
            "The recovery snapshot folder could not be opened.",
            {{"path", pathForLog(pathFromQString(encoded_folder))}});
        statusBar()->showMessage(
            QStringLiteral("The recovery folder could not be opened."), 5000);
    });

    if (dialog.exec() == AutosaveRecoveryDialog::restore_snapshot_result) {
        const auto path = pathFromQString(dialog.selectedSnapshotPath());
        if (!path.empty()) restoreRecoverySnapshot(path);
    }
}
void MainWindow::openGeneralSettings()
{
    GeneralSettingsDialog dialog(settings::previewPerformanceMetricsEnabled(), this);
    connect(&dialog, &GeneralSettingsDialog::previewMetricsEnabledChanged,
            this, [this](bool enabled) {
        settings::setPreviewPerformanceMetricsEnabled(enabled);
        configurePreviewPerformanceMetrics();
    });
    (void)dialog.exec();
}

} // namespace motion::ui
