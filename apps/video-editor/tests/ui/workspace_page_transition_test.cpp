#include "ui/workspace/workspace_page_transition.h"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QImage>
#include <QPalette>
#include <QThread>
#include <QWidget>

#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void setColor(QWidget* widget, const QColor& color) {
    auto palette = widget->palette();
    palette.setColor(QPalette::Window, color);
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
}

bool waitUntil(const std::function<bool()>& predicate, int timeout_ms) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout_ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

bool hasOverlay(QWidget* widget) {
    return widget->findChild<QWidget*>(
        QStringLiteral("workspacePageTransitionOverlay")) != nullptr;
}

QColor pixelAt(QWidget* widget, int x, int y) {
    const QImage image = widget->grab().toImage();
    require(!image.isNull(), "A transition surface snapshot is empty.");
    return image.pixelColor(x, y);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    try {
        QWidget root;
        root.setObjectName("workspaceTransitionTestRoot");
        auto* layout = new QHBoxLayout(&root);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        const std::array colors{
            QColor(220, 20, 30),
            QColor(20, 170, 60),
            QColor(30, 60, 220)};
        std::array<QWidget*, 3> surfaces{};
        for (std::size_t index = 0; index < surfaces.size(); ++index) {
            surfaces[index] = new QWidget(&root);
            surfaces[index]->setFixedSize(120, 100);
            setColor(surfaces[index], colors[0]);
            layout->addWidget(surfaces[index]);
        }
        root.show();
        QApplication::processEvents();

        ui::WorkspacePageTransition transition;
        int completed = 0;
        QObject::connect(&transition, &ui::WorkspacePageTransition::finished,
                         [&completed]() { ++completed; });

        transition.start(
            {surfaces[0], surfaces[1], surfaces[2]},
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Fusion,
            500,
            [&surfaces]() {
                for (auto* surface : surfaces) {
                    setColor(surface, QColor(20, 170, 60));
                }
            });
        require(transition.isRunning() && transition.slideDirection() == 1,
                "Moving from Edit to Fusion must slide from the right.");
        require(std::abs(transition.progress()) < 0.001,
                "A workspace slide must begin at its first frame.");
        for (auto* surface : surfaces) {
            require(hasOverlay(surface),
                    "Every workspace surface must animate together.");
            auto* overlay = surface->findChild<QWidget*>(
                QStringLiteral("workspacePageTransitionOverlay"));
            require(overlay->property("slideDirection").toInt() == 1,
                    "Forward navigation must move incoming content from the right.");
        }
        require(pixelAt(surfaces[0], 60, 50).red() > 150,
                "The outgoing page must cover the destination at the start.");
        require(waitUntil(
                    [&transition]() {
                        return transition.progress() >= 0.12 &&
                            transition.progress() <= 0.78;
                    },
                    400),
                "The forward slide did not advance smoothly.");
        for (auto* surface : surfaces) {
            auto* overlay = surface->findChild<QWidget*>(
                QStringLiteral("workspacePageTransitionOverlay"));
            require(overlay != nullptr &&
                        std::abs(overlay->property("transitionProgress").toReal() -
                                 transition.progress()) < 0.001,
                    "Workspace surfaces must use the same animation progress.");
            require(overlay->property("incomingOffset").toInt() > 0 &&
                        overlay->property("outgoingOffset").toInt() < 0,
                    "Forward navigation must move the incoming and outgoing pages in opposite directions.");
        }
        require(pixelAt(surfaces[0], 5, 50).red() > 150,
                "Forward navigation must move the incoming page in from the right.");
        require(waitUntil([&transition]() { return !transition.isRunning(); }, 800),
                "The forward workspace slide did not finish.");
        require(completed == 1 && !hasOverlay(surfaces[0]) &&
                    pixelAt(surfaces[0], 60, 50).green() > 120,
                "The destination page was not revealed after the slide.");

        transition.start(
            {surfaces[0], surfaces[1], surfaces[2]},
            ui::WorkspacePageId::Render,
            ui::WorkspacePageId::Edit,
            500,
            [&surfaces]() {
                for (auto* surface : surfaces) {
                    setColor(surface, QColor(220, 20, 30));
                }
            });
        require(transition.isRunning() && transition.slideDirection() == -1,
                "Moving from Render to Edit must slide from the left.");
        require(waitUntil(
                    [&transition]() {
                        return transition.progress() >= 0.12 &&
                            transition.progress() <= 0.78;
                    },
                    400),
                "The reverse slide did not advance smoothly.");
        auto* reverse_midpoint_overlay = surfaces[0]->findChild<QWidget*>(
            QStringLiteral("workspacePageTransitionOverlay"));
        require(reverse_midpoint_overlay != nullptr &&
                    reverse_midpoint_overlay->property("incomingOffset").toInt() < 0 &&
                    reverse_midpoint_overlay->property("outgoingOffset").toInt() > 0,
                "Backward navigation must move the incoming and outgoing pages in opposite directions.");
        require(pixelAt(surfaces[0], 5, 50).red() > 150,
                "Backward navigation must reveal the destination from the left.");
        require(waitUntil([&transition]() { return !transition.isRunning(); }, 800),
                "The reverse workspace slide did not finish.");
        require(completed == 2 && pixelAt(surfaces[0], 60, 50).red() > 150,
                "The reverse slide did not reveal its destination page.");

        transition.start(
            {surfaces[0]},
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Render,
            100,
            [&surfaces]() { setColor(surfaces[0], QColor(30, 60, 220)); });
        require(transition.slideDirection() == 1,
                "Skipping from Edit to Render must follow the forward direction.");
        require(waitUntil([&transition]() { return !transition.isRunning(); }, 500),
                "The non-adjacent workspace slide did not finish.");

        bool same_page_applied = false;
        transition.start(
            {surfaces[0]},
            ui::WorkspacePageId::Fusion,
            ui::WorkspacePageId::Fusion,
            100,
            [&same_page_applied]() { same_page_applied = true; });
        require(same_page_applied && !transition.isRunning() &&
                    !hasOverlay(surfaces[0]),
                "Selecting the current page must not create a slide.");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
