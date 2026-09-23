#include "ui/timeline_end_buttons.h"
#include "ui/workspace_page_view.h"

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
        auto* page_view = new ui::WorkspacePageView(
            preview, edit_inspector, &window);
        window.setCentralWidget(page_view);

        auto* inspector_dock = new QDockWidget("Inspector", &window);
        inspector_dock->setWidget(page_view->inspectorPanel());
        window.addDockWidget(Qt::RightDockWidgetArea, inspector_dock);

        const auto buttons = ui::createTimelineEndButtons(&window);
        auto* toolbar = new QToolBar(&window);
        toolbar->addWidget(buttons.container);
        window.addToolBar(Qt::BottomToolBarArea, toolbar);
        QObject::connect(buttons.edit, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setFusionPageActive(false);
        });
        QObject::connect(buttons.fusion, &QPushButton::clicked, page_view, [page_view]() {
            page_view->setFusionPageActive(true);
        });

        window.resize(960, 720);
        window.show();
        application.processEvents();

        require(buttons.edit->isChecked() && !buttons.fusion->isChecked(),
                "The workspace must open on Edit.");
        require(page_view->nodeEditorPanel()->isHidden(),
                "The Node Editor must be hidden on Edit.");
        require(page_view->inspectorPanel()->currentWidget() == edit_inspector,
                "The normal Inspector must be shown on Edit.");
        require(page_view->previewWidget() == preview && preview->isVisible(),
                "Edit must show the existing Preview widget.");

        buttons.fusion->click();
        application.processEvents();
        require(!buttons.edit->isChecked() && buttons.fusion->isChecked(),
                "Selecting Fusion must select only the Fusion button.");
        require(!page_view->nodeEditorPanel()->isHidden(),
                "Fusion must show the Node Editor panel.");
        require(page_view->inspectorPanel()->currentWidget() ==
                    page_view->fusionInspectorPage(),
                "Fusion must show the Fusion Inspector placeholder.");
        require(page_view->previewWidget() == preview && preview->isVisible(),
                "Fusion must keep the same Preview visible as its Viewer.");
        auto* viewer_title = page_view->findChild<QLabel*>(
            "workspaceViewerTitle");
        require(viewer_title != nullptr && viewer_title->isVisible(),
                "Fusion must label the existing Preview as Viewer.");
        buttons.edit->click();
        application.processEvents();
        require(buttons.edit->isChecked() && !buttons.fusion->isChecked(),
                "Returning to Edit must restore the exclusive button state.");
        require(page_view->nodeEditorPanel()->isHidden(),
                "Returning to Edit must hide the Node Editor.");
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
