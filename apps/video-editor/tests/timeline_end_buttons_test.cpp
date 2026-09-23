#include "ui/timeline_end_buttons.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

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
        QWidget parent;
        const auto buttons = ui::createTimelineEndButtons(&parent);
        auto* buttons_container = buttons.container;
        auto* layout = qobject_cast<QHBoxLayout*>(buttons_container->layout());
        require(layout != nullptr,
                "Timeline end buttons must use a horizontal layout.");
        require(layout->count() == 2,
                "Timeline end buttons must contain exactly two buttons.");

        auto* edit_button = qobject_cast<QPushButton*>(
            layout->itemAt(0)->widget());
        auto* unnamed_button = qobject_cast<QPushButton*>(
            layout->itemAt(1)->widget());
        require(edit_button != nullptr && unnamed_button != nullptr,
                "Timeline end buttons must be ordered button widgets.");
        require(edit_button->objectName() == "timelineEditButton",
                "The first timeline end button must be Edit.");
        require(edit_button->text() == "Edit",
                "The first timeline end button label must be Edit.");
        require(edit_button->size() == QSize(68, 28),
                "The Edit button must be 68 by 28 pixels so its label fits.");
        require(edit_button == buttons.edit,
                "The first timeline end button must be the Edit selector.");
        require(edit_button->isCheckable() && edit_button->isChecked(),
                "The Edit selector must be active by default.");
        require(edit_button->accessibleName() == "Edit workspace",
                "The Edit selector must have an accessible name.");
        require(!edit_button->toolTip().isEmpty(),
                "The Edit selector must explain its action.");
        require(unnamed_button->objectName() == "timelineUnnamedButton",
                "The Fusion selector must keep a stable object name.");
        require(unnamed_button == buttons.fusion,
                "The blank button must be the Fusion selector.");
        require(unnamed_button->text().isEmpty(),
                "The second timeline end button must have no visible label.");
        require(unnamed_button->icon().isNull(),
                "The second timeline end button must have no icon.");
        require(unnamed_button->accessibleName() == "Fusion",
                "The blank button must be accessible as Fusion.");
        require(!unnamed_button->toolTip().isEmpty(),
                "The blank button must explain that it opens Fusion.");
        require(unnamed_button->size() == QSize(32, 28),
                "The blank timeline end button must be 32 by 28 pixels.");
        require(unnamed_button->isCheckable() &&
                    !unnamed_button->isChecked(),
                "The Fusion selector must start inactive.");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
