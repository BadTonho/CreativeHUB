#include "app_status_badge.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>

namespace creative_suite::hub {

AppStatusBadge::AppStatusBadge(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(5);

    m_label = new QLabel(this);
    m_label->setStyleSheet(QStringLiteral("font-size: 10px; font-weight: 700; background: transparent;"));
    layout->addWidget(m_label);

    updateAppearance();
}

void AppStatusBadge::setStatus(AppStatus status, const QString& versionInfo) {
    m_status = status;
    m_versionInfo = versionInfo;
    updateAppearance();
}

void AppStatusBadge::updateAppearance() {
    QString text;
    QColor bgColor;
    QColor textColor;
    QColor borderColor;

    switch (m_status) {
        case AppStatus::Installed:
            text = m_versionInfo.isEmpty() ? QStringLiteral("● Instalado")
                                           : QStringLiteral("● v%1").arg(m_versionInfo);
            bgColor = QColor(0x0e, 0x28, 0x1a);
            textColor = QColor(0x34, 0xd3, 0x99);
            borderColor = QColor(0x13, 0x4e, 0x2e);
            break;

        case AppStatus::UpdateAvailable:
            text = m_versionInfo.isEmpty() ? QStringLiteral("● Atualização")
                                           : QStringLiteral("● Nova v%1").arg(m_versionInfo);
            bgColor = QColor(0x11, 0x22, 0x3e);
            textColor = QColor(0x60, 0xa5, 0xfa);
            borderColor = QColor(0x1d, 0x3d, 0x6e);
            break;

        case AppStatus::Downloading:
            text = QStringLiteral("● Baixando...");
            bgColor = QColor(0x2a, 0x1f, 0x05);
            textColor = QColor(0xfb, 0xbf, 0x24);
            borderColor = QColor(0x52, 0x3d, 0x0a);
            break;

        case AppStatus::Installing:
            text = QStringLiteral("● Instalando...");
            bgColor = QColor(0x26, 0x12, 0x3d);
            textColor = QColor(0xc0, 0x84, 0xfc);
            borderColor = QColor(0x48, 0x20, 0x72);
            break;

        case AppStatus::NotInstalled:
        default:
            text = QStringLiteral("○ Disponível");
            bgColor = QColor(0x1a, 0x1a, 0x24);
            textColor = QColor(0x8e, 0x8e, 0xa0);
            borderColor = QColor(0x2a, 0x2a, 0x38);
            break;
    }

    m_label->setText(text);
    m_label->setStyleSheet(QStringLiteral("color: %1; font-size: 10px; font-weight: 700; background: transparent;").arg(textColor.name()));

    setStyleSheet(QStringLiteral(
        "AppStatusBadge {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 10px;"
        "}"
    ).arg(bgColor.name(), borderColor.name()));
}

} // namespace creative_suite::hub
