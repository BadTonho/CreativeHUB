#pragma once

#include "model/composition_document.h"
#include "model/motion_project_data.h"
#include "composition_history.h"
#include "../persistence/motion_recovery_store.h"

#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QMainWindow>

#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QAction;
class QLabel;
class QPushButton;
class QSplitter;
class QDoubleSpinBox;
class QCloseEvent;
class QProgressDialog;
class QStackedWidget;
class QTabWidget;
class QTextEdit;
class QToolButton;
class QTimer;
class QFontComboBox;
class QComboBox;
class QSpinBox;

namespace creative_suite::media { struct MediaImportBatchResult; }

namespace motion::ui {

class CompositionViewer;
class MediaDetailsWidget;
class MediaPoolWidget;
class PreviewRenderer;
class TimelineNavigator;
class MotionVideoExportWorker;
struct MotionExportResult;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr,
                        std::filesystem::path recovery_root = {},
                        std::string recovery_session_id = {});
    ~MainWindow() override;

    [[nodiscard]] const model::CompositionDocument* compositionDocument() const noexcept;
    [[nodiscard]] MediaPoolWidget* mediaPoolWidget() const noexcept;

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
    void finishVideoExport(MotionExportResult result);
    [[nodiscard]] bool confirmReplaceDocument();
    void updateDocumentState();
    void updateHistoryActions();
    [[nodiscard]] bool documentIsDirty() const;
    [[nodiscard]] model::MotionProjectData projectData() const;
    [[nodiscard]] CompositionEditState captureEditState() const;
    [[nodiscard]] bool recordCompositionEdit(CompositionEditState before);
    void finishPendingTransformEdit();
    void finishPendingContentEdit();
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
    void openShortcutSettings();
    void openAutosaveRecoverySettings();
    void refreshAutosaveRecoveryDialog(class AutosaveRecoveryDialog& dialog) const;
    void autosaveProject();
    void configureAutosaveTimer();
    void cleanupCurrentUnsavedSnapshots(const char* operation) noexcept;
    void cleanupRecoveredUnsavedSnapshot(const char* operation) noexcept;
    void openMedia();
    void updateMediaDetails();
    void refreshTimeline();
    void selectLayer(model::LayerId id);
    void syncTransformInspector();
    void syncLayerContentInspector(const model::CompositionLayer* selected);
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
    bool eventFilter(QObject* watched, QEvent* event) override;

    std::optional<model::CompositionDocument> document_;
    QLabel* empty_state_ = nullptr;
    QPushButton* empty_state_new_composition_button_ = nullptr;
    QAction* new_composition_action_ = nullptr;
    QAction* open_composition_action_ = nullptr;
    QAction* save_composition_action_ = nullptr;
    QAction* save_composition_as_action_ = nullptr;
    QAction* export_video_action_ = nullptr;
    QAction* import_media_action_ = nullptr;
    QAction* new_text_layer_action_ = nullptr;
    QAction* new_rectangle_layer_action_ = nullptr;
    QAction* new_ellipse_layer_action_ = nullptr;
    QAction* settings_action_ = nullptr;
    QAction* autosave_settings_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* play_pause_action_ = nullptr;
    QAction* previous_frame_action_ = nullptr;
    QAction* next_frame_action_ = nullptr;
    QAction* loop_action_ = nullptr;
    QAction* zoom_in_action_ = nullptr;
    QAction* zoom_out_action_ = nullptr;
    QSplitter* composition_splitter_ = nullptr;
    QSplitter* workspace_ = nullptr;
    MediaPoolWidget* media_pool_ = nullptr;
    CompositionViewer* viewer_ = nullptr;
    MediaDetailsWidget* media_details_ = nullptr;
    QTabWidget* inspector_tabs_ = nullptr;
    QWidget* transform_inspector_ = nullptr;
    QWidget* layer_content_inspector_ = nullptr;
    QStackedWidget* layer_content_pages_ = nullptr;
    QWidget* text_content_page_ = nullptr;
    QWidget* shape_content_page_ = nullptr;
    QTextEdit* text_content_field_ = nullptr;
    QFontComboBox* text_font_field_ = nullptr;
    QSpinBox* text_font_size_field_ = nullptr;
    QPushButton* text_color_button_ = nullptr;
    QComboBox* text_alignment_field_ = nullptr;
    QSpinBox* text_box_width_field_ = nullptr;
    QSpinBox* text_box_height_field_ = nullptr;
    QSpinBox* shape_width_field_ = nullptr;
    QSpinBox* shape_height_field_ = nullptr;
    QPushButton* shape_fill_button_ = nullptr;
    QPushButton* shape_stroke_button_ = nullptr;
    QSpinBox* shape_stroke_width_field_ = nullptr;
    int layer_content_tab_index_ = -1;
    std::array<QDoubleSpinBox*, 5> transform_fields_{};
    std::array<QToolButton*, 5> transform_key_buttons_{};
    TimelineNavigator* timeline_ = nullptr;
    std::unique_ptr<PreviewRenderer> preview_renderer_;
    std::optional<std::filesystem::path> document_path_;
    std::optional<model::MotionProjectData> saved_data_;
    std::optional<model::MotionProjectData> last_autosaved_data_;
    std::optional<std::filesystem::path> recovered_untitled_snapshot_path_;
    persistence::MotionRecoveryStore recovery_store_;
    QTimer* autosave_timer_ = nullptr;
    CompositionHistory composition_history_;
    std::optional<std::pair<model::LayerId, std::size_t>> active_transform_edit_;
    std::optional<model::LayerId> active_content_edit_layer_;
    QProgressDialog* open_progress_ = nullptr;
    QProgressDialog* export_progress_ = nullptr;
    std::unique_ptr<MotionVideoExportWorker> export_worker_;
    std::shared_ptr<std::atomic_bool> open_cancel_requested_;
    std::uint64_t open_generation_ = 0;
    creative_suite::shortcuts::ShortcutManager shortcut_manager_{
        QStringLiteral("MotionStudio/KeyboardShortcuts")};
    model::LayerId selected_layer_id_ = 0;
};

} // namespace motion::ui
