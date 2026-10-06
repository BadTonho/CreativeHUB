#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace creative_suite::hub {

class StoragePage : public QWidget {
    Q_OBJECT

public:
    explicit StoragePage(QWidget* parent = nullptr);

    void refreshStorageInfo();

private slots:
    void onClearCache();
    void onOpenCacheFolder();

private:
    void setupUi();

    QLabel* m_totalSizeLabel{nullptr};
    QLabel* m_statusNote{nullptr};
    QVBoxLayout* m_breakdownLayout{nullptr};
};

} // namespace creative_suite::hub
