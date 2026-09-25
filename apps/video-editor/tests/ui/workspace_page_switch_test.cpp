#include "ui/timeline/timeline_end_buttons.h"
#include "ui/workspace/workspace_page_view.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolBar>

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
        auto* page_view = new ui::WorkspacePageView(
            preview, edit_inspector, timeline, &window);
        window.setCentralWidget(page_view);

        auto* inspector_dock = new QDockWidget("Inspector", &window);
        inspector_dock->setWidget(page_view->inspectorPanel());
        window.addDockWidget(Qt::RightDockWidgetArea, inspector_dock);

        auto* lower_dock = new QDockWidget("Timeline", &window);
        lower_dock->setWidget(page_view->lowerWorkspacePanel());
        window.addDockWidget(Qt::BottomDockWidgetArea, lower_dock);

        const auto buttons = ui::createTimelineEndButtons(&window);
        auto* toolbar = new QToolBar(&window);
        toolbar->addWidget(buttons.container);
        window.addToolBar(Qt::TopToolBarArea, toolbar);
        QObject::connect(buttons.edit, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setFusionPageActive(false);
        });
        QObject::connect(buttons.edit, &QPushButton::clicked, lower_dock, [lower_dock]() {
            lower_dock->setWindowTitle("Timeline");
        });
        QObject::connect(buttons.fusion, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setFusionPageActive(true);
        });
        QObject::connect(buttons.fusion, &QPushButton::clicked, lower_dock, [lower_dock]() {
            lower_dock->setWindowTitle("Node Editor");
        });
        QObject::connect(buttons.render, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setRenderPageActive(true);
        });
        QObject::connect(buttons.edit, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setRenderPageActive(false);
        });
        QObject::connect(buttons.fusion, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setRenderPageActive(false);
        });

        window.resize(960, 720);
        window.show();
        application.processEvents();

        require(buttons.edit->isChecked() && !buttons.fusion->isChecked() &&
                    !buttons.render->isChecked(),
                "The workspace must open on Edit.");
        require(page_view->lowerWorkspacePanel()->currentWidget() == timeline,
                "Edit must show the Timeline in the lower workspace dock.");
        require(page_view->nodeEditorPanel()->isHidden(),
                "The Node Editor must be hidden on Edit.");
        require(lower_dock->windowTitle() == "Timeline",
                "The lower workspace dock must be titled Timeline on Edit.");
        require(page_view->inspectorPanel()->currentWidget() == edit_inspector,
                "The normal Inspector must be shown on Edit.");
        require(page_view->previewWidget() == preview && preview->isVisible(),
                "Edit must show the existing Preview widget.");

        buttons.fusion->click();
        application.processEvents();
        require(!buttons.edit->isChecked() && buttons.fusion->isChecked(),
                "Selecting Fusion must select only the Fusion button.");
        require(page_view->lowerWorkspacePanel()->currentWidget() ==
                    page_view->nodeEditorPanel(),
                "Fusion must replace the Timeline with the Node Editor.");
        require(!page_view->nodeEditorPanel()->isHidden(),
                "Fusion must show the Node Editor panel.");
        require(timeline->isHidden(),
                "Fusion must hide the Timeline page.");
        require(lower_dock->windowTitle() == "Node Editor",
                "The lower workspace dock must be titled Node Editor in Fusion.");
        require(page_view->inspectorPanel()->currentWidget() ==
                    page_view->fusionInspectorPage(),
                "Fusion must show the Fusion Inspector placeholder.");
        require(page_view->previewWidget() == preview && preview->isVisible(),
                "Fusion must keep the same Preview visible as its Viewer.");
        auto* viewer_title = page_view->findChild<QLabel*>(
            "workspaceViewerTitle");
        require(viewer_title != nullptr && viewer_title->isVisible(),
                "Fusion must label the existing Preview as Viewer.");

        buttons.render->click();
        application.processEvents();
        require(!buttons.edit->isChecked() && !buttons.fusion->isChecked() &&
                    buttons.render->isChecked(),
                "Selecting Render must select only the Render button.");
        require(page_view->renderPage() != nullptr &&
                    page_view->renderPage()->isVisible(),
                "Render must display its empty workspace page.");
        require(page_view->previewWidget() == preview && preview->isHidden(),
                "Render must hide the Preview widget.");
        require(viewer_title->isHidden(),
                "Render must hide the Viewer title.");
        require(page_view->lowerWorkspacePanel()->currentWidget() == timeline,
                "Render must keep the shared Timeline page in the lower workspace dock.");
        require(page_view->renderPage()->layout() == nullptr &&
                    page_view->renderPage()->findChildren<QWidget*>(
                        QString(), Qt::FindDirectChildrenOnly).isEmpty(),
                "The Render page must contain no controls or placeholder text.");

        buttons.fusion->click();
        application.processEvents();
        require(buttons.fusion->isChecked() && !buttons.render->isChecked(),
                "Returning from Render to Fusion must select Fusion only.");
        require(page_view->previewWidget() == preview && preview->isVisible(),
                "Returning to Fusion must restore the shared Preview.");
        require(page_view->lowerWorkspacePanel()->currentWidget() ==
                    page_view->nodeEditorPanel() &&
                    page_view->inspectorPanel()->currentWidget() ==
                        page_view->fusionInspectorPage(),
                "Returning to Fusion must restore its panels.");

        buttons.edit->click();
        application.processEvents();
        require(buttons.edit->isChecked() && !buttons.fusion->isChecked() &&
                    !buttons.render->isChecked(),
                "Returning to Edit must restore the exclusive button state.");
        require(page_view->lowerWorkspacePanel()->currentWidget() == timeline,
                "Returning to Edit must restore the Timeline in the lower dock.");
        require(page_view->nodeEditorPanel()->isHidden(),
                "Returning to Edit must hide the Node Editor.");
        require(lower_dock->windowTitle() == "Timeline",
                "Returning to Edit must restore the Timeline dock title.");
        require(page_view->inspectorPanel()->currentWidget() == edit_inspector,
                "Returning to Edit must restore the normal Inspector.");
        require(page_view->previewWidget() == preview && preview->isVisible(),
                "Returning to Edit must preserve the Preview widget.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
