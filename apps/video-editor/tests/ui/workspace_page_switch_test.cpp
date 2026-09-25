#include "ui/timeline/timeline_end_buttons.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_transition_controller.h"
#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "ui/workspace/pages/render/render_queue_model.h"
#include "ui/workspace/pages/render/render_workspace.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QSplitter>
#include <QSpinBox>
#include <QToolBar>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void sendWheel(QWidget* target, int angle_delta_y) {
    const QPoint position = target->rect().center();
    QWheelEvent event(
        QPointF(position),
        QPointF(target->mapToGlobal(position)),
        QPoint(),
        QPoint(0, angle_delta_y),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    QApplication::sendEvent(target, &event);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    try {
        QMainWindow window;
        auto* preview = new QWidget;
        preview->setObjectName("sharedPreview");
        preview->setMinimumSize(320, 180);
        auto* edit_inspector = new QWidget;
        edit_inspector->setObjectName("editInspectorPage");
        auto* timeline = new QWidget;
        timeline->setObjectName("timelinePage");
        auto* fusion_workspace = new ui::FusionWorkspace(&window);
        fusion_workspace->createPanels(&window);
        project::ProjectDocument project_snapshot;
        project::ProjectTrack project_track;
        project_track.track_id = 41;
        project_track.name = "Video 1";
        project_snapshot.timeline_tracks.push_back(project_track);
        bool timeline_read_only = false;
        auto* render_workspace = new ui::RenderWorkspace(
            [&timeline_read_only](bool active) {
                timeline_read_only = active;
            },
            [&project_snapshot] { return project_snapshot; },
            [] { return 23.976; },
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

        window.resize(1400, 720);
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
                "Render must display its settings and queue page.");
        require(workspace_host->previewWidget() == preview && preview->isVisible() &&
                    render_workspace->previewWidget() == preview,
                "Render must show the same shared Preview widget.");
        require(viewer_title->isHidden(),
                "Render must hide the Viewer title.");
        require(workspace_host->lowerWorkspacePanel()->currentWidget() == timeline,
                "Render must keep the shared Timeline page in the lower workspace dock.");
        auto* render_splitter = render_workspace->splitter();
        auto* render_page_scroll = workspace_host->renderPage()->findChild<QScrollArea*>(
            "renderWorkspaceScrollArea");
        auto* settings_scroll = workspace_host->renderPage()->findChild<QScrollArea*>(
            "renderSettingsScrollArea");
        auto* output_path = workspace_host->renderPage()->findChild<QLineEdit*>(
            "renderOutputPath");
        auto* browse_output = workspace_host->renderPage()->findChild<QPushButton*>(
            "renderBrowseOutputButton");
        auto* container_combo = workspace_host->renderPage()->findChild<QComboBox*>(
            "renderContainerCombo");
        auto* video_encoder_combo = workspace_host->renderPage()->findChild<QComboBox*>(
            "renderVideoEncoderCombo");
        auto* frame_rate_spin = workspace_host->renderPage()->findChild<QDoubleSpinBox*>(
            "renderFrameRate");
        auto* resolution_combo = workspace_host->renderPage()->findChild<QComboBox*>(
            "renderResolutionCombo");
        auto* custom_width = workspace_host->renderPage()->findChild<QSpinBox*>(
            "renderCustomWidth");
        auto* custom_height = workspace_host->renderPage()->findChild<QSpinBox*>(
            "renderCustomHeight");
        auto* quality_preset = workspace_host->renderPage()->findChild<QComboBox*>(
            "renderQualityPresetCombo");
        auto* video_bitrate = workspace_host->renderPage()->findChild<QDoubleSpinBox*>(
            "renderVideoBitrate");
        auto* audio_bitrate = workspace_host->renderPage()->findChild<QSpinBox*>(
            "renderAudioBitrate");
        auto* add_to_queue = workspace_host->renderPage()->findChild<QPushButton*>(
            "renderAddToQueueButton");
        require(render_splitter != nullptr && render_splitter->count() == 3 &&
                    render_workspace->settingsPanel() != nullptr &&
                    render_workspace->previewPanel() != nullptr &&
                    render_workspace->queuePanel() != nullptr &&
                    render_splitter->widget(0) == render_workspace->settingsPanel() &&
                    render_splitter->widget(1) == render_workspace->previewPanel() &&
                    render_splitter->widget(2) == render_workspace->queuePanel() &&
                    render_workspace->previewPanel()->findChild<QWidget*>(
                        "sharedPreview") == preview && output_path != nullptr &&
                    render_page_scroll != nullptr && settings_scroll != nullptr &&
                    browse_output != nullptr &&
                    container_combo != nullptr && video_encoder_combo != nullptr &&
                    frame_rate_spin != nullptr && resolution_combo != nullptr &&
                    custom_width != nullptr && custom_height != nullptr &&
                    quality_preset != nullptr && video_bitrate != nullptr &&
                    audio_bitrate != nullptr && add_to_queue != nullptr,
                "Render must show settings, Preview, and queue columns in order.");
        const auto initial_column_sizes = render_splitter->sizes();
        require(initial_column_sizes.size() == 3 &&
                    initial_column_sizes[1] > initial_column_sizes[0] &&
                    initial_column_sizes[1] > initial_column_sizes[2] &&
                    render_splitter->orientation() == Qt::Horizontal,
                "Render must initially give the Preview the widest column.");

        window.resize(520, 420);
        application.processEvents();
        const auto browse_position = browse_output->mapTo(
            settings_scroll->viewport(), QPoint(0, 0));
        const QRect browse_rect(browse_position, browse_output->size());
        const auto encoder_position = video_encoder_combo->mapTo(
            settings_scroll->viewport(), QPoint(0, 0));
        const QRect encoder_rect(encoder_position, video_encoder_combo->size());
        require(render_splitter->orientation() == Qt::Vertical &&
                    render_splitter->widget(0) == render_workspace->settingsPanel() &&
                    render_splitter->widget(1) == render_workspace->previewPanel() &&
                    render_splitter->widget(2) == render_workspace->queuePanel(),
                "A narrow Render page must stack Settings, Preview, and Queue.");
        require(render_page_scroll->horizontalScrollBar()->maximum() == 0 &&
                    settings_scroll->horizontalScrollBar()->maximum() == 0 &&
                    render_page_scroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                    render_page_scroll->verticalScrollBar()->maximum() > 0 &&
                    render_workspace->previewWidget() == preview &&
                    render_workspace->queueModel()->jobCount() == 0,
                "A compact Render page must retain vertical access and its existing state.");
        require(browse_output->isVisible() &&
                    settings_scroll->viewport()->rect().contains(browse_rect),
                "Compact Settings must keep the Browse button inside its viewport.");
        require(add_to_queue->parentWidget() == render_workspace->settingsPanel() &&
                    add_to_queue->isVisible() &&
                    add_to_queue->geometry().top() > settings_scroll->geometry().bottom() &&
                    render_workspace->centralPage()->findChildren<QPushButton*>(
                        "renderAddToQueueButton").size() == 1,
                "Add to Queue must be the single fixed footer button in Render Settings.");
        require(encoder_rect.left() >= 0 &&
                    encoder_rect.right() < settings_scroll->viewport()->width() &&
                    video_encoder_combo->width() > 0 &&
                    video_encoder_combo->toolTip() == video_encoder_combo->currentText(),
                "Compact Settings must keep codec controls inside its viewport.");

        auto* settings_scrollbar = settings_scroll->verticalScrollBar();
        const int container_selection_before_wheel = container_combo->currentIndex();
        settings_scrollbar->setValue(0);
        sendWheel(container_combo, -120);
        require(container_combo->currentIndex() == container_selection_before_wheel &&
                    settings_scrollbar->value() > 0,
                "Wheeling over a closed selector must scroll Settings without changing it.");

        settings_scrollbar->setValue(0);
        settings_scroll->ensureWidgetVisible(frame_rate_spin);
        const int frame_rate_scroll_before_wheel = settings_scrollbar->value();
        const double frame_rate_before_wheel = frame_rate_spin->value();
        sendWheel(frame_rate_spin, -120);
        require(frame_rate_spin->value() == frame_rate_before_wheel &&
                    settings_scrollbar->value() > frame_rate_scroll_before_wheel,
                "Wheeling over a numeric field must scroll Settings without changing it.");

        settings_scrollbar->setValue(settings_scrollbar->maximum());
        require(add_to_queue->isVisible(),
                "The Add to Queue footer must stay visible while Settings are scrolled.");
        settings_scrollbar->setValue(0);

        window.resize(1400, 720);
        application.processEvents();
        const auto restored_column_sizes = render_splitter->sizes();
        require(render_splitter->orientation() == Qt::Horizontal &&
                    restored_column_sizes.size() == 3 &&
                    restored_column_sizes[1] > restored_column_sizes[0] &&
                    restored_column_sizes[1] > restored_column_sizes[2] &&
                    render_workspace->previewWidget() == preview && preview->isVisible() &&
                    render_workspace->queueModel()->jobCount() == 0,
                "Widening Render must restore the three columns without losing state.");
        require(frame_rate_spin->value() == 23.976 && container_combo->count() > 0 &&
                    video_encoder_combo->count() > 0 &&
                    resolution_combo->currentData().toSize() == QSize(1920, 1080) &&
                    quality_preset->currentIndex() == 1 &&
                    std::abs(video_bitrate->value() - 8.0) < 0.11 &&
                    audio_bitrate->value() == 192,
                "Render must initialize project-derived FPS and discover encoders at runtime.");
        require(!add_to_queue->isEnabled(),
                "Render must require an output path before adding a job.");
        quality_preset->setCurrentIndex(2);
        require(std::abs(video_bitrate->value() - 16.0) < 0.11 &&
                    audio_bitrate->value() == 320,
                "The High profile must suggest scaled video and audio bitrates.");
        resolution_combo->setCurrentIndex(resolution_combo->count() - 1);
        custom_width->setValue(2048);
        custom_height->setValue(1080);
        frame_rate_spin->setValue(30.0);
        quality_preset->setCurrentIndex(1);
        require(custom_width->isVisible() && custom_height->isVisible() &&
                    std::abs(video_bitrate->value() - 10.7) < 0.11,
                "Custom dimensions must be editable and update bitrate suggestions.");
        video_bitrate->setValue(11.1);
        require(quality_preset->currentIndex() == 3,
                "Manually editing a bitrate must select the Custom profile.");
        resolution_combo->setCurrentIndex(0);
        frame_rate_spin->setValue(23.976);
        quality_preset->setCurrentIndex(1);
        output_path->setText(QStringLiteral("prepared-output.mp4"));
        require(add_to_queue->isEnabled(),
                "Render must allow a valid output configuration to enter the queue.");
        add_to_queue->click();
        project_snapshot.timeline_tracks.front().name = "Changed after queueing";
        const auto* prepared_job = render_workspace->queueModel()->jobAt(0);
        require(render_workspace->queueModel()->jobCount() == 1 &&
                    prepared_job != nullptr && prepared_job->settings.width == 1920 &&
                    prepared_job->settings.height == 1080 &&
                    prepared_job->settings.frame_rate == 23.976 &&
                    prepared_job->project_snapshot.timeline_tracks.front().name ==
                        "Video 1",
                "Adding a Render job must capture its settings and project state.");
        frame_rate_spin->setValue(48.0);
        window.resize(520, 420);
        application.processEvents();
        require(render_splitter->orientation() == Qt::Vertical &&
                    frame_rate_spin->value() == 48.0 &&
                    render_workspace->queueModel()->jobCount() == 1 &&
                    prepared_job->settings.frame_rate == 23.976 &&
                    prepared_job->project_snapshot.timeline_tracks.front().name ==
                        "Video 1",
                "Narrowing Render must preserve edited settings and the queued snapshot.");
        window.resize(1400, 720);
        application.processEvents();
        const auto job_restored_column_sizes = render_splitter->sizes();
        require(render_splitter->orientation() == Qt::Horizontal &&
                    job_restored_column_sizes.size() == 3 &&
                    job_restored_column_sizes[1] > job_restored_column_sizes[0] &&
                    job_restored_column_sizes[1] > job_restored_column_sizes[2] &&
                    frame_rate_spin->value() == 48.0 &&
                    render_workspace->queueModel()->jobCount() == 1 &&
                    prepared_job->settings.frame_rate == 23.976 &&
                    prepared_job->project_snapshot.timeline_tracks.front().name ==
                        "Video 1",
                "Widening Render must restore its columns and retain settings and jobs.");
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
        require(render_workspace->previewWidget() == nullptr &&
                    preview->parentWidget() == workspace_host->findChild<QStackedWidget*>(
                        "centralWorkspacePages"),
                "Leaving Render must return the Preview to the central workspace stack.");
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
        require(frame_rate_spin->value() == 48.0 &&
                    render_workspace->queueModel()->jobCount() == 1,
                "Render must preserve edited settings and queued jobs across workspace switches.");
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
