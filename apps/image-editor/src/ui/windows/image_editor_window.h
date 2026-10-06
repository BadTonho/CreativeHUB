#pragma once

#include "image_document_session.h"
#include "image_editor_logger.h"
#include "image_editor_performance_log.h"
#include "image_editor_performance_metrics.h"
#include "recovery_store.h"
#include "../tools/tool_sidebar.h"

#include <creative_suite/system_monitor/performance_usage.h>

#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QByteArray>
#include <QHash>
#include <QMainWindow>
#include <QMetaObject>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>
#include <cstdint>

class QAction;
class QCloseEvent;
class QDockWidget;
class QKeySequence;
class QLabel;
class QTabBar;
class QToolButton;
class QTimer;
class QStackedWidget;
class QWidget;

namespace image_editor {

class ImageCanvas;
class ImageToolOptionsBar;
class LayerPanel;
class PerformanceMetricsPanel;
class ShapePalette;
class ToolSidebar;
struct ImageEditorDocumentTab;

class ImageEditorWindow final : public QMainWindow {
public:
    explicit ImageEditorWindow(QWidget* parent = nullptr,
                                QString recovery_data_directory = {},
                                QString performance_log_directory = {});
    ~ImageEditorWindow() override;
    [[nodiscard]] bool importImagePaths(const QStringList& paths,
        std::optional<QPointF> center = {}, const QString& relink_id = {});

    // Accepts paths from application launchers or future handoff adapters.
    [[nodiscard]] bool openImagePath(const QString& path);
    [[nodiscard]] bool openDocumentPath(const QString& path);
    [[nodiscard]] bool openLinkedImage(
        const QString& source_path,
        const QString& document_path,
        const QString& published_output_path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class OpenTarget { CurrentTab, NewTab };

    void connectCanvas(ImageCanvas* canvas);
    [[nodiscard]] int addDocumentTab(bool activate = true);
    void activateDocumentTab(int index);
    void closeDocumentTab(int index);
    void openNewTabMenu();
    void updateDocumentTabLabel();
    void resetActiveDocumentState();
    void resetActiveDocumentSelection();
    [[nodiscard]] bool hasActiveDocumentTab() const noexcept;
    [[nodiscard]] ImageEditorDocumentTab& activeTabState() noexcept;
    [[nodiscard]] const ImageEditorDocumentTab& activeTabState() const noexcept;
    [[nodiscard]] ImageDocumentSession& activeSession() noexcept;
    [[nodiscard]] ImageCanvas* activeCanvas() noexcept;
    [[nodiscard]] QHash<QString, QString>& loggedRasterProblems() noexcept;
    [[nodiscard]] QStringList& selectedObjectIds() noexcept;
    [[nodiscard]] QVector<ImageStackItemData>& selectedStackItems() noexcept;
    [[nodiscard]] QString& selectedMaskLayerId() noexcept;
    [[nodiscard]] QString& linkedDocumentPath() noexcept;
    [[nodiscard]] QString& linkedOutputPath() noexcept;
    [[nodiscard]] QByteArray& linkedDocumentFingerprint() noexcept;
    [[nodiscard]] bool openImagePathInTarget(const QString& path, OpenTarget target);
    [[nodiscard]] bool openDocumentPathInTarget(const QString& path, OpenTarget target);
    void createActions();
    void createToolOptionsBar();
    void createLayerPanel();
    void registerShortcutAction(QAction* action, const QKeySequence& default_sequence);
    void loadShortcutPreferences();
    void openShortcutSettings();
    void updateToolOptions();
    void updateSelectionContext();
    void updateLayerPanel();
    [[nodiscard]] bool editingMask() const;
    void updateView(bool preserveCanvasView = false);
    void deactivateCanvasTools();
    void createNewCanvas(bool new_tab = false);
    void openImage();
    void openDocument();
    void resizeCanvas();
    void relinkSource();
    void saveDocument();
    void saveDocumentAs();
    void exportImage(bool quick_export = false);
    void maybeOfferRecovery();
    [[nodiscard]] bool confirmDiscardOrSave(bool clearRecoveryOnDiscard = true);
    [[nodiscard]] bool saveToPath(QString path = {});
    void handleCrop(const QRect& crop);
    void handlePaintStroke(const QVector<QPointF>& points,
                           const QColor& color,
                           int diameter,
                           std::optional<QPainterPath> clipping_path = {});
    void handleEraseStroke(const QVector<QPointF>& points, int diameter,
                           std::optional<QPainterPath> clipping_path = {});
    void handleBucketFill(const QPoint& seed, int tolerance, const QColor& color,
                          std::optional<QPainterPath> clipping_path = {});
    void updateCanvasToolState(ToolSidebar::Tool tool, bool preserveSelection = false);
    void updateCanvasBrush();
    void updateShapeOptions();
    void updateTextOptions();
    void updateShapePalette();
    void openShapePalette();
    void setShapeKind(ImageShapeKind kind);
    void updateObjectPlacements();
    void applyShapeStyleToSelection(bool include_kind = false);
    void applyTextStyleToSelection();
    void handleShapeCreated(const ImageShapeData& shape);
    void handleTextCommitted(const ImageTextData& text, bool existing);
    void handleTextEditingStarted(const ImageTextData& text, bool existing);
    void handleObjectsGeometryChanged(const QVector<ImageObjectPlacement>& objects);
    void deleteSelectedObjects();
    void deleteSelection();
    void updateDeleteActions();
    void setPerformanceMetricsEnabled(bool enabled);
    void updatePerformanceMetrics();
    void writePerformanceMetricsSummary(
        const ImageEditorPerformanceSnapshot& snapshot);
    void reportError(const QString& operation,
                     const QString& cause,
                     const QString& path = {});

    ImageEditorLogger logger_;
    ImageEditorPerformanceLog performance_log_;
    RecoveryStore recovery_store_;
    ToolSidebar* tool_sidebar_ = nullptr;
    QTabBar* document_tab_bar_ = nullptr;
    QStackedWidget* document_stack_ = nullptr;
    QToolButton* new_document_tab_button_ = nullptr;
    QVector<ImageEditorDocumentTab*> document_tabs_;
    int active_document_tab_ = -1;
    QDockWidget* layer_dock_ = nullptr;
    LayerPanel* layer_panel_ = nullptr;
    ImageToolOptionsBar* tool_options_bar_ = nullptr;
    ShapePalette* shape_palette_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QTimer* performance_metrics_timer_ = nullptr;
    QDockWidget* performance_metrics_dock_ = nullptr;
    PerformanceMetricsPanel* performance_metrics_panel_ = nullptr;
    QAction* performance_metrics_action_ = nullptr;
    system_monitor::PerformanceSampler performance_sampler_;
    system_monitor::PerformanceSnapshot latest_performance_resources_;
    std::optional<std::uint64_t> peak_working_set_bytes_;
    std::optional<std::uint64_t> peak_private_usage_bytes_;
    int performance_log_ticks_ = 0;
    std::uint64_t performance_log_sample_count_ = 0;
    bool performance_log_error_reported_ = false;
    QMetaObject::Connection focus_changed_connection_;
    QAction* relink_action_ = nullptr;
    QAction* import_layer_action_ = nullptr;
    QAction* relink_raster_action_ = nullptr;
    bool importing_ = false;
    bool eraser_preview_enabled_ = false;
    int bucket_fill_tolerance_ = 0;
    std::unique_ptr<ImageEditorDocumentTab> empty_document_state_;
    QAction* new_canvas_action_ = nullptr;
    QAction* resize_canvas_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* export_action_ = nullptr;
    QAction* quick_export_action_ = nullptr;
    QAction* open_image_action_ = nullptr;
    QAction* open_document_action_ = nullptr;
    QAction* new_tab_canvas_action_ = nullptr;
    QAction* new_tab_open_image_action_ = nullptr;
    QAction* new_tab_open_document_action_ = nullptr;
    QAction* close_document_tab_action_ = nullptr;
    QAction* next_document_tab_action_ = nullptr;
    QAction* previous_document_tab_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* crop_action_ = nullptr;
    QAction* cancel_crop_action_ = nullptr;
    QAction* rotate_left_action_ = nullptr;
    QAction* rotate_right_action_ = nullptr;
    QAction* flip_horizontal_action_ = nullptr;
    QAction* flip_vertical_action_ = nullptr;
    QAction* fit_action_ = nullptr;
    QAction* paint_tool_action_ = nullptr;
    QAction* eraser_tool_action_ = nullptr;
    QAction* shapes_tool_action_ = nullptr;
    QAction* text_tool_action_ = nullptr;
    QAction* select_tool_action_ = nullptr;
    QAction* area_selection_tool_action_ = nullptr;
    QAction* deselect_area_selection_action_ = nullptr;
    QAction* delete_objects_action_ = nullptr;
    QAction* delete_selection_action_ = nullptr;
    ImageShapeData shape_style_;
    ImageTextData text_style_;
    bool shape_colors_initialized_ = false;
    int paint_diameter_ = 12;
    int eraser_diameter_ = 12;
    int area_selection_shape_ = 0;
    int area_selection_mode_ = 0;
    creative_suite::shortcuts::ShortcutManager shortcut_manager_{
        QStringLiteral("ImageEditor/KeyboardShortcuts")};
};

} // namespace image_editor
