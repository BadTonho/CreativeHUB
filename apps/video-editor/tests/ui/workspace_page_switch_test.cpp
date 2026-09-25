#include "ui/timeline/timeline_end_buttons.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_transition_controller.h"
#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "ui/workspace/pages/render/render_workspace.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolBar>

#include <array>
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
        QMainWindow window;
        auto* preview = new QWidget;
        preview->setObjectName("sharedPreview");
        auto* edit_inspector = new QWidget;
        edit_inspector->setObjectName("editInspectorPage");
        auto* timeline = new QWidget;
        timeline->setObjectName("timelinePage");
        auto* fusion_workspace = new ui::FusionWorkspace(&window);
        fusion_workspace->createPanels(&window);
        bool timeline_read_only = false;
        auto* render_workspace = new ui::RenderWorkspace(
            [&timeline_read_only](bool active) {
                timeline_read_only = active;
            },
            &window);
        render_workspace->createPanels(&window);
        auto* workspace_host = new ui::WorkspaceHost(
            preview, edit_inspector, timeline, fusion_workspace,
            render_workspace, &window);
        window.setCentralWidget(workspace_host);

        auto* inspector_dock = new QDockWidget("Inspector", &window);
        inspector_dock->setWidget(workspace_host->inspectorPanel());
        window.addDockWidget(Qt::RightDockWidgetArea, inspector_dock);

        auto* lower_dock = new QDockWidget("Timeline", &window);
        lower_dock->setWidget(workspace_host->lowerWorkspacePanel());
        window.addDockWidget(Qt::BottomDockWidgetArea, lower_dock);

        const auto create_dock = [&window](const QString& title) {
            auto* dock = new QDockWidget(title, &window);
            dock->setWidget(new QWidget);
            window.addDockWidget(Qt::LeftDockWidgetArea, dock);
            return dock;
        };
        auto* bins_dock = create_dock("Bins");
        auto* media_dock = create_dock("Media");
        auto* toolbox_dock = create_dock("Toolbox");
        auto* favorites_dock = create_dock("Favorites");
        auto* effects_dock = create_dock("Effects");

        const auto buttons = ui::createTimelineEndButtons(&window);
        auto* toolbar = new QToolBar(&window);
        toolbar->addWidget(buttons.container);
        window.addToolBar(Qt::TopToolBarArea, toolbar);
        ui::WorkspaceTransitionController transition_controller(
            workspace_host,
            {
                bins_dock,
                media_dock,
                toolbox_dock,
                favorites_dock,
                effects_dock,
                inspector_dock,
                lower_dock},
            {buttons.edit, buttons.fusion, buttons.render},
            &window);
        transition_controller.setPage(ui::WorkspacePageId::Edit);

        window.resize(960, 720);
        window.show();
        application.processEvents();

        require(buttons.edit->isChecked() && !buttons.fusion->isChecked() &&
                    !buttons.render->isChecked(),
                "The workspace must open on Edit.");
        require(workspace_host->currentPage() == ui::WorkspacePageId::Edit &&
                    workspace_host->lowerWorkspacePanel()->currentWidget() == timeline,
                "Edit must show the Timeline in the lower workspace dock.");
        require(workspace_host->nodeEditorPanel()->isHidden(),
                "The Node Editor must be hidden on Edit.");
        require(lower_dock->windowTitle() == "Timeline",
                "The lower workspace dock must be titled Timeline on Edit.");
        require(workspace_host->inspectorPanel()->currentWidget() == edit_inspector,
                "The normal Inspector must be shown on Edit.");
        require(workspace_host->previewWidget() == preview && preview->isVisible(),
                "Edit must show the existing Preview widget.");

        buttons.fusion->click();
        application.processEvents();
        require(!buttons.edit->isChecked() && buttons.fusion->isChecked(),
                "Selecting Fusion must select only the Fusion button.");
        require(workspace_host->currentPage() == ui::WorkspacePageId::Fusion &&
                    workspace_host->lowerWorkspacePanel()->currentWidget() ==
                        workspace_host->nodeEditorPanel(),
                "Fusion must replace the Timeline with the Node Editor.");
        require(!workspace_host->nodeEditorPanel()->isHidden(),
                "Fusion must show the Node Editor panel.");
        require(timeline->isHidden(),
                "Fusion must hide the Timeline page.");
        require(lower_dock->windowTitle() == "Node Editor",
                "The lower workspace dock must be titled Node Editor in Fusion.");
        require(workspace_host->inspectorPanel()->currentWidget() ==
                    workspace_host->fusionInspectorPage() &&
                    workspace_host->fusionInspectorPage() ==
                        fusion_workspace->inspectorPanel(),
                "Fusion must show the Fusion Inspector placeholder.");
        require(workspace_host->nodeEditorPanel() ==
                    fusion_workspace->nodeEditorPanel(),
                "The Fusion Node Editor must come from FusionWorkspace.");
        require(workspace_host->previewWidget() == preview && preview->isVisible(),
                "Fusion must keep the same Preview visible as its Viewer.");
        auto* viewer_title = workspace_host->findChild<QLabel*>(
            "workspaceViewerTitle");
        require(viewer_title != nullptr && viewer_title->isVisible() &&
                    viewer_title == fusion_workspace->viewerTitle(),
                "Fusion must label the existing Preview as Viewer.");

        const std::array<QDockWidget*, 7> workspace_docks{
            bins_dock,
            media_dock,
            toolbox_dock,
            favorites_dock,
            effects_dock,
            inspector_dock,
            lower_dock};
        const std::array<bool, 7> visibility_before_render{
            false, true, false, true, false, true, false};
        for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
            workspace_docks[index]->setVisible(visibility_before_render[index]);
        }

        buttons.render->click();
        application.processEvents();
        require(!buttons.edit->isChecked() && !buttons.fusion->isChecked() &&
                    buttons.render->isChecked(),
                "Selecting Render must select only the Render button.");
        require(workspace_host->currentPage() == ui::WorkspacePageId::Render &&
                    workspace_host->renderPage() != nullptr &&
                    workspace_host->renderPage()->isVisible() &&
                    workspace_host->renderPage() ==
                        render_workspace->centralPage() &&
                    render_workspace->isActive() && timeline_read_only,
                "Render must display its empty workspace page.");
        require(workspace_host->previewWidget() == preview && preview->isHidden(),
                "Render must hide the Preview widget.");
        require(viewer_title->isHidden(),
                "Render must hide the Viewer title.");
        require(workspace_host->lowerWorkspacePanel()->currentWidget() == timeline,
                "Render must keep the shared Timeline page in the lower workspace dock.");
        require(workspace_host->renderPage()->layout() == nullptr &&
                    workspace_host->renderPage()->findChildren<QWidget*>(
                        QString(), Qt::FindDirectChildrenOnly).isEmpty(),
                "The Render page must contain no controls or placeholder text.");
        for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
            require(workspace_docks[index]->isVisible() == (index == 6),
                    "Render must show only the Timeline dock.");
        }

        buttons.render->click();
        application.processEvents();
        require(buttons.render->isChecked() &&
                    workspace_host->currentPage() == ui::WorkspacePageId::Render,
                "Selecting Render again must keep the current workspace active.");

        buttons.fusion->click();
        application.processEvents();
        require(buttons.fusion->isChecked() && !buttons.render->isChecked(),
                "Returning from Render to Fusion must select Fusion only.");
        require(workspace_host->currentPage() == ui::WorkspacePageId::Fusion &&
                    workspace_host->previewWidget() == preview && preview->isVisible(),
                "Returning to Fusion must restore the shared Preview.");
        require(!render_workspace->isActive() && !timeline_read_only,
                "Leaving Render for Fusion must restore Timeline interaction.");
        require(workspace_host->lowerWorkspacePanel()->currentWidget() ==
                    workspace_host->nodeEditorPanel() &&
                    workspace_host->inspectorPanel()->currentWidget() ==
                        workspace_host->fusionInspectorPage(),
                "Returning to Fusion must restore its panels.");
        for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
            require(workspace_docks[index]->isVisible() ==
                        visibility_before_render[index],
                    "Returning from Render must restore each dock's prior visibility.");
        }

        buttons.edit->click();
        application.processEvents();
        require(buttons.edit->isChecked() && !buttons.fusion->isChecked() &&
                    !buttons.render->isChecked(),
                "Returning to Edit must restore the exclusive button state.");
        require(workspace_host->currentPage() == ui::WorkspacePageId::Edit &&
                    workspace_host->lowerWorkspacePanel()->currentWidget() == timeline,
                "Returning to Edit must restore the Timeline in the lower dock.");
        require(workspace_host->nodeEditorPanel()->isHidden(),
                "Returning to Edit must hide the Node Editor.");
        require(lower_dock->windowTitle() == "Timeline",
                "Returning to Edit must restore the Timeline dock title.");
        require(workspace_host->inspectorPanel()->currentWidget() == edit_inspector,
                "Returning to Edit must restore the normal Inspector.");
        require(workspace_host->previewWidget() == preview && preview->isVisible(),
                "Returning to Edit must preserve the Preview widget.");

        const std::array<bool, 7> visibility_before_close{
            true, false, true, false, true, false, true};
        for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
            workspace_docks[index]->setVisible(visibility_before_close[index]);
        }
        buttons.render->click();
        application.processEvents();
        transition_controller.prepareForClose();
        application.processEvents();
        require(workspace_host->currentPage() == ui::WorkspacePageId::Edit &&
                    buttons.edit->isChecked() && !buttons.render->isChecked(),
                "Preparing to close from Render must return to Edit.");
        for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
            require(workspace_docks[index]->isVisible() ==
                        visibility_before_close[index],
                    "Preparing to close must restore the previous dock layout.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
