#include "ui/cards/app_card_widget.h"
#include <QApplication>
#include <QMouseEvent>
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
        QStringLiteral("0.1.0"),
        QStringLiteral("creative-suite-video-editor.exe"),
        QStringLiteral(""),
        AppStatus::NotInstalled,
        QStringList{QStringLiteral("Linha do tempo"), QStringLiteral("Transições")},
        QStringLiteral(".csp")
    );

    AppCardWidget card;
    card.setAppInfo(info);

    // Verify compact block dimensions
    assert(card.width() == AppCardWidget::kCardWidth);
    assert(card.height() == AppCardWidget::kCardHeight);

    bool receivedDetailsSignal = false;
    QString receivedAppId;
    QObject::connect(&card, &AppCardWidget::detailsRequested, [&receivedDetailsSignal, &receivedAppId](const QString& id) {
        receivedDetailsSignal = true;
        receivedAppId = id;
    });

    // Simulate clicking on the card body
    QMouseEvent event(QEvent::MouseButtonRelease, QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&card, &event);

    assert(receivedDetailsSignal);
    assert(receivedAppId == QStringLiteral("video-editor"));

    std::cout << "All AppCardWidget tests passed successfully!" << std::endl;
    return 0;
}
