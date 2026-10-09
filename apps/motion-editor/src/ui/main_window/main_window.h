#pragma once

#include "model/composition_document.h"
#include "model/motion_project_data.h"
#include <creative_suite/motion_handoff/request.h>
#include "application/history/composition_history.h"
#include "persistence/motion_recovery_store.h"
#include "diagnostics/performance_metrics.h"

#include <creative_suite/shortcuts/shortcut_manager.h>
#include <creative_suite/system_monitor/performance_usage.h>
#include <creative_suite/media/video_frame.h>

#include <QMainWindow>
#include <QPoint>

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QPushButton;
class QCloseEvent;
class QProgressDialog;
class QTimer;
class QComboBox;
class QOffscreenSurface;

namespace creative_suite::media { struct MediaImportBatchResult; }

namespace motion::ui {

class CompositionViewer;
class InspectorWidget;
class MediaPoolWidget;
class MotionWorkspace;
class PropertyCurveEditor;
class PreviewRenderer;
class TimelineNavigator;
class AudioKeyframeGenerationWorker;
class MotionVideoExportWorker;
struct AudioKeyframeGenerationResult;
struct MotionExportResult;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr,
                        std::filesystem::path recovery_root = {},
                        std::string recovery_session_id = {});
    ~MainWindow() override;

    [[nodiscard]] const model::CompositionDocument* compositionDocument() const noexcept;
    [[nodiscard]] MediaPoolWidget* mediaPoolWidget() const noexcept;
    void openHandoffRequest(const std::filesystem::path& request_path);

private:
    void createNewComposition();
    void openComposition();
    void stageOpenProject(std::filesystem::path target_path,
                          model::MotionProjectData project,
                          bool recovered = false,
                          std::filesystem::path recovery_snapshot_path = {});
    void restoreRecoverySnapshot(const std::filesystem::path& snapshot_path);
    void maybeOfferUnsavedRecovery();
    [[nodiscard]] std::optional<std::filesystem::path> chooseRecoverySnapshot(
        std::vector<persistence::MotionRecoverySnapshot> snapshots,
        const QString& project_label);
    [[nodiscard]] bool saveComposition();
    [[nodiscard]] bool saveCompositionAs();
    [[nodiscard]] bool saveToPath(const std::filesystem::path& path);
    void startVideoExport();
    void startLinkedPublication();
    void finishLinkedHandoffInitialization();
    void finishVideoExport(MotionExportResult result);
    void generateKeyframesFromAudio();
    void finishAudioKeyframeGeneration(AudioKeyframeGenerationResult result);
    [[nodiscard]] bool confirmReplaceDocument();
    void updateDocumentState();
    void updateHistoryActions();
    [[nodiscard]] bool documentIsDirty() const;
    [[nodiscard]] model::MotionProjectData projectData() const;
    [[nodiscard]] CompositionEditState captureEditState() const;
    [[nodiscard]] bool recordCompositionEdit(CompositionEditState before);
    void finishPendingTransformEdit();
    void finishPendingContentEdit();
    void finishPendingEffectEdit();
    void undoComposition();
    void redoComposition();
    void applyEditState(CompositionEditState state);
    void finishOpen(std::uint64_t generation,
                    std::filesystem::path path,
                    model::MotionProjectData project,
                    creative_suite::media::MediaImportBatchResult result,
                    bool recovered,
                    std::filesystem::path recovery_snapshot_path);
    void reportDocumentError(const char* operation,
                             const std::filesystem::path& path,
                             const std::exception& error,
                             int error_code = -1,
                             int system_error = -1);
    void createWorkspace();
    void restoreWorkspaceLayout();
    void saveWorkspaceLayout();
    void restoreDefaultPanelLayout();
    void openShortcutSettings();
    void openAutosaveRecoverySettings();
    void openGeneralSettings();
    void configurePreviewPerformanceMetrics();
    void flushPreviewPerformanceMetrics();
    void refreshAutosaveRecoveryDialog(class AutosaveRecoveryDialog& dialog) const;
    void autosaveProject();
    void configureAutosaveTimer();
    void cleanupCurrentUnsavedSnapshots(const char* operation) noexcept;
    void cleanupRecoveredUnsavedSnapshot(const char* operation) noexcept;
    void openMedia();
    void updateMediaDetails();
    void refreshTimeline();
    void selectLayer(model::LayerId id);
    void editSelectedLayerImage();
    void showLayerContextMenu(model::LayerId id, const QPoint& global_position);
    void initializeLinkedImageFrames();
    void refreshLinkedImagePublications();
    void decodeLinkedImageFrame(model::LayerId id,
                                const std::filesystem::path& path,
                                bool published_output);
    void syncTransformInspector();
    void refreshCurveEditor();
    void selectCurveSegment(
        model::LayerId id,
        creative_suite::animation::TransformProperty property,
        std::int64_t local_start_frame);
    void applyCurvePreset(int preset_index);
    void applyCurveEasing(
        std::int64_t local_start_frame,
        creative_suite::animation::CubicBezierEasing easing);
    void resetCurveEditor();
    void syncLayerContentInspector(const model::CompositionLayer* selected);
    void syncEffectsInspector(const model::CompositionLayer* selected);
    void syncSelectedEffectInspector(const model::CompositionLayer* selected,
                                     int selected_row);
    void addLayerEffect(int kind);
    void editSelectedEffectParameters();
    void applySelectedEffectStack(std::vector<model::LayerEffect> effects,
                                  bool coalesce_parameters = false);
    void reorderEffectsFromList(std::size_t source_index, int insertion_row);
    void moveSelectedEffect(int direction);
    void removeSelectedEffect();
    void selectEffectRow(int row);
    void createContentLayer(model::LayerKind kind, model::ShapeKind shape);
    void editSelectedLayerContent();
    void chooseSelectedLayerColor(bool text_color, bool stroke_color);
    void editSelectedLayerTransform(std::size_t property_index);
    void toggleSelectedLayerKeyframe(std::size_t property_index);
    void handleMediaDrop(const std::filesystem::path& path,
                         std::int64_t start_frame,
                         model::LayerId before_layer_id);
    void requestPreview(bool playback_tick = false);
    void closeEvent(QCloseEvent* event) override;

    std::optional<model::CompositionDocument> document_;
    QLabel* empty_state_ = nullptr;
    QPushButton* empty_state_new_composition_button_ = nullptr;
    QAction* new_composition_action_ = nullptr;
    QAction* open_composition_action_ = nullptr;
    QAction* save_composition_action_ = nullptr;
    QAction* save_composition_as_action_ = nullptr;
    QAction* export_video_action_ = nullptr;
    QAction* generate_audio_keyframes_action_ = nullptr;
    QAction* import_media_action_ = nullptr;
    QAction* new_text_layer_action_ = nullptr;
    QAction* new_rectangle_layer_action_ = nullptr;
    QAction* new_ellipse_layer_action_ = nullptr;
    QAction* edit_image_in_image_editor_action_ = nullptr;
    QAction* settings_action_ = nullptr;
    QAction* general_settings_action_ = nullptr;
    QAction* autosave_settings_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* reset_panel_layout_action_ = nullptr;
    QAction* play_pause_action_ = nullptr;
    QAction* previous_frame_action_ = nullptr;
    QAction* next_frame_action_ = nullptr;
    QAction* loop_action_ = nullptr;
    QAction* zoom_in_action_ = nullptr;
    QAction* zoom_out_action_ = nullptr;
    QMenu* view_menu_ = nullptr;
    QDockWidget* media_pool_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    QDockWidget* graph_editor_dock_ = nullptr;
    MediaPoolWidget* media_pool_ = nullptr;
    CompositionViewer* viewer_ = nullptr;
    InspectorWidget* inspector_ = nullptr;
    MotionWorkspace* workspace_ = nullptr;
    QWidget* curve_editor_panel_ = nullptr;
    PropertyCurveEditor* curve_editor_ = nullptr;
    QComboBox* curve_preset_combo_ = nullptr;
    QLabel* curve_custom_label_ = nullptr;
    TimelineNavigator* timeline_ = nullptr;
    std::unique_ptr<QOffscreenSurface> gpu_composition_surface_;
    std::unique_ptr<QOffscreenSurface> gpu_export_surface_;
    std::unique_ptr<PreviewRenderer> preview_renderer_;
    std::optional<std::filesystem::path> document_path_;
    std::optional<creative_suite::motion_handoff::Request> linked_handoff_;
    bool handoff_needs_initial_layer_ = false;
    std::optional<creative_suite::motion_handoff::Request> pending_linked_handoff_;
    bool pending_handoff_needs_initial_layer_ = false;
    bool startup_handoff_requested_ = false;
    bool linked_publication_pending_ = false;
    std::int64_t document_revision_ = 0;
    std::optional<model::MotionProjectData> saved_data_;
    std::optional<model::MotionProjectData> last_autosaved_data_;
    std::optional<std::filesystem::path> recovered_untitled_snapshot_path_;
    persistence::MotionRecoveryStore recovery_store_;
    QTimer* autosave_timer_ = nullptr;
    QTimer* performance_metrics_timer_ = nullptr;
    QTimer* linked_image_refresh_timer_ = nullptr;
    system_monitor::PerformanceSampler performance_sampler_;
    CompositionHistory composition_history_;
    std::optional<std::pair<model::LayerId, std::size_t>> active_transform_edit_;
    std::optional<model::LayerId> active_content_edit_layer_;
    std::optional<std::pair<model::LayerId, std::size_t>> active_effect_edit_;
    std::optional<std::pair<model::LayerId, std::size_t>> selected_effect_;
    struct CurveSelection {
        model::LayerId layer_id = 0;
        creative_suite::animation::TransformProperty property =
            creative_suite::animation::TransformProperty::PositionX;
        std::int64_t segment_start_frame = 0;
    };
    std::optional<CurveSelection> curve_selection_;
    QProgressDialog* open_progress_ = nullptr;
    QProgressDialog* export_progress_ = nullptr;
    std::unique_ptr<MotionVideoExportWorker> export_worker_;
    QProgressDialog* audio_keyframe_progress_ = nullptr;
    std::unique_ptr<AudioKeyframeGenerationWorker> audio_keyframe_worker_;
    std::shared_ptr<std::atomic_bool> open_cancel_requested_;
    std::uint64_t open_generation_ = 0;
    std::uint64_t linked_image_generation_ = 0;
    std::map<model::LayerId, creative_suite::media::RgbaFramePtr> linked_image_frames_;
    std::map<model::LayerId, std::pair<std::int64_t, std::int64_t>>
        linked_image_signatures_;
    std::map<model::LayerId, std::uint64_t> linked_image_decode_requests_;
    creative_suite::shortcuts::ShortcutManager shortcut_manager_{
        QStringLiteral("MotionStudio/KeyboardShortcuts")};
    model::LayerId selected_layer_id_ = 0;
};

} // namespace motion::ui
