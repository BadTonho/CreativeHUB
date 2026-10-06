#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QCheckBox;
class QSpinBox;
class QTableWidget;

namespace motion::ui {

struct AutosaveSnapshotRow {
    QString project_name;
    QString type;
    QString modified;
    QString snapshot_name;
    QString snapshot_path;
    QString project_path;
    QString folder_path;
};

class AutosaveRecoveryDialog final : public QDialog {
    Q_OBJECT

public:
    static constexpr int restore_snapshot_result = QDialog::Accepted + 1;

    explicit AutosaveRecoveryDialog(bool enabled,
                                    int interval_seconds,
                                    int retention,
                                    QWidget* parent = nullptr);

    void setSnapshots(const std::vector<AutosaveSnapshotRow>& snapshots);
    [[nodiscard]] QString selectedSnapshotPath() const;
    [[nodiscard]] QString selectedProjectPath() const;
    [[nodiscard]] QString selectedFolderPath() const;

signals:
    void autosaveSettingsChanged(bool enabled, int interval_seconds, int retention);
    void refreshRequested();
    void deleteSnapshotRequested(const QString& snapshot_path);
    void openFolderRequested(const QString& folder_path);

private:
    void updateActions();

    QCheckBox* enabled_check_ = nullptr;
    QSpinBox* interval_spin_ = nullptr;
    QSpinBox* retention_spin_ = nullptr;
    QTableWidget* snapshots_table_ = nullptr;
};

} // namespace motion::ui
