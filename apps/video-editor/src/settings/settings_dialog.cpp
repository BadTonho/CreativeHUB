#include "settings/settings_dialog.h"

#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

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
        "When enabled, the Video Editor records aggregated decoding, composition, UI, and GPU timing data. "
        "This preference is global and does not modify projects.",
        page);
    description->setWordWrap(true);

    layout->addWidget(metrics_check);
    layout->addWidget(description);

    auto* gpu_check = new QCheckBox(
        "Use GPU for timeline preview (Experimental)", page);
    gpu_check->setObjectName("gpuCompositionCheckBox");
    gpu_check->setToolTip("Applies immediately. Falls back to CPU when needed. Export is unchanged.");
    gpu_check->setAccessibleDescription(gpu_check->toolTip());
    gpu_check->setChecked(settings::gpuCompositionEnabled());
    layout->addWidget(gpu_check);
    connect(gpu_check, &QCheckBox::toggled, this, [this](bool enabled) {
        settings::setGpuCompositionEnabled(enabled);
        emit gpuCompositionEnabledChanged(enabled);
    });

    auto* transitions_group = new QGroupBox(
        "Workspace transitions", page);
    transitions_group->setObjectName("workspaceTransitionsGroup");
    auto* transitions_layout = new QVBoxLayout(transitions_group);
    auto* transitions_check = new QCheckBox(
        "Animate workspace switches", transitions_group);
    transitions_check->setObjectName("workspacePageTransitionsCheckBox");
    transitions_check->setToolTip(
        "Slide the workspace panels when switching among Edit, Fusion, and Render.");
    transitions_check->setChecked(settings::workspacePageTransitionsEnabled());
    transitions_layout->addWidget(transitions_check);

    auto* style_row = new QWidget(transitions_group);
    auto* style_layout = new QHBoxLayout(style_row);
    style_layout->setContentsMargins(0, 0, 0, 0);
    auto* style_label = new QLabel("Style:", style_row);
    auto* style_combo = new QComboBox(style_row);
    style_combo->setObjectName("workspacePageTransitionStyleComboBox");
    style_combo->addItem(
        "Workspace content",
        static_cast<int>(
            WorkspacePageTransitionStyle::WorkspaceContent));
    style_combo->addItem(
        "Entire application window",
        static_cast<int>(
            WorkspacePageTransitionStyle::EntireApplicationWindow));
    style_combo->setAccessibleName("Workspace transition style");
    const auto selected_style = settings::workspacePageTransitionStyle();
    style_combo->setCurrentIndex(style_combo->findData(
        static_cast<int>(selected_style)));
    style_layout->addWidget(style_label);
    style_layout->addWidget(style_combo, 1);
    transitions_layout->addWidget(style_row);

    auto* duration_row = new QWidget(transitions_group);
    auto* duration_layout = new QHBoxLayout(duration_row);
    duration_layout->setContentsMargins(0, 0, 0, 0);
    auto* duration_label = new QLabel("Duration:", duration_row);
    auto* duration_slider = new QSlider(Qt::Horizontal, duration_row);
    duration_slider->setObjectName("workspacePageTransitionDurationSlider");
    duration_slider->setRange(
        settings::kMinimumWorkspacePageTransitionDurationMs,
        settings::kMaximumWorkspacePageTransitionDurationMs);
    duration_slider->setSingleStep(
        settings::kWorkspacePageTransitionDurationStepMs);
    duration_slider->setPageStep(
        settings::kWorkspacePageTransitionDurationStepMs);
    duration_slider->setTickInterval(
        settings::kWorkspacePageTransitionDurationStepMs);
    duration_slider->setTickPosition(QSlider::TicksBelow);
    duration_slider->setValue(settings::workspacePageTransitionDurationMs());
    duration_slider->setAccessibleName("Workspace transition duration");
    auto* duration_value = new QLabel(
        QStringLiteral("%1 ms").arg(duration_slider->value()), duration_row);
    duration_value->setObjectName("workspacePageTransitionDurationLabel");
    duration_value->setMinimumWidth(64);
    duration_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    duration_layout->addWidget(duration_label);
    duration_layout->addWidget(duration_slider, 1);
    duration_layout->addWidget(duration_value);
    transitions_layout->addWidget(duration_row);

    auto* transitions_description = new QLabel(
        "Workspace content slides everything below the menu bar as one block. Entire application window also moves the native window frame. These preferences are saved on this device and do not modify projects.",
        transitions_group);
    transitions_description->setWordWrap(true);
    transitions_layout->addWidget(transitions_description);
    transitions_check->setAccessibleDescription(
        transitions_description->text());
    duration_slider->setToolTip(
        "Choose a duration from 100 to 600 milliseconds in 25 millisecond steps.");
    duration_slider->setEnabled(transitions_check->isChecked());
    style_combo->setEnabled(transitions_check->isChecked());

    connect(transitions_check, &QCheckBox::toggled, this,
            [duration_slider, style_combo](bool enabled) {
                settings::setWorkspacePageTransitionsEnabled(enabled);
                duration_slider->setEnabled(enabled);
                style_combo->setEnabled(enabled);
            });
    connect(style_combo, &QComboBox::currentIndexChanged, this,
            [style_combo](int index) {
                const auto value = style_combo->itemData(index).toInt();
                settings::setWorkspacePageTransitionStyle(
                    static_cast<WorkspacePageTransitionStyle>(value));
            });
    connect(duration_slider, &QSlider::valueChanged, this,
            [duration_slider, duration_value](int value) {
                settings::setWorkspacePageTransitionDurationMs(value);
                const auto normalized =
                    settings::workspacePageTransitionDurationMs();
                if (normalized != value) {
                    const QSignalBlocker blocker(duration_slider);
                    duration_slider->setValue(normalized);
                }
                duration_value->setText(
                    QStringLiteral("%1 ms").arg(normalized));
            });
    layout->addWidget(transitions_group);

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
    auto* waveform_row = new QWidget(page);
    auto* waveform_layout = new QHBoxLayout(waveform_row);
    waveform_layout->setContentsMargins(0, 0, 0, 0);
    auto* waveform_label = new QLabel("Audio waveform display:", waveform_row);
    auto* waveform_mode = new QComboBox(waveform_row);
    waveform_mode->setObjectName("audioWaveformDisplayModeComboBox");
    waveform_mode->addItem("Mono", static_cast<int>(
        AudioWaveformDisplayMode::Mono));
    waveform_mode->addItem("Stereo", static_cast<int>(
        AudioWaveformDisplayMode::Stereo));
    waveform_mode->setCurrentIndex(waveform_mode->findData(static_cast<int>(
        settings::audioWaveformDisplayMode())));
    waveform_mode->setToolTip(
        "Choose a combined mono waveform or separate left and right channels.");
    waveform_label->setBuddy(waveform_mode);
    waveform_layout->addWidget(waveform_label);
    waveform_layout->addWidget(waveform_mode);
    waveform_layout->addStretch();
    auto* waveform_description = new QLabel(
        "This global preference applies immediately and does not modify projects. "
        "Stereo media shows left and right channels in separate halves; mono media "
        "keeps one centered waveform.", page);
    waveform_description->setWordWrap(true);
    layout->addWidget(waveform_row);
    layout->addWidget(waveform_description);
    layout->addStretch();
    connect(waveform_mode, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this, waveform_mode](int index) {
            const auto mode = static_cast<AudioWaveformDisplayMode>(
                waveform_mode->itemData(index).toInt());
            settings::setAudioWaveformDisplayMode(mode);
            emit audioWaveformStereoModeChanged(
                mode == AudioWaveformDisplayMode::Stereo);
        });
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
    feedback->setObjectName(QStringLiteral("shortcutFeedback"));
    feedback->setWordWrap(true);
    feedback->setStyleSheet("color: #d26a6a;");

    const auto add_scope = [this, content, content_layout, feedback, &editors](
                               ShortcutScope scope,
                               const QString& description_text) {
        const auto first = std::find_if(
            shortcut_manager_.entries().cbegin(),
            shortcut_manager_.entries().cend(),
            [scope](const ShortcutEntry& entry) { return entry.scope == scope; });
        if (first == shortcut_manager_.entries().cend()) return;

        auto* heading = new QLabel(
            shortcut_manager_.scopeLabel(scope), content);
        heading->setObjectName(
            QStringLiteral("shortcutScope_%1")
                .arg(shortcut_manager_.scopeLabel(scope).toLower()));
        QFont heading_font = heading->font();
        heading_font.setBold(true);
        heading->setFont(heading_font);
        content_layout->addWidget(heading);

        auto* availability = new QLabel(description_text, content);
        availability->setObjectName(
            QStringLiteral("shortcutScopeDescription_%1")
                .arg(shortcut_manager_.scopeLabel(scope).toLower()));
        availability->setWordWrap(true);
        content_layout->addWidget(availability);

        for (const auto& entry : shortcut_manager_.entries()) {
            if (entry.scope != scope) continue;
            auto* row = new QWidget(content);
            row->setObjectName(QStringLiteral("shortcutRow_%1").arg(entry.id));
            auto* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);

            auto* label = new QLabel(entry.label, row);
            label->setMinimumWidth(210);
            auto* editor = new QKeySequenceEdit(
                shortcut_manager_.shortcut(entry.id), row);
            editor->setObjectName(
                QStringLiteral("shortcutEditor_%1").arg(entry.id));
            editor->setToolTip(QString("Shortcut for %1").arg(entry.label));
            auto* reset_button = new QPushButton("Reset", row);
            reset_button->setObjectName(
                QStringLiteral("shortcutReset_%1").arg(entry.id));
            reset_button->setToolTip(
                QString("Restore the default shortcut for %1").arg(entry.label));
            auto* clear_button = new QPushButton("Clear", row);
            clear_button->setObjectName(
                QStringLiteral("shortcutClear_%1").arg(entry.id));
            clear_button->setToolTip(
                QString("Disable the shortcut for %1").arg(entry.label));

            row_layout->addWidget(label);
            row_layout->addWidget(editor, 1);
            row_layout->addWidget(clear_button);
            row_layout->addWidget(reset_button);
            content_layout->addWidget(row);
            editors.insert(entry.id, editor);

            connect(editor, &QKeySequenceEdit::keySequenceChanged, this,
                    [this, editor, feedback, id = entry.id](
                        const QKeySequence& sequence) {
                        // QKeySequenceEdit emits an empty sequence while a new
                        // combination is being captured. Keep the configured
                        // binding until the user finishes or explicitly clears it.
                        if (sequence.isEmpty()) return;
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
            connect(editor, &QKeySequenceEdit::editingFinished, this,
                    [this, editor, id = entry.id]() {
                        if (!editor->keySequence().isEmpty() ||
                            shortcut_manager_.shortcut(id).isEmpty()) {
                            return;
                        }
                        const QSignalBlocker blocker(editor);
                        editor->setKeySequence(shortcut_manager_.shortcut(id));
                    });
            connect(clear_button, &QPushButton::clicked, this,
                    [this, editor, feedback, id = entry.id]() {
                        QString error_message;
                        if (!shortcut_manager_.setShortcut(
                                id, {}, &error_message)) {
                            feedback->setText(error_message);
                            return;
                        }
                        const QSignalBlocker blocker(editor);
                        editor->clear();
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
    };

    add_scope(ShortcutScope::Application,
              QStringLiteral("Available in every workspace."));
    add_scope(ShortcutScope::Shared,
              QStringLiteral("Available in the Edit and Fusion workspaces."));
    add_scope(ShortcutScope::Edit,
              QStringLiteral("Available only in the Edit workspace."));
    add_scope(ShortcutScope::Fusion,
              QStringLiteral("Available only in the Fusion workspace."));
    add_scope(ShortcutScope::Render,
              QStringLiteral("Available only in the Render workspace."));
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
