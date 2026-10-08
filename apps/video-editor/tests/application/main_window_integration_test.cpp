#include "main_window/main_window.h"
#include "main_window/main_window_support.h"

#include "ui/preview/preview_widget.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "fusion/nodes/ui/node_canvas.h"
#include "ui/workspace/pages/render/render_queue_model.h"
#include "ui/workspace/pages/render/render_queue_controller.h"
#include "ui/workspace/pages/render/render_workspace.h"
#include "project/project_file.h"
#include "settings/user_preferences.h"
#include "settings/settings_dialog.h"
#include "timeline/timeline_geometry.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_browser_bin_tree_widget.h"
#include "ui/media_browser/media_browser_list_widget.h"
#include <creative_suite/effects/effects.h>
#if defined(CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS)
#include "image_document_session.h"
#endif

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QEventLoop>
#include <QDockWidget>
#include <QClipboard>
#include <QLineEdit>
#include <QLabel>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QKeySequence>
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
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QEvent>
#include <QGraphicsScene>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QUrl>
#include <QRunnable>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-main-window-test-" + std::to_string(stamp));
}

void writeLittleEndian(std::ostream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

void writeLittleEndian(std::ostream& output, std::uint32_t value) {
    writeLittleEndian(output, static_cast<std::uint16_t>(value & 0xffffU));
    writeLittleEndian(output, static_cast<std::uint16_t>(value >> 16U));
}

void createWaveformWav(const std::filesystem::path& path) {
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t block_align = channels * 2;
    constexpr std::uint32_t sample_frames = sample_rate;
    constexpr std::uint32_t data_size = sample_frames * block_align;
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not create the waveform WAV fixture.");
    output.write("RIFF", 4);
    writeLittleEndian(output, 36U + data_size);
    output.write("WAVEfmt ", 8);
    writeLittleEndian(output, 16U);
    writeLittleEndian(output, static_cast<std::uint16_t>(1));
    writeLittleEndian(output, channels);
    writeLittleEndian(output, sample_rate);
    writeLittleEndian(output, sample_rate * block_align);
    writeLittleEndian(output, block_align);
    writeLittleEndian(output, static_cast<std::uint16_t>(16));
    output.write("data", 4);
    writeLittleEndian(output, data_size);
    for (std::uint32_t index = 0; index < sample_frames; ++index) {
        const auto sample = static_cast<std::uint16_t>(
            index >= sample_rate / 4 && index < sample_rate / 2 ? 16384 : 0);
        writeLittleEndian(output, sample);
        writeLittleEndian(output, sample);
    }
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
    document.timeline_tracks[0].clips[0].source_duration_frames = 1;
    document.timeline_tracks[1].track_id = 2;
    document.timeline_tracks[1].clips[0].clip_id = 2;
    document.timeline_tracks[1].clips[0].source_duration_frames = 1;
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
    static void runProjectSettingsOnly(
        const std::filesystem::path& first_source,
        const std::filesystem::path& second_source) {
        run(first_source, second_source, true);
    }

    static void run(
        const std::filesystem::path& first_source,
        const std::filesystem::path& second_source,
        bool project_settings_only = false) {
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);

        const auto image_patterns = main_window_detail::stillImageFilePatterns();
        for (const auto& format : QImageReader::supportedImageFormats()) {
            const auto pattern = QStringLiteral("*.") +
                QString::fromLatin1(format).toLower();
            require(image_patterns.contains(pattern),
                    "The Open Media image filter omitted a runtime QImageReader format.");
        }
        if (avcodec_find_decoder(AV_CODEC_ID_WEBP) != nullptr) {
            require(image_patterns.contains(QStringLiteral("*.webp")),
                    "The Open Media image filter omitted the available FFmpeg WebP fallback.");
        }
        if (avcodec_find_decoder(AV_CODEC_ID_TIFF) != nullptr) {
            require(image_patterns.contains(QStringLiteral("*.tif")) &&
                        image_patterns.contains(QStringLiteral("*.tiff")),
                    "The Open Media image filter omitted the available FFmpeg TIFF fallback.");
        }

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
            const auto startup_project = render_window.currentProjectDocument();
            require(startup_project.canvas_width == 1920 &&
                        startup_project.canvas_height == 1080 &&
                        startup_project.timeline_frame_rate ==
                            timeline::FrameRate{30, 1} &&
                        !render_window.project_dirty_,
                    "The editor must open on a clean default 16:9, 30 fps project.");
            const auto& inspector_ui = render_window.edit_workspace_->ui();
            auto* inspector_tabs = inspector_ui.inspector_tabs;
            require(inspector_tabs != nullptr && inspector_tabs->count() == 3 &&
                        inspector_tabs->tabText(0) == QStringLiteral("Inspector") &&
                        inspector_tabs->tabText(1) == QStringLiteral("Audio") &&
                        inspector_tabs->tabText(2) == QStringLiteral("Effects") &&
                        inspector_tabs->widget(0)->isAncestorOf(
                            inspector_ui.transform_spins[0]) &&
                        inspector_tabs->widget(1)->isAncestorOf(inspector_ui.clip_volume) &&
                        inspector_tabs->widget(2)->isAncestorOf(
                            inspector_ui.clip_effects_controls) &&
                        inspector_tabs->widget(2)->isAncestorOf(
                            inspector_ui.effect_selection_hint),
                    "Inspector, Audio, and Effects controls must live on their dedicated tabs.");
            require(!inspector_ui.clip_effects_controls->isEnabled() &&
                        !inspector_ui.effect_selection_hint->isHidden(),
                    "The Effects tab must guide users and disable controls when no clip is selected.");
            require(render_window.copy_attributes_action_ != nullptr &&
                        render_window.paste_attributes_action_ != nullptr &&
                        render_window.copy_attributes_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+C")) &&
                        render_window.paste_attributes_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Shift+V")) &&
                        !render_window.copy_attributes_action_->isEnabled() &&
                        !render_window.paste_attributes_action_->isEnabled() &&
                        !render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.copy_attributes")).isEmpty() &&
                        !render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.paste_attributes")).isEmpty(),
                    "Copy Attributes and Paste Attributes must be registered shortcuts and unavailable before a clip is copied.");
            require(render_window.delete_clip_action_ != nullptr &&
                        render_window.ripple_delete_clip_action_ != nullptr &&
                        render_window.delete_clip_action_->shortcut() ==
                            QKeySequence(Qt::Key_Delete) &&
                        render_window.ripple_delete_clip_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Shift+Delete")) &&
                        !render_window.ripple_delete_clip_action_->isEnabled() &&
                        !render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.ripple_delete_clip")).isEmpty(),
                    "Ripple Delete must be a separate registered command with Shift+Delete and be disabled without a selected clip.");
            require(render_window.shortcut_manager_->setShortcut(
                        QStringLiteral("edit.ripple_delete_clip"),
                        QKeySequence(QStringLiteral("Ctrl+Alt+Delete"))) &&
                        render_window.ripple_delete_clip_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Alt+Delete")) &&
                        render_window.shortcut_manager_->resetShortcut(
                            QStringLiteral("edit.ripple_delete_clip")) &&
                        render_window.ripple_delete_clip_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Shift+Delete")),
                    "Ripple Delete must support shortcut customization and reset to Shift+Delete.");
            require(render_window.shortcut_manager_->setShortcut(
                        QStringLiteral("edit.copy_attributes"),
                        QKeySequence(QStringLiteral("Ctrl+Alt+C"))) &&
                        render_window.copy_attributes_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Alt+C")) &&
                        render_window.shortcut_manager_->resetShortcut(
                            QStringLiteral("edit.copy_attributes")) &&
                        render_window.copy_attributes_action_->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+C")),
                    "Copy Attributes must support shortcut customization and reset to its default.");
            const bool dirty_before_inspector_tab_change = render_window.project_dirty_;
            inspector_tabs->setCurrentIndex(2);
            QApplication::processEvents();
            require(QSettings().value("inspector/active_tab").toInt() == 2 &&
                        render_window.project_dirty_ == dirty_before_inspector_tab_change,
                    "Selecting the Effects tab must persist globally without dirtying the project.");
            auto* full_quality = render_window.findChild<QAction*>(
                "playbackPreviewQualityFull");
            auto* half_quality = render_window.findChild<QAction*>(
                "playbackPreviewQualityHalf");
            auto* quarter_quality = render_window.findChild<QAction*>(
                "playbackPreviewQualityQuarter");
            require(full_quality != nullptr && half_quality != nullptr &&
                        quarter_quality != nullptr && full_quality->isChecked() &&
                        !half_quality->isChecked() && !quarter_quality->isChecked(),
                    "Playback Preview Quality must offer three exclusive options and default to Full.");
            const bool dirty_before_quality_change = render_window.project_dirty_;
            quarter_quality->trigger();
            QApplication::processEvents();
            require(render_window.playback_preview_quality_ ==
                        playback::PreviewQuality::Quarter &&
                        quarter_quality->isChecked() && !full_quality->isChecked() &&
                        QSettings().value("preview/playback_quality").toInt() == 2,
                    "Selecting Quarter must persist and check the selected Playback Preview Quality.");
            require(render_window.project_dirty_ == dirty_before_quality_change,
                    "Changing Playback Preview Quality must not dirty the project.");

            auto* render_settings = render_window.render_workspace_->settingsPanel();
            auto* render_resolution = render_settings != nullptr
                ? render_settings->findChild<QComboBox*>("renderResolutionCombo")
                : nullptr;
            auto* render_frame_rate = render_settings != nullptr
                ? render_settings->findChild<QDoubleSpinBox*>("renderFrameRate")
                : nullptr;
            require(render_resolution != nullptr && render_frame_rate != nullptr,
                    "The Render settings controls were not available to Project Settings.");
            const auto edit_project_settings_from_dialog =
                [&render_window](int canvas_index, int frame_rate_index, bool accept) {
                    bool handled = false;
                    bool choices_valid = false;
                    QTimer::singleShot(0, [&] {
                        auto* dialog = qobject_cast<QDialog*>(
                            QApplication::activeModalWidget());
                        auto* canvas = dialog != nullptr
                            ? dialog->findChild<QComboBox*>(
                                  "projectSettingsCanvasCombo")
                            : nullptr;
                        auto* frame_rate = dialog != nullptr
                            ? dialog->findChild<QComboBox*>(
                                  "projectSettingsFrameRateCombo")
                            : nullptr;
                        auto* buttons = dialog != nullptr
                            ? dialog->findChild<QDialogButtonBox*>()
                            : nullptr;
                        if (dialog == nullptr || canvas == nullptr ||
                            frame_rate == nullptr || buttons == nullptr ||
                            dialog->objectName() !=
                                QStringLiteral("projectSettingsDialog")) {
                            return;
                        }
                        choices_valid = canvas->count() == 2 &&
                            canvas->currentIndex() == 0 &&
                            canvas->itemData(0).toSize() == QSize(1920, 1080) &&
                            canvas->itemData(1).toSize() == QSize(1080, 1920) &&
                            frame_rate->count() == 6 &&
                            frame_rate->currentData().toMap()
                                .value(QStringLiteral("numerator")).toLongLong() == 30 &&
                            frame_rate->currentData().toMap()
                                .value(QStringLiteral("denominator")).toLongLong() == 1;
                        canvas->setCurrentIndex(canvas_index);
                        frame_rate->setCurrentIndex(frame_rate_index);
                        if (accept) {
                            buttons->button(QDialogButtonBox::Ok)->click();
                        } else {
                            dialog->reject();
                        }
                        handled = true;
                    });
                    render_window.project_settings_action_->trigger();
                    return handled && choices_valid;
                };
            require(edit_project_settings_from_dialog(1, 0, false) &&
                        render_window.currentProjectDocument().canvas_width == 1920 &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{30, 1} && !render_window.project_dirty_,
                    "Canceling Project Settings changed the active project.");
            require(edit_project_settings_from_dialog(1, 0, true),
                    "Project Settings did not show its defaults or supported choices.");
            require(render_window.currentProjectDocument().canvas_width == 1080 &&
                        render_window.currentProjectDocument().canvas_height == 1920 &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{24, 1} && render_window.project_dirty_ &&
                        render_resolution->currentText() ==
                            QStringLiteral("Project (1080 × 1920)") &&
                        render_frame_rate->value() == 24.0,
                    "Project Settings did not update the project and Render defaults.");
            const auto project_settings_undo =
                render_window.edit_workspace_->controller()->undo();
            auto* resolution_after_undo = render_settings->findChild<QComboBox*>(
                "renderResolutionCombo");
            auto* frame_rate_after_undo = render_settings->findChild<QDoubleSpinBox*>(
                "renderFrameRate");
            const auto document_after_undo = render_window.currentProjectDocument();
            require(project_settings_undo.changed() &&
                        document_after_undo.canvas_width == 1920 &&
                        document_after_undo.canvas_height == 1080 &&
                        document_after_undo.timeline_frame_rate ==
                            timeline::FrameRate{30, 1} && !render_window.project_dirty_ &&
                        resolution_after_undo != nullptr &&
                        resolution_after_undo->currentText() ==
                            QStringLiteral("Project (1920 × 1080)") &&
                        frame_rate_after_undo != nullptr &&
                        frame_rate_after_undo->value() == 30.0,
                    "Undo did not restore Project Settings and Render defaults.");
            require(render_window.edit_workspace_->controller()->redo().changed() &&
                        render_window.currentProjectDocument().canvas_width == 1080 &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{24, 1},
                    "Redo did not reapply Project Settings.");
            require(render_window.edit_workspace_->controller()->undo().changed() &&
                        !render_window.project_dirty_,
                    "Undo could not return Project Settings to the saved baseline.");

            const auto unusual_rate_change = render_window.timeline_command_service_.execute(
                application::SetProjectSettingsCommand{1920, 1080, {30000, 1001}});
            require(unusual_rate_change.changed(),
                    "The Project Settings UI test could not prepare a nonstandard current rate.");
            render_window.applyTimelineEditResult(unusual_rate_change, true);
            render_window.updateTimelineState();
            render_window.refreshPlaybackComposition();
            bool unusual_rate_option_valid = false;
            QTimer::singleShot(0, [&] {
                auto* dialog = qobject_cast<QDialog*>(
                    QApplication::activeModalWidget());
                auto* frame_rate = dialog != nullptr
                    ? dialog->findChild<QComboBox*>(
                          "projectSettingsFrameRateCombo")
                    : nullptr;
                if (dialog == nullptr || frame_rate == nullptr) return;
                const auto value = frame_rate->currentData().toMap();
                unusual_rate_option_valid = frame_rate->count() == 7 &&
                    frame_rate->currentIndex() == 6 &&
                    value.value(QStringLiteral("numerator")).toLongLong() == 30000 &&
                    value.value(QStringLiteral("denominator")).toLongLong() == 1001;
                dialog->reject();
            });
            render_window.project_settings_action_->trigger();
            require(unusual_rate_option_valid &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{30000, 1001},
                    "Project Settings did not preserve a valid nonstandard current rate in its choices.");
            require(render_window.edit_workspace_->controller()->undo().changed() &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{30, 1} && !render_window.project_dirty_,
                    "Undo did not restore the nonstandard-rate test fixture.");
            if (project_settings_only) return;

            bool gpu_ui_forwarded = false;
            QTimer::singleShot(0, [&] {
                auto* dialog = qobject_cast<settings::SettingsDialog*>(QApplication::activeModalWidget());
                auto* checkbox = dialog ? dialog->findChild<QCheckBox*>("gpuCompositionCheckBox") : nullptr;
                if (checkbox) {
                    checkbox->setChecked(true);
                    gpu_ui_forwarded = settings::gpuCompositionEnabled() &&
                        render_window.playback_controller_->gpuCompositionEnabled();
                }
                if (dialog) dialog->accept();
            });
            render_window.showSettingsDialog();
            require(gpu_ui_forwarded && render_window.project_dirty_ == dirty_before_quality_change,
                    "Settings GPU checkbox did not reach controller or modified project.");

            const auto create_project_from_dialog =
                [&render_window](int canvas_index, int frame_rate_index, bool accept) {
                    bool handled = false;
                    bool choices_valid = false;
                    QTimer::singleShot(0, [&] {
                        auto* dialog = qobject_cast<QDialog*>(
                            QApplication::activeModalWidget());
                        auto* canvas = dialog != nullptr
                            ? dialog->findChild<QComboBox*>("newProjectCanvasCombo")
                            : nullptr;
                        auto* frame_rate = dialog != nullptr
                            ? dialog->findChild<QComboBox*>("newProjectFrameRateCombo")
                            : nullptr;
                        auto* buttons = dialog != nullptr
                            ? dialog->findChild<QDialogButtonBox*>()
                            : nullptr;
                        if (canvas == nullptr || frame_rate == nullptr ||
                            buttons == nullptr) {
                            return;
                        }
                        constexpr std::array<int, 6> expected_rates{
                            24, 25, 30, 48, 50, 60};
                        choices_valid = canvas->count() == 2 &&
                            canvas->currentIndex() == 0 &&
                            canvas->itemData(0).toSize() == QSize(1920, 1080) &&
                            canvas->itemData(1).toSize() == QSize(1080, 1920) &&
                            frame_rate->count() == static_cast<int>(expected_rates.size()) &&
                            frame_rate->currentData().toInt() == 30;
                        for (std::size_t index = 0; choices_valid &&
                             index < expected_rates.size(); ++index) {
                            choices_valid = frame_rate->itemData(
                                static_cast<int>(index)).toInt() == expected_rates[index];
                        }
                        if (canvas_index >= 0) canvas->setCurrentIndex(canvas_index);
                        if (frame_rate_index >= 0) {
                            frame_rate->setCurrentIndex(frame_rate_index);
                        }
                        if (accept) {
                            buttons->button(QDialogButtonBox::Ok)->click();
                        } else {
                            dialog->reject();
                        }
                        handled = true;
                    });
                    render_window.new_project_action_->trigger();
                    return handled && choices_valid;
                };
            require(create_project_from_dialog(1, 5, true),
                    "The project-creation dialog did not expose its canvas and frame-rate choices.");
            auto created_portrait = render_window.currentProjectDocument();
            require(created_portrait.canvas_width == 1080 &&
                        created_portrait.canvas_height == 1920 &&
                        created_portrait.timeline_frame_rate == timeline::FrameRate{60, 1} &&
                        !render_window.project_dirty_,
                    "Creating a portrait 60 fps project did not apply clean project settings.");
            require(render_resolution != nullptr &&
                        render_resolution->currentText() ==
                            QStringLiteral("Project (1080 × 1920)") &&
                        render_frame_rate != nullptr &&
                        render_frame_rate->value() == 60.0,
                    "Render defaults did not follow the active portrait canvas and project FPS.");
            require(create_project_from_dialog(-1, -1, false),
                    "Canceling project creation did not close its dialog.");
            require(render_window.currentProjectDocument().canvas_width == 1080 &&
                        render_window.currentProjectDocument().canvas_height == 1920 &&
                        render_window.editor_session_.timeline().frameRate() ==
                            timeline::FrameRate{60, 1},
                    "Canceling project creation changed the active project.");
            require(create_project_from_dialog(-1, -1, true),
                    "Creating a project with the default choices failed.");
            const auto created_default = render_window.currentProjectDocument();
            require(created_default.canvas_width == 1920 &&
                        created_default.canvas_height == 1080 &&
                        created_default.timeline_frame_rate == timeline::FrameRate{30, 1} &&
                        !render_window.project_dirty_ &&
                        render_resolution->currentText() ==
                            QStringLiteral("Project (1920 × 1080)") &&
                        render_frame_rate->value() == 30.0,
                    "The New Project defaults or clean-state behavior changed.");
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
            require(reopened_window.edit_workspace_->ui().inspector_tabs->currentIndex() == 2,
                    "The Effects Inspector tab must be restored after reopening the application.");
            auto* reopened_quarter_quality = reopened_window.findChild<QAction*>(
                "playbackPreviewQualityQuarter");
            auto* reopened_full_quality = reopened_window.findChild<QAction*>(
                "playbackPreviewQualityFull");
            require(reopened_window.playback_preview_quality_ ==
                        playback::PreviewQuality::Quarter &&
                        reopened_quarter_quality != nullptr &&
                        reopened_quarter_quality->isChecked() &&
                        reopened_full_quality != nullptr,
                    "Playback Preview Quality must persist across application restarts.");
            reopened_full_quality->trigger();
            require(reopened_window.playback_controller_->gpuCompositionEnabled(),
                    "Persisted GPU preference was not applied at startup.");
            settings::setGpuCompositionEnabled(false);
            reopened_window.playback_controller_->setGpuCompositionEnabled(false);
            QApplication::processEvents();
            require(QSettings().value("preview/playback_quality").toInt() == 0,
                    "The Full Playback Preview Quality selection was not persisted.");
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
                        window.timeline_model_.trackCount() == 2 &&
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
            const auto& inspector_ui = window.edit_workspace_->ui();
            const auto& first_track = window.editor_session_.timeline().tracks().front();
            require(!first_track.clips.empty(),
                    "The Inspector tab test requires the opened project to contain a video clip.");
            window.edit_workspace_->controller()->handleTimelineClipSelectionChanged(
                first_track.track_id, first_track.clips.front().clip_id);
            QApplication::processEvents();
            require(inspector_ui.inspector_tabs->currentIndex() == 2 &&
                        inspector_ui.clip_effects_controls->isEnabled() &&
                        inspector_ui.effect_selection_hint->isHidden(),
                    "Selecting a compatible clip must enable Effects without changing the active tab.");

            auto* edit_controller = window.edit_workspace_->controller();
            const auto selected_clip_id = first_track.clips.front().clip_id;
            auto* timeline_widget = window.edit_workspace_->ui().timeline;
            const auto fusion_target_location =
                window.editor_session_.timeline().locateClip(selected_clip_id);
            require(timeline_widget != nullptr && fusion_target_location.has_value(),
                    "The Fusion context-menu test could not locate the selected Timeline clip.");
            const auto& fusion_target_track = window.editor_session_.timeline().tracks()[
                fusion_target_location->track_index];
            const auto& fusion_target_clip = fusion_target_track.clips[
                fusion_target_location->clip_index];
            const timeline::TimelineGeometry fusion_context_geometry(
                window.editor_session_.timeline().tracks(),
                QSizeF(timeline_widget->size()), timeline_widget->trackRowHeight(),
                timeline_widget->zoomFactor(), std::nullopt, 30.0);
            const auto fusion_context_position = fusion_context_geometry.clipRect(
                fusion_target_clip, fusion_target_location->track_index).center().toPoint();
            bool fusion_context_action_found = false;
            QTimer::singleShot(0, [timeline_widget, &fusion_context_action_found]() {
                auto* menu = timeline_widget->findChild<QMenu*>();
                if (menu == nullptr) return;
                for (auto* action : menu->actions()) {
                    if (action->text() == QStringLiteral("Open in Fusion")) {
                        fusion_context_action_found = true;
                        action->trigger();
                        break;
                    }
                }
                menu->close();
            });
            QMouseEvent open_fusion_mouse_event(
                QEvent::MouseButtonPress,
                QPointF(fusion_context_position),
                QPointF(fusion_context_position),
                QPointF(timeline_widget->mapToGlobal(fusion_context_position)),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier,
                Qt::MouseEventNotSynthesized,
                QPointingDevice::primaryPointingDevice());
            QApplication::sendEvent(timeline_widget, &open_fusion_mouse_event);
            QApplication::processEvents();
            auto* fusion_canvas = window.fusion_workspace_->nodeEditorPanel()
                ->findChild<fusion::nodes::NodeCanvas*>("fusionNodeCanvas");
            require(fusion_context_action_found &&
                        window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.editor_session_.selection().active_clip_id ==
                            selected_clip_id && fusion_canvas != nullptr &&
                        fusion_canvas->isEnabled() && fusion_canvas->scene() != nullptr &&
                        fusion_canvas->scene()->items().size() >= 2,
                    "Timeline Open in Fusion did not open the selected clip's node graph.");
            window.setWorkspacePage(ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() == ui::WorkspacePageId::Edit,
                    "The integration test could not restore the Edit workspace after Fusion routing.");
            edit_controller->addEffectToSelectedClip(QStringLiteral("video.grayscale"));
            auto* effect_item = inspector_ui.clip_effects_list->item(0);
            require(effect_item != nullptr &&
                        effect_item->flags().testFlag(Qt::ItemIsUserCheckable) &&
                        effect_item->checkState() == Qt::Checked,
                    "The Effects list must show a checked state control for each new filter.");
            effect_item->setCheckState(Qt::Unchecked);
            QApplication::processEvents();
            auto selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        !window.timeline_model_.tracks()[selected_clip_location->track_index]
                             .clips[selected_clip_location->clip_index].effects.front().enabled &&
                        inspector_ui.clip_effect_parameter_value->isEnabled(),
                    "Unchecking a filter must disable processing while keeping its parameters editable.");
            edit_controller->applySelectedClipEffectParameter(37.0);
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        !window.timeline_model_.tracks()[selected_clip_location->track_index]
                             .clips[selected_clip_location->clip_index].effects.front().enabled &&
                        creative_suite::effects::parameterValue(
                            window.timeline_model_.tracks()[selected_clip_location->track_index]
                                .clips[selected_clip_location->clip_index].effects.front(),
                            "amount") == 37.0,
                    "A disabled filter's parameters must remain editable without re-enabling it.");
            require(edit_controller->undo().changed() && edit_controller->undo().changed() &&
                        edit_controller->undo().changed(),
                    "Effect parameter, checkbox, and insertion actions must each be undoable.");
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        window.timeline_model_.tracks()[selected_clip_location->track_index]
                            .clips[selected_clip_location->clip_index].effects.empty() &&
                        !window.project_dirty_,
                    "Undoing the Effects UI test must restore the clean project state.");

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
                            window.workspace_host_->timelinePanel() &&
                        window.fusion_workspace_ != nullptr &&
                        window.workspace_host_->nodeEditorPanel() ==
                            window.fusion_workspace_->nodeEditorPanel() &&
                        window.workspace_host_->fusionInspectorPage() ==
                            window.fusion_workspace_->inspectorPanel() &&
                        window.workspace_host_->findChild<QWidget*>(
                            "workspaceViewerTitle") ==
                            window.fusion_workspace_->viewerTitle() &&
                        window.render_workspace_ != nullptr &&
                        window.workspace_host_->renderPage() ==
                            window.render_workspace_->centralPage(),
                    "The MainWindow must start with Edit selected and expose all workspace selectors.");
            const auto edit_dock_visibility = dock_visibility();
            window.editor_session_.setPlayheadFrame(11);
            const auto history_probe =
                window.edit_workspace_->controller()->addTrack(
                    "Workspace history probe");
            require(history_probe.changed() &&
                        window.edit_workspace_->controller()->undo().changed() &&
                        window.edit_workspace_->controller()->canRedo() &&
                        !window.project_dirty_,
                    "The workspace preservation check could not prepare a clean redo history state.");
            const auto selection_before_workspace_switch =
                window.editor_session_.selection();
            const auto playhead_before_workspace_switch =
                window.editor_session_.playheadFrame();
            const auto can_undo_before_workspace_switch =
                window.edit_workspace_->controller()->canUndo();
            const auto can_redo_before_workspace_switch =
                window.edit_workspace_->controller()->canRedo();
            const auto playback_controller_before_workspace_switch =
                window.playback_controller_.get();
            const auto playback_active_before_workspace_switch =
                window.playback_is_playing_;
            const auto playback_worker_active_before_workspace_switch =
                window.playback_controller_ != nullptr &&
                window.playback_controller_->isPlaying();
            const auto project_dirty_before_workspace_switch =
                window.project_dirty_;
            const auto require_workspace_state_unchanged = [&window,
                &selection_before_workspace_switch,
                playhead_before_workspace_switch,
                can_undo_before_workspace_switch,
                can_redo_before_workspace_switch,
                playback_controller_before_workspace_switch,
                playback_active_before_workspace_switch,
                playback_worker_active_before_workspace_switch,
                project_dirty_before_workspace_switch]() {
                const auto& selection_after = window.editor_session_.selection();
                require(
                    selection_after.active_track_id ==
                            selection_before_workspace_switch.active_track_id &&
                        selection_after.active_clip_id ==
                            selection_before_workspace_switch.active_clip_id &&
                        selection_after.selected_source_path ==
                            selection_before_workspace_switch.selected_source_path &&
                        selection_after.active_transition.has_value() ==
                            selection_before_workspace_switch.active_transition.has_value() &&
                        window.editor_session_.playheadFrame() ==
                            playhead_before_workspace_switch &&
                        window.edit_workspace_->controller()->canUndo() ==
                            can_undo_before_workspace_switch &&
                        window.edit_workspace_->controller()->canRedo() ==
                            can_redo_before_workspace_switch &&
                        window.playback_controller_.get() ==
                            playback_controller_before_workspace_switch &&
                        window.playback_is_playing_ ==
                            playback_active_before_workspace_switch &&
                        window.playback_controller_ != nullptr &&
                        window.playback_controller_->isPlaying() ==
                            playback_worker_active_before_workspace_switch &&
                        window.project_dirty_ == project_dirty_before_workspace_switch,
                    "Switching workspaces must preserve selection, playhead, playback, history, and dirty state.");
            };
            window.setWorkspacePage(ui::WorkspacePageId::Fusion);
            require_workspace_state_unchanged();
            window.setWorkspacePage(ui::WorkspacePageId::Render);
            require_workspace_state_unchanged();
            QApplication::processEvents();
            require(window.render_workspace_button_->isChecked() &&
                        !window.edit_workspace_button_->isChecked() &&
                        !window.fusion_workspace_button_->isChecked(),
                    "The MainWindow must select only Render.");
            require(window.workspace_host_->renderPage()->isVisible() &&
                        window.preview_widget_->isVisible() &&
                        window.render_workspace_->previewWidget() ==
                            window.preview_widget_,
                    "Render must show its settings, shared Preview, and queue page.");
            auto* render_output_path = window.render_workspace_->centralPage()
                ->findChild<QLineEdit*>("renderOutputPath");
            auto* add_render_job = window.render_workspace_->centralPage()
                ->findChild<QPushButton*>("renderAddToQueueButton");
            auto* gpu_export = window.render_workspace_->centralPage()->findChild<QCheckBox*>("renderGpuCompositionCheck");
            require(gpu_export && !gpu_export->isChecked() && !gpu_export->accessibleDescription().isEmpty(),
                "Render GPU option must default off and describe fallback accessibly.");
            gpu_export->setChecked(true);
            require(window.workspace_host_->renderPage()->findChild<QWidget*>(
                        "renderSettingsPanel") != nullptr &&
                        window.workspace_host_->renderPage()->findChild<QWidget*>(
                            "renderPreviewPanel") != nullptr &&
                        window.workspace_host_->renderPage()->findChild<QWidget*>(
                            "renderQueuePanel") != nullptr &&
                        render_output_path != nullptr && add_render_job != nullptr,
                    "Render must expose settings, the shared Preview, and queue from left to right.");
            render_output_path->setText(QString::fromStdString(
                (directory / "queued-render.mp4").string()));
            const auto project_before_queue_add = window.currentProjectDocument();
            require(add_render_job->isEnabled(),
                    "A valid Render configuration must be addable to the queue.");
            add_render_job->click();
            const auto* queued_render_job = window.render_workspace_->queueModel()->jobAt(0);
            require(window.render_workspace_->queueModel()->jobCount() == 1 &&
                        queued_render_job != nullptr &&
                        queued_render_job->project_snapshot == project_before_queue_add &&
                        queued_render_job->settings.gpu_composition_enabled &&
                        !window.project_dirty_,
                    "Preparing a queued Render job must snapshot the project without marking it dirty.");
            gpu_export->setChecked(false);
            require(queued_render_job->settings.gpu_composition_enabled && !window.project_dirty_ &&
                window.currentProjectDocument() == project_before_queue_add,
                "Changing the GPU export option mutated the prepared job or project.");
            auto* export_controller = window.render_workspace_->findChild<ui::RenderQueueController*>();
            auto* gpu_warning = window.render_workspace_->centralPage()->findChild<QLabel*>("renderGpuWarning");
            require(export_controller && gpu_warning && gpu_warning->isHidden(), "GPU warning must start hidden.");
            export_controller->jobWarning(queued_render_job->id, "Export is continuing with CPU fallback.", "-37");
            require(!gpu_warning->isHidden() && gpu_warning->text().contains("CPU fallback") &&
                QApplication::activeModalWidget() == nullptr, "Export fallback must show a nonmodal warning.");
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Render &&
                        window.workspace_host_->lowerWorkspacePanel()->currentWidget() ==
                            window.workspace_host_->timelinePanel() &&
                        window.timeline_dock_->windowTitle() == "Timeline" &&
                        window.edit_workspace_->ui().timeline->isReadOnly() &&
                        window.edit_workspace_->ui().timeline_controls->isHidden() &&
                        window.edit_workspace_->ui().timeline_footer->isHidden(),
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
            require_workspace_state_unchanged();
            require_dock_visibility(
                edit_dock_visibility,
                "Returning to Fusion must restore the dock visibility from before Render.");
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.workspace_host_->previewWidget()->isVisible() &&
                        window.render_workspace_->previewWidget() == nullptr,
                    "Returning to Fusion must restore its Preview.");
            require(window.timeline_dock_->windowTitle() == "Node Editor",
                    "Returning to Fusion must restore the Node Editor title.");
            require(!window.edit_workspace_->ui().timeline->isReadOnly() &&
                        !window.edit_workspace_->ui().timeline_controls->isHidden() &&
                        !window.edit_workspace_->ui().timeline_footer->isHidden(),
                    "Returning to Fusion must restore Timeline interaction and controls.");

            window.media_dock_->hide();
            window.effects_dock_->show();
            QApplication::processEvents();
            const auto mixed_dock_visibility = dock_visibility();
            window.setWorkspacePage(ui::WorkspacePageId::Render);
            window.setWorkspacePage(ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require_workspace_state_unchanged();
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
            require(!window.edit_workspace_->ui().timeline->isReadOnly() &&
                        !window.edit_workspace_->ui().timeline_controls->isHidden() &&
                        !window.edit_workspace_->ui().timeline_footer->isHidden() &&
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
            require(window.playback_controller_ != nullptr,
                    "The MainWindow playback controller was unavailable.");
            bool activation_terminal = false;
            bool activation_committed = false;
            std::string activation_observed_events;
            std::string activation_outcome = "no terminal activation event";
            const auto activation_wait_started = std::chrono::steady_clock::now();
            window.playback_controller_->setEventHandler(
                [&](const playback::PlaybackControllerEvent& event) {
                    if (!activation_observed_events.empty()) activation_observed_events += ",";
                    activation_observed_events += std::to_string(event.index());
                    if (const auto* activation =
                            std::get_if<playback::PlaybackActivationEvent>(&event);
                        activation != nullptr && activation->clip_id == 1 &&
                        activation->phase != playback::PlaybackActivationPhase::Pending) {
                        activation_terminal = true;
                        activation_committed = activation->phase ==
                            playback::PlaybackActivationPhase::Committed;
                        activation_outcome = activation_committed
                            ? "committed"
                            : "discarded";
                    } else if (const auto* error =
                                   std::get_if<playback::PlaybackErrorEvent>(&event);
                               error != nullptr &&
                               error->activation_clip_id == std::optional<timeline::ClipId>{1}) {
                        activation_terminal = true;
                        activation_outcome =
                            "worker error: " + error->message.toStdString();
                    }
                    window.handlePlaybackEvent(event);
                });
            require(window.playback_controller_->activateClip(1, 0, false) ==
                            playback::PlaybackCommandResult::Pending,
                    "The playback controller did not accept a stable-identity activation.");
            const auto activation_deadline = activation_wait_started +
                std::chrono::seconds(30);
            while (!activation_terminal &&
                   std::chrono::steady_clock::now() < activation_deadline) {
                QApplication::processEvents(QEventLoop::AllEvents, 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            window.playback_controller_->setEventHandler(
                [&window](const playback::PlaybackControllerEvent& event) {
                    window.handlePlaybackEvent(event);
                });
            const auto activation_wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - activation_wait_started).count();
            if (!activation_terminal) {
                activation_outcome += " after " + std::to_string(activation_wait_ms) +
                    "; events=" + activation_observed_events;
            }
            require(window.playback_controller_ != nullptr &&
                        activation_terminal && activation_committed &&
                        !window.playback_activation_loading_,
                    "The controller activation did not commit; terminal result: " +
                        activation_outcome);
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

            const auto drop_bin = window.media_controller_.createBin("DropTarget");
            require(drop_bin.changed(),
                    "The external-drop integration test could not create its destination bin.");
            window.populateMediaBrowser();
            window.edit_workspace_->controller()->clearTimeline();
            window.timeline_command_service_.clearHistory();
            const auto video_track = std::find_if(
                window.timeline_model_.tracks().begin(),
                window.timeline_model_.tracks().end(),
                [](const timeline::TimelineTrack& track) {
                    return track.kind == timeline::TrackKind::Video;
                });
            require(video_track != window.timeline_model_.tracks().end(),
                    "The external-drop integration test has no video track.");
            const auto video_track_id = video_track->track_id;

            const auto waitForDropImport = [&window](const char* failure_message) {
                QEventLoop loop;
                QTimer timeout;
                timeout.setSingleShot(true);
                QTimer poll;
                QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
                    if (!window.active_media_import_cancel_) loop.quit();
                });
                timeout.start(30000);
                poll.start(10);
                loop.exec();
                require(!window.active_media_import_cancel_, failure_message);
            };
            const auto sendFileDrop = [](QWidget* viewport,
                                         const QPoint& position,
                                         QMimeData* mime_data) {
                QDragEnterEvent enter(
                    position, Qt::CopyAction, mime_data,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &enter);
                QDragMoveEvent move(
                    position, Qt::CopyAction, mime_data,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &move);
                QDropEvent drop(
                    QPointF(position), Qt::CopyAction, mime_data,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &drop);
                return enter.isAccepted() && move.isAccepted() && drop.isAccepted();
            };
            const auto first_source_qt = QString::fromStdWString(first_source.wstring());
            const auto second_source_qt = QString::fromStdWString(second_source.wstring());
            QListWidgetItem* drop_target_item = nullptr;
            for (int row = 0; row < window.media_list_->count(); ++row) {
                auto* item = window.media_list_->item(row);
                if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                        media_browser_ui::kMediaItemTypeBin &&
                    item->data(media_browser_ui::kMediaBinPathRole).toString() ==
                        QStringLiteral("DropTarget")) {
                    drop_target_item = item;
                    break;
                }
            }
            require(drop_target_item != nullptr,
                    "The external-drop integration test could not find its target bin in the list.");
            window.media_list_->scrollToItem(drop_target_item);
            QApplication::processEvents();
            const auto list_drop_position =
                window.media_list_->visualItemRect(drop_target_item).center();
            QMimeData browser_drop_mime;
            browser_drop_mime.setUrls({QUrl::fromLocalFile(first_source_qt)});
            require(sendFileDrop(
                        window.media_list_->viewport(),
                        list_drop_position,
                        &browser_drop_mime),
                    "The Media Browser viewport did not accept the operating-system drop.");
            waitForDropImport(
                "The Media Browser did not complete an operating-system file drop.");
            const auto first_media_index =
                window.media_controller_.library().indexForPath(first_source);
            require(first_media_index != window.media_controller_.library().size() &&
                        window.media_controller_.library().items()[first_media_index].bin_path ==
                            "DropTarget",
                    "A file dropped on the Media Browser did not use its target bin.");

            const auto fallback_source = directory /
                ("unassigned-" + first_source.filename().string());
            std::filesystem::copy_file(first_source, fallback_source);
            require(window.bin_tree_ != nullptr &&
                        window.bin_tree_->topLevelItemCount() > 0,
                    "The external-drop integration test could not find All Media.");
            window.bin_tree_->setCurrentItem(window.bin_tree_->topLevelItem(0));
            QApplication::processEvents();
            QListWidgetItem* media_drop_target = nullptr;
            for (int row = 0; row < window.media_list_->count(); ++row) {
                auto* item = window.media_list_->item(row);
                if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                    media_browser_ui::kMediaItemTypeMedia) {
                    media_drop_target = item;
                    break;
                }
            }
            require(media_drop_target != nullptr,
                    "All Media did not show an item for the fallback drop test.");
            window.media_list_->scrollToItem(media_drop_target);
            QApplication::processEvents();
            const auto fallback_drop_position =
                window.media_list_->visualItemRect(media_drop_target).center();
            const auto fallback_source_qt =
                QString::fromStdWString(fallback_source.wstring());
            QMimeData fallback_drop_mime;
            fallback_drop_mime.setUrls({QUrl::fromLocalFile(fallback_source_qt)});
            int stale_bin_warning_count = 0;
            QTimer dismiss_stale_bin_warning;
            QObject::connect(&dismiss_stale_bin_warning, &QTimer::timeout, [&]() {
                auto* message = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget());
                if (message != nullptr &&
                    message->windowTitle() ==
                        QStringLiteral("Some media could not be imported")) {
                    if (message->text().contains(
                            QStringLiteral("The destination bin no longer exists"))) {
                        ++stale_bin_warning_count;
                    }
                    message->accept();
                }
            });
            dismiss_stale_bin_warning.start(10);
            require(sendFileDrop(
                        window.media_list_->viewport(),
                        fallback_drop_position,
                        &fallback_drop_mime),
                    "The Media Browser did not accept a drop without a bin target.");
            waitForDropImport(
                "The Media Browser did not complete a drop without a bin target.");
            dismiss_stale_bin_warning.stop();
            const auto fallback_media_index =
                window.media_controller_.library().indexForPath(fallback_source);
            require(fallback_media_index != window.media_controller_.library().size() &&
                        window.media_controller_.library().items()[fallback_media_index]
                                .bin_path == "Unsorted" &&
                        stale_bin_warning_count == 0,
                    "A drop without a concrete bin must fall back to Unsorted without a stale-bin warning.");

            auto& edit_ui = window.edit_workspace_->ui();
            const auto track_location =
                window.timeline_model_.locateTrack(video_track_id);
            require(track_location.has_value() && edit_ui.timeline != nullptr &&
                        edit_ui.timeline_scroll != nullptr,
                    "The external-drop integration test could not find the production Timeline viewport.");
            const timeline::TimelineGeometry timeline_geometry(
                window.timeline_model_.tracks(),
                QSizeF(edit_ui.timeline->size()),
                edit_ui.timeline->trackRowHeight(),
                edit_ui.timeline->zoomFactor());
            const auto timeline_local_position = QPoint(
                360,
                static_cast<int>(timeline_geometry.trackRect(
                    *track_location).center().y()));
            const auto expected_timeline_frame =
                edit_ui.timeline->frameAtContentX(timeline_local_position.x());
            require(expected_timeline_frame.has_value(),
                    "The Timeline integration drop point did not map to a frame.");
            const auto timeline_drop_position = edit_ui.timeline->mapTo(
                edit_ui.timeline_scroll->viewport(),
                timeline_local_position);
            QMimeData timeline_drop_mime;
            timeline_drop_mime.setUrls({
                QUrl::fromLocalFile(first_source_qt),
                QUrl::fromLocalFile(second_source_qt)});
            require(sendFileDrop(
                        edit_ui.timeline_scroll->viewport(),
                        timeline_drop_position,
                        &timeline_drop_mime),
                    "The Timeline scroll viewport did not route the operating-system drop.");
            waitForDropImport(
                "The Timeline did not complete an operating-system file drop.");
            const auto placed_track_index = window.timeline_model_.locateTrack(
                video_track_id);
            require(placed_track_index.has_value(),
                    "The Timeline drop target disappeared during import.");
            const auto& placed_clips = window.timeline_model_.tracks()[
                *placed_track_index].clips;
            require(placed_clips.size() == 2,
                    "External Timeline drop did not insert two video clips.");
            require(placed_clips[0].source_path ==
                            media::MediaLibrary::canonicalPath(first_source) &&
                        placed_clips[1].source_path ==
                            media::MediaLibrary::canonicalPath(second_source),
                    "External Timeline drop did not preserve file order.");
            require(placed_clips[0].timeline_start_frame ==
                            *expected_timeline_frame,
                    "External Timeline drop did not preserve its insertion frame: expected " +
                        std::to_string(*expected_timeline_frame) + ", got " +
                        std::to_string(placed_clips[0].timeline_start_frame) + ".");
            require(placed_clips[1].timeline_start_frame ==
                        placed_clips[0].timeline_start_frame +
                            placed_clips[0].timeline_duration_frames,
                    "External Timeline batch clips were not placed sequentially.");
            require(window.timeline_command_service_.undoCount() == 1,
                    "External Timeline batch did not create one undo step.");
            static_cast<void>(window.edit_workspace_->controller()->undo());
            const auto emptied_track_index = window.timeline_model_.locateTrack(
                video_track_id);
            require(emptied_track_index.has_value() &&
                        window.timeline_model_.tracks()[*emptied_track_index].clips.empty() &&
                        window.media_controller_.library().contains(first_source) &&
                        window.media_controller_.library().contains(second_source),
                    "Undo of a dropped Timeline batch removed imported media or left partial clips.");
        }

        const auto waveform_source = directory / "waveform-background.wav";
        createWaveformWav(waveform_source);
        {
            MainWindow waveform_window;
            waveform_window.show();
            QApplication::processEvents();
            media::VideoMetadata metadata;
            metadata.kind = media::MediaKind::Audio;
            metadata.source_path = waveform_source;
            metadata.display_name = "Waveform background";
            metadata.duration_seconds = 1.0;
            metadata.audio = media::AudioMetadata{
                "pcm_s16le", 8000, 2, 1.0};
            require(waveform_window.media_controller_.commitImported({
                        metadata, {}, metadata.display_name, "Unsorted", false}).changed(),
                    "The waveform integration test could not register its audio source.");
            const auto audio_track = std::find_if(
                waveform_window.timeline_model_.tracks().begin(),
                waveform_window.timeline_model_.tracks().end(),
                [](const timeline::TimelineTrack& track) {
                    return track.kind == timeline::TrackKind::Audio;
                });
            require(audio_track != waveform_window.timeline_model_.tracks().end(),
                    "A new project did not provide an Audio track for the waveform test.");
            require(waveform_window.timeline_command_service_.execute(
                        application::AddMediaClipCommand{
                            waveform_source, audio_track->track_id, 0}).changed(),
                    "The waveform integration test could not add its clip.");

            const auto canonical_waveform_source = media::MediaLibrary::canonicalPath(
                waveform_source);
            const auto signature = media::audioWaveformSourceSignature(
                canonical_waveform_source);
            require(signature.has_value(),
                    "The waveform integration fixture has no file signature.");
            std::atomic_bool request_blocker_entered = false;
            std::atomic_bool release_request_blocker = false;
            waveform_window.media_task_pool_.start(QRunnable::create([&]() {
                request_blocker_entered.store(true, std::memory_order_release);
                while (!release_request_blocker.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }));
            for (int attempt = 0; attempt < 2000 &&
                 !request_blocker_entered.load(std::memory_order_acquire); ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool request_worker_blocked =
                request_blocker_entered.load(std::memory_order_acquire);
            waveform_window.updateTimelineState();
            const bool waveform_request_queued =
                waveform_window.pending_audio_waveforms_.contains(
                    canonical_waveform_source);
            const auto dirty_before_waveform = waveform_window.project_dirty_;

            bool event_loop_responsive = false;
            QTimer::singleShot(0, [&event_loop_responsive, &release_request_blocker]() {
                event_loop_responsive = true;
                release_request_blocker.store(true, std::memory_order_release);
            });
            QEventLoop waveform_loop;
            QTimer waveform_poll;
            waveform_poll.setInterval(5);
            QObject::connect(&waveform_poll, &QTimer::timeout, &waveform_loop, [&]() {
                if (waveform_window.audio_waveform_cache_.find(
                        canonical_waveform_source, *signature)) {
                    waveform_loop.quit();
                }
            });
            QTimer waveform_timeout;
            waveform_timeout.setSingleShot(true);
            QObject::connect(&waveform_timeout, &QTimer::timeout, &waveform_loop, [&]() {
                release_request_blocker.store(true, std::memory_order_release);
                waveform_loop.quit();
            });
            waveform_poll.start();
            waveform_timeout.start(10000);
            waveform_loop.exec();
            release_request_blocker.store(true, std::memory_order_release);
            waveform_window.media_task_pool_.waitForDone();
            const auto cached_waveform = waveform_window.audio_waveform_cache_.find(
                canonical_waveform_source, *signature);
            require(request_worker_blocked && waveform_request_queued &&
                        event_loop_responsive && cached_waveform != nullptr,
                    "The waveform did not arrive asynchronously while the UI remained responsive.");
            require(waveform_window.project_dirty_ == dirty_before_waveform,
                    "Generating a waveform changed the project dirty state.");

            const auto dirty_before_waveform_mode = waveform_window.project_dirty_;
            bool stereo_mode_applied = false;
            bool mono_mode_applied = false;
            auto* waveform_timeline = waveform_window.editUi().timeline;
            QTimer::singleShot(0, [&]() {
                auto* settings_dialog = qobject_cast<settings::SettingsDialog*>(
                    QApplication::activeModalWidget());
                if (settings_dialog == nullptr) return;
                auto* mode = settings_dialog->findChild<QComboBox*>(
                    "audioWaveformDisplayModeComboBox");
                if (mode == nullptr || waveform_timeline == nullptr) {
                    settings_dialog->accept();
                    return;
                }
                mode->setCurrentIndex(1);
                stereo_mode_applied =
                    waveform_timeline->stereoWaveformDisplayEnabled();
                mode->setCurrentIndex(0);
                mono_mode_applied =
                    !waveform_timeline->stereoWaveformDisplayEnabled();
                settings_dialog->accept();
            });
            waveform_window.showSettingsDialog();
            require(stereo_mode_applied && mono_mode_applied &&
                        settings::audioWaveformDisplayMode() ==
                            settings::AudioWaveformDisplayMode::Mono &&
                        waveform_window.project_dirty_ == dirty_before_waveform_mode,
                    "Changing the waveform view mode did not apply live without dirtying the project.");

            std::atomic_bool blocker_entered = false;
            std::atomic_bool release_blocker = false;
            waveform_window.media_task_pool_.start(QRunnable::create([&]() {
                blocker_entered.store(true, std::memory_order_release);
                while (!release_blocker.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }));
            for (int attempt = 0; attempt < 2000 &&
                 !blocker_entered.load(std::memory_order_acquire); ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool blocker_started =
                blocker_entered.load(std::memory_order_acquire);
            std::error_code original_time_error;
            const auto old_modified = std::filesystem::last_write_time(
                canonical_waveform_source, original_time_error);
            std::error_code modified_error;
            if (blocker_started && !original_time_error) {
                std::filesystem::last_write_time(
                    canonical_waveform_source,
                    old_modified + std::chrono::seconds(2),
                    modified_error);
            }
            const auto changed_signature = modified_error || original_time_error
                ? std::optional<media::AudioWaveformSourceSignature>{}
                : media::audioWaveformSourceSignature(canonical_waveform_source);
            if (changed_signature.has_value() && *changed_signature != *signature) {
                waveform_window.updateTimelineState();
            }
            const bool regeneration_queued =
                !waveform_window.pending_audio_waveforms_.empty();
            waveform_window.clearProjectState();
            release_blocker.store(true, std::memory_order_release);
            waveform_window.media_task_pool_.waitForDone();
            QApplication::processEvents();
            require(blocker_started,
                    "The waveform cancellation test could not occupy the media worker.");
            require(!modified_error && !original_time_error &&
                        changed_signature.has_value() &&
                        *changed_signature != *signature && regeneration_queued,
                    "The changed waveform source was not queued for regeneration.");
            require(waveform_window.pending_audio_waveforms_.empty() &&
                        !waveform_window.audio_waveform_cache_.find(
                            canonical_waveform_source, *changed_signature),
                    "Changing projects did not cancel an obsolete waveform request.");
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
        linked_clip.source_duration_frames = 30;
        linked_clip.kind = timeline::ClipKind::Image;
        linked_track.clips.push_back(linked_clip);
        project::ProjectClip variant_clip;
        variant_clip.clip_id = 82;
        variant_clip.source_path = linked_source;
        variant_clip.timeline_start_frame = 30;
        variant_clip.duration_frames = 30;
        variant_clip.source_duration_frames = 30;
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

#if defined(CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS)
            image_editor::ImageDocumentSession masked_image;
            QString mask_error;
            QImage imported_source(16, 16, QImage::Format_ARGB32);
            imported_source.fill(QColor(25, 40, 235));
            const QString imported_path = QString::fromStdString((directory / "imported-layer.png").string());
            require(imported_source.save(imported_path), "Could not create imported image fixture.");
            require(masked_image.createCanvas(QSize(16, 16), Qt::transparent, &mask_error) &&
                        masked_image.importRasterImages(image_editor::prepareRasterImport({imported_path}).images, {}, &mask_error) &&
                        masked_image.addLayerMask(masked_image.selectedLayerId()) &&
                        masked_image.applyLayerMaskEraseStroke({QPointF(12, 10)}, 3, &mask_error) &&
                        masked_image.resizeCanvas(QSize(24, 12), image_editor::CanvasAnchor::TopLeft, &mask_error) &&
                        masked_image.saveDocument(QString::fromStdString(linked_document.string()), &mask_error) &&
                        masked_image.exportImage(QString::fromStdString(linked_output.string()), &mask_error),
                    "The Image Editor could not publish its resized and masked v12 image: " + mask_error.toStdString());
#else
            QImage next_output(24, 12, QImage::Format_ARGB32);
            next_output.fill(QColor(25, 40, 235, 255));
            for (int x = 0; x < next_output.width(); ++x) {
                next_output.setPixelColor(x, 11, QColor(x * 10, 245 - x * 8, 100, 255));
            }
            require(writePngAtomically(linked_output, next_output),
                    "The updated linked PNG could not be atomically published.");
#endif

            QEventLoop linked_refresh_loop;
            QTimer linked_refresh_timeout;
            linked_refresh_timeout.setSingleShot(true);
            QObject::connect(&linked_refresh_timeout, &QTimer::timeout,
                             &linked_refresh_loop, &QEventLoop::quit);
            QTimer linked_refresh_poll;
            QObject::connect(&linked_refresh_poll, &QTimer::timeout,
                             &linked_refresh_loop, [&]() {
                const bool media_refreshed = !linked_window.media_items_.empty() &&
                    linked_window.media_items_.front().metadata.width == 24 &&
                    linked_window.media_items_.front().metadata.height == 12 &&
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
                        linked_window.media_items_.front().metadata.width == 24 &&
                        linked_window.media_items_.front().metadata.height == 12 &&
                        media_color == QColor(25, 40, 235, 255) && !preview.isNull() &&
                        preview_color == QColor(25, 40, 235, 255) &&
                        linked_window.timeline_model_.tracks().front().clips[1]
                                .still_image_override != nullptr &&
                        variant_color == QColor(230, 210, 15, 255),
                    "A shared-image save did not refresh its preview while keeping the clip variant isolated. media=" +
                        media_color.name(QColor::HexArgb).toStdString() + " preview=" +
                        preview_color.name(QColor::HexArgb).toStdString() + " variant=" +
                        variant_color.name(QColor::HexArgb).toStdString());
#if defined(CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS)
            const auto& refreshed_frame = linked_window.media_items_.front().first_frame;
            const auto alpha_offset = static_cast<std::size_t>(10 * refreshed_frame.stride + 12 * 4 + 3);
            require(refreshed_frame.rgba_pixels.size() > alpha_offset &&
                        refreshed_frame.rgba_pixels[alpha_offset] == 0,
                    "The Video Editor lost transparency from the Image Editor's mask publication.");
#endif
        }

        {
            MainWindow effects_window;
            media::VideoMetadata image_metadata;
            image_metadata.kind = media::MediaKind::Image;
            image_metadata.source_path = first_source;
            image_metadata.display_name = "Effect clipboard fixture";
            image_metadata.width = 2;
            image_metadata.height = 2;
            image_metadata.frame_rate = 30.0;
            image_metadata.duration_seconds = 5.0;
            image_metadata.frame_count = 150;
            media::VideoFrame image_frame;
            image_frame.width = 2;
            image_frame.height = 2;
            image_frame.stride = 8;
            image_frame.rgba_pixels = {
                255, 0, 0, 255, 0, 255, 0, 255,
                0, 0, 255, 255, 255, 255, 255, 255};
            require(effects_window.media_controller_.commitImported({
                        image_metadata, image_frame,
                        image_metadata.display_name, "Unsorted", false}).changed(),
                    "The effects clipboard integration test could not register an image.");

            const auto source_track_id =
                effects_window.timeline_model_.tracks().front().track_id;
            const auto source_add = effects_window.timeline_command_service_.execute(
                application::AddMediaClipCommand{
                    first_source, source_track_id, 0});
            require(source_add.changed(),
                    "The effects clipboard integration test could not add its source clip.");
            const auto source_clip_id = source_add.affected_clip_ids.front();
            const auto target_track_add =
                effects_window.timeline_command_service_.execute(
                    application::AddTrackCommand{"Effects Paste Target"});
            require(target_track_add.changed(),
                    "The effects clipboard integration test could not add a destination track.");
            const auto target_track_id = target_track_add.affected_track_ids.front();
            const auto target_add = effects_window.timeline_command_service_.execute(
                application::AddMediaClipCommand{
                    first_source, target_track_id, 0});
            require(target_add.changed(),
                    "The effects clipboard integration test could not add its destination clip.");
            const auto target_clip_id = target_add.affected_clip_ids.front();
            const auto mixed_text_target = effects_window.timeline_command_service_.execute(
                application::AddTextClipCommand{target_track_id, 150, 90, 30.0});
            require(mixed_text_target.changed(),
                    "The effects clipboard integration test could not add a mixed-type destination.");
            const auto mixed_text_clip_id = mixed_text_target.affected_clip_ids.front();

            auto source_effects = std::vector<creative_suite::effects::EffectInstance>{
                creative_suite::effects::makeDefaultInstance("video.grayscale"),
                creative_suite::effects::makeDefaultInstance("video.saturation")};
            require(creative_suite::effects::setParameterValue(
                        source_effects[0], "amount", 64.0) &&
                        creative_suite::effects::setParameterValue(
                            source_effects[1], "amount", 135.0),
                    "The effects clipboard integration test could not prepare its source stack.");
            source_effects[0].enabled = false;
            auto previous_effects = std::vector<creative_suite::effects::EffectInstance>{
                creative_suite::effects::makeDefaultInstance("video.brightness")};
            require(creative_suite::effects::setParameterValue(
                        previous_effects[0], "amount", 30.0),
                    "The effects clipboard integration test could not prepare its destination stack.");
            require(effects_window.timeline_command_service_.execute(
                        application::SetClipEffectsCommand{
                            source_clip_id, source_effects}).changed() &&
                        effects_window.timeline_command_service_.execute(
                            application::SetClipEffectsCommand{
                                target_clip_id, previous_effects}).changed(),
                    "The effects clipboard integration test could not set both effect stacks.");

            auto& selection = effects_window.editor_session_.selectionForUi();
            selection.active_track_id = source_track_id;
            selection.active_clip_id = source_clip_id;
            selection.active_transition.reset();
            effects_window.updateAttributeClipboardActions();
            effects_window.updatePlaybackControls();
            require(effects_window.copy_attributes_action_->isEnabled() &&
                        !effects_window.paste_attributes_action_->isEnabled() &&
                        effects_window.ripple_delete_clip_action_->isEnabled(),
                    "Copy Attributes and Ripple Delete must be enabled for a selected Timeline clip, while Paste remains disabled before copying.");
            const bool dirty_before_copy = effects_window.project_dirty_;
            effects_window.show();
            QApplication::processEvents();
            QLineEdit text_field;
            text_field.setText(QStringLiteral("normal text copy"));
            text_field.show();
            text_field.selectAll();
            text_field.setFocus();
            QApplication::processEvents();
            require(QApplication::focusWidget() == &text_field,
                    "The text-copy fixture could not focus its editable field.");
            QApplication::clipboard()->clear();
            effects_window.copy_attributes_action_->trigger();
            require(QApplication::clipboard()->text() == QStringLiteral("normal text copy"),
                    "Ctrl+C must preserve normal text-field copy when a clip is selected.");
            QApplication::clipboard()->clear();
            effects_window.ripple_delete_clip_action_->trigger();
            require(text_field.text().isEmpty() &&
                        QApplication::clipboard()->text() ==
                            QStringLiteral("normal text copy") &&
                        effects_window.timeline_model_.locateClip(source_clip_id).has_value(),
                    "Shift+Delete must preserve normal text-field cut instead of Ripple Delete.");
            text_field.hide();
            effects_window.hide();
            QApplication::processEvents();
            require(QApplication::focusWidget() == nullptr,
                    "The attribute-copy fixture could not clear text focus before testing clip copy.");
            effects_window.copy_attributes_action_->trigger();
            effects_window.show();
            QApplication::processEvents();
            require(effects_window.project_dirty_ == dirty_before_copy &&
                        effects_window.paste_attributes_action_->isEnabled(),
                    "Copy Attributes must fill the in-memory clipboard without dirtying the project.");

            selection.active_track_id = target_track_id;
            selection.active_clip_id = target_clip_id;
            effects_window.edit_workspace_->controller()->updateTimelineState();
            effects_window.edit_workspace_->ui().timeline->setSelectedClipIds(
                {target_clip_id, mixed_text_clip_id});
            effects_window.updateAttributeClipboardActions();
            const auto history_before_paste =
                effects_window.timeline_command_service_.undoCount();
            bool paste_dialog_verified = false;
            QTimer::singleShot(0, [&paste_dialog_verified]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (dialog == nullptr) return;
                auto* effects = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesEffects"));
                auto* transform = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesTransform"));
                auto* audio = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesAudioGain"));
                auto* envelope = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesAudioEnvelope"));
                auto* text = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesText"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                paste_dialog_verified = effects != nullptr && effects->isEnabled() &&
                    effects->isChecked() &&
                    effects->text().contains(QStringLiteral("1/2 compatible")) &&
                    transform != nullptr && transform->isEnabled() &&
                    transform->isChecked() &&
                    transform->text().contains(QStringLiteral("2/2 compatible")) &&
                    text != nullptr && !text->isEnabled() && buttons != nullptr;
                if (transform != nullptr) transform->setChecked(false);
                if (audio != nullptr) audio->setChecked(false);
                if (envelope != nullptr) envelope->setChecked(false);
                if (auto* apply = buttons != nullptr
                        ? buttons->button(QDialogButtonBox::Apply) : nullptr) {
                    apply->click();
                }
            });
            effects_window.paste_attributes_action_->trigger();
            require(paste_dialog_verified,
                    "Paste Attributes must show compatible groups checked and incompatible groups disabled.");
            const auto target_location =
                effects_window.timeline_model_.locateClip(target_clip_id);
            require(target_location.has_value() &&
                        effects_window.timeline_model_.tracks()[target_location->track_index]
                                .clips[target_location->clip_index].effects == source_effects &&
                        effects_window.timeline_command_service_.undoCount() ==
                            history_before_paste + 1,
                    "Paste Attributes did not replace the selected group in one edit.");
            require(effects_window.edit_workspace_->controller()->undo().changed() &&
                        effects_window.timeline_model_.tracks()[target_location->track_index]
                                .clips[target_location->clip_index].effects == previous_effects &&
                        effects_window.edit_workspace_->controller()->redo().changed() &&
                        effects_window.timeline_model_.tracks()[target_location->track_index]
                                .clips[target_location->clip_index].effects == source_effects,
                    "Paste Attributes did not integrate with Undo/Redo.");
            require(effects_window.edit_workspace_->ui().timeline->selectedClipIds().size() == 2,
                    "Paste Attributes did not preserve the destination multi-selection.");
            const auto single_clip_edit =
                effects_window.edit_workspace_->controller()->execute(
                    application::SetClipEffectsCommand{target_clip_id, source_effects});
            require(single_clip_edit.status == application::EditStatus::NoChange &&
                        effects_window.edit_workspace_->ui().timeline->selectedClipIds() ==
                            std::vector<timeline::ClipId>{target_clip_id},
                    "Starting an individual Timeline edit did not reduce the selection to its primary clip.");

            selection.active_track_id.reset();
            selection.active_clip_id.reset();
            selection.selected_source_path.reset();
            selection.active_transition.reset();
            effects_window.edit_workspace_->controller()->updateTimelineState();
            effects_window.updateAttributeClipboardActions();
            bool no_target_dialog_verified = false;
            QTimer::singleShot(0, [&no_target_dialog_verified]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (dialog == nullptr) return;
                auto* effects = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesEffects"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                auto* apply = buttons != nullptr
                    ? buttons->button(QDialogButtonBox::Apply) : nullptr;
                no_target_dialog_verified = effects != nullptr &&
                    !effects->isEnabled() && apply != nullptr && !apply->isEnabled();
                if (auto* cancel = buttons != nullptr
                        ? buttons->button(QDialogButtonBox::Cancel) : nullptr) {
                    cancel->click();
                }
            });
            effects_window.paste_attributes_action_->trigger();
            require(no_target_dialog_verified,
                    "Paste Attributes without a selected clip must explain the missing target and disable Apply.");
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
    application.setQuitOnLastWindowClosed(false);
    if (argc == 4 && std::string(argv[3]) == "--project-settings") {
        try {
            MainWindowIntegrationTest::runProjectSettingsOnly(argv[1], argv[2]);
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
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
