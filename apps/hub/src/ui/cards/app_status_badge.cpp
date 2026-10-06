#include "app_status_badge.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>

namespace creative_suite::hub {

AppStatusBadge::AppStatusBadge(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(4);

    m_label = new QLabel(this);
    m_label->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 500; background: transparent;"));
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
            text = m_versionInfo.isEmpty() ? QStringLiteral("Instalado")
                                           : QStringLiteral("Instalado (v%1)").arg(m_versionInfo);
            bgColor = QColor(0x1a, 0x3d, 0x24);
            textColor = QColor(0x4a, 0xde, 0x80);
            borderColor = QColor(0x23, 0x5a, 0x34);
            break;

        case AppStatus::UpdateAvailable:
            text = m_versionInfo.isEmpty() ? QStringLiteral("Atualização disponível")
                                           : QStringLiteral("Atualização (v%1)").arg(m_versionInfo);
            bgColor = QColor(0x10, 0x2a, 0x4c);
            textColor = QColor(0x60, 0xa5, 0xfa);
            borderColor = QColor(0x1e, 0x40, 0x70);
            break;

        case AppStatus::Downloading:
            text = QStringLiteral("Baixando...");
            bgColor = QColor(0x33, 0x25, 0x05);
            textColor = QColor(0xfb, 0xbf, 0x24);
            borderColor = QColor(0x5c, 0x43, 0x0a);
            break;

        case AppStatus::Installing:
            text = QStringLiteral("Instalando...");
            bgColor = QColor(0x2d, 0x18, 0x45);
            textColor = QColor(0xc0, 0x84, 0xfc);
            borderColor = QColor(0x50, 0x28, 0x78);
            break;

        case AppStatus::NotInstalled:
        default:
            text = QStringLiteral("Não instalado");
            bgColor = QColor(0x28, 0x28, 0x30);
            textColor = QColor(0x90, 0x90, 0x9c);
            borderColor = QColor(0x3a, 0x3a, 0x46);
            break;
    }

    m_label->setText(text);
    m_label->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; font-weight: 600; background: transparent;").arg(textColor.name()));

    setStyleSheet(QStringLiteral(
        "AppStatusBadge {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 12px;"
        "}"
    ).arg(bgColor.name(), borderColor.name()));
}

} // namespace creative_suite::hub
