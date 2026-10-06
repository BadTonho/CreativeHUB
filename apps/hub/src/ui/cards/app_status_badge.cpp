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
            bgColor = QColor(0x1e, 0x1e, 0x1e);
            textColor = QColor(0xff, 0xff, 0xff);
            borderColor = QColor(0x33, 0x33, 0x33);
            break;

        case AppStatus::UpdateAvailable:
            text = m_versionInfo.isEmpty() ? QStringLiteral("● Atualização")
                                           : QStringLiteral("● Nova v%1").arg(m_versionInfo);
            bgColor = QColor(0x26, 0x26, 0x26);
            textColor = QColor(0xff, 0xff, 0xff);
            borderColor = QColor(0x3e, 0x3e, 0x3e);
            break;

        case AppStatus::Downloading:
            text = QStringLiteral("● Baixando...");
            bgColor = QColor(0x22, 0x22, 0x22);
            textColor = QColor(0xee, 0xee, 0xee);
            borderColor = QColor(0x38, 0x38, 0x38);
            break;

        case AppStatus::Installing:
            text = QStringLiteral("● Instalando...");
            bgColor = QColor(0x22, 0x22, 0x22);
            textColor = QColor(0xee, 0xee, 0xee);
            borderColor = QColor(0x38, 0x38, 0x38);
            break;

        case AppStatus::NotInstalled:
        default:
            text = QStringLiteral("○ Disponível");
            bgColor = QColor(0x16, 0x16, 0x16);
            textColor = QColor(0x88, 0x88, 0x88);
            borderColor = QColor(0x28, 0x28, 0x28);
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
