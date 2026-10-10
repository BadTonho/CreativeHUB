#include "ui/cards/app_card_widget.h"
#include <QApplication>
#include <QMouseEvent>
#include "../../../cmake/test_support/test_check.h"
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
    CS_TEST_CHECK(card.width() == AppCardWidget::kCardWidth);
    CS_TEST_CHECK(card.height() == AppCardWidget::kCardHeight);

    bool receivedDetailsSignal = false;
    QString receivedAppId;
    QObject::connect(&card, &AppCardWidget::detailsRequested, [&receivedDetailsSignal, &receivedAppId](const QString& id) {
        receivedDetailsSignal = true;
        receivedAppId = id;
    });

    // Simulate clicking on the card body
    QMouseEvent event(QEvent::MouseButtonRelease, QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&card, &event);

    CS_TEST_CHECK(receivedDetailsSignal);
    CS_TEST_CHECK(receivedAppId == QStringLiteral("video-editor"));

    std::cout << "All AppCardWidget tests passed successfully!" << std::endl;
    return 0;
}
