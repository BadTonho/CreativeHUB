#include "main_window/main_window.h"
#include "ui/workspace/workspace_host.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
#include <QEventLoop>
#include <QPushButton>
#include <QRect>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <stdexcept>
#include <cstdlib>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void settle() {
    for (int index = 0; index < 12; ++index)
        QApplication::processEvents(QEventLoop::AllEvents);
}

void settleExpansion() {
    QEventLoop loop;
    QTimer::singleShot(190, &loop, &QEventLoop::quit);
    loop.exec();
    settle();
}

QString savedLayoutId() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("workspace/saved_layouts"));
    const auto ids = settings.childKeys();
    settings.endGroup();
    require(ids.size() == 1, "Expected one saved layout.");
    return ids.front();
}

} // namespace

class WorkspaceLayoutGalleryTest {
public:
    static void run() {
        MainWindow window;
        window.setDockOptions(window.dockOptions() & ~QMainWindow::AnimatedDocks);
        window.showNormal();
        window.resize(1440, 900);
        settle();
        const auto main_capture = qEnvironmentVariable(
            "CREATIVE_SUITE_MAIN_LAYOUT_CAPTURE");
        if (!main_capture.isEmpty()) window.grab().save(main_capture);

        auto* toolbar_action = window.findChild<QAction*>("layoutsToolbarAction");
        auto* view_action = window.findChild<QAction*>("layoutsViewAction");
        require(toolbar_action && view_action && toolbar_action->isVisible() &&
                    view_action->isVisible(),
                "Edit must expose the layout gallery in the toolbar and View menu.");
        bool gallery_ok = false;
        QTimer::singleShot(0, [&] {
            auto* gallery = window.findChild<QDialog*>("workspaceLayoutsGallery");
            gallery_ok = gallery && gallery->findChild<QToolButton*>("builtInLayout0") &&
                gallery->findChild<QToolButton*>("builtInLayout1") &&
                gallery->findChild<QToolButton*>("builtInLayout2");
            const auto capture_path = qEnvironmentVariable(
                "CREATIVE_SUITE_LAYOUT_GALLERY_CAPTURE");
            if (gallery && !capture_path.isEmpty())
                gallery->grab().save(capture_path);
            if (gallery) gallery->reject();
        });
        view_action->trigger();
        require(gallery_ok, "The gallery must display all three built-in thumbnails.");

        window.applyBuiltInWorkspaceLayout(1);
        settle();
        const int preview_focus_height = window.preview_dock_->height();
        window.applyBuiltInWorkspaceLayout(2);
        settle();
        require(window.preview_dock_->height() < preview_focus_height &&
                    window.timeline_dock_->isVisible(),
                "Timeline Focus must give the Timeline more space than Preview Focus.");
        window.applyBuiltInWorkspaceLayout(0);
        settle();

        window.tabifyDockWidget(window.bins_dock_, window.media_dock_);
        window.media_dock_->raise();
        window.inspector_dock_->hide();
        window.effects_dock_->setFloating(true);
        window.effects_dock_->show();
        window.effects_dock_->setGeometry(80, 80, 250, 180);
        settle();
        const QRect floating_geometry = window.effects_dock_->frameGeometry();
        require(window.saveNamedWorkspaceLayout(QStringLiteral(" Editing ")),
                "Could not save the custom layout.");
        const auto id = savedLayoutId();
        bool saved_card_ok = false;
        QTimer::singleShot(0, [&] {
            auto* gallery = window.findChild<QDialog*>("workspaceLayoutsGallery");
            saved_card_ok = gallery &&
                gallery->findChild<QToolButton*>(
                    QStringLiteral("savedLayout_%1").arg(id)) &&
                gallery->findChild<QPushButton*>(
                    QStringLiteral("updateLayout_%1").arg(id)) &&
                gallery->findChild<QPushButton*>(
                    QStringLiteral("renameLayout_%1").arg(id)) &&
                gallery->findChild<QPushButton*>(
                    QStringLiteral("deleteLayout_%1").arg(id));
            const auto capture_path = qEnvironmentVariable(
                "CREATIVE_SUITE_SAVED_LAYOUT_GALLERY_CAPTURE");
            if (gallery && !capture_path.isEmpty())
                gallery->grab().save(capture_path);
            if (gallery) gallery->reject();
        });
        toolbar_action->trigger();
        require(saved_card_ok,
                "The gallery must display saved-layout actions and a thumbnail.");
        require(!window.saveNamedWorkspaceLayout(QStringLiteral("editing")),
                "Layout names must be unique without regard to case.");
        auto record = QSettings().value(
            QStringLiteral("workspace/saved_layouts/%1").arg(id)).toMap();
        require(record.value(QStringLiteral("name")).toString() == QStringLiteral("Editing") &&
                    !record.value(QStringLiteral("tiles")).toList().isEmpty(),
                "Saved layouts need a trimmed name and thumbnail geometry.");

        window.restoreDefaultLayout();
        settle();
        require(window.applyNamedWorkspaceLayout(id),
                "The saved dock arrangement could not be restored.");
        settle();
        require(window.inspector_dock_->isHidden() &&
                    window.effects_dock_->isFloating() &&
                    window.tabifiedDockWidgets(window.bins_dock_).contains(window.media_dock_),
                "Hidden, floating, and tabbed docks must round-trip together.");
        require(std::abs(window.effects_dock_->frameGeometry().x() -
                         floating_geometry.x()) <= 20 &&
                    std::abs(window.effects_dock_->frameGeometry().y() -
                             floating_geometry.y()) <= 20,
                "Floating panel position must round-trip with the layout.");

        require(window.renameNamedWorkspaceLayout(id, QStringLiteral("My Panels")),
                "The saved layout could not be renamed.");
        require(window.saveNamedWorkspaceLayout(QStringLiteral("My Panels"), id),
                "The saved layout could not be updated.");
        require(!window.renameNamedWorkspaceLayout(id, QStringLiteral("")),
                "An empty layout name must be rejected.");
        window.restoreDefaultLayout();
        settle();
        require(window.applyNamedWorkspaceLayout(id) && window.inspector_dock_->isHidden(),
                "Updating a saved layout must keep its new dock arrangement.");
        require(!window.project_dirty_,
                "Saving and applying panel layouts must not modify the project.");

        const auto original = QSettings().value(
            QStringLiteral("workspace/saved_layouts/%1").arg(id));
        auto damaged = original.toMap();
        damaged.insert(QStringLiteral("dock_state"), QByteArray("invalid"));
        QSettings().setValue(QStringLiteral("workspace/saved_layouts/%1").arg(id), damaged);
        const bool inspector_hidden_before_failure = window.inspector_dock_->isHidden();
        const auto persisted_state_before_failure = QSettings().value(
            QStringLiteral("workspace/dock_layout_state")).toByteArray();
        require(!window.applyNamedWorkspaceLayout(id) &&
                    window.inspector_dock_->isHidden() == inspector_hidden_before_failure &&
                    window.preview_dock_->isVisible() &&
                    QSettings().value(QStringLiteral("workspace/dock_layout_state"))
                        .toByteArray() == persisted_state_before_failure,
                "Invalid state must be refused without losing the current layout.");
        QSettings().setValue(QStringLiteral("workspace/saved_layouts/%1").arg(id), original);

        window.restoreDefaultLayout();
        window.addDockWidget(Qt::LeftDockWidgetArea, window.preview_dock_);
        window.splitDockWidget(window.preview_dock_, window.bins_dock_,
                               Qt::Horizontal);
        settle();
        require(window.dockWidgetArea(window.preview_dock_) ==
                    Qt::LeftDockWidgetArea &&
                    window.corner(Qt::BottomLeftCorner) ==
                        Qt::LeftDockWidgetArea &&
                    window.saveNamedWorkspaceLayout(QStringLiteral("Side"), id),
                "The outer-edge Preview layout must be savable.");
        window.restoreDefaultLayout();
        settle();
        require(window.applyNamedWorkspaceLayout(id),
                "The outer-edge Preview layout did not apply.");
        settle();
        require(window.dockWidgetArea(window.preview_dock_) ==
                    Qt::LeftDockWidgetArea &&
                    window.corner(Qt::BottomLeftCorner) ==
                        Qt::LeftDockWidgetArea,
                "Applying an outer-edge Preview layout must restore its corner.");

        window.restoreDefaultLayout();
        settle();
        window.media_pool_expand_button_->click();
        settleExpansion();
        require(window.media_pool_expanded_ &&
                    window.saveNamedWorkspaceLayout(QStringLiteral("Expanded"), id),
                "Expanded panel state must be saved.");
        window.restoreDefaultLayout();
        settle();
        require(window.applyNamedWorkspaceLayout(id) && window.media_pool_expanded_ &&
                    window.media_pool_expand_button_->isChecked() &&
                    !window.workspace_expansion_restore_layout_state_.isEmpty(),
                "Expanded panel state and its baseline must be restored.");
        window.media_pool_expand_button_->click();
        settleExpansion();
        require(!window.media_pool_expanded_ &&
                    !window.media_pool_expand_button_->isChecked(),
                "A restored Media Pool layout must collapse normally.");
        window.inspector_expand_button_->click();
        settleExpansion();
        require(window.inspector_expanded_ &&
                    window.saveNamedWorkspaceLayout(QStringLiteral("Inspector"), id),
                "Expanded Inspector state must be saved.");
        window.restoreDefaultLayout();
        settle();
        require(window.applyNamedWorkspaceLayout(id) && window.inspector_expanded_ &&
                    window.inspector_expand_button_->isChecked(),
                "Expanded Inspector state must be restored.");
        window.inspector_expand_button_->click();
        settleExpansion();
        require(!window.inspector_expanded_,
                "A restored Inspector layout must collapse normally.");

        window.workspace_host_->setPage(ui::WorkspacePageId::Fusion);
        window.handleWorkspacePageChanged(ui::WorkspacePageId::Fusion);
        require(toolbar_action->isVisible() &&
                    window.saveNamedWorkspaceLayout(QStringLiteral("Fusion"), id),
                "The same layout controls must be usable in Fusion.");
        window.workspace_host_->setPage(ui::WorkspacePageId::Render);
        window.handleWorkspacePageChanged(ui::WorkspacePageId::Render);
        require(!toolbar_action->isVisible() && !view_action->isVisible() &&
                    !window.saveNamedWorkspaceLayout(QStringLiteral("Render")),
                "Render must not offer editable dock layouts.");
        window.workspace_host_->setPage(ui::WorkspacePageId::Edit);
        window.handleWorkspacePageChanged(ui::WorkspacePageId::Edit);
        require(window.removeNamedWorkspaceLayout(id) &&
                    !window.applyNamedWorkspaceLayout(id),
                "A deleted layout must no longer be available.");

        window.applyBuiltInWorkspaceLayout(1);
        settle();
        const int last_preview_height = window.preview_dock_->height();
        require(window.close(), "The Video Editor did not close cleanly.");
        MainWindow reopened;
        reopened.setDockOptions(reopened.dockOptions() & ~QMainWindow::AnimatedDocks);
        reopened.showNormal();
        reopened.resize(1440, 900);
        settle();
        require(reopened.preview_dock_->height() > reopened.timeline_dock_->height() &&
                    last_preview_height > 0,
                "The last used dock arrangement must reopen automatically.");
        require(reopened.close(), "The reopened Video Editor did not close cleanly.");
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("CreativeSuiteTests");
    QCoreApplication::setApplicationName("WorkspaceLayoutGalleryTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings_directory.path());
    try {
        WorkspaceLayoutGalleryTest::run();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
