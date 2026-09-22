#include "settings/settings_dialog.h"

#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace settings {

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
    auto* retention_label = new QLabel("Snapshots:", autosave_options);
    auto* retention_spin = new QSpinBox(autosave_options);
    retention_spin->setObjectName("projectAutosaveRetentionSpinBox");
    retention_spin->setRange(
        settings::kMinimumProjectAutosaveRetention,
        settings::kMaximumProjectAutosaveRetention);
    retention_spin->setValue(settings::projectAutosaveRetention());
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
