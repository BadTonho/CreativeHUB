#pragma once

#include "../../model/app_status.h"
#include <QWidget>
#include <QLabel>

namespace creative_suite::hub {

class AppStatusBadge : public QWidget {
    Q_OBJECT

public:
    explicit AppStatusBadge(QWidget* parent = nullptr);

    void setStatus(AppStatus status, const QString& versionInfo = QString());

private:
    void updateAppearance();

    AppStatus m_status{AppStatus::NotInstalled};
    QString m_versionInfo;
    QLabel* m_label{nullptr};
};

} // namespace creative_suite::hub
