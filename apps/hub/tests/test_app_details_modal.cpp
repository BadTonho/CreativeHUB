#include "ui/dialogs/app_details_modal.h"
#include <QApplication>
#include <cassert>
#include <iostream>

using namespace creative_suite::hub;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    AppInfo info(
        QStringLiteral("video-editor"),
        QStringLiteral("Video Editor"),
        QStringLiteral("Edição audiovisual e pós-produção"),
        QStringLiteral("Editor completo de vídeo."),
        QStringLiteral(""),
        QStringLiteral("0.1.6"),
        QStringLiteral("creative-suite-video-editor.exe"),
        QStringLiteral(""),
        AppStatus::NotInstalled,
        QStringList{QStringLiteral("Linha do tempo"), QStringLiteral("Transições")},
        QStringLiteral(".csp")
    );

    QWidget parent;
    parent.resize(800, 600);

    AppDetailsModal modal(&parent);
    assert(!modal.isVisible());

    // Show app modal originating from a card rect
    QRect originRect(100, 100, 236, 264);
    modal.showApp(info, originRect);

    assert(modal.isVisible());
    assert(modal.cardGeometry().isValid());

    // Close with animation
    modal.closeWithAnimation();

    std::cout << "All AppDetailsModal tests passed successfully!" << std::endl;
    return 0;
}
