#include "main_window/main_window.h"

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QRect>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void settleLayout() {
    for (int index = 0; index < 10; ++index) {
        QApplication::processEvents(QEventLoop::AllEvents);
    }
}

struct Docks {
    QDockWidget* bins = nullptr;
    QDockWidget* media = nullptr;
    QDockWidget* preview = nullptr;
    QDockWidget* inspector = nullptr;
    QDockWidget* timeline = nullptr;
    QToolButton* expand = nullptr;
};

Docks findDocks(QWidget& window) {
    return {
        window.findChild<QDockWidget*>("binsDock"),
        window.findChild<QDockWidget*>("mediaDock"),
        window.findChild<QDockWidget*>("previewDock"),
        window.findChild<QDockWidget*>("inspectorDock"),
        window.findChild<QDockWidget*>("timelineDock"),
        window.findChild<QToolButton*>("mediaPoolExpandButton")};
}

void requireAllPanelsVisible(const Docks& docks) {
    require(docks.bins != nullptr && docks.media != nullptr &&
                docks.preview != nullptr && docks.inspector != nullptr &&
                docks.timeline != nullptr,
            "One or more Media Pool, Preview, Inspector, or Timeline docks are missing.");
    require(docks.bins->isVisible() && docks.media->isVisible() &&
                docks.preview->isVisible() && docks.inspector->isVisible() &&
                docks.timeline->isVisible(),
            "Expanding the Media Pool must not hide any editor dock.");
}

void requireExpandedGeometry(const Docks& docks) {
    requireAllPanelsVisible(docks);
    require(docks.bins->x() == docks.media->x() &&
                docks.bins->y() < docks.media->y(),
            "Bins and Media must remain stacked together in the left column.");
    require(docks.timeline->x() >= docks.media->x() + docks.media->width() - 1,
            "The Timeline must move to the right of the full-height Media Pool.");
    require(docks.media->geometry().bottom() >=
                docks.timeline->geometry().bottom() - 24,
            "The Media Pool column must extend to the bottom of the window.");
    require(docks.preview->x() >= docks.media->x() + docks.media->width() - 1 &&
                docks.inspector->x() >= docks.preview->x(),
            "Preview and Inspector must remain arranged to the right of Media Pool.");
    require(docks.timeline->y() > docks.preview->y() &&
                docks.timeline->y() > docks.inspector->y(),
            "The Timeline must remain below Preview and Inspector in the expanded layout.");
}

void requirePreviewAndInspectorUnchanged(
    const Docks& docks,
    const QRect& preview_geometry,
    const QRect& inspector_geometry) {
    require(docks.preview->geometry() == preview_geometry,
            "Expanding the Media Pool must not move or resize the Preview.");
    require(docks.inspector->geometry() == inspector_geometry,
            "Expanding the Media Pool must not move or resize the Inspector.");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("CreativeSuiteTests");
    QCoreApplication::setApplicationName("MediaPoolDockLayoutTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_directory.path());

    try {
        MainWindow window;
        window.setDockOptions(window.dockOptions() & ~QMainWindow::AnimatedDocks);
        window.showNormal();
        window.resize(1600, 900);
        settleLayout();

        auto docks = findDocks(window);
        require(docks.expand != nullptr,
                "The reusable dock title bar is missing the Media Pool expansion button.");
        require(docks.expand->isCheckable(),
                "The Media Pool expansion action must expose its toggle state.");
        require(docks.bins != nullptr && docks.media != nullptr &&
                    docks.preview != nullptr && docks.inspector != nullptr &&
                    docks.timeline != nullptr,
                "The test could not locate the editor dock set.");
        const auto normal_state = window.saveState(9);
        const auto preview_geometry = docks.preview->geometry();
        const auto inspector_geometry = docks.inspector->geometry();

        docks.expand->click();
        settleLayout();
        require(docks.expand->isChecked(),
                "The expansion button did not enter its checked state.");
        requireExpandedGeometry(docks);
        requirePreviewAndInspectorUnchanged(
            docks, preview_geometry, inspector_geometry);

        docks.expand->click();
        settleLayout();
        require(!docks.expand->isChecked(),
                "The expansion button did not return to its unchecked state.");
        requireAllPanelsVisible(docks);
        require(docks.timeline->x() < docks.media->x() + docks.media->width(),
                "Restoring the normal layout did not return the Timeline below the side docks.");
        require(window.saveState(9) == normal_state,
                "Toggling the Media Pool off did not restore the previous dock layout.");

        docks.expand->click();
        settleLayout();
        requireExpandedGeometry(docks);
        require(window.close(), "The expanded editor did not close cleanly.");

        MainWindow reopened;
        reopened.setDockOptions(reopened.dockOptions() & ~QMainWindow::AnimatedDocks);
        reopened.showNormal();
        settleLayout();
        const auto reopened_docks = findDocks(reopened);
        require(reopened_docks.expand != nullptr &&
                    reopened_docks.expand->isChecked(),
                "Reopening the editor did not restore the expanded Media Pool state.");
        requireExpandedGeometry(reopened_docks);
        reopened.resize(960, 640);
        settleLayout();
        requireExpandedGeometry(reopened_docks);
        reopened_docks.expand->click();
        settleLayout();
        const auto small_preview_geometry = reopened_docks.preview->geometry();
        const auto small_inspector_geometry = reopened_docks.inspector->geometry();
        reopened_docks.expand->click();
        settleLayout();
        requireExpandedGeometry(reopened_docks);
        requirePreviewAndInspectorUnchanged(
            reopened_docks,
            small_preview_geometry,
            small_inspector_geometry);
        reopened.resize(1600, 900);
        settleLayout();

        auto* float_button = reopened_docks.media->findChild<QToolButton*>(
            "dockFloatButton");
        auto* close_button = reopened_docks.media->findChild<QToolButton*>(
            "dockCloseButton");
        require(float_button != nullptr && close_button != nullptr,
                "The custom title bar did not retain the native dock controls.");
        float_button->click();
        settleLayout();
        require(reopened_docks.media->isFloating(),
                "The title-bar float control did not float the Media dock.");
        float_button->click();
        settleLayout();
        require(!reopened_docks.media->isFloating(),
                "The title-bar float control did not re-dock the Media dock.");
        close_button->click();
        settleLayout();
        require(reopened_docks.media->isHidden(),
                "The title-bar close control did not close the Media dock.");
        reopened_docks.media->show();
        settleLayout();
        require(reopened.close(), "The reopened editor did not close cleanly.");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
