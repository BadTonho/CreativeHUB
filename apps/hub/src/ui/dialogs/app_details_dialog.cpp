#include "app_details_dialog.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"
#include "../cards/app_status_badge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QScrollArea>

namespace creative_suite::hub {

namespace {

QPixmap createFallbackIcon(const QString& id) {
    constexpr int size = 72;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor iconBg(0x00, 0x7a, 0xff);
    QString initials = QStringLiteral("CS");

    if (id == QStringLiteral("video-editor")) {
        iconBg = QColor(0x99, 0x45, 0xff);
        initials = QStringLiteral("Ve");
    } else if (id == QStringLiteral("image-editor")) {
        iconBg = QColor(0x00, 0x84, 0xff);
        initials = QStringLiteral("Ie");
    } else if (id == QStringLiteral("motion-editor")) {
        iconBg = QColor(0xec, 0x3b, 0x83);
        initials = QStringLiteral("Mo");
    }

    painter.setBrush(iconBg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(0, 0, size, size), 16, 16);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(24);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, initials);

    return pixmap;
}

} // namespace

QPixmap AppDetailsDialog::getAppIcon(const AppInfo& app) {
    if (!app.iconPath().isEmpty() && QFile::exists(app.iconPath())) {
        return QPixmap(app.iconPath()).scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return createFallbackIcon(app.id());
}

AppDetailsDialog::AppDetailsDialog(const AppInfo& app, QWidget* parent)
    : QDialog(parent)
    , m_app(app)
{
    setWindowTitle(QStringLiteral("Detalhes - %1").arg(app.name()));
    resize(580, 520);
    setMinimumSize(480, 440);

    setStyleSheet(QString(R"(
        QDialog {
            background-color: #161616;
            color: #ffffff;
            font-family: 'Segoe UI', -apple-system, sans-serif;
        }
    )"));

    setupUi();
}

void AppDetailsDialog::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(28, 24, 28, 24);
    rootLayout->setSpacing(20);

    // Header section: Icon + Titles + Action button
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(18);

    auto* iconLabel = new QLabel(this);
    iconLabel->setFixedSize(72, 72);
    iconLabel->setPixmap(getAppIcon(m_app));
    headerLayout->addWidget(iconLabel);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    auto* nameLabel = new QLabel(m_app.name(), this);
    nameLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff;"));
    titleCol->addWidget(nameLabel);

    auto* taglineLabel = new QLabel(m_app.tagLine(), this);
    taglineLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #888888;"));
    titleCol->addWidget(taglineLabel);

    auto* badge = new AppStatusBadge(this);
    QString versionStr = m_app.isInstalled() ? m_app.installedVersion() : m_app.latestVersion();
    badge->setStatus(m_app.status(), versionStr);
    titleCol->addWidget(badge, 0, Qt::AlignLeft);

    headerLayout->addLayout(titleCol);
    headerLayout->addStretch();

    // Primary action button in dialog header
    auto* actionBtn = new QPushButton(this);
    actionBtn->setFixedSize(110, 36);
    if (m_app.isInstalled()) {
        if (m_app.hasUpdate()) {
            actionBtn->setText(QStringLiteral("Atualizar"));
            actionBtn->setStyleSheet(HubStyle::primaryButtonStyle());
        } else {
            actionBtn->setText(QStringLiteral("Abrir"));
            actionBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
        }
    } else {
        actionBtn->setText(QStringLiteral("Baixar"));
        actionBtn->setStyleSheet(HubStyle::primaryButtonStyle());
    }

    connect(actionBtn, &QPushButton::clicked, this, [this]() {
        emit actionRequested(m_app.id());
        accept();
    });
    headerLayout->addWidget(actionBtn, 0, Qt::AlignTop);

    rootLayout->addLayout(headerLayout);

    // Separator line
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("background-color: #282828; max-height: 1px; border: none;"));
    rootLayout->addWidget(separator);

    // Scrollable content area
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* contentWidget = new QWidget(scrollArea);
    contentWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 4, 0, 4);
    contentLayout->setSpacing(16);

    // Description section
    auto* descTitle = new QLabel(QStringLiteral("SOBRE O APLICATIVO"), contentWidget);
    descTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px;"));
    contentLayout->addWidget(descTitle);

    auto* descText = new QLabel(m_app.description(), contentWidget);
    descText->setWordWrap(true);
    descText->setStyleSheet(QStringLiteral("font-size: 13px; color: #d0d0d0; line-height: 1.5;"));
    contentLayout->addWidget(descText);

    // Key Features section
    if (!m_app.features().isEmpty()) {
        auto* featTitle = new QLabel(QStringLiteral("PRINCIPAIS RECURSOS"), contentWidget);
        featTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px; margin-top: 6px;"));
        contentLayout->addWidget(featTitle);

        for (const auto& feat : m_app.features()) {
            auto* featRow = new QHBoxLayout();
            featRow->setSpacing(10);

            auto* checkIcon = new QLabel(QStringLiteral("✓"), contentWidget);
            checkIcon->setStyleSheet(QStringLiteral("color: %1; font-weight: bold; font-size: 13px;").arg(HubPalette::accentPrimary.name()));
            featRow->addWidget(checkIcon);

            auto* featLabel = new QLabel(feat, contentWidget);
            featLabel->setWordWrap(true);
            featLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #c4c4c4;"));
            featRow->addWidget(featLabel, 1);

            contentLayout->addLayout(featRow);
        }
    }

    // Technical specs section
    auto* techTitle = new QLabel(QStringLiteral("INFORMAÇÕES TÉCNICAS"), contentWidget);
    techTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px; margin-top: 6px;"));
    contentLayout->addWidget(techTitle);

    auto* techCard = new QFrame(contentWidget);
    techCard->setStyleSheet(QStringLiteral(
        "background-color: #1a1a1a;"
        "border: 1px solid #282828;"
        "border-radius: 8px;"
        "padding: 10px 14px;"
    ));
    auto* techLayout = new QVBoxLayout(techCard);
    techLayout->setSpacing(6);

    if (!m_app.projectFormat().isEmpty()) {
        auto* formatLabel = new QLabel(QStringLiteral("<b>Formato nativo de projeto:</b> %1").arg(m_app.projectFormat()), techCard);
        formatLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0b0;"));
        techLayout->addWidget(formatLabel);
    }

    auto* exeLabel = new QLabel(QStringLiteral("<b>Arquivo executável:</b> %1").arg(m_app.executableName()), techCard);
    exeLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0b0;"));
    techLayout->addWidget(exeLabel);

    contentLayout->addWidget(techCard);
    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    rootLayout->addWidget(scrollArea, 1);

    // Bottom close button
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();

    auto* closeBtn = new QPushButton(QStringLiteral("Fechar"), this);
    closeBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(closeBtn);

    rootLayout->addLayout(bottomRow);
}

} // namespace creative_suite::hub
