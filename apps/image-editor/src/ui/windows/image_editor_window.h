#pragma once

#include "image_document_session.h"
#include "image_editor_logger.h"
#include "recovery_store.h"
#include "../tools/tool_sidebar.h"

#include <QList>
#include <QMainWindow>

class QAction;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QKeySequence;
class QLabel;
class QSlider;
class QSpinBox;
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
    void exportImage();
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
    QWidget* paint_size_options_ = nullptr;
    QSlider* brush_size_slider_ = nullptr;
    QSpinBox* brush_size_spin_ = nullptr;
    QLabel* tool_size_label_ = nullptr;
    QCheckBox* eraser_preview_check_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QAction* relink_action_ = nullptr;
    QAction* new_canvas_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* export_action_ = nullptr;
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
    int paint_diameter_ = 12;
    int eraser_diameter_ = 12;
    QList<QAction*> shortcut_actions_;
};

} // namespace image_editor
