#include "main_window/main_window.h"

#include <QRect>
#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
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

void waitForWidthPersistence() {
    QEventLoop loop;
    QTimer::singleShot(180, &loop, &QEventLoop::quit);
    loop.exec();
    settleLayout();
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

        const int original_normal_width = docks.media->width();
        window.resizeDocks(
            {docks.media, docks.preview},
            {original_normal_width + 100,
             std::max(280, docks.preview->width() - 100)},
            Qt::Horizontal);
        settleLayout();
        const int normal_width_before_expand = docks.media->width();
        require(normal_width_before_expand > original_normal_width,
                "The normal Media Pool dock could not be widened before expansion.");
        QSettings().setValue(
            "workspace/media_pool_expanded_width", original_normal_width);
        const auto preview_geometry = docks.preview->geometry();
        const auto inspector_geometry = docks.inspector->geometry();

        docks.expand->click();
        settleLayout();
        require(docks.expand->isChecked(),
                "The expansion button did not enter its checked state.");
        requireExpandedGeometry(docks);
        requirePreviewAndInspectorUnchanged(
            docks, preview_geometry, inspector_geometry);
        require(docks.media->width() == normal_width_before_expand,
                "Expansion reapplied a stale expanded width instead of the current dock width.");
        require(QSettings().value("workspace/media_pool_width").toInt() ==
                    normal_width_before_expand,
                "Expansion did not update the shared Media Pool width.");

        const int initial_expanded_width = docks.media->width();
        window.resizeDocks(
            {docks.media, docks.preview},
            {initial_expanded_width + 120,
             std::max(280, docks.preview->width() - 120)},
            Qt::Horizontal);
        settleLayout();
        const int widened_expanded_width = docks.media->width();
        require(widened_expanded_width > initial_expanded_width,
                "The expanded Media Pool column could not be widened.");
        const int target_expanded_width = widened_expanded_width - 40;
        window.resizeDocks(
            {docks.media, docks.preview},
            {target_expanded_width,
             docks.preview->width() +
                 (widened_expanded_width - target_expanded_width)},
            Qt::Horizontal);
        settleLayout();
        int resized_expanded_width = docks.media->width();
        require(resized_expanded_width < widened_expanded_width,
                "The expanded Media Pool column could not be narrowed.");

        docks.expand->click();
        settleLayout();
        require(!docks.expand->isChecked(),
                "The expansion button did not return to its unchecked state.");
        requireAllPanelsVisible(docks);
        require(docks.timeline->x() < docks.media->x() + docks.media->width(),
                "Restoring the normal layout did not return the Timeline below the side docks.");
        waitForWidthPersistence();
        require(QSettings().value(
                    "workspace/media_pool_width").toInt() ==
                    resized_expanded_width,
                "Collapsing the Media Pool did not preserve its latest width.");
        require(docks.media->width() == resized_expanded_width,
                "Collapsing the Media Pool restored an older width.");

        docks.expand->click();
        settleLayout();
        requireExpandedGeometry(docks);
        require(docks.media->width() == resized_expanded_width,
                "Re-expanding did not restore the previous expanded width.");

        docks.expand->click();
        settleLayout();
        require(!docks.expand->isChecked(),
                "The Media Pool did not collapse after restoring its expanded width.");
        const int changed_normal_width_target = normal_width_before_expand + 60;
        window.resizeDocks(
            {docks.media, docks.preview},
            {changed_normal_width_target,
             std::max(280, docks.preview->width() - 60)},
            Qt::Horizontal);
        settleLayout();
        const int changed_normal_width = docks.media->width();
        require(changed_normal_width != resized_expanded_width,
                "The normal Media Pool width did not change after collapse.");
        waitForWidthPersistence();
        require(QSettings().value("workspace/media_pool_width").toInt() ==
                    changed_normal_width,
                "A normal-layout resize did not update the shared Media Pool width.");
        const auto changed_preview_geometry = docks.preview->geometry();
        const auto changed_inspector_geometry = docks.inspector->geometry();

        docks.expand->click();
        settleLayout();
        requireExpandedGeometry(docks);
        requirePreviewAndInspectorUnchanged(
            docks, changed_preview_geometry, changed_inspector_geometry);
        require(docks.media->width() == changed_normal_width,
                "Expansion did not preserve the latest normal-layout width.");

        const int next_target_expanded_width = changed_normal_width + 70;
        window.resizeDocks(
            {docks.media, docks.preview},
            {next_target_expanded_width,
             std::max(280, docks.preview->width() - 70)},
            Qt::Horizontal);
        settleLayout();
        resized_expanded_width = docks.media->width();
        require(resized_expanded_width > next_target_expanded_width - 10,
                "The expanded Media Pool could not accept a later width adjustment.");
        waitForWidthPersistence();
        require(QSettings().value(
                    "workspace/media_pool_width").toInt() ==
                    resized_expanded_width,
                "A later expanded-width adjustment did not update the shared width after the dock settled.");
        require(window.close(), "The expanded editor did not close cleanly.");

        MainWindow reopened;
        reopened.setDockOptions(reopened.dockOptions() & ~QMainWindow::AnimatedDocks);
        reopened.resize(1600, 900);
        reopened.showNormal();
        settleLayout();
        const auto reopened_docks = findDocks(reopened);
        require(reopened_docks.expand != nullptr &&
                    reopened_docks.expand->isChecked(),
                "Reopening the editor did not restore the expanded Media Pool state.");
        requireExpandedGeometry(reopened_docks);
        require(reopened_docks.media->width() == resized_expanded_width,
                "Reopening the editor did not restore the adjusted expanded width.");
        reopened_docks.expand->click();
        settleLayout();
        require(!reopened_docks.expand->isChecked(),
                "The reopened Media Pool did not collapse to its normal layout.");
        require(reopened_docks.media->width() == resized_expanded_width,
                "Collapsing after restart did not retain the latest Media Pool width.");
        reopened_docks.expand->click();
        settleLayout();
        require(reopened_docks.media->width() == resized_expanded_width,
                "Re-expanding after restart did not restore the saved expanded width.");
        reopened.resize(960, 640);
        settleLayout();
        requireExpandedGeometry(reopened_docks);
        reopened_docks.expand->click();
        settleLayout();
        reopened_docks.expand->click();
        settleLayout();
        requireExpandedGeometry(reopened_docks);
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
