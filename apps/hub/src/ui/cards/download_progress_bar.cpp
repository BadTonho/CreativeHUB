#include "download_progress_bar.h"
#include "../theme/hub_palette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>

namespace creative_suite::hub {

DownloadProgressBar::DownloadProgressBar(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 4, 0, 0);
    mainLayout->setSpacing(4);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setStyleSheet(QString(R"(
        QProgressBar {
            background-color: #262626;
            border: none;
            border-radius: 3px;
        }
        QProgressBar::chunk {
            background-color: %1;
            border-radius: 3px;
        }
    )").arg(HubPalette::accentPrimary.name()));
    mainLayout->addWidget(m_progressBar);

    auto* metaLayout = new QHBoxLayout();
    metaLayout->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel(QStringLiteral("0%"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; background: transparent;")
        .arg(HubPalette::textSecondary.name()));
    metaLayout->addWidget(m_statusLabel);

    metaLayout->addStretch();

    m_cancelButton = new QPushButton(QStringLiteral("Cancelar"), this);
    m_cancelButton->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background: transparent;
            color: #e06060;
            border: none;
            font-size: 11px;
            font-weight: 500;
            padding: 2px 6px;
        }
        QPushButton:hover {
            color: #ff8080;
            text-decoration: underline;
        }
    )"));
    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadProgressBar::cancelRequested);
    metaLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(metaLayout);
}

void DownloadProgressBar::setProgress(double percentage, const QString& statusText) {
    const int val = static_cast<int>(percentage);
    m_progressBar->setValue(val);
    if (!statusText.isEmpty()) {
        m_statusLabel->setText(statusText);
    } else {
        m_statusLabel->setText(QStringLiteral("%1%").arg(val));
    }
}

void DownloadProgressBar::reset() {
    m_progressBar->setValue(0);
    m_statusLabel->setText(QStringLiteral("0%"));
}

} // namespace creative_suite::hub
