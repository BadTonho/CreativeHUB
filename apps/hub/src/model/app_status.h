#pragma once

#include <QString>

namespace creative_suite::hub {

enum class AppStatus {
    NotInstalled,
    Installed,
    UpdateAvailable,
    Downloading,
    Installing
};

inline QString appStatusToString(AppStatus status) {
    switch (status) {
        case AppStatus::NotInstalled:
            return QStringLiteral("Não instalado");
        case AppStatus::Installed:
            return QStringLiteral("Instalado");
        case AppStatus::UpdateAvailable:
            return QStringLiteral("Atualização disponível");
        case AppStatus::Downloading:
            return QStringLiteral("Baixando...");
        case AppStatus::Installing:
            return QStringLiteral("Instalando...");
    }
    return QStringLiteral("Desconhecido");
}

} // namespace creative_suite::hub
