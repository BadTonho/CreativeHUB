#include "ui/cards/app_card_widget.h"
#include <QApplication>
#include <cassert>
#include <iostream>

using namespace creative_suite::hub;

int main(int argc, char* argv[]) {
    // QApplication required for QWidget
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

    AppCardWidget card;
    card.setAppInfo(info);

    // Initial state: collapsed
    assert(!card.isExpanded());
    assert(card.cardHeight() == AppCardWidget::kCollapsedHeight);

    bool receivedExpandedSignal = false;
    bool lastExpandedState = false;
    QObject::connect(&card, &AppCardWidget::expansionToggled, [&receivedExpandedSignal, &lastExpandedState](const QString&, bool expanded) {
        receivedExpandedSignal = true;
        lastExpandedState = expanded;
    });

    // Expand non-animated
    card.expand(false);
    assert(card.isExpanded());
    assert(card.cardHeight() == AppCardWidget::kExpandedHeight);
    assert(receivedExpandedSignal);
    assert(lastExpandedState == true);

    // Collapse non-animated
    receivedExpandedSignal = false;
    card.collapse(false);
    assert(!card.isExpanded());
    assert(card.cardHeight() == AppCardWidget::kCollapsedHeight);
    assert(receivedExpandedSignal);
    assert(lastExpandedState == false);

    // Toggle
    card.toggleExpanded(false);
    assert(card.isExpanded());
    assert(card.cardHeight() == AppCardWidget::kExpandedHeight);

    card.toggleExpanded(false);
    assert(!card.isExpanded());
    assert(card.cardHeight() == AppCardWidget::kCollapsedHeight);

    std::cout << "All AppCardWidget tests passed successfully!" << std::endl;
    return 0;
}
