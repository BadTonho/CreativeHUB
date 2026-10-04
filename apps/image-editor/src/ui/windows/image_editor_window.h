#pragma once

#include "image_document_session.h"
#include "image_editor_logger.h"
#include "recovery_store.h"
#include "../tools/tool_sidebar.h"

#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QByteArray>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QAction;
class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QDialog;
class QComboBox;
class QFontComboBox;
class QKeySequence;
class QLabel;
class QSlider;
class QSpinBox;
class QPushButton;
class QToolBar;
class QTabBar;
class QToolButton;
class QTimer;
class QStackedWidget;
class QWidget;
class QWidgetAction;

namespace image_editor {

class ImageCanvas;
class LayerPanel;
class ToolSidebar;
struct ImageEditorDocumentTab;

class ImageEditorWindow final : public QMainWindow {
public:
    explicit ImageEditorWindow(QWidget* parent = nullptr,
                                QString recovery_data_directory = {});
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
    [[nodiscard]] bool openImagePathInTarget(const QString& path, OpenTarget target);
    [[nodiscard]] bool openDocumentPathInTarget(const QString& path, OpenTarget target);
    void createActions();
    void createToolOptionsBar();
    void createShapePalette();
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
                           int diameter);
    void handleEraseStroke(const QVector<QPointF>& points, int diameter);
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
    void reportError(const QString& operation,
                     const QString& cause,
                     const QString& path = {});

    ImageDocumentSession session_;
    ImageEditorLogger logger_;
    RecoveryStore recovery_store_;
    ToolSidebar* tool_sidebar_ = nullptr;
    ImageCanvas* canvas_ = nullptr;
    QTabBar* document_tab_bar_ = nullptr;
    QStackedWidget* document_stack_ = nullptr;
    QToolButton* new_document_tab_button_ = nullptr;
    QVector<ImageEditorDocumentTab*> document_tabs_;
    int active_document_tab_ = -1;
    QDockWidget* layer_dock_ = nullptr;
    LayerPanel* layer_panel_ = nullptr;
    QToolBar* tool_options_toolbar_ = nullptr;
    QWidgetAction* paint_options_action_ = nullptr;
    QWidgetAction* shape_options_action_ = nullptr;
    QWidgetAction* text_options_action_ = nullptr;
    QWidgetAction* selection_options_action_ = nullptr;
    QWidget* paint_size_options_ = nullptr;
    QWidget* shape_options_widget_ = nullptr;
    QWidget* text_options_widget_ = nullptr;
    QDialog* shape_palette_window_ = nullptr;
    QButtonGroup* shape_palette_button_group_ = nullptr;
    QList<QToolButton*> shape_palette_buttons_;
    QSlider* brush_size_slider_ = nullptr;
    QSpinBox* brush_size_spin_ = nullptr;
    QLabel* tool_size_label_ = nullptr;
    QCheckBox* eraser_preview_check_ = nullptr;
    QCheckBox* shape_stroke_check_ = nullptr;
    QCheckBox* shape_fill_check_ = nullptr;
    QPushButton* shape_stroke_color_button_ = nullptr;
    QPushButton* shape_fill_color_button_ = nullptr;
    QSpinBox* shape_stroke_width_spin_ = nullptr;
    QPushButton* delete_selected_shape_button_ = nullptr;
    QFontComboBox* text_font_combo_ = nullptr;
    QSpinBox* text_size_spin_ = nullptr;
    QPushButton* text_color_button_ = nullptr;
    QComboBox* text_alignment_combo_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QAction* relink_action_ = nullptr;
    QAction* import_layer_action_ = nullptr;
    QAction* relink_raster_action_ = nullptr;
    bool importing_ = false;
    QHash<QString, QString> logged_raster_problems_;
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
    QAction* delete_objects_action_ = nullptr;
    QAction* delete_selection_action_ = nullptr;
    ImageShapeData shape_style_;
    ImageTextData text_style_;
    QStringList selected_object_ids_;
    QVector<ImageStackItemData> selected_stack_items_;
    QString selected_mask_layer_id_;
    bool shape_colors_initialized_ = false;
    bool shape_palette_positioned_ = false;
    int paint_diameter_ = 12;
    int eraser_diameter_ = 12;
    creative_suite::shortcuts::ShortcutManager shortcut_manager_{
        QStringLiteral("ImageEditor/KeyboardShortcuts")};
    QString linked_document_path_;
    QString linked_output_path_;
    QByteArray linked_document_fingerprint_;
};

} // namespace image_editor
