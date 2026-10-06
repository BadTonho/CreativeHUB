#include "app_card_widget.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
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

    QColor iconBg(0x00, 0x7a, 0xff);
    QString initials = QStringLiteral("CS");

    if (id == QStringLiteral("video-editor")) {
        iconBg = QColor(0x99, 0x45, 0xff); // Violet/Purple
        initials = QStringLiteral("Ve");
    } else if (id == QStringLiteral("image-editor")) {
        iconBg = QColor(0x00, 0x84, 0xff); // Blue
        initials = QStringLiteral("Ie");
    } else if (id == QStringLiteral("motion-editor")) {
        iconBg = QColor(0xec, 0x3b, 0x83); // Pink/Magenta
        initials = QStringLiteral("Mo");
    }

    painter.setBrush(iconBg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(0, 0, size, size), 14, 14);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(20);
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
    setFixedSize(236, 264);
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(QString(R"(
        #AppCardWidget {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 12px;
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
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(m_titleLabel);

    // Tagline (short category)
    m_tagLineLabel = new QLabel(this);
    m_tagLineLabel->setAlignment(Qt::AlignCenter);
    m_tagLineLabel->setWordWrap(true);
    m_tagLineLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #9a9aa8; background: transparent;"));
    mainLayout->addWidget(m_tagLineLabel);

    mainLayout->addStretch();

    // Progress bar (hidden by default)
    m_progressBar = new DownloadProgressBar(this);
    m_progressBar->setVisible(false);
    connect(m_progressBar, &DownloadProgressBar::cancelRequested, this, [this]() {
        emit cancelDownloadRequested(m_appInfo.id());
    });
    mainLayout->addWidget(m_progressBar);

    // Action button: Full width at the bottom of the card
    m_actionButton = new QPushButton(this);
    m_actionButton->setFixedHeight(34);
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

    // Icon loading: check if resource exists, else render styled fallback
    QPixmap iconPix;
    if (!m_appInfo.iconPath().isEmpty() && QFile::exists(m_appInfo.iconPath())) {
        iconPix = QPixmap(m_appInfo.iconPath()).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

void AppCardWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->pos();
        if (m_actionButton && m_actionButton->geometry().contains(pos)) {
            // Cliques dentro do botão de ação são tratados pelo botão
        } else {
            emit detailsRequested(m_appInfo.id());
        }
    }
    QFrame::mouseReleaseEvent(event);
}

} // namespace creative_suite::hub
