#include "autosave_recovery_dialog.h"

#include "../settings/autosave_preferences.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace motion::ui {
namespace {

constexpr int kSnapshotPathRole = Qt::UserRole + 1;
constexpr int kProjectPathRole = Qt::UserRole + 2;
constexpr int kFolderPathRole = Qt::UserRole + 3;

} // namespace

AutosaveRecoveryDialog::AutosaveRecoveryDialog(
    bool enabled,
    int interval_seconds,
    int retention,
    QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("motion-autosave-recovery-dialog"));
    setWindowTitle(QStringLiteral("Autosave & Recovery"));
    resize(760, 460);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    enabled_check_ = new QCheckBox(QStringLiteral("Enable autosave"), this);
    enabled_check_->setObjectName(QStringLiteral("motion-autosave-enabled"));
    enabled_check_->setChecked(enabled);
    interval_spin_ = new QSpinBox(this);
    interval_spin_->setObjectName(QStringLiteral("motion-autosave-interval"));
    interval_spin_->setRange(settings::minimum_autosave_interval_seconds,
                             settings::maximum_autosave_interval_seconds);
    interval_spin_->setValue(interval_seconds);
    interval_spin_->setSuffix(QStringLiteral(" s"));
    retention_spin_ = new QSpinBox(this);
    retention_spin_->setObjectName(QStringLiteral("motion-autosave-retention"));
    retention_spin_->setRange(settings::minimum_recovery_retention,
                              settings::maximum_recovery_retention);
    retention_spin_->setValue(retention);
    form->addRow(enabled_check_);
    form->addRow(QStringLiteral("Interval:"), interval_spin_);
    form->addRow(QStringLiteral("Keep snapshots:"), retention_spin_);
    layout->addLayout(form);

    auto* description = new QLabel(
        QStringLiteral("Recovery snapshots never replace the original .motion file."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    snapshots_table_ = new QTableWidget(0, 4, this);
    snapshots_table_->setObjectName(QStringLiteral("motion-recovery-snapshots"));
    snapshots_table_->setHorizontalHeaderLabels({
        QStringLiteral("Composition"), QStringLiteral("Type"),
        QStringLiteral("Modified"), QStringLiteral("Snapshot")});
    snapshots_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    snapshots_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    snapshots_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    snapshots_table_->verticalHeader()->hide();
    snapshots_table_->horizontalHeader()->setStretchLastSection(true);
    snapshots_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    layout->addWidget(snapshots_table_, 1);

    auto* buttons = new QHBoxLayout();
    auto* refresh = new QPushButton(QStringLiteral("Refresh"), this);
    refresh->setObjectName(QStringLiteral("motion-recovery-refresh"));
    auto* restore = new QPushButton(QStringLiteral("Restore"), this);
    restore->setObjectName(QStringLiteral("motion-recovery-restore"));
    auto* remove = new QPushButton(QStringLiteral("Delete"), this);
    remove->setObjectName(QStringLiteral("motion-recovery-delete"));
    auto* open_folder = new QPushButton(QStringLiteral("Open Folder"), this);
    open_folder->setObjectName(QStringLiteral("motion-recovery-open-folder"));
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    close->setObjectName(QStringLiteral("motion-recovery-close"));
    buttons->addWidget(refresh);
    buttons->addWidget(restore);
    buttons->addWidget(remove);
    buttons->addWidget(open_folder);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(enabled_check_, &QCheckBox::toggled, this, [this] {
        emit autosaveSettingsChanged(enabled_check_->isChecked(),
                                     interval_spin_->value(),
                                     retention_spin_->value());
    });
    const auto emit_settings = [this] {
        emit autosaveSettingsChanged(enabled_check_->isChecked(),
                                     interval_spin_->value(),
                                     retention_spin_->value());
    };
    connect(interval_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, [emit_settings](int) { emit_settings(); });
    connect(retention_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, [emit_settings](int) { emit_settings(); });
    connect(refresh, &QPushButton::clicked, this, &AutosaveRecoveryDialog::refreshRequested);
    connect(restore, &QPushButton::clicked, this, [this] {
        if (!selectedSnapshotPath().isEmpty()) done(restore_snapshot_result);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (!selectedSnapshotPath().isEmpty())
            emit deleteSnapshotRequested(selectedSnapshotPath());
    });
    connect(open_folder, &QPushButton::clicked, this, [this] {
        if (!selectedFolderPath().isEmpty()) emit openFolderRequested(selectedFolderPath());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(snapshots_table_, &QTableWidget::itemSelectionChanged,
            this, &AutosaveRecoveryDialog::updateActions);
    connect(snapshots_table_, &QTableWidget::itemDoubleClicked, this,
            [this] { if (!selectedSnapshotPath().isEmpty()) done(restore_snapshot_result); });
    updateActions();
}

void AutosaveRecoveryDialog::setSnapshots(
    const std::vector<AutosaveSnapshotRow>& snapshots)
{
    snapshots_table_->setRowCount(0);
    for (const auto& snapshot : snapshots) {
        const int row = snapshots_table_->rowCount();
        snapshots_table_->insertRow(row);
        auto* project = new QTableWidgetItem(snapshot.project_name);
        project->setData(kSnapshotPathRole, snapshot.snapshot_path);
        project->setData(kProjectPathRole, snapshot.project_path);
        project->setData(kFolderPathRole, snapshot.folder_path);
        snapshots_table_->setItem(row, 0, project);
        snapshots_table_->setItem(row, 1, new QTableWidgetItem(snapshot.type));
        snapshots_table_->setItem(row, 2, new QTableWidgetItem(snapshot.modified));
        snapshots_table_->setItem(row, 3, new QTableWidgetItem(snapshot.snapshot_name));
    }
    if (snapshots_table_->rowCount() > 0) snapshots_table_->selectRow(0);
    updateActions();
}

QString AutosaveRecoveryDialog::selectedSnapshotPath() const
{
    const auto items = snapshots_table_->selectedItems();
    return items.isEmpty() ? QString{} :
        items.front()->data(kSnapshotPathRole).toString();
}

QString AutosaveRecoveryDialog::selectedProjectPath() const
{
    const auto items = snapshots_table_->selectedItems();
    return items.isEmpty() ? QString{} :
        items.front()->data(kProjectPathRole).toString();
}

QString AutosaveRecoveryDialog::selectedFolderPath() const
{
    const auto items = snapshots_table_->selectedItems();
    return items.isEmpty() ? QString{} :
        items.front()->data(kFolderPathRole).toString();
}

void AutosaveRecoveryDialog::updateActions()
{
    const bool has_snapshot = !selectedSnapshotPath().isEmpty();
    for (auto* button : findChildren<QPushButton*>()) {
        if (button->objectName() == QStringLiteral("motion-recovery-restore") ||
            button->objectName() == QStringLiteral("motion-recovery-delete") ||
            button->objectName() == QStringLiteral("motion-recovery-open-folder")) {
            button->setEnabled(has_snapshot);
        }
    }
}

} // namespace motion::ui
