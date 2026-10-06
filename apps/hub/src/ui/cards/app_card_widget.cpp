#include "app_card_widget.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QLinearGradient>
#include <QPixmap>
#include <QFile>
#include <QMouseEvent>

namespace creative_suite::hub {

namespace {

QPixmap createFallbackIcon(const QString& id) {
    constexpr int size = 64;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(QColor(0x22, 0x22, 0x22));
    painter.setPen(QColor(0x38, 0x38, 0x38));
    painter.drawRoundedRect(QRectF(0, 0, size, size), 14, 14);

    QString initials = QStringLiteral("CS");
    if (id == QStringLiteral("video-editor")) {
        initials = QStringLiteral("V");
    } else if (id == QStringLiteral("image-editor")) {
        initials = QStringLiteral("I");
    } else if (id == QStringLiteral("motion-editor")) {
        initials = QStringLiteral("M");
    }

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(22);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, initials);

    return pixmap;
}

} // namespace

AppCardWidget::AppCardWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("AppCardWidget"));
    setFixedSize(kCardWidth, kCardHeight);
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);

    setupUi();
}

void AppCardWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 16);
    mainLayout->setSpacing(8);

    // Top row: Status badge aligned right
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addStretch();

    m_statusBadge = new AppStatusBadge(this);
    topRow->addWidget(m_statusBadge);
    mainLayout->addLayout(topRow);

    // Center icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

    mainLayout->addSpacing(4);

    // Title
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 800; color: #ffffff; background: transparent;"
    ));
    mainLayout->addWidget(m_titleLabel);

    // Tagline (short category)
    m_tagLineLabel = new QLabel(this);
    m_tagLineLabel->setAlignment(Qt::AlignCenter);
    m_tagLineLabel->setWordWrap(true);
    m_tagLineLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: #8e8e9e; background: transparent; line-height: 1.3;"
    ));
    mainLayout->addWidget(m_tagLineLabel);

    mainLayout->addStretch();

    // Progress bar (hidden by default)
    m_progressBar = new DownloadProgressBar(this);
    m_progressBar->setVisible(false);
    connect(m_progressBar, &DownloadProgressBar::cancelRequested, this, [this]() {
        emit cancelDownloadRequested(m_appInfo.id());
    });
    mainLayout->addWidget(m_progressBar);

    // Action button
    m_actionButton = new QPushButton(this);
    m_actionButton->setFixedHeight(36);
    m_actionButton->setCursor(Qt::PointingHandCursor);
    connect(m_actionButton, &QPushButton::clicked, this, &AppCardWidget::onActionButtonClicked);
    mainLayout->addWidget(m_actionButton);
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

    QPixmap iconPix;
    if (!m_appInfo.iconPath().isEmpty() && QFile::exists(m_appInfo.iconPath())) {
        iconPix = QPixmap(m_appInfo.iconPath()).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        iconPix = createFallbackIcon(m_appInfo.id());
    }
    m_iconLabel->setPixmap(iconPix);

    QString versionInfo;
    if (m_appInfo.status() == AppStatus::Installed) {
        versionInfo = m_appInfo.installedVersion();
    } else if (m_appInfo.status() == AppStatus::UpdateAvailable) {
        versionInfo = m_appInfo.latestVersion();
    }
    m_statusBadge->setStatus(m_appInfo.status(), versionInfo);

    // Clean monochrome card styling
    setStyleSheet(QStringLiteral(R"(
        #AppCardWidget {
            background-color: #161616;
            border: 1px solid #262626;
            border-radius: 14px;
        }
        #AppCardWidget:hover {
            background-color: #1e1e1e;
            border-color: #484848;
        }
    )"));

    switch (m_appInfo.status()) {
        case AppStatus::Installed:
            m_actionButton->setText(QStringLiteral("Abrir"));
            m_actionButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #202020;"
                "   color: #ffffff;"
                "   font-weight: 700;"
                "   font-size: 12px;"
                "   border: 1px solid #333333;"
                "   border-radius: 8px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #2a2a2a;"
                "   border-color: #484848;"
                "}"
            ));
            m_actionButton->setEnabled(true);
            m_progressBar->setVisible(false);
            break;

        case AppStatus::UpdateAvailable:
            m_actionButton->setText(QStringLiteral("Atualizar"));
            m_actionButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #ffffff;"
                "   color: #000000;"
                "   font-weight: 700;"
                "   font-size: 12px;"
                "   border: none;"
                "   border-radius: 8px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #e4e4e4;"
                "}"
            ));
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
            m_actionButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #ffffff;"
                "   color: #000000;"
                "   font-weight: 700;"
                "   font-size: 12px;"
                "   border: none;"
                "   border-radius: 8px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #e4e4e4;"
                "}"
                "QPushButton:pressed {"
                "   background-color: #cccccc;"
                "}"
            ));
            m_actionButton->setEnabled(true);
            m_progressBar->setVisible(false);
            break;
    }
}

void AppCardWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->pos();
        const bool inActionButton = m_actionButton && m_actionButton->geometry().contains(pos);
        const bool inProgressBar = m_progressBar && m_progressBar->isVisible() && m_progressBar->geometry().contains(pos);

        if (!inActionButton && !inProgressBar) {
            emit detailsRequested(m_appInfo.id());
        }
    }
    QFrame::mouseReleaseEvent(event);
}

} // namespace creative_suite::hub
