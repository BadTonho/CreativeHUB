#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;

namespace creative_suite::updater {

class UpdateRuntime;
class UpdateService;

class UpdateDialog final : public QDialog {
    Q_OBJECT

public:
    UpdateDialog(UpdateService* service,
                 UpdateRuntime* runtime,
                 QWidget* parent = nullptr);

private:
    void refresh();
    void beginOrRetry();
    void cancelDownload();

    UpdateService* m_service{nullptr};
    UpdateRuntime* m_runtime{nullptr};
    QLabel* m_version_label{nullptr};
    QLabel* m_status_label{nullptr};
    QLabel* m_notes_label{nullptr};
    QProgressBar* m_progress_bar{nullptr};
    QPushButton* m_primary_button{nullptr};
    QPushButton* m_cancel_button{nullptr};
    QString m_runtime_status;
    bool m_installation_in_progress{false};
};

} // namespace creative_suite::updater
