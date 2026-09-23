#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QWidget;
class QLabel;
class QPushButton;
class QTableWidget;

namespace settings {

class ShortcutManager;

struct AutosaveSnapshotItem {
    QString project_name;
    QString type;
    QString modified;
    QString snapshot_name;
    QString snapshot_path;
    QString project_path;
    QString folder_path;
};

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(
        QWidget* parent,
        ShortcutManager& shortcut_manager);

    void setAutosaveSnapshots(
        const std::vector<AutosaveSnapshotItem>& snapshots);

signals:
    void previewPerformanceMetricsEnabledChanged(bool enabled);
    void projectAutosaveSettingsChanged(
        bool enabled,
        int interval_seconds,
        int retention);
    void autosaveRefreshRequested();
    void autosaveRestoreRequested(
        const QString& snapshot_path,
        const QString& project_path);
    void autosaveDeleteRequested(const QString& snapshot_path);
    void autosaveOpenFolderRequested(const QString& folder_path);

private:
    [[nodiscard]] QWidget* createGeneralPage();
    [[nodiscard]] QWidget* createTimelinePage();
    [[nodiscard]] QWidget* createShortcutsPage();
    [[nodiscard]] QWidget* createAutosavePage();
    void updateAutosaveActions();

    ShortcutManager& shortcut_manager_;
    QTableWidget* autosave_table_ = nullptr;
    QLabel* autosave_empty_label_ = nullptr;
    QPushButton* autosave_restore_button_ = nullptr;
    QPushButton* autosave_delete_button_ = nullptr;
    QPushButton* autosave_open_folder_button_ = nullptr;
};

} // namespace settings
