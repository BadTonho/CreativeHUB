#include "image_editor_window.h"

#include "image_canvas.h"
#include "image_document_store.h"
#include "new_canvas_dialog.h"
#include "shortcut_settings_dialog.h"
#include "tool_sidebar.h"
#include "layer_panel.h"

#include <QAction>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

namespace image_editor {
namespace {

QString imageFilter() {
    return QStringLiteral("Images (%1)").arg(
        ImageDocumentStore::supportedImageExtensions().join(' '));
}

} // namespace

ImageEditorWindow::ImageEditorWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Image Editor"));
    resize(1180, 760);

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tool_sidebar_ = new ToolSidebar(central);
    layout->addWidget(tool_sidebar_);
    canvas_ = new ImageCanvas(central);
    layout->addWidget(canvas_, 1);
    setCentralWidget(central);

    status_label_ = new QLabel(QStringLiteral("No image open"), this);
    status_label_->setObjectName(QStringLiteral("imageStatusLabel"));
    statusBar()->addWidget(status_label_, 1);

    createToolOptionsBar();
    createLayerPanel();
    createActions();
    connect(canvas_, &ImageCanvas::cropSelected, this,
            [this](const QRect& crop) { handleCrop(crop); });
    connect(canvas_, &ImageCanvas::paintStrokeSelected, this,
            [this](const QVector<QPointF>& points, const QColor& color, int diameter) {
                handlePaintStroke(points, color, diameter);
            });
    connect(canvas_, &ImageCanvas::brushDiameterChanged,
            brush_size_spin_, &QSpinBox::setValue);
    connect(tool_sidebar_, &ToolSidebar::paintToolToggled, this, [this](bool active) {
        if (active) crop_action_->setChecked(false);
        if (paint_tool_action_ != nullptr && paint_tool_action_->isChecked() != active) {
            paint_tool_action_->setChecked(active);
        }
        canvas_->setPaintMode(active && session_.hasSource());
        updateToolOptions();
    });
    connect(tool_sidebar_, &ToolSidebar::brushColorChanged,
            canvas_, [this](const QColor& color) {
                canvas_->setBrush(color, brush_size_spin_->value());
            });
    connect(layer_panel_, &LayerPanel::layerSelected, this, [this](const QString& id) {
        if (!session_.selectLayer(id)) return;
        if (!session_.selectedLayerIsEditable()) deactivateCanvasTools();
        updateView(true);
    });
    connect(layer_panel_, &LayerPanel::layerVisibilityChanged, this,
            [this](const QString& id, bool visible) {
                if (session_.setLayerVisible(id, visible)) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::layerRenamed, this,
            [this](const QString& id, const QString& name) {
                QString error;
                if (session_.renameLayer(id, name, &error)) updateView(true);
                else {
                    updateView(true);
                    if (!error.isEmpty()) statusBar()->showMessage(error, 4000);
                }
            });
    connect(layer_panel_, &LayerPanel::addLayerRequested, this, [this]() {
        const QString id = session_.addLayer();
        if (id.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("A layer could not be added"), 3000);
            return;
        }
        static_cast<void>(session_.selectLayer(id));
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Layer added"), 1800);
    });
    connect(layer_panel_, &LayerPanel::deleteLayerRequested, this,
            [this](const QString& id) {
                if (!session_.deleteLayer(id)) return;
                if (!session_.selectedLayerIsEditable()) deactivateCanvasTools();
                updateView(true);
            });
    connect(layer_panel_, &LayerPanel::moveLayerRequested, this,
            [this](const QString& id, int direction) {
                if (session_.moveLayer(id, direction)) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::opacityEditStarted, this,
            [this]() { session_.beginLayerOpacityEdit(); });
    connect(layer_panel_, &LayerPanel::layerOpacityChanged, this,
            [this](const QString& id, int opacity) {
                if (session_.setLayerOpacity(id, opacity)) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::opacityEditFinished, this, [this]() {
        session_.endLayerOpacityEdit();
        updateView(true);
    });

    autosave_timer_ = new QTimer(this);
    autosave_timer_->setInterval(60'000);
    connect(autosave_timer_, &QTimer::timeout, this, [this]() {
        if (!session_.isDirty() || !session_.hasSource()) return;
        QString error;
        if (!recovery_store_.save(session_, &error)) {
            QString context = session_.sourcePath();
            if (context.isEmpty()) context = session_.documentPath();
            if (context.isEmpty()) context = session_.recoverySessionId();
            logger_.logError(QStringLiteral("autosave"), error, context);
            statusBar()->showMessage(QStringLiteral("Recovery snapshot could not be saved"), 5000);
        }
    });
    autosave_timer_->start();

    QTimer::singleShot(0, this, [this]() { maybeOfferRecovery(); });
    updateView();
}

void ImageEditorWindow::createLayerPanel() {
    layer_dock_ = new QDockWidget(QStringLiteral("Layers"), this);
    layer_dock_->setObjectName(QStringLiteral("imageEditorLayersDock"));
    layer_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    layer_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable |
                             QDockWidget::DockWidgetClosable);
    layer_panel_ = new LayerPanel(layer_dock_);
    layer_dock_->setWidget(layer_panel_);
    layer_dock_->setMinimumWidth(220);
    addDockWidget(Qt::RightDockWidgetArea, layer_dock_);
}

void ImageEditorWindow::createToolOptionsBar() {
    tool_options_toolbar_ = new QToolBar(QStringLiteral("Tool Options"), this);
    tool_options_toolbar_->setObjectName(QStringLiteral("toolOptionsToolBar"));
    tool_options_toolbar_->setMovable(false);
    tool_options_toolbar_->setFloatable(false);
    tool_options_toolbar_->setAllowedAreas(Qt::TopToolBarArea);
    tool_options_toolbar_->setMinimumHeight(40);
    addToolBar(Qt::TopToolBarArea, tool_options_toolbar_);

    paint_size_options_ = new QWidget(tool_options_toolbar_);
    paint_size_options_->setObjectName(QStringLiteral("paintBrushSizeOptions"));
    paint_size_options_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* options_layout = new QHBoxLayout(paint_size_options_);
    options_layout->setContentsMargins(8, 3, 8, 3);
    options_layout->setSpacing(8);

    auto* size_label = new QLabel(QStringLiteral("Brush Size"), paint_size_options_);
    size_label->setObjectName(QStringLiteral("paintBrushSizeLabel"));
    options_layout->addWidget(size_label);

    brush_size_slider_ = new QSlider(Qt::Horizontal, paint_size_options_);
    brush_size_slider_->setObjectName(QStringLiteral("paintBrushSizeSlider"));
    brush_size_slider_->setAccessibleName(QStringLiteral("Brush size"));
    brush_size_slider_->setRange(1, 512);
    brush_size_slider_->setValue(12);
    brush_size_slider_->setMinimumWidth(140);
    brush_size_slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    options_layout->addWidget(brush_size_slider_, 1);

    brush_size_spin_ = new QSpinBox(paint_size_options_);
    brush_size_spin_->setObjectName(QStringLiteral("paintBrushSizeSpinBox"));
    brush_size_spin_->setAccessibleName(QStringLiteral("Brush size in pixels"));
    brush_size_spin_->setRange(1, 512);
    brush_size_spin_->setValue(12);
    brush_size_spin_->setSuffix(QStringLiteral(" px"));
    brush_size_spin_->setFixedWidth(96);
    options_layout->addWidget(brush_size_spin_);

    paint_options_action_ = new QWidgetAction(tool_options_toolbar_);
    paint_options_action_->setObjectName(QStringLiteral("paintBrushSizeAction"));
    paint_options_action_->setDefaultWidget(paint_size_options_);
    tool_options_toolbar_->addAction(paint_options_action_);
    paint_options_action_->setVisible(false);

    connect(brush_size_slider_, &QSlider::valueChanged,
            brush_size_spin_, &QSpinBox::setValue);
    connect(brush_size_spin_, &QSpinBox::valueChanged, this, [this](int diameter) {
        brush_size_slider_->setValue(diameter);
        canvas_->setBrush(tool_sidebar_->brushColor(), diameter);
    });
}

void ImageEditorWindow::updateToolOptions() {
    if (paint_options_action_ == nullptr || paint_size_options_ == nullptr ||
        tool_sidebar_ == nullptr || canvas_ == nullptr) return;
    const bool paint_active = tool_sidebar_->paintToolActive() && canvas_->paintMode();
    paint_options_action_->setVisible(paint_active);
    paint_size_options_->setVisible(paint_active);
    paint_size_options_->setEnabled(paint_active);
}

void ImageEditorWindow::createActions() {
    auto makeAction = [this](const QString& text, const QKeySequence& shortcut,
                             auto callback) {
        auto* action = new QAction(text, this);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };

    new_canvas_action_ = makeAction(
        QStringLiteral("New Canvas..."), QKeySequence::New,
        [this]() { createNewCanvas(); });
    new_canvas_action_->setObjectName(QStringLiteral("newCanvasAction"));
    auto* open_image_action = makeAction(
        QStringLiteral("Open Image..."), QKeySequence::Open, [this]() { openImage(); });
    open_image_action->setObjectName(QStringLiteral("openImageAction"));
    auto* open_document_action = makeAction(
        QStringLiteral("Open Editable Document..."), {}, [this]() { openDocument(); });
    relink_action_ = makeAction(
        QStringLiteral("Relink Source Image..."), {}, [this]() { relinkSource(); });
    save_action_ = makeAction(
        QStringLiteral("Save Document"), QKeySequence::Save, [this]() { saveDocument(); });
    save_as_action_ = makeAction(
        QStringLiteral("Save Document As..."), QKeySequence::SaveAs, [this]() { saveDocumentAs(); });
    export_action_ = makeAction(
        QStringLiteral("Export Image..."), {}, [this]() { exportImage(); });
    auto* quit_action = makeAction(
        QStringLiteral("Quit"), QKeySequence::Quit, [this]() { close(); });

    undo_action_ = makeAction(
        QStringLiteral("Undo"), QKeySequence::Undo, [this]() {
            if (session_.undo()) updateView();
        });
    undo_action_->setObjectName(QStringLiteral("undoAction"));
    redo_action_ = makeAction(
        QStringLiteral("Redo"), QKeySequence::Redo, [this]() {
            if (session_.redo()) updateView();
        });
    redo_action_->setObjectName(QStringLiteral("redoAction"));
    rotate_left_action_ = makeAction(
        QStringLiteral("Rotate Left 90°"), {}, [this]() {
            if (session_.hasSource()) { session_.rotateLeft(); updateView(); }
        });
    rotate_right_action_ = makeAction(
        QStringLiteral("Rotate Right 90°"), {}, [this]() {
            if (session_.hasSource()) { session_.rotateRight(); updateView(); }
        });
    rotate_right_action_->setObjectName(QStringLiteral("rotateRightAction"));
    flip_horizontal_action_ = makeAction(
        QStringLiteral("Flip Horizontal"), {}, [this]() {
            if (session_.hasSource()) { session_.flipHorizontal(); updateView(); }
        });
    flip_vertical_action_ = makeAction(
        QStringLiteral("Flip Vertical"), {}, [this]() {
            if (session_.hasSource()) { session_.flipVertical(); updateView(); }
        });
    crop_action_ = new QAction(QStringLiteral("Crop Selection"), this);
    crop_action_->setObjectName(QStringLiteral("cropSelectionAction"));
    crop_action_->setCheckable(true);
    connect(crop_action_, &QAction::toggled, this, [this](bool enabled) {
        if (enabled && tool_sidebar_->paintToolActive()) {
            tool_sidebar_->setPaintToolActive(false);
        }
        updateToolOptions();
        canvas_->setPaintMode(false);
        canvas_->setCropMode(enabled && session_.hasSource());
        if (cancel_crop_action_ != nullptr) {
            cancel_crop_action_->setEnabled(enabled && session_.hasSource());
        }
        statusBar()->showMessage(enabled
            ? QStringLiteral("Drag over the image to crop; press Esc to cancel")
            : QStringLiteral("Ready"));
    });
    fit_action_ = makeAction(
        QStringLiteral("Fit Image"), {}, [this]() { canvas_->fitToWindow(); });
    cancel_crop_action_ = new QAction(QStringLiteral("Cancel Crop"), this);
    cancel_crop_action_->setObjectName(QStringLiteral("cancelCropAction"));
    cancel_crop_action_->setEnabled(false);
    addAction(cancel_crop_action_);
    connect(cancel_crop_action_, &QAction::triggered, this, [this]() {
        if (!crop_action_->isChecked()) return;
        crop_action_->setChecked(false);
        canvas_->setCropMode(false);
        statusBar()->showMessage(QStringLiteral("Crop cancelled"), 2500);
    });

    open_document_action->setObjectName(QStringLiteral("openDocumentAction"));
    relink_action_->setObjectName(QStringLiteral("relinkSourceAction"));
    save_action_->setObjectName(QStringLiteral("saveDocumentAction"));
    save_as_action_->setObjectName(QStringLiteral("saveDocumentAsAction"));
    export_action_->setObjectName(QStringLiteral("exportImageAction"));
    quit_action->setObjectName(QStringLiteral("quitAction"));
    rotate_left_action_->setObjectName(QStringLiteral("rotateLeftAction"));
    flip_horizontal_action_->setObjectName(QStringLiteral("flipHorizontalAction"));
    flip_vertical_action_->setObjectName(QStringLiteral("flipVerticalAction"));
    fit_action_->setObjectName(QStringLiteral("fitImageAction"));

    registerShortcutAction(new_canvas_action_, QKeySequence::New);
    registerShortcutAction(open_image_action, QKeySequence::Open);
    registerShortcutAction(open_document_action, {});
    registerShortcutAction(relink_action_, {});
    registerShortcutAction(save_action_, QKeySequence::Save);
    registerShortcutAction(save_as_action_, QKeySequence::SaveAs);
    registerShortcutAction(export_action_, {});
    registerShortcutAction(quit_action, QKeySequence::Quit);
    registerShortcutAction(undo_action_, QKeySequence::Undo);
    registerShortcutAction(redo_action_, QKeySequence::Redo);
    registerShortcutAction(crop_action_, {});
    registerShortcutAction(cancel_crop_action_, QKeySequence(Qt::Key_Escape));
    registerShortcutAction(rotate_left_action_, {});
    registerShortcutAction(rotate_right_action_, {});
    registerShortcutAction(flip_horizontal_action_, {});
    registerShortcutAction(flip_vertical_action_, {});
    registerShortcutAction(fit_action_, {});

    paint_tool_action_ = new QAction(QStringLiteral("Paint"), this);
    paint_tool_action_->setObjectName(QStringLiteral("paintToolAction"));
    paint_tool_action_->setCheckable(true);
    registerShortcutAction(paint_tool_action_, QKeySequence(Qt::Key_B));
    addAction(paint_tool_action_);
    connect(paint_tool_action_, &QAction::toggled, this, [this](bool active) {
        tool_sidebar_->setPaintToolActive(active);
        const bool actual_state = tool_sidebar_->paintToolActive();
        if (paint_tool_action_->isChecked() != actual_state) {
            paint_tool_action_->setChecked(actual_state);
        }
        canvas_->setPaintMode(actual_state && session_.hasSource());
        updateToolOptions();
    });

    auto* file_menu = menuBar()->addMenu(QStringLiteral("File"));
    file_menu->addAction(new_canvas_action_);
    file_menu->addSeparator();
    file_menu->addAction(open_image_action);
    file_menu->addAction(open_document_action);
    file_menu->addAction(relink_action_);
    file_menu->addSeparator();
    file_menu->addAction(save_action_);
    file_menu->addAction(save_as_action_);
    file_menu->addAction(export_action_);
    file_menu->addSeparator();
    file_menu->addAction(quit_action);

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("Edit"));
    edit_menu->addAction(undo_action_);
    edit_menu->addAction(redo_action_);
    edit_menu->addSeparator();
    edit_menu->addAction(crop_action_);
    edit_menu->addAction(rotate_left_action_);
    edit_menu->addAction(rotate_right_action_);
    edit_menu->addAction(flip_horizontal_action_);
    edit_menu->addAction(flip_vertical_action_);

    auto* view_menu = menuBar()->addMenu(QStringLiteral("View"));
    view_menu->addAction(fit_action_);
    auto* layers_view_action = layer_dock_->toggleViewAction();
    layers_view_action->setObjectName(QStringLiteral("toggleLayersPanelAction"));
    registerShortcutAction(layers_view_action, {});
    view_menu->addAction(layers_view_action);

    auto* settings_menu = menuBar()->addMenu(QStringLiteral("Settings"));
    settings_menu->setObjectName(QStringLiteral("settingsMenu"));
    auto* shortcuts_action = settings_menu->addAction(
        QStringLiteral("Keyboard Shortcuts..."));
    shortcuts_action->setObjectName(QStringLiteral("keyboardShortcutsAction"));
    connect(shortcuts_action, &QAction::triggered,
            this, [this]() { openShortcutSettings(); });

    auto* help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto* system_action = help_menu->addAction(QStringLiteral("&System"));
    connect(system_action, &QAction::triggered, this, [this]() {
        const auto version = QCoreApplication::applicationVersion();
        const auto executable_path = QCoreApplication::applicationFilePath();
        QMessageBox::information(
            this,
            QStringLiteral("System"),
            QStringLiteral("Image Editor\n\nVersion: %1\nExecutable: %2")
                .arg(version.isEmpty() ? QStringLiteral("Beta 0.1.0") : version,
                     executable_path.isEmpty()
                         ? QStringLiteral("N/A")
                         : executable_path));
    });

    loadShortcutPreferences();
}

void ImageEditorWindow::registerShortcutAction(
    QAction* action, const QKeySequence& default_sequence) {
    action->setShortcutContext(Qt::WindowShortcut);
    action->setProperty("defaultShortcut",
                        default_sequence.toString(QKeySequence::PortableText));
    action->setShortcut(default_sequence);
    shortcut_actions_.append(action);
}

void ImageEditorWindow::loadShortcutPreferences() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("ImageEditor/KeyboardShortcuts"));
    for (auto* action : shortcut_actions_) {
        const QString default_text = action->property("defaultShortcut").toString();
        const QString stored_text = settings.value(action->objectName(), default_text).toString();
        action->setShortcut(QKeySequence::fromString(
            stored_text, QKeySequence::PortableText));
    }
    settings.endGroup();
    if (settings.status() != QSettings::NoError) {
        const QString cause = QStringLiteral(
            "The keyboard shortcut preferences could not be read.");
        logger_.logError(QStringLiteral("load_keyboard_shortcuts"),
                         cause, settings.fileName());
        statusBar()->showMessage(
            QStringLiteral("Keyboard shortcut preferences could not be loaded; defaults may be in use."),
            5000);
    }
}

void ImageEditorWindow::openShortcutSettings() {
    QList<ShortcutBinding> bindings;
    bindings.reserve(shortcut_actions_.size());
    for (const auto* action : shortcut_actions_) {
        ShortcutBinding binding;
        binding.id = action->objectName();
        binding.label = action->text();
        binding.default_sequence = QKeySequence::fromString(
            action->property("defaultShortcut").toString(),
            QKeySequence::PortableText);
        binding.sequence = action->shortcut();
        bindings.append(binding);
    }

    ShortcutSettingsDialog dialog(bindings, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto updated_bindings = dialog.bindings();

    QSettings settings;
    settings.beginGroup(QStringLiteral("ImageEditor/KeyboardShortcuts"));
    for (const auto& binding : updated_bindings) {
        settings.setValue(binding.id,
                          binding.sequence.toString(QKeySequence::PortableText));
    }
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        const QString cause = QStringLiteral(
            "The keyboard shortcut preferences could not be saved.");
        logger_.logError(QStringLiteral("save_keyboard_shortcuts"), cause);
        QMessageBox::warning(this, QStringLiteral("Settings Error"), cause);
        return;
    }

    for (const auto& binding : updated_bindings) {
        for (auto* action : shortcut_actions_) {
            if (action->objectName() == binding.id) {
                action->setShortcut(binding.sequence);
                break;
            }
        }
    }
}

void ImageEditorWindow::updateView(bool preserveCanvasView) {
    const QImage rendered = session_.renderedImage();
    canvas_->setImage(rendered, !preserveCanvasView);
    tool_sidebar_->setDocumentAvailable(session_.hasSource());
    tool_sidebar_->setPaintingAllowed(session_.hasSource() &&
                                      session_.selectedLayerIsEditable());
    layer_panel_->setLayers(session_.data().layers, session_.selectedLayerId(),
                            session_.renderedLayerThumbnails(QSize(
                                LayerPanel::kThumbnailWidth,
                                LayerPanel::kThumbnailHeight)));
    updateToolOptions();
    canvas_->setBrush(tool_sidebar_->brushColor(), brush_size_spin_->value());
    undo_action_->setEnabled(session_.canUndo());
    redo_action_->setEnabled(session_.canRedo());
    save_action_->setEnabled(session_.hasSource());
    save_as_action_->setEnabled(session_.hasSource());
    export_action_->setEnabled(session_.hasSource());
    relink_action_->setEnabled(session_.sourceIsMissing());
    const bool selected_layer_editable = session_.hasSource() &&
        session_.selectedLayerIsEditable();
    crop_action_->setEnabled(selected_layer_editable);
    rotate_left_action_->setEnabled(selected_layer_editable);
    rotate_right_action_->setEnabled(selected_layer_editable);
    flip_horizontal_action_->setEnabled(selected_layer_editable);
    flip_vertical_action_->setEnabled(selected_layer_editable);
    fit_action_->setEnabled(session_.hasSource());
    cancel_crop_action_->setEnabled(crop_action_->isChecked() && selected_layer_editable);
    paint_tool_action_->setEnabled(selected_layer_editable);

    QString title = QStringLiteral("Image Editor");
    if (!session_.documentPath().isEmpty()) {
        title = QFileInfo(session_.documentPath()).fileName() + QStringLiteral(" — Image Editor");
    } else if (!session_.sourcePath().isEmpty()) {
        title = QFileInfo(session_.sourcePath()).fileName() + QStringLiteral(" — Image Editor");
    } else if (session_.hasDocument()) {
        title = QStringLiteral("Untitled Canvas — Image Editor");
    }
    if (session_.isDirty()) title.prepend('*');
    setWindowTitle(title);

    if (!session_.hasSource()) {
        status_label_->setText(session_.sourceIsMissing()
            ? QStringLiteral("Source image is missing — relink it to continue")
            : QStringLiteral("No image open"));
        return;
    }
    const QSize size = rendered.size();
    status_label_->setText(QStringLiteral("%1 × %2 px  |  %3%")
        .arg(size.width()).arg(size.height())
        .arg(static_cast<int>(canvas_->zoomFactor() * 100.0)));
}

void ImageEditorWindow::createNewCanvas() {
    NewCanvasDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !confirmDiscardOrSave()) return;

    QString error;
    if (!session_.createCanvas(dialog.canvasSize(), dialog.backgroundColor(), &error)) {
        reportError(QStringLiteral("create_canvas"), error);
        return;
    }
    deactivateCanvasTools();
    updateView();
    statusBar()->showMessage(QStringLiteral("New canvas created"), 3000);
}

bool ImageEditorWindow::confirmDiscardOrSave() {
    if (!session_.isDirty()) return true;
    QMessageBox prompt(QMessageBox::Warning,
                       QStringLiteral("Unsaved image edits"),
                       QStringLiteral("Save the changes to this image document?"),
                       QMessageBox::NoButton, this);
    auto* save = prompt.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    auto* discard = prompt.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.exec();
    if (prompt.clickedButton() == cancel) return false;
    if (prompt.clickedButton() == discard) return true;
    return prompt.clickedButton() == save && saveToPath();
}

void ImageEditorWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Image"), {}, imageFilter());
    if (path.isEmpty()) return;
    static_cast<void>(openImagePath(path));
}

bool ImageEditorWindow::openImagePath(const QString& path) {
    if (path.isEmpty()) return false;
    if (!confirmDiscardOrSave()) return false;
    QString error;
    if (!session_.openImage(path, &error)) {
        reportError(QStringLiteral("open_image"), error, path);
        return false;
    }
    deactivateCanvasTools();
    updateView();
    statusBar()->showMessage(QStringLiteral("Image opened"), 3000);
    return true;
}

void ImageEditorWindow::openDocument() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Editable Image Document"), {},
        QStringLiteral("Image Editor documents (*.cimg)"));
    if (path.isEmpty()) return;
    static_cast<void>(openDocumentPath(path));
}

bool ImageEditorWindow::openDocumentPath(const QString& path) {
    if (path.isEmpty()) return false;
    if (!confirmDiscardOrSave()) return false;
    QString error;
    if (!session_.openDocument(path, &error)) {
        reportError(QStringLiteral("open_document"), error, path);
        return false;
    }
    deactivateCanvasTools();
    updateView();
    if (session_.sourceIsMissing()) {
        QMessageBox prompt(QMessageBox::Warning,
                           QStringLiteral("Source image is missing"),
                           QStringLiteral("The editable document is open, but its source image could not be found."),
                           QMessageBox::NoButton, this);
        auto* relink = prompt.addButton(QStringLiteral("Relink Source..."), QMessageBox::AcceptRole);
        prompt.addButton(QMessageBox::Close);
        prompt.exec();
        if (prompt.clickedButton() == relink) relinkSource();
    }
    return true;
}

void ImageEditorWindow::relinkSource() {
    if (!session_.sourceIsMissing()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Relink Source Image"), {}, imageFilter());
    if (path.isEmpty()) return;
    QString error;
    if (!session_.relinkSource(path, &error)) {
        reportError(QStringLiteral("relink_source"), error, path);
        return;
    }
    deactivateCanvasTools();
    updateView();
    statusBar()->showMessage(QStringLiteral("Source image relinked"), 3000);
}

bool ImageEditorWindow::saveToPath(QString path) {
    const QString previous_recovery = recovery_store_.pathFor(session_);
    if (path.isEmpty() && session_.documentPath().isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Editable Image Document"), {},
            QStringLiteral("Image Editor document (*.cimg)"));
    }
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("cimg"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".cimg");
    }

    QString error;
    if (!session_.saveDocument(path, &error)) {
        reportError(QStringLiteral("save_document"), error, path);
        return false;
    }
    static_cast<void>(recovery_store_.remove(previous_recovery));
    updateView();
    statusBar()->showMessage(QStringLiteral("Editable document saved"), 3000);
    return true;
}

void ImageEditorWindow::saveDocument() {
    static_cast<void>(saveToPath());
}

void ImageEditorWindow::saveDocumentAs() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Editable Image Document As"), {},
        QStringLiteral("Image Editor document (*.cimg)"));
    if (path.isEmpty()) return;
    static_cast<void>(saveToPath(path));
}

void ImageEditorWindow::exportImage() {
    QString selected_filter;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Flattened Image"), {},
        QStringLiteral("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"),
        &selected_filter);
    if (path.isEmpty()) return;
    QString output = path;
    if (QFileInfo(output).suffix().isEmpty()) {
        output += selected_filter.startsWith(QStringLiteral("JPEG"), Qt::CaseInsensitive)
            ? QStringLiteral(".jpg") : QStringLiteral(".png");
    }
    QString error;
    if (!session_.exportImage(output, &error)) {
        reportError(QStringLiteral("export_image"), error, output);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Image exported"), 3000);
}

void ImageEditorWindow::handleCrop(const QRect& crop) {
    crop_action_->setChecked(false);
    QString error;
    if (session_.applyCrop(crop, &error)) {
        updateView();
        statusBar()->showMessage(QStringLiteral("Image cropped"), 3000);
    } else if (!error.isEmpty()) {
        reportError(QStringLiteral("crop_image"), error, session_.sourcePath());
    }
}

void ImageEditorWindow::handlePaintStroke(const QVector<QPointF>& points,
                                          const QColor& color,
                                          int diameter) {
    QString error;
    if (session_.applyPaintStroke(points, color, diameter, &error)) {
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Paint stroke applied"), 1800);
    } else if (!error.isEmpty()) {
        reportError(QStringLiteral("paint_stroke"), error, session_.sourcePath());
    }
}

void ImageEditorWindow::deactivateCanvasTools() {
    tool_sidebar_->setPaintToolActive(false);
    updateToolOptions();
    if (crop_action_ != nullptr && crop_action_->isChecked()) {
        crop_action_->setChecked(false);
    }
    canvas_->setPaintMode(false);
    canvas_->setCropMode(false);
}

void ImageEditorWindow::maybeOfferRecovery() {
    const auto snapshots = recovery_store_.snapshots();
    if (snapshots.isEmpty()) return;
    const QString latest = snapshots.front();
    QMessageBox prompt(QMessageBox::Question,
                       QStringLiteral("Recover image edits?"),
                       QStringLiteral("A recovery snapshot is available. Restore it?"),
                       QMessageBox::NoButton, this);
    auto* restore = prompt.addButton(QStringLiteral("Restore"), QMessageBox::AcceptRole);
    auto* discard = prompt.addButton(QStringLiteral("Discard Snapshot"), QMessageBox::DestructiveRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.exec();
    if (prompt.clickedButton() == discard) {
        static_cast<void>(recovery_store_.remove(latest));
        return;
    }
    if (prompt.clickedButton() != restore) return;

    QString error;
    if (!session_.restoreRecovery(latest, &error)) {
        reportError(QStringLiteral("restore_recovery"), error, latest);
        return;
    }
    deactivateCanvasTools();
    updateView();
    if (session_.sourceIsMissing()) {
        QMessageBox::information(this, QStringLiteral("Source image is missing"),
            QStringLiteral("The recovered document needs its source image to be relinked."));
        relinkSource();
    }
}

void ImageEditorWindow::reportError(const QString& operation,
                                    const QString& cause,
                                    const QString& path) {
    logger_.logError(operation, cause, path);
    QMessageBox::critical(this, QStringLiteral("Image Editor"), cause);
}

void ImageEditorWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardOrSave()) event->accept();
    else event->ignore();
}

} // namespace image_editor
