#include "ui/workspace/workspace_page_transition.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMenuBar>
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
            },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
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
            },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
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
            [&page]() { page = ui::WorkspacePageId::Render; },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
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
            [&same_page_applied]() { same_page_applied = true; },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
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
            [&canceled_before_switch]() { canceled_before_switch = true; },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
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
            [&canceled_after_switch]() { canceled_after_switch = true; },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
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
            },
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
        require(!window.isMaximized(),
                "A maximized window must be restored before it moves.");
        require(waitUntil(
                    [&transition]() { return !transition.isRunning(); },
                    1000),
                "The maximized window transition did not finish.");
        require(switched_while_restored && window.isMaximized() &&
                    window.geometry() == maximized_geometry,
                "A maximized window must return to its original maximized state and geometry.");

        QMainWindow content_window;
        content_window.menuBar()->addMenu("File");
        content_window.menuBar()->addMenu("Edit");
        content_window.menuBar()->addMenu("View");
        content_window.menuBar()->addMenu("Settings");
        content_window.menuBar()->addMenu("Help");
        auto* content = new QWidget(&content_window);
        content->setStyleSheet("background-color: #263746;");
        content_window.setCentralWidget(content);
        content_window.resize(500, 320);
        content_window.move(160, 120);
        content_window.show();
        QApplication::processEvents();
        const auto content_window_position = content_window.pos();
        const auto menu_position = content_window.menuBar()->mapToGlobal(
            QPoint(0, 0));
        const auto menu_geometry = content_window.menuBar()->geometry();
        auto content_page = ui::WorkspacePageId::Edit;
        ui::WorkspacePageTransition content_transition;
        content_transition.start(
            &content_window,
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Fusion,
            120,
            [&content_page]() { content_page = ui::WorkspacePageId::Fusion; },
            settings::WorkspacePageTransitionStyle::WorkspaceContent);
        auto* overlay = content_window.findChild<QWidget*>(
            "workspacePageTransitionOverlay");
        require(overlay != nullptr && overlay->isVisible() &&
                    content_window.pos() == content_window_position &&
                    content_window.menuBar()->mapToGlobal(QPoint(0, 0)) ==
                        menu_position &&
                    overlay->geometry().top() == menu_geometry.bottom() + 1 &&
                    overlay->geometry().left() == 0 &&
                    overlay->property("slideOffset").toInt() == 0 &&
                    overlay->size() == QSize(
                        content_window.width(),
                        content_window.height() - menu_geometry.bottom() - 1),
                "Workspace content mode must cover everything below the fixed menu bar.");
        require(waitUntil(
                    [&content_transition]() {
                        return content_transition.pageWasApplied();
                    },
                    1000),
                "Workspace content transition did not apply the destination at its midpoint.");
        require(content_page == ui::WorkspacePageId::Fusion &&
                    content_window.pos() == content_window_position &&
                    content_window.menuBar()->mapToGlobal(QPoint(0, 0)) ==
                        menu_position &&
                    overlay->geometry().left() == 0 &&
                    overlay->property("slideOffset").toInt() > 0,
                "The fixed window and menus must remain still while the destination block enters from the right.");
        require(waitUntil(
                    [&content_transition]() {
                        return !content_transition.isRunning();
                    },
                    1000) &&
                    content_window.findChild<QWidget*>(
                        "workspacePageTransitionOverlay") == nullptr &&
                    content_window.pos() == content_window_position,
                "Workspace content transition did not clean up its moving surface.");

        content_transition.start(
            &content_window,
            ui::WorkspacePageId::Render,
            ui::WorkspacePageId::Edit,
            120,
            [&content_page]() { content_page = ui::WorkspacePageId::Edit; },
            settings::WorkspacePageTransitionStyle::WorkspaceContent);
        overlay = content_window.findChild<QWidget*>(
            "workspacePageTransitionOverlay");
        require(waitUntil(
                    [&content_transition]() {
                        return content_transition.pageWasApplied();
                    },
                    1000) &&
                    content_page == ui::WorkspacePageId::Edit &&
                    overlay != nullptr &&
                    overlay->property("slideOffset").toInt() < 0 &&
                    content_window.pos() == content_window_position,
                "The reverse workspace block must enter from the left without moving the window.");
        content_transition.cancel();
        require(content_window.pos() == content_window_position &&
                    content_window.findChild<QWidget*>(
                        "workspacePageTransitionOverlay") == nullptr,
                "Canceling a content transition must remove its surface and keep window geometry.");

        content_transition.start(
            &content_window,
            ui::WorkspacePageId::Edit,
            ui::WorkspacePageId::Fusion,
            600,
            [&content_page]() { content_page = ui::WorkspacePageId::Fusion; },
            settings::WorkspacePageTransitionStyle::WorkspaceContent);
        require(content_window.findChild<QWidget*>(
                    "workspacePageTransitionOverlay") != nullptr,
                "A cancelable content transition did not create its surface.");
        content_transition.cancel();
        require(content_page == ui::WorkspacePageId::Edit &&
                    content_window.pos() == content_window_position &&
                    content_window.menuBar()->mapToGlobal(QPoint(0, 0)) ==
                        menu_position &&
                    content_window.findChild<QWidget*>(
                        "workspacePageTransitionOverlay") == nullptr,
                "Canceling before the content switch must keep the current page and clean up the surface.");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
