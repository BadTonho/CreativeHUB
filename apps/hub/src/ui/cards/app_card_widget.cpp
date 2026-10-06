#include "app_card_widget.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QMouseEvent>
#include <QEasingCurve>
#include <algorithm>

namespace creative_suite::hub {

namespace {

QPixmap createFallbackIcon(const QString& id) {
    constexpr int size = 60;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor iconBg(0x00, 0x7a, 0xff);
    QString initials = QStringLiteral("CS");

    if (id == QStringLiteral("video-editor")) {
        iconBg = QColor(0x99, 0x45, 0xff); // Violet
        initials = QStringLiteral("Ve");
    } else if (id == QStringLiteral("image-editor")) {
        iconBg = QColor(0x00, 0x84, 0xff); // Blue
        initials = QStringLiteral("Ie");
    } else if (id == QStringLiteral("motion-editor")) {
        iconBg = QColor(0xec, 0x3b, 0x83); // Pink
        initials = QStringLiteral("Mo");
    }

    painter.setBrush(iconBg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(0, 0, size, size), 13, 13);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(19);
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
    setFixedSize(kCardWidth, kCollapsedHeight);
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);

    setupUi();
    applyCardStyle(false);
}

void AppCardWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 12, 15, 14);
    mainLayout->setSpacing(6);

    // Top row: Close button (left, when expanded) + Status badge (right)
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);

    m_closeButton = new QPushButton(QStringLiteral("✕"), this);
    m_closeButton->setFixedSize(22, 22);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setToolTip(QStringLiteral("Fechar detalhes"));
    m_closeButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background-color: rgba(255, 255, 255, 0.08);"
        "   border: 1px solid rgba(255, 255, 255, 0.12);"
        "   border-radius: 11px;"
        "   color: #b0b0c0;"
        "   font-size: 11px;"
        "   font-weight: bold;"
        "   padding: 0px;"
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 70, 70, 0.25);"
        "   border-color: rgba(255, 90, 90, 0.45);"
        "   color: #ffffff;"
        "}"
    ));
    m_closeButton->setVisible(false);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        collapse(true);
    });
    topRow->addWidget(m_closeButton);

    topRow->addStretch();

    m_statusBadge = new AppStatusBadge(this);
    topRow->addWidget(m_statusBadge);
    mainLayout->addLayout(topRow);

    // Center icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(60, 60);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

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

    // Details widget (collapsible with smooth animation)
    m_detailsWidget = new QWidget(this);
    auto* detailsLayout = new QVBoxLayout(m_detailsWidget);
    detailsLayout->setContentsMargins(0, 4, 0, 0);
    detailsLayout->setSpacing(6);

    auto* divider = new QFrame(m_detailsWidget);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("background-color: #2c2c38; max-height: 1px; border: none;"));
    detailsLayout->addWidget(divider);

    m_descriptionLabel = new QLabel(m_detailsWidget);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #b2b2c2; line-height: 14px; background: transparent;"));
    detailsLayout->addWidget(m_descriptionLabel);

    auto* featHeader = new QLabel(QStringLiteral("DESTAQUES"), m_detailsWidget);
    featHeader->setStyleSheet(QStringLiteral("font-size: 9px; font-weight: 700; color: #727282; letter-spacing: 0.5px; background: transparent;"));
    detailsLayout->addWidget(featHeader);

    m_featuresLabel = new QLabel(m_detailsWidget);
    m_featuresLabel->setWordWrap(true);
    m_featuresLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #d4d4e0; background: transparent;"));
    detailsLayout->addWidget(m_featuresLabel);

    m_formatBadgeLabel = new QLabel(m_detailsWidget);
    m_formatBadgeLabel->setStyleSheet(QStringLiteral(
        "background-color: #16161c;"
        "color: #9898aa;"
        "border: 1px solid #2a2a36;"
        "border-radius: 6px;"
        "padding: 2px 7px;"
        "font-size: 10px;"
        "font-weight: 600;"
    ));
    detailsLayout->addWidget(m_formatBadgeLabel, 0, Qt::AlignLeft);

    m_collapseLinkButton = new QPushButton(QStringLiteral("▲ Recolher detalhes"), m_detailsWidget);
    m_collapseLinkButton->setCursor(Qt::PointingHandCursor);
    m_collapseLinkButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background: transparent;"
        "   color: #589bff;"
        "   border: none;"
        "   font-size: 11px;"
        "   font-weight: 600;"
        "   padding: 2px 0;"
        "}"
        "QPushButton:hover {"
        "   color: #8ac0ff;"
        "   text-decoration: underline;"
        "}"
    ));
    connect(m_collapseLinkButton, &QPushButton::clicked, this, [this]() {
        collapse(true);
    });
    detailsLayout->addWidget(m_collapseLinkButton, 0, Qt::AlignCenter);

    m_detailsOpacityEffect = new QGraphicsOpacityEffect(m_detailsWidget);
    m_detailsOpacityEffect->setOpacity(0.0);
    m_detailsWidget->setGraphicsEffect(m_detailsOpacityEffect);
    m_detailsWidget->setVisible(false);
    mainLayout->addWidget(m_detailsWidget);

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

void AppCardWidget::applyCardStyle(bool expanded) {
    if (expanded) {
        setStyleSheet(QString(R"(
            #AppCardWidget {
                background-color: %1;
                border: 1px solid %2;
                border-radius: 12px;
            }
            #AppCardWidget:hover {
                background-color: %1;
                border-color: #3b94ff;
            }
        )")
        .arg(HubPalette::cardHover.name())
        .arg(HubPalette::accentPrimary.name()));
    } else {
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
    }
}

void AppCardWidget::setAppInfo(const AppInfo& app) {
    m_appInfo = app;
    updateVisuals();
}

void AppCardWidget::setDownloadingProgress(double percentage, const QString& statusText) {
    m_progressBar->setVisible(true);
    m_progressBar->setProgress(percentage, statusText);
}

void AppCardWidget::setCardHeight(int h) {
    setFixedSize(kCardWidth, h);
    updateGeometry();
}

void AppCardWidget::expand(bool animated) {
    if (m_isExpanded) {
        return;
    }
    m_isExpanded = true;
    emit expansionToggled(m_appInfo.id(), true);

    applyCardStyle(true);
    m_closeButton->setVisible(true);
    m_detailsWidget->setVisible(true);

    if (m_heightAnimation && m_heightAnimation->state() == QAbstractAnimation::Running) {
        m_heightAnimation->stop();
    }
    if (m_opacityAnimation && m_opacityAnimation->state() == QAbstractAnimation::Running) {
        m_opacityAnimation->stop();
    }

    if (!animated) {
        setFixedSize(kCardWidth, kExpandedHeight);
        m_detailsOpacityEffect->setOpacity(1.0);
        return;
    }

    m_heightAnimation = new QPropertyAnimation(this, "cardHeight", this);
    m_heightAnimation->setDuration(260);
    m_heightAnimation->setStartValue(height());
    m_heightAnimation->setEndValue(kExpandedHeight);
    m_heightAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnimation = new QPropertyAnimation(m_detailsOpacityEffect, "opacity", this);
    m_opacityAnimation->setDuration(240);
    m_opacityAnimation->setStartValue(m_detailsOpacityEffect->opacity());
    m_opacityAnimation->setEndValue(1.0);

    m_heightAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    m_opacityAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void AppCardWidget::collapse(bool animated) {
    if (!m_isExpanded) {
        return;
    }
    m_isExpanded = false;
    emit expansionToggled(m_appInfo.id(), false);

    if (m_heightAnimation && m_heightAnimation->state() == QAbstractAnimation::Running) {
        m_heightAnimation->stop();
    }
    if (m_opacityAnimation && m_opacityAnimation->state() == QAbstractAnimation::Running) {
        m_opacityAnimation->stop();
    }

    if (!animated) {
        setFixedSize(kCardWidth, kCollapsedHeight);
        m_detailsOpacityEffect->setOpacity(0.0);
        m_detailsWidget->setVisible(false);
        m_closeButton->setVisible(false);
        applyCardStyle(false);
        return;
    }

    m_heightAnimation = new QPropertyAnimation(this, "cardHeight", this);
    m_heightAnimation->setDuration(240);
    m_heightAnimation->setStartValue(height());
    m_heightAnimation->setEndValue(kCollapsedHeight);
    m_heightAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnimation = new QPropertyAnimation(m_detailsOpacityEffect, "opacity", this);
    m_opacityAnimation->setDuration(180);
    m_opacityAnimation->setStartValue(m_detailsOpacityEffect->opacity());
    m_opacityAnimation->setEndValue(0.0);

    connect(m_heightAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_isExpanded) {
            m_detailsWidget->setVisible(false);
            m_closeButton->setVisible(false);
            applyCardStyle(false);
        }
    });

    m_heightAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    m_opacityAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void AppCardWidget::toggleExpanded(bool animated) {
    if (m_isExpanded) {
        collapse(animated);
    } else {
        expand(animated);
    }
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
    m_descriptionLabel->setText(m_appInfo.description());

    // Format bullet highlights
    QString featuresText;
    const auto& features = m_appInfo.features();
    const int count = std::min(static_cast<int>(features.size()), 3);
    for (int i = 0; i < count; ++i) {
        featuresText += QStringLiteral("• %1\n").arg(features[i]);
    }
    m_featuresLabel->setText(featuresText.trimmed());

    // Project format
    if (!m_appInfo.projectFormat().isEmpty()) {
        m_formatBadgeLabel->setText(QStringLiteral("📄 Formato: %1").arg(m_appInfo.projectFormat()));
        m_formatBadgeLabel->setVisible(true);
    } else {
        m_formatBadgeLabel->setVisible(false);
    }

    // Icon loading: check if resource exists, else render styled fallback
    QPixmap iconPix;
    if (!m_appInfo.iconPath().isEmpty() && QFile::exists(m_appInfo.iconPath())) {
        iconPix = QPixmap(m_appInfo.iconPath()).scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

        const bool inActionButton = m_actionButton && m_actionButton->geometry().contains(pos);
        const bool inCloseButton = m_closeButton && m_closeButton->isVisible() && m_closeButton->geometry().contains(pos);
        const bool inProgressBar = m_progressBar && m_progressBar->isVisible() && m_progressBar->geometry().contains(pos);
        const bool inDetailsArea = m_detailsWidget && m_detailsWidget->isVisible() && m_detailsWidget->geometry().contains(pos);

        // Click outside action buttons / close button toggles expansion
        if (!inActionButton && !inCloseButton && !inProgressBar) {
            if (!m_isExpanded) {
                expand(true);
            } else if (!inDetailsArea) {
                // Clicking header / card background when expanded closes it
                collapse(true);
            }
        }
    }
    QFrame::mouseReleaseEvent(event);
}

} // namespace creative_suite::hub
