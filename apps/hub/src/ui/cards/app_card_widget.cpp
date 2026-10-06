#include "app_card_widget.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QFile>

namespace creative_suite::hub {

namespace {

QPixmap createFallbackIcon(const QString& id) {
    QPixmap pixmap(52, 52);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor iconBg(0x00, 0x7a, 0xff);
    QString initials = QStringLiteral("CS");

    if (id == QStringLiteral("video-editor")) {
        iconBg = QColor(0x99, 0x45, 0xff); // Violet/Purple for video
        initials = QStringLiteral("Ve");
    } else if (id == QStringLiteral("image-editor")) {
        iconBg = QColor(0x00, 0x84, 0xff); // Blue for image
        initials = QStringLiteral("Ie");
    } else if (id == QStringLiteral("motion-editor")) {
        iconBg = QColor(0xec, 0x3b, 0x83); // Pink/Magenta for motion
        initials = QStringLiteral("Mo");
    }

    painter.setBrush(iconBg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(0, 0, 52, 52), 10, 10);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(16);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, 52, 52), Qt::AlignCenter, initials);

    return pixmap;
}

} // namespace

AppCardWidget::AppCardWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("AppCardWidget"));
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QString(R"(
        #AppCardWidget {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 10px;
        }
        #AppCardWidget:hover {
            background-color: %3;
            border-color: #4a4a58;
        }
    )")
    .arg(HubPalette::cardBackground.name())
    .arg(HubPalette::cardBorder.name())
    .arg(HubPalette::cardHover.name()));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    // Top section: Icon, Title, TagLine, and Status Badge
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(14);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(52, 52);
    topRow->addWidget(m_iconLabel);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #ffffff; background: transparent;"));
    titleLayout->addWidget(m_titleLabel);

    m_tagLineLabel = new QLabel(this);
    m_tagLineLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #9a9aa4; background: transparent;"));
    titleLayout->addWidget(m_tagLineLabel);

    topRow->addLayout(titleLayout);
    topRow->addStretch();

    m_statusBadge = new AppStatusBadge(this);
    topRow->addWidget(m_statusBadge, 0, Qt::AlignTop);

    mainLayout->addLayout(topRow);

    // Description
    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0bc; line-height: 1.4; background: transparent;"));
    mainLayout->addWidget(m_descLabel);

    // Progress bar (hidden by default)
    m_progressBar = new DownloadProgressBar(this);
    m_progressBar->setVisible(false);
    connect(m_progressBar, &DownloadProgressBar::cancelRequested, this, [this]() {
        emit cancelDownloadRequested(m_appInfo.id());
    });
    mainLayout->addWidget(m_progressBar);

    // Bottom action row
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 4, 0, 0);
    bottomRow->addStretch();

    m_actionButton = new QPushButton(this);
    connect(m_actionButton, &QPushButton::clicked, this, &AppCardWidget::onActionButtonClicked);
    bottomRow->addWidget(m_actionButton);

    mainLayout->addLayout(bottomRow);
}

void AppCardWidget::setAppInfo(const AppInfo& app) {
    m_appInfo = app;
    updateVisuals();
}

void AppCardWidget::setDownloadingProgress(double percentage, const QString& statusText) {
    m_progressBar->setVisible(true);
    m_progressBar->setProgress(percentage, statusText);
}

void AppCardWidget::onActionButtonClicked() {
    if (m_appInfo.isInstalled()) {
        if (m_appInfo.hasUpdate()) {
            emit downloadRequested(m_appInfo.id());
        } else {
            emit openRequested(m_appInfo.id());
        }
    } else {
        emit downloadRequested(m_appInfo.id());
    }
}

void AppCardWidget::updateVisuals() {
    m_titleLabel->setText(m_appInfo.name());
    m_tagLineLabel->setText(m_appInfo.tagLine());
    m_descLabel->setText(m_appInfo.description());

    // Icon loading: check if resource exists, else render styled fallback
    QPixmap iconPix;
    if (!m_appInfo.iconPath().isEmpty() && QFile::exists(m_appInfo.iconPath())) {
        iconPix = QPixmap(m_appInfo.iconPath()).scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        iconPix = createFallbackIcon(m_appInfo.id());
    }
    m_iconLabel->setPixmap(iconPix);

    // Version info for badge
    QString versionInfo;
    if (m_appInfo.status() == AppStatus::Installed) {
        versionInfo = m_appInfo.installedVersion();
    } else if (m_appInfo.status() == AppStatus::UpdateAvailable) {
        versionInfo = m_appInfo.latestVersion();
    }
    m_statusBadge->setStatus(m_appInfo.status(), versionInfo);

    // Action button text and style
    switch (m_appInfo.status()) {
        case AppStatus::Installed:
            m_actionButton->setText(QStringLiteral("Abrir"));
            m_actionButton->setStyleSheet(HubStyle::secondaryButtonStyle());
            m_actionButton->setEnabled(true);
            m_progressBar->setVisible(false);
            break;

        case AppStatus::UpdateAvailable:
            m_actionButton->setText(QStringLiteral("Atualizar"));
            m_actionButton->setStyleSheet(HubStyle::primaryButtonStyle());
            m_actionButton->setEnabled(true);
            m_progressBar->setVisible(false);
            break;

        case AppStatus::Downloading:
        case AppStatus::Installing:
            m_actionButton->setText(QStringLiteral("Instalando..."));
            m_actionButton->setEnabled(false);
            m_progressBar->setVisible(true);
            break;

        case AppStatus::NotInstalled:
        default:
            m_actionButton->setText(QStringLiteral("Baixar"));
            m_actionButton->setStyleSheet(HubStyle::primaryButtonStyle());
            m_actionButton->setEnabled(true);
            m_progressBar->setVisible(false);
            break;
    }
}

} // namespace creative_suite::hub
