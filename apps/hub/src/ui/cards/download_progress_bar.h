#pragma once

#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>

namespace creative_suite::hub {

class DownloadProgressBar : public QWidget {
    Q_OBJECT

public:
    explicit DownloadProgressBar(QWidget* parent = nullptr);

    void setProgress(double percentage, const QString& statusText = QString());
    void reset();

signals:
    void cancelRequested();

private:
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_cancelButton{nullptr};
};

} // namespace creative_suite::hub
