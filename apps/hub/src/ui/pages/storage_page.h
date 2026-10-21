#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QStringList>
#include <QPointer>
#include <QVector>

namespace creative_suite::hub {

class StoragePage : public QWidget {
    Q_OBJECT

public:
    explicit StoragePage(QWidget* parent = nullptr);
    // Explicit cache roots keep UI regression checks isolated from user caches.
    explicit StoragePage(QStringList cachePaths, QWidget* parent = nullptr);

    void refreshStorageInfo();

private slots:
    void onClearCache();
    void onOpenCacheFolder();

private:
    void setupUi();
    QStringList m_cachePaths;

    QLabel* m_totalSizeLabel{nullptr};
    QLabel* m_statusNote{nullptr};
    QVBoxLayout* m_breakdownLayout{nullptr};
    QVector<QPointer<QLabel>> m_sizeLabels;
    quint64 m_refresh_generation{0};
};

} // namespace creative_suite::hub
