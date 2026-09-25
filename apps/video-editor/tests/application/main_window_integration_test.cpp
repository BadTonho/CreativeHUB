#include "main_window/main_window.h"

#include "ui/preview/preview_widget.h"
#include "ui/workspace/workspace_host.h"
#include "project/project_file.h"
#include "settings/user_preferences.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_browser_list_widget.h"

#include <QApplication>
#include <QEventLoop>
#include <QDockWidget>
#include <QImage>
#include <QImageWriter>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QMessageBox>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-main-window-test-" + std::to_string(stamp));
}

project::ProjectDocument makeMultiTrackProject(
    const std::filesystem::path& first_source,
    const std::filesystem::path& second_source) {
    project::ProjectDocument document;
    document.bins = {"Unsorted"};
    document.media = {
        {first_source, "Reference", "Unsorted", false},
        {second_source, "Reference Second", "Unsorted", false},
    };
    document.timeline_tracks = {
        {"Video 1", 1.0, false, {
            {first_source, 0, 0, 1},
        }},
        {"Video 2", 1.0, false, {
            {second_source, 0, 0, 1},
        }},
    };
    document.timeline_tracks[0].track_id = 1;
    document.timeline_tracks[0].clips[0].clip_id = 1;
    document.timeline_tracks[1].track_id = 2;
    document.timeline_tracks[1].clips[0].clip_id = 2;
    return document;
}

QColor framePixel(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < 4 ||
        frame.rgba_pixels.size() < 4) return {};
    return QColor(frame.rgba_pixels[0], frame.rgba_pixels[1],
                  frame.rgba_pixels[2], frame.rgba_pixels[3]);
}

bool writePngAtomically(const std::filesystem::path& path, const QImage& image) {
    QSaveFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly)) return false;
    QImageWriter writer(&file, QByteArrayLiteral("png"));
    if (!writer.write(image)) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace

class MainWindowIntegrationTest final {
public:
    static void run(
        const std::filesystem::path& first_source,
        const std::filesystem::path& second_source) {
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);

        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat,
            QSettings::UserScope,
            QString::fromStdString((directory / "settings").string()));
        QCoreApplication::setOrganizationName("CreativeSuiteTests");
        QCoreApplication::setApplicationName("MainWindowIntegration");
        QSettings().clear();
        settings::setProjectAutosaveEnabled(false);
        settings::setPreviewPerformanceMetricsEnabled(false);

        std::array<bool, 7> dock_visibility_before_close{};
        {
            MainWindow render_window;
            render_window.show();
            QApplication::processEvents();
            const std::array<QDockWidget*, 7> docks{
                render_window.bins_dock_, render_window.media_dock_,
                render_window.toolbox_dock_, render_window.favorites_dock_,
                render_window.effects_dock_, render_window.inspector_dock_,
                render_window.timeline_dock_};
            for (std::size_t index = 0; index < docks.size(); ++index) {
                if (index == docks.size() - 1) {
                    docks[index]->hide();
                }
                dock_visibility_before_close[index] = docks[index]->isVisible();
            }
            render_window.setWorkspacePage(ui::WorkspacePageId::Render);
            for (std::size_t index = 0; index < docks.size(); ++index) {
                require(docks[index]->isVisible() == (index == docks.size() - 1),
                        "Render must keep only the Timeline dock visible before the close-persistence check.");
            }
            require(render_window.close(),
                    "Closing the editor from Render must be accepted.");
        }
        {
            MainWindow reopened_window;
            reopened_window.show();
            QApplication::processEvents();
            const std::array<QDockWidget*, 7> docks{
                reopened_window.bins_dock_, reopened_window.media_dock_,
                reopened_window.toolbox_dock_, reopened_window.favorites_dock_,
                reopened_window.effects_dock_, reopened_window.inspector_dock_,
                reopened_window.timeline_dock_};
            require(reopened_window.edit_workspace_button_->isChecked(),
                    "The application must reopen in Edit after closing from Render.");
            for (std::size_t index = 0; index < docks.size(); ++index) {
                require(docks[index]->isVisible() == dock_visibility_before_close[index],
                        "Closing from Render must preserve the previous dock layout.");
            }
        }
        QSettings().remove("workspace/dock_layout_state");
        QSettings().sync();

        const auto project_path = directory / "multi-track.csp";
        const auto round_trip_path = directory / "multi-track-round-trip.csp";
        const auto original = makeMultiTrackProject(first_source, second_source);
        project::save(project_path, original);
        const auto loaded = project::load(project_path);

        {
            MainWindow window;
            window.show();
            QApplication::processEvents();
            QEventLoop open_loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool open_succeeded = false;
            QObject::connect(&timeout, &QTimer::timeout, &open_loop, &QEventLoop::quit);
            require(window.openProjectPath(
                        project_path,
                        std::nullopt,
                        std::nullopt,
                        [&open_loop, &open_succeeded](bool succeeded) {
                            open_succeeded = succeeded;
                            open_loop.quit();
                        }),
                    "The MainWindow could not open the multi-track project.");
            require(window.project_load_pending_ && window.timeline_dock_ != nullptr &&
                        !window.timeline_dock_->isEnabled() &&
                        window.menuBar()->isEnabled() &&
                        window.project_load_progress_ != nullptr &&
                        window.project_load_progress_->windowModality() == Qt::NonModal &&
                        window.new_project_action_ != nullptr &&
                        !window.new_project_action_->isEnabled() &&
                        window.timeline_model_.trackCount() == 1 &&
                        !window.timeline_model_.hasClip(),
                    "Opening a project did not preserve the visible session while disabling editing.");
            const auto menu_is_available = [&window](const QString& title) {
                for (auto* action : window.menuBar()->actions()) {
                    auto visible_title = action->text();
                    visible_title.remove('&');
                    if (visible_title == title && action->menu() != nullptr) {
                        return action->isEnabled() && action->menu()->isEnabled();
                    }
                }
                return false;
            };
            require(menu_is_available(QStringLiteral("File")) &&
                        menu_is_available(QStringLiteral("Edit")) &&
                        menu_is_available(QStringLiteral("View")) &&
                        menu_is_available(QStringLiteral("Help")),
                    "The top-level menus were unavailable during project preparation.");
            timeout.start(30000);
            open_loop.exec();
            require(open_succeeded && !window.project_load_pending_,
                    "The background project open did not finish successfully.");

            const std::array<QDockWidget*, 7> workspace_docks{
                window.bins_dock_, window.media_dock_, window.toolbox_dock_,
                window.favorites_dock_, window.effects_dock_,
                window.inspector_dock_, window.timeline_dock_};
            const auto dock_visibility = [&workspace_docks]() {
                std::array<bool, 7> visibility{};
                for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
                    visibility[index] = workspace_docks[index]->isVisible();
                }
                return visibility;
            };
            const auto require_dock_visibility =
                [&workspace_docks](const std::array<bool, 7>& expected,
                                   const char* message) {
                    for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
                        require(workspace_docks[index]->isVisible() == expected[index],
                                message);
                    }
                };
            require(window.edit_workspace_button_ != nullptr &&
                        window.fusion_workspace_button_ != nullptr &&
                        window.render_workspace_button_ != nullptr &&
                        window.edit_workspace_button_->isChecked() &&
                        window.edit_workspace_ != nullptr &&
                        window.edit_workspace_->controller() != nullptr &&
                        &window.edit_workspace_->controller()->session() ==
                            &window.editor_session_ &&
                        window.edit_workspace_->previewWidget() ==
                            window.preview_widget_ &&
                        window.edit_workspace_->inspectorPanel() ==
                            window.workspace_host_->editInspectorPage() &&
                        window.edit_workspace_->timelinePanel() ==
                            window.workspace_host_->timelinePanel(),
                    "The MainWindow must start with Edit selected and expose all workspace selectors.");
            const auto edit_dock_visibility = dock_visibility();
            window.setWorkspacePage(ui::WorkspacePageId::Fusion);
            window.setWorkspacePage(ui::WorkspacePageId::Render);
            QApplication::processEvents();
            require(window.render_workspace_button_->isChecked() &&
                        !window.edit_workspace_button_->isChecked() &&
                        !window.fusion_workspace_button_->isChecked(),
                    "The MainWindow must select only Render.");
            require(window.workspace_host_->renderPage()->isVisible() &&
                        window.preview_widget_->isHidden(),
                    "Render must show its empty central page and hide the Preview.");
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Render &&
                        window.workspace_host_->lowerWorkspacePanel()->currentWidget() ==
                            window.workspace_host_->timelinePanel() &&
                        window.timeline_dock_->windowTitle() == "Timeline" &&
                        window.timeline_widget_->isReadOnly() &&
                        window.timeline_controls_container_->isHidden() &&
                        window.timeline_footer_->isHidden(),
                    "Render must show the read-only Timeline without its controls or footer.");
            require(window.render_workspace_button_->isVisible(),
                    "The Render selector must remain visible while Render is active.");
            for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
                require(workspace_docks[index]->isVisible() ==
                            (workspace_docks[index] == window.timeline_dock_),
                        "Entering Render must keep only the Timeline dock visible.");
            }
            window.setWorkspacePage(ui::WorkspacePageId::Fusion);
            QApplication::processEvents();
            require_dock_visibility(
                edit_dock_visibility,
                "Returning to Fusion must restore the dock visibility from before Render.");
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.workspace_host_->previewWidget()->isVisible(),
                    "Returning to Fusion must restore its Preview.");
            require(window.timeline_dock_->windowTitle() == "Node Editor",
                    "Returning to Fusion must restore the Node Editor title.");
            require(!window.timeline_widget_->isReadOnly() &&
                        !window.timeline_controls_container_->isHidden() &&
                        !window.timeline_footer_->isHidden(),
                    "Returning to Fusion must restore Timeline interaction and controls.");

            window.media_dock_->hide();
            window.effects_dock_->show();
            QApplication::processEvents();
            const auto mixed_dock_visibility = dock_visibility();
            window.setWorkspacePage(ui::WorkspacePageId::Render);
            window.setWorkspacePage(ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require_dock_visibility(
                mixed_dock_visibility,
                "Returning to Edit must restore mixed dock visibility from before Render.");

            window.timeline_dock_->hide();
            QApplication::processEvents();
            const auto hidden_timeline_visibility = dock_visibility();
            window.setWorkspacePage(ui::WorkspacePageId::Render);
            QApplication::processEvents();
            require(window.timeline_dock_->isVisible(),
                    "Render must show the Timeline even when its prior workspace visibility was hidden.");
            window.setWorkspacePage(ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require_dock_visibility(
                hidden_timeline_visibility,
                "Leaving Render must restore the prior hidden state of the Timeline dock.");
            require(!window.timeline_widget_->isReadOnly() &&
                        !window.timeline_controls_container_->isHidden() &&
                        !window.timeline_footer_->isHidden() &&
                        !window.project_dirty_,
                    "Workspace changes must restore Timeline interaction without dirtying the project.");
            window.restoreDefaultLayout();
            window.setWorkspacePage(ui::WorkspacePageId::Edit);
            QApplication::processEvents();

            require(window.timeline_model_.trackCount() == 2,
                    "Opening the project did not preserve both timeline tracks.");
            require(window.timeline_model_.clipCount(0) == 1 &&
                        window.timeline_model_.clipCount(1) == 1,
                    "Opening the project did not preserve clips on both tracks.");
            require(window.timeline_model_.tracks()[0].track_id == 1 &&
                        window.timeline_model_.tracks()[1].track_id == 2 &&
                        window.timeline_model_.tracks()[0].clips[0].clip_id == 1 &&
                        window.timeline_model_.tracks()[1].clips[0].clip_id == 2,
                    "Opening the project did not preserve stable track and clip identifiers.");
            require(window.active_timeline_track_id_.has_value() &&
                        window.active_timeline_clip_id_.has_value() &&
                        *window.active_timeline_track_id_ == 1 &&
                        *window.active_timeline_clip_id_ == 1,
                    "Opening the project did not select the first clip by stable identity.");
            require(!window.project_dirty_,
                    "Opening a saved multi-track project incorrectly marked it dirty.");
            require(window.windowTitle() == QStringLiteral("Video Editor"),
                    "Opening a saved multi-track project incorrectly added the dirty marker.");

            const auto failed_open_path = directory / "corrupt.csp";
            {
                std::ofstream corrupt_file(failed_open_path, std::ios::binary);
                corrupt_file << "not a project document";
            }
            const auto preserved_timeline = window.editor_session_.timeline().snapshot();
            const auto preserved_project_path = window.project_controller_.projectPath();
            const auto preserved_media_count = window.editor_session_.mediaItems().size();
            const auto preserved_selection = window.editor_session_.selection();
            const auto preserved_playhead = window.editor_session_.playheadFrame();
            QEventLoop failed_open_loop;
            QTimer failed_open_timeout;
            failed_open_timeout.setSingleShot(true);
            QTimer dismiss_project_error;
            dismiss_project_error.setInterval(10);
            QObject::connect(&dismiss_project_error, &QTimer::timeout, []() {
                auto* modal = QApplication::activeModalWidget();
                auto* message = qobject_cast<QMessageBox*>(modal);
                if (message != nullptr &&
                    message->windowTitle() == QStringLiteral("Could not open project")) {
                    message->accept();
                }
            });
            bool failed_open_succeeded = true;
            QObject::connect(&failed_open_timeout, &QTimer::timeout,
                             &failed_open_loop, &QEventLoop::quit);
            require(window.openProjectPath(
                        failed_open_path,
                        std::nullopt,
                        std::nullopt,
                        [&failed_open_loop, &failed_open_succeeded](bool succeeded) {
                            failed_open_succeeded = succeeded;
                            failed_open_loop.quit();
                        }),
                    "The MainWindow did not start a corrupt-project open attempt.");
            dismiss_project_error.start();
            failed_open_timeout.start(30000);
            failed_open_loop.exec();
            dismiss_project_error.stop();
            require(!failed_open_succeeded && !window.project_load_pending_ &&
                        window.editor_session_.timeline().snapshot() == preserved_timeline &&
                        window.project_controller_.projectPath() == preserved_project_path &&
                        window.editor_session_.mediaItems().size() == preserved_media_count &&
                        window.editor_session_.selection().active_track_id ==
                            preserved_selection.active_track_id &&
                        window.editor_session_.selection().active_clip_id ==
                            preserved_selection.active_clip_id &&
                        window.editor_session_.playheadFrame() == preserved_playhead,
                    "A failed project open replaced or changed the current editor session.");

            const auto current = window.currentProjectDocument();
            require(current.canvas_width == loaded.canvas_width &&
                        current.canvas_height == loaded.canvas_height &&
                        current.timeline_zoom == loaded.timeline_zoom &&
                        current.timeline_row_height == loaded.timeline_row_height,
                    "The current MainWindow document has different canvas or view settings.");
            require(current.media == loaded.media,
                    "The current MainWindow document has different media entries.");
            require(current.timeline_tracks == loaded.timeline_tracks,
                    "The current MainWindow document has different timeline tracks.");
            require(current.bins == loaded.bins,
                    "The current MainWindow document has different media bins.");

            require(window.saveProjectTo(round_trip_path, "save_as"),
                    "The MainWindow could not save the multi-track project.");
            const auto round_tripped = project::load(round_trip_path);
            require(round_tripped == current,
                    "The MainWindow multi-track project did not round-trip through save.");
            require(round_tripped.timeline_tracks.size() == 2 &&
                        round_tripped.timeline_tracks[0].clips.size() == 1 &&
                        round_tripped.timeline_tracks[1].clips.size() == 1,
                    "The round-trip project lost a track or clip.");

            window.clearActiveTimelineSelection();
            require(window.playback_controller_ != nullptr &&
                        window.playback_controller_->activateClip(1, 0, false) ==
                            playback::PlaybackCommandResult::Pending,
                    "The playback controller did not accept a stable-identity activation.");
            QEventLoop activation_loop;
            QTimer activation_timeout;
            activation_timeout.setSingleShot(true);
            QObject::connect(&activation_timeout, &QTimer::timeout,
                             &activation_loop, &QEventLoop::quit);
            QTimer activation_poll;
            QObject::connect(&activation_poll, &QTimer::timeout, &activation_loop, [&]() {
                if (window.playback_controller_ == nullptr ||
                    !window.playback_activation_loading_) {
                    activation_loop.quit();
                }
            });
            activation_timeout.start(30000);
            activation_poll.start(10);
            activation_loop.exec();
            require(window.playback_controller_ != nullptr &&
                        !window.playback_activation_loading_,
                    "The controller activation did not leave its pending state.");
            require(window.active_timeline_track_id_ == 1 &&
                        window.active_timeline_clip_id_ == 1 &&
                        window.active_timeline_track_index_cache_ == 0 &&
                        window.active_timeline_clip_index_cache_ == 0 &&
                        window.editor_session_.selection().active_track_id == 1 &&
                        window.editor_session_.selection().active_clip_id == 1,
                    "A controller activation did not update the timeline selection projection.");
            require(window.media_list_ != nullptr &&
                        window.media_list_->currentRow() == 0,
                    "A controller activation did not update the media-browser selection projection.");
            require(window.timeline_command_service_.execute(
                        application::DeleteClipCommand{1}).changed(),
                    "The integration test could not prepare its cross-track move fixture.");
            window.timeline_command_service_.clearHistory();
            window.clearActiveTimelineSelection();

            window.setActiveTimelineSelection(timeline::ClipLocation{1, 0});
            window.edit_workspace_->controller()->moveClip(2, 1, 0);
            require(window.active_timeline_track_id_ == 1 &&
                        window.active_timeline_clip_id_ == 2 &&
                        window.active_timeline_track_index_cache_ == 0 &&
                        window.active_timeline_clip_index_cache_ == 0 &&
                        window.editor_session_.selection().active_track_id == 1 &&
                        window.editor_session_.selection().active_clip_id == 2 &&
                        window.playback_frame_index_ == window.editor_session_.playheadFrame(),
                    "A service-backed move did not update the MainWindow selection projection.");
            require(window.project_dirty_,
                    "A service-backed move did not update the project dirty state.");

            static_cast<void>(window.edit_workspace_->controller()->undo());
            require(window.active_timeline_track_id_ == 2 &&
                        window.active_timeline_clip_id_ == 2 &&
                        window.active_timeline_track_index_cache_ == 1 &&
                        window.timeline_model_.locateClip(2) == timeline::ClipLocation{1, 0},
                    "Undo did not restore the service-backed move and selection.");

            window.media_controller_.clear();
            window.populateMediaBrowser();
            require(window.startMediaImport({first_source}),
                    "The MainWindow did not start the background media import.");
            media::VideoMetadata selected_metadata;
            selected_metadata.source_path = second_source;
            selected_metadata.display_name = "Keep this selection";
            require(window.media_controller_.commitImported({
                        selected_metadata, {}, selected_metadata.display_name,
                        "Unsorted", true}).changed(),
                    "The integration test could not add its selection fixture.");
            const auto selection_after_import_start = window.selection_generation_;
            window.populateMediaBrowser(second_source);
            QEventLoop import_loop;
            QTimer import_timeout;
            import_timeout.setSingleShot(true);
            QObject::connect(&import_timeout, &QTimer::timeout,
                             &import_loop, &QEventLoop::quit);
            QTimer import_poll;
            QObject::connect(&import_poll, &QTimer::timeout, &import_loop, [&]() {
                if (!window.active_media_import_cancel_) import_loop.quit();
            });
            require(window.media_import_progress_ != nullptr,
                    "The MainWindow did not present media import progress.");
            import_timeout.start(30000);
            import_poll.start(10);
            import_loop.exec();
            require(!window.active_media_import_cancel_ &&
                        window.media_controller_.library().contains(first_source) &&
                        window.selection_generation_ > selection_after_import_start,
                    "The MainWindow did not apply the completed import to the session library.");
            require(window.selectedMediaIndex().has_value(),
                    "The media-browser selection was lost after the import completed.");
            require(window.media_items_[*window.selectedMediaIndex()].metadata.source_path ==
                        media::MediaLibrary::canonicalPath(second_source),
                    "A late import changed the selection made while it was running.");

            window.media_controller_.clear();
            window.populateMediaBrowser();
            QEventLoop stale_import_loop;
            QTimer stale_import_timeout;
            stale_import_timeout.setSingleShot(true);
            QObject::connect(&stale_import_timeout, &QTimer::timeout,
                             &stale_import_loop, &QEventLoop::quit);
            QTimer stale_import_poll;
            QObject::connect(&stale_import_poll, &QTimer::timeout,
                             &stale_import_loop, [&]() {
                if (!window.active_media_import_cancel_) stale_import_loop.quit();
            });
            require(window.startMediaImport({second_source}),
                    "The MainWindow did not start the stale-generation import.");
            ++window.project_generation_;
            stale_import_timeout.start(30000);
            stale_import_poll.start(10);
            stale_import_loop.exec();
            require(!window.active_media_import_cancel_ &&
                        !window.media_controller_.library().contains(second_source) &&
                        !window.selectedMediaIndex().has_value(),
                    "An import result from an earlier project generation changed the session.");
        }

        const auto linked_source = directory / "linked-original.png";
        const auto linked_document = directory / "linked-edit.cimg";
        const auto linked_output = directory / "linked-output.png";
        const auto variant_document = directory / "linked-clip-variant.cimg";
        const auto variant_output = directory / "linked-clip-variant.png";
        const auto linked_project = directory / "linked-image.csp";
        QImage source_image(16, 16, QImage::Format_ARGB32);
        source_image.fill(QColor(230, 20, 15, 255));
        QImage first_output(16, 16, QImage::Format_ARGB32);
        first_output.fill(QColor(20, 220, 35, 255));
        QImage isolated_variant(16, 16, QImage::Format_ARGB32);
        isolated_variant.fill(QColor(230, 210, 15, 255));
        require(source_image.save(QString::fromStdString(linked_source.string())) &&
                    writePngAtomically(linked_output, first_output) &&
                    writePngAtomically(variant_output, isolated_variant),
                "The MainWindow linked-image fixtures could not be written.");
        const media::LinkedImageReference shared_link{
            "shared-main-window-link", linked_document, linked_output};
        project::ProjectDocument linked_document_data;
        linked_document_data.media.push_back({
            linked_source, "Linked still", "Unsorted", false,
            media::MediaKind::Image, shared_link});
        project::ProjectTrack linked_track;
        linked_track.track_id = 71;
        linked_track.name = "V1";
        project::ProjectClip linked_clip;
        linked_clip.clip_id = 81;
        linked_clip.source_path = linked_source;
        linked_clip.duration_frames = 30;
        linked_clip.kind = timeline::ClipKind::Image;
        linked_track.clips.push_back(linked_clip);
        project::ProjectClip variant_clip;
        variant_clip.clip_id = 82;
        variant_clip.source_path = linked_source;
        variant_clip.timeline_start_frame = 30;
        variant_clip.duration_frames = 30;
        variant_clip.kind = timeline::ClipKind::Image;
        variant_clip.image_editor_variant = media::LinkedImageReference{
            "clip-specific-window-link", variant_document, variant_output};
        linked_track.clips.push_back(variant_clip);
        linked_document_data.timeline_tracks.push_back(linked_track);
        project::save(linked_project, linked_document_data);

        {
            MainWindow linked_window;
            linked_window.show();
            QEventLoop linked_open_loop;
            QTimer linked_open_timeout;
            linked_open_timeout.setSingleShot(true);
            QObject::connect(&linked_open_timeout, &QTimer::timeout,
                             &linked_open_loop, &QEventLoop::quit);
            bool linked_open_succeeded = false;
            require(linked_window.openProjectPath(
                        linked_project, std::nullopt, std::nullopt,
                        [&linked_open_loop, &linked_open_succeeded](bool succeeded) {
                            linked_open_succeeded = succeeded;
                            linked_open_loop.quit();
                        }),
                    "The MainWindow could not start opening a linked-image project.");
            linked_open_timeout.start(30000);
            linked_open_loop.exec();
            require(linked_open_succeeded && linked_window.media_items_.size() == 1 &&
                        framePixel(linked_window.media_items_.front().first_frame) ==
                            QColor(20, 220, 35, 255) &&
                        linked_window.timeline_model_.clipCount(0) == 2 &&
                        linked_window.timeline_model_.tracks().front().clips[1]
                                .still_image_override != nullptr &&
                        framePixel(*linked_window.timeline_model_.tracks().front()
                                        .clips[1].still_image_override) ==
                            QColor(230, 210, 15, 255),
                    "The MainWindow did not open the saved shared image output.");

            media::VideoMetadata stale_metadata;
            stale_metadata.kind = media::MediaKind::Image;
            stale_metadata.source_path = linked_output;
            stale_metadata.width = 1;
            stale_metadata.height = 1;
            const media::VideoFrame stale_frame{1, 1, 4, {250, 0, 250, 255}};
            linked_window.applyLinkedImageRefresh(
                shared_link, linked_source, {}, true,
                linked_window.project_generation_ - 1, 4,
                std::filesystem::file_time_type{}, stale_metadata,
                stale_frame, {});
            require(framePixel(linked_window.media_items_.front().first_frame) ==
                        QColor(20, 220, 35, 255),
                    "A linked-output result from an old project generation changed the current media.");

            std::error_code revision_error;
            const auto current_output_size = std::filesystem::file_size(
                linked_output, revision_error);
            require(!revision_error && current_output_size > 0,
                    "The linked output revision could not be inspected in the integration test.");
            const auto current_output_modified = std::filesystem::last_write_time(
                linked_output, revision_error);
            require(!revision_error,
                    "The linked output timestamp could not be inspected in the integration test.");
            linked_window.applyLinkedImageRefresh(
                shared_link, linked_source, {}, true,
                linked_window.project_generation_, current_output_size - 1,
                current_output_modified, stale_metadata, stale_frame, {});
            require(framePixel(linked_window.media_items_.front().first_frame) ==
                        QColor(20, 220, 35, 255),
                    "A linked-output result from an obsolete file revision changed the current media.");

            QImage next_output(16, 16, QImage::Format_ARGB32);
            next_output.fill(QColor(25, 40, 235, 255));
            for (int x = 0; x < next_output.width(); ++x) {
                next_output.setPixelColor(x, 15, QColor(x * 10, 245 - x * 8, 100, 255));
            }
            require(writePngAtomically(linked_output, next_output),
                    "The updated linked PNG could not be atomically published.");

            QEventLoop linked_refresh_loop;
            QTimer linked_refresh_timeout;
            linked_refresh_timeout.setSingleShot(true);
            QObject::connect(&linked_refresh_timeout, &QTimer::timeout,
                             &linked_refresh_loop, &QEventLoop::quit);
            QTimer linked_refresh_poll;
            QObject::connect(&linked_refresh_poll, &QTimer::timeout,
                             &linked_refresh_loop, [&]() {
                const bool media_refreshed = !linked_window.media_items_.empty() &&
                    framePixel(linked_window.media_items_.front().first_frame) ==
                        QColor(25, 40, 235, 255);
                const auto preview = linked_window.preview_widget_->grab().toImage();
                const bool preview_refreshed = !preview.isNull() &&
                    preview.pixelColor(preview.width() / 2, preview.height() / 2) ==
                        QColor(25, 40, 235, 255);
                if (media_refreshed && preview_refreshed) linked_refresh_loop.quit();
            });
            linked_refresh_timeout.start(10000);
            linked_refresh_poll.start(20);
            linked_refresh_loop.exec();
            const auto preview = linked_window.preview_widget_->grab().toImage();
            const auto media_color = linked_window.media_items_.empty()
                ? QColor() : framePixel(linked_window.media_items_.front().first_frame);
            const auto preview_color = preview.isNull()
                ? QColor() : preview.pixelColor(preview.width() / 2, preview.height() / 2);
            const auto variant_color = linked_window.timeline_model_.tracks().front()
                    .clips[1].still_image_override == nullptr
                ? QColor()
                : framePixel(*linked_window.timeline_model_.tracks().front()
                                  .clips[1].still_image_override);
            require(!linked_window.media_items_.empty() &&
                        media_color == QColor(25, 40, 235, 255) && !preview.isNull() &&
                        preview_color == QColor(25, 40, 235, 255) &&
                        linked_window.timeline_model_.tracks().front().clips[1]
                                .still_image_override != nullptr &&
                        variant_color == QColor(230, 210, 15, 255),
                    "A shared-image save did not refresh its preview while keeping the clip variant isolated. media=" +
                        media_color.name(QColor::HexArgb).toStdString() + " preview=" +
                        preview_color.name(QColor::HexArgb).toStdString() + " variant=" +
                        variant_color.name(QColor::HexArgb).toStdString());
        }

        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        if (cleanup_error) {
            throw std::runtime_error(
                "The MainWindow integration test could not clean up its temporary directory.");
        }
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "Expected two media fixture paths.\n";
        return 1;
    }

    try {
        MainWindowIntegrationTest::run(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
