#include "main_window/main_window.h"
#include "settings/user_preferences.h"
#include "timeline/timeline_widget.h"
#include "ui/workspace/workspace_host.h"

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void settleLayout() {
    for (int index = 0; index < 10; ++index) {
        QApplication::processEvents(QEventLoop::AllEvents);
    }
}

void dragDockTop(QMainWindow& window, QDockWidget& dock, int target_height) {
    const QPoint start(dock.geometry().center().x(), dock.geometry().top() - 3);
    const auto end = start + QPoint(0, dock.height() - target_height);
    const auto send = [&window](QEvent::Type type, QPoint point,
                                Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, window.mapToGlobal(point),
                          button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&window, &event);
    };
    send(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
    send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    settleLayout();
    send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    settleLayout();
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("CreativeSuiteTests");
    QCoreApplication::setApplicationName("TimelineDockResizeTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_directory.path());
    settings::setWorkspacePageTransitionsEnabled(false);

    try {
        MainWindow window;
        window.setDockOptions(window.dockOptions() & ~QMainWindow::AnimatedDocks);
        window.showNormal();
        window.resize(1400, 900);
        settleLayout();
        auto* dock = window.findChild<QDockWidget*>("timelineDock");
        auto* host = dynamic_cast<ui::WorkspaceHost*>(
            window.findChild<QWidget*>("workspaceHost"));
        auto* timeline = window.findChild<timeline::TimelineWidget*>();
        require(dock != nullptr && host != nullptr && timeline != nullptr,
                "The real editor did not mount its Timeline dock.");
        auto* scroll = host->timelinePanel()->findChild<QScrollArea*>();
        require(scroll != nullptr, "The Timeline viewport is missing.");
        window.resizeDocks({dock}, {460}, Qt::Vertical);
        settleLayout();
        std::cout << "Expanded dock=" << dock->height() << '\n';
        dragDockTop(window, *dock, 240);
        std::cout << "Compact dock=" << dock->height()
                  << " viewport=" << scroll->viewport()->height()
                  << " timeline=" << timeline->height() << '\n';
        require(dock->height() <= 270,
                "The entire Timeline dock cannot shrink to a compact height.");
        require(scroll->viewport()->height() >= timeline->minimumHeight(),
                "Shrinking the dock clipped the Timeline below its minimum viewport.");
        require(host->width() <= 1,
                "Side docks left an empty central column in the editing workspace.");

        const auto original_video_height = timeline->trackRowHeight(timeline::TrackKind::Video);
        const auto original_audio_height = timeline->trackRowHeight(timeline::TrackKind::Audio);
        const auto minimum_timeline_height = timeline->minimumHeight();
        timeline->setTrackRowHeights(
            timeline::kMaximumTrackRowHeight, timeline::kMaximumTrackRowHeight);
        window.resizeDocks({dock}, {460}, Qt::Vertical);
        settleLayout();
        dragDockTop(window, *dock, 1);
        require(dock->height() <= 270 &&
                    timeline->minimumHeight() == minimum_timeline_height &&
                    scroll->viewport()->height() >= minimum_timeline_height,
                "Large row preferences prevented compact dock resizing or clipped its viewports.");
        for (auto kind : {timeline::TrackKind::Video, timeline::TrackKind::Audio}) {
            require(timeline->trackRowHeight(kind) == timeline::kMaximumTrackRowHeight &&
                        timeline->trackGroupViewportRect(kind).height() >=
                            timeline::kMinimumTrackRowHeight &&
                        timeline->trackScrollMaximum(kind) > 0,
                    "Compact resizing changed row heights or removed a group's scrollable viewport.");
            timeline->setTrackScrollOffset(kind, timeline->trackScrollMaximum(kind));
            require(timeline->trackScrollOffset(kind) == timeline->trackScrollMaximum(kind),
                    "A compact track group cannot scroll to the end of a tall row.");
        }
        timeline->setTrackRowHeights(original_video_height, original_audio_height);
        settleLayout();
        const auto original_zoom = timeline->zoomFactor();
        timeline->setZoomFactor(4.0);
        settleLayout();
        dragDockTop(window, *dock, 1);
        require(scroll->horizontalScrollBar()->isVisible() &&
                    scroll->viewport()->height() >= minimum_timeline_height,
                "The horizontal scrollbar clipped the compact Timeline viewports.");
        timeline->setZoomFactor(original_zoom);
        settleLayout();
        const auto saved_layout = window.saveState(9);
        std::vector<QDockWidget*> visible_docks;
        for (auto* other : window.findChildren<QDockWidget*>()) {
            if (other != dock && !other->isHidden()) {
                visible_docks.push_back(other);
                other->hide();
            }
        }
        settleLayout();
        require(host->maximumWidth() == QWIDGETSIZE_MAX && host->width() > 0,
                "A lone Timeline dock did not retain a central resize area.");
        window.resizeDocks({dock}, {460}, Qt::Vertical);
        settleLayout();
        dragDockTop(window, *dock, 240);
        require(dock->height() <= 270,
                "The lone Timeline dock cannot be shortened using its separator.");
        auto* side_dock = window.findChild<QDockWidget*>("binsDock");
        require(side_dock != nullptr, "The resize test cannot find the Bins dock.");
        side_dock->show();
        settleLayout();
        require(host->width() <= 1, "Showing a side dock left an empty central column.");
        side_dock->setFloating(true);
        settleLayout();
        require(host->maximumWidth() == QWIDGETSIZE_MAX,
                "Floating the last side dock did not expand the central resize area.");
        side_dock->setFloating(false);
        settleLayout();
        require(host->width() <= 1, "Redocking a side panel left an empty central column.");
        window.addDockWidget(Qt::TopDockWidgetArea, side_dock);
        settleLayout();
        require(host->maximumWidth() == QWIDGETSIZE_MAX,
                "Moving the last side dock to the top left the resize area collapsed.");
        window.addDockWidget(Qt::LeftDockWidgetArea, side_dock);
        settleLayout();
        require(host->width() <= 1, "Moving a panel back to a side left an empty column.");
        for (auto* other : visible_docks) other->show();
        require(window.restoreState(saved_layout, 9), "The compact dock layout did not restore.");
        host->refreshCentralWorkspaceVisibility();
        settleLayout();

        for (const char* name : {"timelineUnnamedButton", "timelineRenderButton", "timelineEditButton"}) {
            auto* button = window.findChild<QPushButton*>(name);
            require(button != nullptr, "A workspace navigation button is missing.");
            button->click();
            settleLayout();
            if (host->currentPage() == ui::WorkspacePageId::Render) {
                require(host->maximumWidth() == QWIDGETSIZE_MAX && host->width() > 300,
                        "Entering Render retained the collapsed editing-center width.");
            }
        }
        require(host->currentPage() == ui::WorkspacePageId::Edit && host->width() <= 1,
                "Returning to Edit did not restore the compact central layout.");
        window.resizeDocks({dock}, {460}, Qt::Vertical);
        settleLayout();
        dragDockTop(window, *dock, 1);
        require(dock->height() <= 270 &&
                    scroll->viewport()->height() >= minimum_timeline_height,
                "Workspace switching lost compact Timeline resizing.");
        std::cout << "Minimum dock=" << dock->height()
                  << " viewport=" << scroll->viewport()->height() << '\n';
        if (argc == 2) {
            require(window.grab().save(QString::fromLocal8Bit(argv[1])),
                    "The native compact-dock validation snapshot could not be saved.");
        }
        require(window.close(), "The compact editor did not close cleanly.");
        MainWindow reopened;
        reopened.showNormal();
        settleLayout();
        auto* restored_dock = reopened.findChild<QDockWidget*>("timelineDock");
        require(restored_dock != nullptr && restored_dock->height() <= 270,
                "Reopening the editor did not preserve the compact Timeline layout.");
        require(reopened.close(), "The reopened compact editor did not close cleanly.");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
