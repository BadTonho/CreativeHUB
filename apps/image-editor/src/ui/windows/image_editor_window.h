#pragma once

#include "image_document_session.h"
#include "image_editor_logger.h"
#include "recovery_store.h"
#include "../tools/tool_sidebar.h"

#include <QList>
#include <QByteArray>
#include <QMainWindow>
#include <QStringList>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QKeySequence;
class QLabel;
class QSlider;
class QSpinBox;
class QPushButton;
class QToolBar;
class QTimer;
class QWidget;
class QWidgetAction;

namespace image_editor {

class ImageCanvas;
class LayerPanel;
class ToolSidebar;

class ImageEditorWindow final : public QMainWindow {
public:
    explicit ImageEditorWindow(QWidget* parent = nullptr);

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
    void createActions();
    void createToolOptionsBar();
    void createLayerPanel();
    void registerShortcutAction(QAction* action, const QKeySequence& default_sequence);
    void loadShortcutPreferences();
    void openShortcutSettings();
    void updateToolOptions();
    void updateView(bool preserveCanvasView = false);
    void deactivateCanvasTools();
    void createNewCanvas();
    void openImage();
    void openDocument();
    void relinkSource();
    void saveDocument();
    void saveDocumentAs();
    void exportImage(bool quick_export = false);
    void maybeOfferRecovery();
    [[nodiscard]] bool confirmDiscardOrSave();
    [[nodiscard]] bool saveToPath(QString path = {});
    void handleCrop(const QRect& crop);
    void handlePaintStroke(const QVector<QPointF>& points,
                           const QColor& color,
                           int diameter);
    void handleEraseStroke(const QVector<QPointF>& points, int diameter);
    void updateCanvasToolState(ToolSidebar::Tool tool);
    void updateCanvasBrush();
    void updateShapeOptions();
    void updateObjectPlacements();
    void applyShapeStyleToSelection(bool include_kind = false);
    void handleShapeCreated(const ImageShapeData& shape);
    void handleObjectsGeometryChanged(const QVector<ImageObjectPlacement>& objects);
    void deleteSelectedObjects();
    void reportError(const QString& operation,
                     const QString& cause,
                     const QString& path = {});

    ImageDocumentSession session_;
    ImageEditorLogger logger_;
    RecoveryStore recovery_store_;
    ToolSidebar* tool_sidebar_ = nullptr;
    ImageCanvas* canvas_ = nullptr;
    QDockWidget* layer_dock_ = nullptr;
    LayerPanel* layer_panel_ = nullptr;
    QToolBar* tool_options_toolbar_ = nullptr;
    QWidgetAction* paint_options_action_ = nullptr;
    QWidgetAction* shape_options_action_ = nullptr;
    QWidget* paint_size_options_ = nullptr;
    QWidget* shape_options_widget_ = nullptr;
    QSlider* brush_size_slider_ = nullptr;
    QSpinBox* brush_size_spin_ = nullptr;
    QLabel* tool_size_label_ = nullptr;
    QCheckBox* eraser_preview_check_ = nullptr;
    QComboBox* shape_kind_combo_ = nullptr;
    QCheckBox* shape_stroke_check_ = nullptr;
    QCheckBox* shape_fill_check_ = nullptr;
    QPushButton* shape_stroke_color_button_ = nullptr;
    QPushButton* shape_fill_color_button_ = nullptr;
    QSpinBox* shape_stroke_width_spin_ = nullptr;
    QPushButton* delete_selected_shape_button_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QAction* relink_action_ = nullptr;
    QAction* new_canvas_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* export_action_ = nullptr;
    QAction* quick_export_action_ = nullptr;
    QAction* open_image_action_ = nullptr;
    QAction* open_document_action_ = nullptr;
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
    QAction* select_tool_action_ = nullptr;
    QAction* delete_objects_action_ = nullptr;
    ImageShapeData shape_style_;
    QStringList selected_object_ids_;
    bool shape_colors_initialized_ = false;
    int paint_diameter_ = 12;
    int eraser_diameter_ = 12;
    QList<QAction*> shortcut_actions_;
    QString linked_document_path_;
    QString linked_output_path_;
    QByteArray linked_document_fingerprint_;
};

} // namespace image_editor
