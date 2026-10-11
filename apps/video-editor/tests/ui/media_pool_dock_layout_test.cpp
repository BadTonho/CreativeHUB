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
#include <cstdlib>
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
    QToolButton* inspector_expand = nullptr;
};

Docks findDocks(QWidget& window) {
    return {
        window.findChild<QDockWidget*>("binsDock"),
        window.findChild<QDockWidget*>("mediaDock"),
        window.findChild<QDockWidget*>("previewDock"),
        window.findChild<QDockWidget*>("inspectorDock"),
        window.findChild<QDockWidget*>("timelineDock"),
        window.findChild<QToolButton*>("mediaPoolExpandButton"),
        window.findChild<QToolButton*>("inspectorExpandButton")};
}

void requireAllPanelsVisible(const Docks& docks) {
    require(docks.bins != nullptr && docks.media != nullptr &&
                docks.preview != nullptr && docks.inspector != nullptr &&
                docks.timeline != nullptr,
            "One or more Media Pool, Preview, Inspector, or Timeline docks are missing.");
    require(docks.bins->isVisible() && docks.media->isVisible() &&
                docks.preview->isVisible() && docks.inspector->isVisible() &&
                docks.timeline->isVisible(),
            "Changing dock expansion must not hide any editor dock.");
}

void requireInspectorExpandedGeometry(const Docks& docks) {
    requireAllPanelsVisible(docks);
    require(docks.inspector->y() <= docks.preview->y() + 2 &&
                docks.inspector->geometry().bottom() >=
                    docks.timeline->geometry().bottom() - 24,
            "The expanded Inspector must span the full dock workspace height.");
    require(docks.timeline->y() > docks.preview->y(),
            "The Timeline must remain below Preview when Inspector is expanded.");
    require(docks.timeline->x() >=
                docks.media->x() + docks.media->width() - 1 &&
                docks.timeline->geometry().right() <= docks.inspector->x() + 2,
            "The Timeline must remain in the central area between Media Pool and Inspector.");
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
    require(docks.timeline->y() > docks.preview->y(),
            "The Timeline must remain below Preview in the expanded layout.");
    if (docks.inspector_expand == nullptr ||
        !docks.inspector_expand->isChecked()) {
        require(docks.timeline->y() > docks.inspector->y(),
                "The Timeline must remain below Inspector unless it is expanded.");
    }
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

void requirePreviewUnchanged(const Docks& docks, const QRect& preview_geometry) {
    const auto actual = docks.preview->geometry();
    require(actual.x() == preview_geometry.x() &&
                actual.y() == preview_geometry.y() &&
                actual.width() == preview_geometry.width() &&
                std::abs(actual.height() - preview_geometry.height()) <= 3,
            "Expanding the Inspector must not move or resize the Preview.");
}

void requireMediaPoolGeometryUnchanged(
    const Docks& docks,
    const QRect& bins_geometry,
    const QRect& media_geometry) {
    require(docks.expand != nullptr && !docks.expand->isChecked(),
            "The Media Pool expansion action changed while only Inspector was toggled.");
    require(docks.bins->geometry() == bins_geometry &&
                docks.media->geometry() == media_geometry,
            "Expanding Inspector changed the Media Pool dock geometry.");
}

void requireMediaPoolNotExpanded(const Docks& docks) {
    require(docks.expand != nullptr && !docks.expand->isChecked(),
            "The Media Pool expansion action changed while only Inspector was toggled.");
    require(docks.media->geometry().bottom() <
                docks.timeline->geometry().bottom() - 24,
            "Inspector-only expansion stretched the Media Pool to full height.");
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
        window.resize(1920, 1080);
        settleLayout();

        auto docks = findDocks(window);
        require(docks.expand != nullptr,
                "The reusable dock title bar is missing the Media Pool expansion button.");
        require(docks.expand->isCheckable(),
                "The Media Pool expansion action must expose its toggle state.");
        require(docks.inspector_expand != nullptr &&
                    docks.inspector_expand->isCheckable(),
                "The Inspector title bar is missing its expansion toggle.");
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
        const auto normal_bins_geometry = docks.bins->geometry();
        const auto normal_media_geometry = docks.media->geometry();
        QSettings().setValue(
            "workspace/media_pool_expanded_width", original_normal_width);
        const auto preview_geometry = docks.preview->geometry();
        const auto inspector_geometry = docks.inspector->geometry();

        docks.inspector_expand->click();
        settleLayout();
        require(!docks.expand->isChecked() &&
                    docks.inspector_expand->isChecked(),
                "Expanding Inspector alone changed the Media Pool toggle state.");
        requireInspectorExpandedGeometry(docks);
        requireMediaPoolGeometryUnchanged(
            docks, normal_bins_geometry, normal_media_geometry);
        requirePreviewUnchanged(docks, preview_geometry);
        docks.inspector_expand->click();
        settleLayout();
        require(!docks.expand->isChecked() &&
                    !docks.inspector_expand->isChecked(),
                "Collapsing Inspector alone changed the Media Pool toggle state.");

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

        const auto preview_before_inspector_expand = docks.preview->geometry();
        docks.inspector_expand->click();
        settleLayout();
        require(docks.expand->isChecked() && docks.inspector_expand->isChecked(),
                "Expanding the Inspector must keep the Media Pool expanded.");
        requireExpandedGeometry(docks);
        requireInspectorExpandedGeometry(docks);
        requirePreviewUnchanged(docks, preview_before_inspector_expand);
        waitForWidthPersistence();
        require(docks.media->width() == resized_expanded_width,
                "Expanding Inspector changed the Media Pool's saved width.");

        const int inspector_width_before_resize = docks.inspector->width();
        window.resizeDocks(
            {docks.inspector}, {inspector_width_before_resize + 80},
            Qt::Horizontal);
        settleLayout();
        const int resized_inspector_width = docks.inspector->width();
        const int latest_media_pool_width = docks.media->width();
        require(resized_inspector_width > inspector_width_before_resize,
                "The expanded Inspector could not be resized.");
        waitForWidthPersistence();
        require(QSettings().value("workspace/inspector_width").toInt() ==
                    resized_inspector_width,
                "A resized Inspector width was not persisted independently.");
        require(QSettings().value("workspace/media_pool_width").toInt() ==
                    latest_media_pool_width,
                "The Media Pool's latest width was not persisted independently.");
        require(window.close(), "The expanded editor did not close cleanly.");

        MainWindow reopened;
        reopened.setDockOptions(reopened.dockOptions() & ~QMainWindow::AnimatedDocks);
        reopened.resize(1920, 1080);
        reopened.showNormal();
        settleLayout();
        const auto reopened_docks = findDocks(reopened);
        require(reopened_docks.expand != nullptr &&
                    reopened_docks.expand->isChecked(),
                "Reopening the editor did not restore the expanded Media Pool state.");
        require(reopened_docks.inspector_expand != nullptr &&
                    reopened_docks.inspector_expand->isChecked(),
                "Reopening the editor did not restore the expanded Inspector state.");
        requireExpandedGeometry(reopened_docks);
        requireInspectorExpandedGeometry(reopened_docks);
        require(reopened_docks.media->width() == latest_media_pool_width,
                "Reopening the editor did not restore the adjusted expanded width.");
        require(reopened_docks.inspector->width() == resized_inspector_width,
                "Reopening did not restore the Inspector's independent width.");

        reopened_docks.inspector_expand->click();
        settleLayout();
        require(reopened_docks.expand->isChecked() &&
                    !reopened_docks.inspector_expand->isChecked(),
                "Collapsing Inspector must preserve the expanded Media Pool.");
        requireExpandedGeometry(reopened_docks);
        reopened_docks.inspector_expand->click();
        settleLayout();
        require(reopened_docks.expand->isChecked() &&
                    reopened_docks.inspector_expand->isChecked(),
                "The combined expanded layout was not restored.");
        reopened_docks.expand->click();
        settleLayout();
        require(!reopened_docks.expand->isChecked() &&
                    reopened_docks.inspector_expand->isChecked(),
                "Collapsing Media Pool must preserve the expanded Inspector.");
        requireInspectorExpandedGeometry(reopened_docks);
        reopened_docks.expand->click();
        settleLayout();
        require(reopened_docks.expand->isChecked() &&
                    reopened_docks.inspector_expand->isChecked(),
                "Re-expanding Media Pool must preserve the expanded Inspector.");
        reopened_docks.inspector_expand->click();
        settleLayout();
        require(reopened_docks.expand->isChecked() &&
                    !reopened_docks.inspector_expand->isChecked(),
                "Collapsing Inspector must leave Media Pool expanded.");
        reopened_docks.expand->click();
        settleLayout();
        require(!reopened_docks.expand->isChecked() &&
                    !reopened_docks.inspector_expand->isChecked(),
                "Both docks should return to the normal layout independently.");

        reopened.resizeDocks(
            {reopened_docks.inspector},
            {reopened_docks.inspector->width() + 40}, Qt::Horizontal);
        settleLayout();
        const int normal_mode_inspector_width =
            reopened_docks.inspector->width();
        waitForWidthPersistence();
        require(QSettings().value("workspace/inspector_width").toInt() ==
                    normal_mode_inspector_width,
                "A normal-layout Inspector resize was not persisted.");

        const auto normal_preview_geometry = reopened_docks.preview->geometry();
        reopened_docks.inspector_expand->click();
        settleLayout();
        requireInspectorExpandedGeometry(reopened_docks);
        requirePreviewUnchanged(reopened_docks, normal_preview_geometry);
        waitForWidthPersistence();
        require(std::abs(reopened_docks.inspector->width() -
                         normal_mode_inspector_width) <= 20,
                "Inspector expansion changed its width beyond the available dock-area adjustment.");
        require(QSettings().value("workspace/inspector_width").toInt() ==
                    reopened_docks.inspector->width(),
                "Inspector expansion did not persist its effective width.");
        reopened.resizeDocks(
            {reopened_docks.inspector},
            {reopened_docks.inspector->width() + 60}, Qt::Horizontal);
        settleLayout();
        const int latest_inspector_width = reopened_docks.inspector->width();
        waitForWidthPersistence();
        require(QSettings().value("workspace/inspector_width").toInt() ==
                    latest_inspector_width,
                "A later Inspector resize did not replace its saved width.");
        reopened_docks.inspector_expand->click();
        settleLayout();
        require(!reopened_docks.inspector_expand->isChecked() &&
                    reopened_docks.inspector->width() == latest_inspector_width,
                "Collapsing Inspector did not retain its latest width.");
        reopened_docks.inspector_expand->click();
        settleLayout();
        require(reopened_docks.inspector->width() == latest_inspector_width,
                "Re-expanding Inspector did not restore its latest width.");

        reopened.resize(960, 640);
        settleLayout();
        requireInspectorExpandedGeometry(reopened_docks);
        requireMediaPoolNotExpanded(reopened_docks);
        reopened.resize(1920, 1080);
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
