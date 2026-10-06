#pragma once

#include "../../model/app_info.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>

namespace creative_suite::hub {

class AppDetailsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AppDetailsDialog(const AppInfo& app, QWidget* parent = nullptr);

signals:
    void actionRequested(const QString& appId);

private:
    void setupUi();
    static QPixmap getAppIcon(const AppInfo& app);

    AppInfo m_app;
};

} // namespace creative_suite::hub
