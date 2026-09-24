#pragma once

#include "image_document_session.h"
#include "image_editor_logger.h"
#include "recovery_store.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QLabel;
class QTimer;

namespace image_editor {

class ImageCanvas;
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
    void reportError(const QString& operation,
                     const QString& cause,
                     const QString& path = {});

    ImageDocumentSession session_;
    ImageEditorLogger logger_;
    RecoveryStore recovery_store_;
    ToolSidebar* tool_sidebar_ = nullptr;
    ImageCanvas* canvas_ = nullptr;
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
    QAction* rotate_left_action_ = nullptr;
    QAction* rotate_right_action_ = nullptr;
    QAction* flip_horizontal_action_ = nullptr;
    QAction* flip_vertical_action_ = nullptr;
    QAction* fit_action_ = nullptr;
};

} // namespace image_editor
