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
        auto* buttons_container = ui::createTimelineEndButtons(&parent);
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
        require(edit_button->height() == 28,
                "The Edit button must be 28 pixels tall.");
        require(edit_button->isEnabled(),
                "The Edit button must remain active as a placeholder.");
        require(unnamed_button->objectName() == "timelineUnnamedButton",
                "The second timeline end button must have a stable object name.");
        require(unnamed_button->text().isEmpty(),
                "The second timeline end button must have no visible label.");
        require(unnamed_button->icon().isNull(),
                "The second timeline end button must have no icon.");
        require(unnamed_button->accessibleName() ==
                    "Unassigned timeline action",
                "The blank button must have an accessible name.");
        require(unnamed_button->size() == QSize(32, 28),
                "The blank timeline end button must be 32 by 28 pixels.");
        require(unnamed_button->isEnabled(),
                "The blank timeline end button must remain active.");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
