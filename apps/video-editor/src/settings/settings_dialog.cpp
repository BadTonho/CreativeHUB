#include "settings/settings_dialog.h"

#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

namespace settings {
namespace {

constexpr int kSnapshotPathRole = Qt::UserRole;
constexpr int kProjectPathRole = Qt::UserRole + 1;
constexpr int kFolderPathRole = Qt::UserRole + 2;

} // namespace

SettingsDialog::SettingsDialog(
    QWidget* parent,
    ShortcutManager& shortcut_manager)
    : QDialog(parent), shortcut_manager_(shortcut_manager) {
    setWindowTitle("Settings");
    setModal(true);
    resize(960, 720);

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(createGeneralPage(), "General");
    tabs->addTab(createAutosavePage(), "Autosave");
    tabs->addTab(createTimelinePage(), "Timeline");
    tabs->addTab(createShortcutsPage(), "Shortcuts");

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}

QWidget* SettingsDialog::createGeneralPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* metrics_check = new QCheckBox(
        "Enable preview performance metrics", page);
    metrics_check->setObjectName("previewMetricsCheckBox");
    metrics_check->setToolTip(
        "Collect one aggregated Preview performance sample per second in the application log.");
    metrics_check->setChecked(settings::previewPerformanceMetricsEnabled());

    auto* description = new QLabel(
        "When enabled, the Main Editor records aggregated decoding, composition, UI, and GPU timing data. "
        "This preference is global and does not modify projects.",
        page);
    description->setWordWrap(true);

    layout->addWidget(metrics_check);
    layout->addWidget(description);

    auto* autosave_check = new QCheckBox(
        "Enable project autosave", page);
    autosave_check->setObjectName("projectAutosaveCheckBox");
    autosave_check->setToolTip(
        "Save recovery snapshots without overwriting the project file.");
    autosave_check->setChecked(settings::projectAutosaveEnabled());

    auto* autosave_options = new QWidget(page);
    auto* autosave_options_layout = new QHBoxLayout(autosave_options);
    autosave_options_layout->setContentsMargins(0, 0, 0, 0);
    auto* interval_label = new QLabel("Interval (seconds):", autosave_options);
    auto* interval_spin = new QSpinBox(autosave_options);
    interval_spin->setObjectName("projectAutosaveIntervalSpinBox");
    interval_spin->setRange(
        settings::kMinimumProjectAutosaveIntervalSeconds,
        settings::kMaximumProjectAutosaveIntervalSeconds);
    interval_spin->setValue(settings::projectAutosaveIntervalSeconds());
    interval_spin->setSuffix(" s");
    interval_spin->setMinimumWidth(96);
    auto* retention_label = new QLabel("Snapshots:", autosave_options);
    auto* retention_spin = new QSpinBox(autosave_options);
    retention_spin->setObjectName("projectAutosaveRetentionSpinBox");
    retention_spin->setRange(
        settings::kMinimumProjectAutosaveRetention,
        settings::kMaximumProjectAutosaveRetention);
    retention_spin->setValue(settings::projectAutosaveRetention());
    retention_spin->setMinimumWidth(96);
    autosave_options_layout->addWidget(interval_label);
    autosave_options_layout->addWidget(interval_spin);
    autosave_options_layout->addSpacing(16);
    autosave_options_layout->addWidget(retention_label);
    autosave_options_layout->addWidget(retention_spin);
    autosave_options_layout->addStretch();

    auto* autosave_description = new QLabel(
        "Autosave keeps recovery snapshots in a separate folder and never replaces the main .csp file. "
        "The default is every 30 seconds with 5 snapshots retained.",
        page);
    autosave_description->setWordWrap(true);

    layout->addWidget(autosave_check);
    layout->addWidget(autosave_options);
    layout->addWidget(autosave_description);
    layout->addStretch();

    connect(metrics_check, &QCheckBox::toggled, this, [this](bool enabled) {
        settings::setPreviewPerformanceMetricsEnabled(enabled);
        emit previewPerformanceMetricsEnabledChanged(enabled);
    });
    const auto emit_autosave_settings = [this, autosave_check, interval_spin,
                                         retention_spin]() {
        settings::setProjectAutosaveEnabled(autosave_check->isChecked());
        settings::setProjectAutosaveIntervalSeconds(interval_spin->value());
        settings::setProjectAutosaveRetention(retention_spin->value());
        emit projectAutosaveSettingsChanged(
            autosave_check->isChecked(),
            interval_spin->value(),
            retention_spin->value());
    };
    connect(autosave_check, &QCheckBox::toggled,
            this, emit_autosave_settings);
    connect(interval_spin, qOverload<int>(&QSpinBox::valueChanged),
            this, emit_autosave_settings);
    connect(retention_spin, qOverload<int>(&QSpinBox::valueChanged),
            this, emit_autosave_settings);
    return page;
}

void SettingsDialog::setAutosaveSnapshots(
    const std::vector<AutosaveSnapshotItem>& snapshots) {
    if (autosave_table_ == nullptr) return;

    autosave_table_->setRowCount(0);
    for (const auto& snapshot : snapshots) {
        const auto row = autosave_table_->rowCount();
        autosave_table_->insertRow(row);

        auto* project_item = new QTableWidgetItem(snapshot.project_name);
        project_item->setData(kSnapshotPathRole, snapshot.snapshot_path);
        project_item->setData(kProjectPathRole, snapshot.project_path);
        project_item->setData(kFolderPathRole, snapshot.folder_path);
        autosave_table_->setItem(row, 0, project_item);
        autosave_table_->setItem(row, 1, new QTableWidgetItem(snapshot.type));
        autosave_table_->setItem(row, 2, new QTableWidgetItem(snapshot.modified));
        autosave_table_->setItem(row, 3, new QTableWidgetItem(snapshot.snapshot_name));
    }

    const bool has_snapshots = autosave_table_->rowCount() > 0;
    autosave_empty_label_->setVisible(!has_snapshots);
    if (has_snapshots) {
        autosave_table_->selectRow(0);
    } else {
        autosave_table_->clearSelection();
    }
    updateAutosaveActions();
}

void SettingsDialog::updateAutosaveActions() {
    const bool has_selection = autosave_table_ != nullptr &&
        autosave_table_->currentRow() >= 0 &&
        autosave_table_->item(autosave_table_->currentRow(), 0) != nullptr;
    if (autosave_restore_button_ != nullptr) {
        autosave_restore_button_->setEnabled(has_selection);
    }
    if (autosave_delete_button_ != nullptr) {
        autosave_delete_button_->setEnabled(has_selection);
    }
    if (autosave_open_folder_button_ != nullptr) {
        autosave_open_folder_button_->setEnabled(has_selection);
    }
}

QWidget* SettingsDialog::createAutosavePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* description = new QLabel(
        "Manage recovery snapshots for the current project and unsaved projects. "
        "Restoring never overwrites the main .csp file.",
        page);
    description->setWordWrap(true);
    layout->addWidget(description);

    autosave_table_ = new QTableWidget(page);
    autosave_table_->setObjectName("autosaveSnapshotsTable");
    autosave_table_->setColumnCount(4);
    autosave_table_->setHorizontalHeaderLabels(
        {"Project", "Type", "Modified", "Snapshot"});
    autosave_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    autosave_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    autosave_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    autosave_table_->setAlternatingRowColors(true);
    autosave_table_->setSortingEnabled(false);
    autosave_table_->verticalHeader()->setVisible(false);
    autosave_table_->horizontalHeader()->setStretchLastSection(true);
    autosave_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    autosave_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    autosave_table_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    layout->addWidget(autosave_table_, 1);

    autosave_empty_label_ = new QLabel(
        "No valid autosave snapshots found.", page);
    autosave_empty_label_->setObjectName("autosaveEmptyLabel");
    autosave_empty_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(autosave_empty_label_);

    auto* buttons = new QHBoxLayout();
    auto* refresh = new QPushButton("Refresh", page);
    refresh->setObjectName("autosaveRefreshButton");
    refresh->setToolTip("Reload the available recovery snapshots");
    autosave_restore_button_ = new QPushButton("Restore Selected", page);
    autosave_restore_button_->setObjectName("autosaveRestoreButton");
    autosave_restore_button_->setToolTip(
        "Load the selected snapshot as the current dirty project");
    autosave_delete_button_ = new QPushButton("Delete Selected", page);
    autosave_delete_button_->setObjectName("autosaveDeleteButton");
    autosave_delete_button_->setToolTip("Delete the selected recovery snapshot");
    autosave_open_folder_button_ = new QPushButton("Open Folder", page);
    autosave_open_folder_button_->setObjectName("autosaveOpenFolderButton");
    autosave_open_folder_button_->setToolTip(
        "Open the folder containing the selected recovery snapshot");

    buttons->addWidget(refresh);
    buttons->addStretch();
    buttons->addWidget(autosave_restore_button_);
    buttons->addWidget(autosave_delete_button_);
    buttons->addWidget(autosave_open_folder_button_);
    layout->addLayout(buttons);

    connect(refresh, &QPushButton::clicked, this, [this]() {
        emit autosaveRefreshRequested();
    });
    connect(autosave_table_, &QTableWidget::itemSelectionChanged,
            this, &SettingsDialog::updateAutosaveActions);
    connect(autosave_restore_button_, &QPushButton::clicked, this, [this]() {
        const auto row = autosave_table_->currentRow();
        auto* item = row >= 0 ? autosave_table_->item(row, 0) : nullptr;
        if (item == nullptr) return;
        emit autosaveRestoreRequested(
            item->data(kSnapshotPathRole).toString(),
            item->data(kProjectPathRole).toString());
    });
    connect(autosave_delete_button_, &QPushButton::clicked, this, [this]() {
        const auto row = autosave_table_->currentRow();
        auto* item = row >= 0 ? autosave_table_->item(row, 0) : nullptr;
        if (item == nullptr) return;
        emit autosaveDeleteRequested(item->data(kSnapshotPathRole).toString());
    });
    connect(autosave_open_folder_button_, &QPushButton::clicked, this, [this]() {
        const auto row = autosave_table_->currentRow();
        auto* item = row >= 0 ? autosave_table_->item(row, 0) : nullptr;
        if (item == nullptr) return;
        emit autosaveOpenFolderRequested(item->data(kFolderPathRole).toString());
    });

    autosave_empty_label_->setVisible(true);
    updateAutosaveActions();
    return page;
}

QWidget* SettingsDialog::createTimelinePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* message = new QLabel(
        "Timeline preferences will be added here.", page);
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    layout->addWidget(message);
    return page;
}

QWidget* SettingsDialog::createShortcutsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* description = new QLabel(
        "Changes apply immediately. Clear a field to disable that shortcut.",
        page);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    auto* content_layout = new QVBoxLayout(content);
    QHash<QString, QKeySequenceEdit*> editors;

    auto* feedback = new QLabel(page);
    feedback->setWordWrap(true);
    feedback->setStyleSheet("color: #d26a6a;");

    for (const auto& entry : shortcut_manager_.entries()) {
        auto* row = new QWidget(content);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);

        auto* label = new QLabel(entry.label, row);
        label->setMinimumWidth(210);
        auto* editor = new QKeySequenceEdit(entry.action->shortcut(), row);
        editor->setToolTip(QString("Shortcut for %1").arg(entry.label));
        auto* reset_button = new QPushButton("Reset", row);
        reset_button->setToolTip(
            QString("Restore the default shortcut for %1").arg(entry.label));

        row_layout->addWidget(label);
        row_layout->addWidget(editor, 1);
        row_layout->addWidget(reset_button);
        content_layout->addWidget(row);
        editors.insert(entry.id, editor);

        connect(editor, &QKeySequenceEdit::keySequenceChanged, this,
                [this, editor, feedback, id = entry.id](
                    const QKeySequence& sequence) {
                    QString conflict_message;
                    if (!shortcut_manager_.setShortcut(
                            id, sequence, &conflict_message)) {
                        const QSignalBlocker blocker(editor);
                        editor->setKeySequence(shortcut_manager_.shortcut(id));
                        feedback->setText(conflict_message);
                        return;
                    }
                    feedback->clear();
                });
        connect(reset_button, &QPushButton::clicked, this,
                [this, editor, feedback, id = entry.id]() {
                    QString conflict_message;
                    if (!shortcut_manager_.resetShortcut(
                            id, &conflict_message)) {
                        feedback->setText(conflict_message);
                        return;
                    }
                    const QSignalBlocker blocker(editor);
                    editor->setKeySequence(shortcut_manager_.shortcut(id));
                    feedback->clear();
                });
    }
    content_layout->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    auto* reset_all_button = new QPushButton("Reset All", page);
    reset_all_button->setToolTip("Restore all default keyboard shortcuts");
    connect(reset_all_button, &QPushButton::clicked, this,
            [this, feedback, editors]() {
                const auto result = QMessageBox::question(
                    this,
                    "Reset Shortcuts",
                    "Restore all default keyboard shortcuts?",
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (result != QMessageBox::Yes) return;

                shortcut_manager_.resetAll();
                for (auto iterator = editors.cbegin();
                     iterator != editors.cend(); ++iterator) {
                    const QSignalBlocker blocker(iterator.value());
                    iterator.value()->setKeySequence(
                        shortcut_manager_.shortcut(iterator.key()));
                }
                feedback->clear();
            });
    layout->addWidget(reset_all_button, 0, Qt::AlignLeft);
    layout->addWidget(feedback);
    return page;
}

} // namespace settings
