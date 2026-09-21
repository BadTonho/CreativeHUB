#include "ui/system_memory_details_dialog.h"
#include "ui/system_memory_indicator.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGroupBox>
#include <QMouseEvent>
#include <QPointingDevice>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    try {
        SystemMemoryDetailsDialog dialog;
        require(dialog.windowTitle() == "Memory Usage",
                "Memory details dialog title is incorrect.");
        require(!dialog.isModal(),
                "Memory details dialog must be non-modal.");
        require(dialog.windowModality() == Qt::NonModal,
                "Memory details dialog must use non-modal window modality.");

        const auto groups = dialog.findChildren<QGroupBox*>();
        require(groups.size() == 2,
                "Memory details dialog must contain two sections.");
        require(groups.at(0)->title() == "System Memory",
                "System Memory section is missing.");
        require(groups.at(1)->title() == "Main Editor",
                "Main Editor section is missing.");

        SystemMemoryIndicator indicator;
        indicator.resize(120, 24);
        indicator.show();
        QMouseEvent click(
            QEvent::MouseButtonPress,
            QPointF(10.0, 10.0),
            QPointF(10.0, 10.0),
            QPointF(10.0, 10.0),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QApplication::sendEvent(&indicator, &click);

        auto* opened_dialog =
            indicator.findChild<SystemMemoryDetailsDialog*>();
        require(opened_dialog != nullptr,
                "Clicking the RAM indicator must open the details dialog.");
        require(opened_dialog->isVisible(),
                "Opened memory details dialog must be visible.");

        opened_dialog->close();
        QCoreApplication::processEvents();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
