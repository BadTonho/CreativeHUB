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

    QLinearGradient grad(0, 0, size, size);
    QString initials = QStringLiteral("CS");

    if (id == QStringLiteral("video-editor")) {
        grad.setColorAt(0.0, QColor(0xa8, 0x55, 0xf7));
        grad.setColorAt(1.0, QColor(0x7c, 0x3a, 0xed));
        initials = QStringLiteral("Ve");
    } else if (id == QStringLiteral("image-editor")) {
        grad.setColorAt(0.0, QColor(0x38, 0xbd, 0xf8));
        grad.setColorAt(1.0, QColor(0x02, 0x84, 0xc7));
        initials = QStringLiteral("Ie");
    } else if (id == QStringLiteral("motion-editor")) {
        grad.setColorAt(0.0, QColor(0xf4, 0x3f, 0x5e));
        grad.setColorAt(1.0, QColor(0xec, 0x48, 0x99));
        initials = QStringLiteral("Mo");
    } else {
        grad.setColorAt(0.0, QColor(0x63, 0x66, 0xf1));
        grad.setColorAt(1.0, QColor(0x4f, 0x46, 0xe5));
    }

    painter.setBrush(grad);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(0, 0, size, size), 16, 16);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(21);
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
        "font-size: 11px; color: #9da3b4; background: transparent; line-height: 1.3;"
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

    const QColor accent = HubPalette::appAccentColor(m_appInfo.id());

    // Dynamic card stylesheet with app-specific accent on hover
    setStyleSheet(QStringLiteral(R"(
        #AppCardWidget {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #1e1e28, stop:1 #171720);
            border: 1px solid #292938;
            border-radius: 14px;
        }
        #AppCardWidget:hover {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #252534, stop:1 #1b1b26);
            border-color: %1;
        }
    )").arg(accent.name()));

    switch (m_appInfo.status()) {
        case AppStatus::Installed:
            m_actionButton->setText(QStringLiteral("Abrir"));
            m_actionButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #242432;"
                "   color: #ffffff;"
                "   font-weight: 700;"
                "   font-size: 12px;"
                "   border: 1px solid #38384a;"
                "   border-radius: 8px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #2f2f42;"
                "   border-color: #555570;"
                "}"
            ));
            m_actionButton->setEnabled(true);
            m_progressBar->setVisible(false);
            break;

        case AppStatus::UpdateAvailable:
            m_actionButton->setText(QStringLiteral("Atualizar"));
            m_actionButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #2563eb, stop:1 #3b82f6);"
                "   color: #ffffff;"
                "   font-weight: 700;"
                "   font-size: 12px;"
                "   border: 1px solid rgba(255, 255, 255, 0.15);"
                "   border-radius: 8px;"
                "}"
                "QPushButton:hover {"
                "   background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #1d4ed8, stop:1 #2563eb);"
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
                "   background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2);"
                "   color: #ffffff;"
                "   font-weight: 700;"
                "   font-size: 12px;"
                "   border: 1px solid rgba(255, 255, 255, 0.15);"
                "   border-radius: 8px;"
                "}"
                "QPushButton:hover {"
                "   opacity: 0.9;"
                "   border-color: rgba(255, 255, 255, 0.3);"
                "}"
            ).arg(HubPalette::accentPrimary.name(), HubPalette::accentPrimaryHover.name()));
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
