#include "ui/workspace/workspace_page_transition.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMainWindow>
#include <QScreen>
#include <QThread>

#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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

QRect screenGeometryFor(const QWidget& window) {
    auto* screen = QGuiApplication::screenAt(
        window.frameGeometry().center());
    if (screen == nullptr) screen = window.screen();
    if (screen == nullptr) screen = QGuiApplication::primaryScreen();
    return screen != nullptr ? screen->geometry() : QRect{};
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    try {
        QMainWindow window;
        window.resize(420, 260);
        window.move(140, 110);
        window.show();
        QApplication::processEvents();
        if (window.isMaximized()) window.showNormal();
        const QRect normal_geometry = window.geometry();
        const QPoint normal_position = window.pos();
        const QRect screen_geometry = screenGeometryFor(window);
        require(!screen_geometry.isEmpty(),
                "The test window has no screen geometry.");

        ui::WorkspacePageTransition transition;
        int completed = 0;
        QObject::connect(&transition, &ui::WorkspacePageTransition::finished,
                         [&completed]() { ++completed; });

        auto page = ui::WorkspacePageId::Edit;
        QRect forward_exit_frame;
        transition.start(
            &window,
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Fusion,
            300,
            [&]() {
                require(page == ui::WorkspacePageId::Edit,
                        "The outgoing page changed before the window left the screen.");
                forward_exit_frame = window.frameGeometry();
                page = ui::WorkspacePageId::Fusion;
            });
        require(transition.isRunning() && transition.slideDirection() == 1 &&
                    !transition.pageWasApplied() &&
                    page == ui::WorkspacePageId::Edit &&
                    window.pos() == normal_position,
                "Forward navigation must begin by moving the current window left.");
        require(waitUntil(
                    [&transition]() { return transition.pageWasApplied(); },
                    1000),
                "The forward page was not applied after the window exited.");
        require(page == ui::WorkspacePageId::Fusion &&
                    forward_exit_frame.right() < screen_geometry.left() &&
                    window.frameGeometry().left() > screen_geometry.right() &&
                    transition.progress() >= 0.5,
                "Forward navigation must switch pages off-screen and enter from the right.");
        require(waitUntil(
                    [&transition]() {
                        return transition.progress() >= 0.7 &&
                            transition.isRunning();
                    },
                    500),
                "The incoming window did not advance from the right.");
        require(window.pos().x() > normal_position.x(),
                "The whole application window must move during page entry.");
        require(waitUntil(
                    [&transition]() { return !transition.isRunning(); },
                    1000),
                "The forward window transition did not finish.");
        require(completed == 1 && window.geometry() == normal_geometry &&
                    window.pos() == normal_position,
                "Forward navigation must restore the original window geometry.");

        QRect reverse_exit_frame;
        transition.start(
            &window,
            ui::WorkspacePageId::Render,
            ui::WorkspacePageId::Edit,
            300,
            [&]() {
                require(page == ui::WorkspacePageId::Fusion,
                        "The reverse page changed before the window left the screen.");
                reverse_exit_frame = window.frameGeometry();
                page = ui::WorkspacePageId::Edit;
            });
        require(transition.isRunning() && transition.slideDirection() == -1,
                "Backward navigation must move the current window right.");
        require(waitUntil(
                    [&transition]() { return transition.pageWasApplied(); },
                    1000),
                "The reverse page was not applied after the window exited.");
        require(reverse_exit_frame.left() > screen_geometry.right() &&
                    window.frameGeometry().right() <= screen_geometry.left(),
                "Backward navigation must switch pages at the right edge and enter from the left.");
        require(waitUntil(
                    [&transition]() { return !transition.isRunning(); },
                    1000),
                "The reverse window transition did not finish.");
        require(completed == 2 && window.pos() == normal_position &&
                    window.geometry() == normal_geometry,
                "Backward navigation must restore the original window geometry.");

        transition.start(
            &window,
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Render,
            100,
            [&page]() { page = ui::WorkspacePageId::Render; });
        require(transition.slideDirection() == 1,
                "A non-adjacent forward page change must use the forward direction.");
        require(waitUntil(
                    [&transition]() { return !transition.isRunning(); },
                    1000) &&
                    window.pos() == normal_position,
                "A non-adjacent page transition did not restore the window.");

        const auto completed_before_same_page = completed;
        bool same_page_applied = false;
        transition.start(
            &window,
            ui::WorkspacePageId::Render,
            ui::WorkspacePageId::Render,
            100,
            [&same_page_applied]() { same_page_applied = true; });
        require(same_page_applied && !transition.isRunning() &&
                    completed == completed_before_same_page &&
                    window.pos() == normal_position,
                "Selecting the current page must not move the application window.");

        bool canceled_before_switch = false;
        transition.start(
            &window,
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Fusion,
            600,
            [&canceled_before_switch]() { canceled_before_switch = true; });
        transition.cancel();
        require(!transition.isRunning() && !canceled_before_switch &&
                    window.geometry() == normal_geometry &&
                    window.pos() == normal_position,
                "Canceling before the page switch must restore the window without applying the page.");

        bool canceled_after_switch = false;
        transition.start(
            &window,
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Fusion,
            600,
            [&canceled_after_switch]() { canceled_after_switch = true; });
        require(waitUntil(
                    [&transition]() { return transition.pageWasApplied(); },
                    1000),
                "The cancel-after-switch scenario did not reach its page change.");
        transition.cancel();
        require(canceled_after_switch && !transition.isRunning() &&
                    window.geometry() == normal_geometry &&
                    window.pos() == normal_position,
                "Canceling after the page switch must restore the window geometry.");

        window.showMaximized();
        QApplication::processEvents();
        require(window.isMaximized(),
                "The test platform did not enter the maximized state.");
        const QRect maximized_geometry = window.geometry();
        bool switched_while_restored = false;
        transition.start(
            &window,
            ui::WorkspacePageId::Fusion,
            ui::WorkspacePageId::Render,
            100,
            [&]() {
                switched_while_restored = !window.isMaximized();
            });
        require(!window.isMaximized(),
                "A maximized window must be restored before it moves.");
        require(waitUntil(
                    [&transition]() { return !transition.isRunning(); },
                    1000),
                "The maximized window transition did not finish.");
        require(switched_while_restored && window.isMaximized() &&
                    window.geometry() == maximized_geometry,
                "A maximized window must return to its original maximized state and geometry.");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
