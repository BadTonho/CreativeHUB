#pragma once

#include "../../model/app_info.h"
#include "app_status_badge.h"
#include "download_progress_bar.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>

namespace creative_suite::hub {

class AppCardWidget : public QFrame {
    Q_OBJECT

public:
    explicit AppCardWidget(QWidget* parent = nullptr);

    void setAppInfo(const AppInfo& app);
    [[nodiscard]] const AppInfo& appInfo() const noexcept { return m_appInfo; }

    void setDownloadingProgress(double percentage, const QString& statusText);

signals:
    void openRequested(const QString& appId);
    void downloadRequested(const QString& appId);
    void cancelDownloadRequested(const QString& appId);

private slots:
    void onActionButtonClicked();

private:
    void updateVisuals();

    AppInfo m_appInfo;

    QLabel* m_iconLabel{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_tagLineLabel{nullptr};
    QLabel* m_descLabel{nullptr};
    AppStatusBadge* m_statusBadge{nullptr};
    QPushButton* m_actionButton{nullptr};
    DownloadProgressBar* m_progressBar{nullptr};
};

} // namespace creative_suite::hub
