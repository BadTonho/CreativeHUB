#include "image_editor_window.h"

#include "image_canvas.h"
#include "image_document_store.h"
#include "image_export_dialog.h"
#include "image_export_controller.h"
#include "canvas_size_dialog.h"
#include "new_canvas_dialog.h"
#include "shortcut_settings_dialog.h"
#include "tool_sidebar.h"
#include "layer_panel.h"

#include <QAction>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCryptographicHash>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QLockFile>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSignalBlocker>
#include <QSettings>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QThread>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include <memory>
#include <atomic>
#include <algorithm>
#include <utility>

namespace image_editor {

struct ImageEditorDocumentTab {
    ImageDocumentSession session;
    QWidget* page = nullptr;
    ImageCanvas* canvas = nullptr;
    QStringList selected_object_ids;
    QVector<ImageStackItemData> selected_stack_items;
    QString selected_mask_layer_id;
    QHash<QString, QString> logged_raster_problems;
    QString linked_document_path;
    QString linked_output_path;
    QByteArray linked_document_fingerprint;
};

namespace {

bool editingFieldHasFocus() {
    for (QWidget* widget = QApplication::focusWidget(); widget != nullptr;
         widget = widget->parentWidget()) {
        if (qobject_cast<QLineEdit*>(widget) || qobject_cast<QPlainTextEdit*>(widget) ||
            qobject_cast<QTextEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)) return true;
    }
    return false;
}

QString imageObjectId(const ImageOperation& operation) {
    switch (operation.kind) {
    case OperationKind::PaintStroke: return operation.paint_stroke.id;
    case OperationKind::EraseStroke: return operation.erase_stroke.id;
    case OperationKind::Shape: return operation.shape.id;
    case OperationKind::Text: return operation.text.id;
    case OperationKind::RasterImage: return operation.raster.id;
    default: return {};
    }
}

QString imageFilter() {
    return QStringLiteral("Images (%1)").arg(
        ImageDocumentStore::supportedImageExtensions().join(' '));
}

QByteArray documentFingerprint(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) return {};
        hash.addData(block);
    }
    return hash.result();
}

QString normalizedLinkedPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool sameLinkedPath(const QString& left, const QString& right) {
#if defined(Q_OS_WIN)
    return normalizedLinkedPath(left).compare(
               normalizedLinkedPath(right), Qt::CaseInsensitive) == 0;
#else
    return normalizedLinkedPath(left) == normalizedLinkedPath(right);
#endif
}

QIcon shapePaletteIcon(ImageShapeKind kind) {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (kind == ImageShapeKind::Line) {
        painter.setPen(QPen(QColor(238, 196, 88), 3.0, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(5.0, 26.0), QPointF(27.0, 5.0));
    } else if (kind == ImageShapeKind::Rectangle) {
        painter.setPen(QPen(QColor(31, 38, 48), 1.6));
        painter.setBrush(QColor(80, 155, 210, 120));
        painter.drawRect(QRectF(5.0, 6.0, 22.0, 20.0));
    } else {
        painter.setPen(QPen(QColor(31, 38, 48), 1.6));
        painter.setBrush(QColor(225, 125, 170, 120));
        painter.drawEllipse(QRectF(5.0, 6.0, 22.0, 20.0));
    }
    return QIcon(icon);
}

ImageExportOptions loadJpegExportPreferences(bool* read_succeeded,
                                            QString* settings_path) {
    ImageExportOptions options;
    QSettings settings;
    settings.beginGroup(QStringLiteral("ImageEditor/Export"));

    bool quality_is_integer = false;
    const int stored_quality = settings.value(
        QStringLiteral("jpegQuality"), options.jpeg_quality).toInt(&quality_is_integer);
    if (quality_is_integer && stored_quality >= 0 && stored_quality <= 100) {
        options.jpeg_quality = stored_quality;
    }
    const QColor stored_background(settings.value(
        QStringLiteral("jpegBackground"),
        options.jpeg_background.name(QColor::HexArgb)).toString());
    if (stored_background.isValid() && stored_background.alpha() == 255) {
        options.jpeg_background = stored_background;
    }

    settings.endGroup();
    if (read_succeeded != nullptr) {
        *read_succeeded = settings.status() == QSettings::NoError;
    }
    if (settings_path != nullptr) *settings_path = settings.fileName();
    return options;
}

bool saveJpegExportPreferences(const ImageExportOptions& options,
                               QString* error,
                               QString* settings_path) {
    QSettings settings;
    settings.beginGroup(QStringLiteral("ImageEditor/Export"));
    settings.setValue(QStringLiteral("jpegQuality"), options.jpeg_quality);
    settings.setValue(QStringLiteral("jpegBackground"),
                      options.jpeg_background.name(QColor::HexArgb));
    settings.endGroup();
    settings.sync();
    if (settings_path != nullptr) *settings_path = settings.fileName();
    if (settings.status() == QSettings::NoError) return true;
    if (error != nullptr) {
        *error = QStringLiteral("JPEG export preferences could not be saved.");
    }
    return false;
}

} // namespace

ImageEditorWindow::ImageEditorWindow(QWidget* parent, QString recovery_data_directory)
    : QMainWindow(parent),
      recovery_store_(std::move(recovery_data_directory)) {
    empty_document_state_ = std::make_unique<ImageEditorDocumentTab>();
    setWindowTitle(QStringLiteral("Image Editor"));
    resize(1180, 760);

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tool_sidebar_ = new ToolSidebar(central);
    layout->addWidget(tool_sidebar_);

    auto* document_workspace = new QWidget(central);
    auto* document_layout = new QVBoxLayout(document_workspace);
    document_layout->setContentsMargins(0, 0, 0, 0);
    document_layout->setSpacing(0);
    auto* tab_row = new QWidget(document_workspace);
    auto* tab_row_layout = new QHBoxLayout(tab_row);
    tab_row_layout->setContentsMargins(0, 0, 4, 0);
    tab_row_layout->setSpacing(2);
    document_tab_bar_ = new QTabBar(tab_row);
    document_tab_bar_->setObjectName(QStringLiteral("imageDocumentTabBar"));
    document_tab_bar_->setMovable(false);
    document_tab_bar_->setTabsClosable(true);
    document_tab_bar_->setExpanding(false);
    tab_row_layout->addWidget(document_tab_bar_, 1);
    new_document_tab_button_ = new QToolButton(tab_row);
    new_document_tab_button_->setObjectName(QStringLiteral("newDocumentTabButton"));
    new_document_tab_button_->setText(QStringLiteral("+"));
    new_document_tab_button_->setAccessibleName(QStringLiteral("Open a new document tab"));
    new_document_tab_button_->setToolTip(QStringLiteral("Open a new document tab"));
    tab_row_layout->addWidget(new_document_tab_button_);
    document_layout->addWidget(tab_row);
    document_stack_ = new QStackedWidget(document_workspace);
    document_stack_->setObjectName(QStringLiteral("imageDocumentStack"));
    document_layout->addWidget(document_stack_, 1);
    layout->addWidget(document_workspace, 1);
    setCentralWidget(central);

    status_label_ = new QLabel(QStringLiteral("No image open"), this);
    status_label_->setObjectName(QStringLiteral("imageStatusLabel"));
    statusBar()->addWidget(status_label_, 1);

    createToolOptionsBar();
    createLayerPanel();
    createActions();
    connect(document_tab_bar_, &QTabBar::currentChanged, this,
            [this](int index) { activateDocumentTab(index); });
    connect(document_tab_bar_, &QTabBar::tabCloseRequested, this,
            [this](int index) { closeDocumentTab(index); });
    connect(document_tab_bar_, &QTabBar::tabMoved, this,
            [this](int from, int to) {
                if (from < 0 || to < 0 || from >= document_tabs_.size() ||
                    to >= document_tabs_.size()) return;
                document_tabs_.move(from, to);
                if (active_document_tab_ == from) active_document_tab_ = to;
                else if (from < active_document_tab_ && to >= active_document_tab_) --active_document_tab_;
                else if (from > active_document_tab_ && to <= active_document_tab_) ++active_document_tab_;
                updateDocumentTabLabel();
            });
    connect(new_document_tab_button_, &QToolButton::clicked,
            this, [this]() { openNewTabMenu(); });
    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget*, QWidget*) { updateDeleteActions(); });
    connect(layer_panel_, &LayerPanel::deletionSelectionChanged, this,
            [this]() {
                const auto selected_items = layer_panel_->selectedStackItems();
                selectedStackItems() = selected_items.size() > 1
                    ? selected_items : QVector<ImageStackItemData>{};
                updateDeleteActions();
            });
    connect(layer_panel_, &LayerPanel::quickExportRequested, this,
            [this]() { quick_export_action_->trigger(); });
    connect(tool_sidebar_, &ToolSidebar::activeToolChanged,
            this, [this](ToolSidebar::Tool tool) { updateCanvasToolState(tool); });
    connect(tool_sidebar_, &ToolSidebar::shapesPaletteRequested,
            this, [this]() { openShapePalette(); });
    connect(tool_sidebar_, &ToolSidebar::brushColorChanged,
            this, [this](const QColor&) { updateCanvasBrush(); });
    connect(layer_panel_, &LayerPanel::layerSelected, this, [this](const QString& id) {
        static_cast<void>(activeSession().selectLayer(id));
        selectedMaskLayerId().clear();
        selectedObjectIds().clear();
        for (const auto& object : activeSession().visibleObjects())
            if (object.layer_id == id && object.operation.kind == OperationKind::RasterImage) {
                selectedObjectIds() = {object.operation.raster.id};
                break;
            }
        updateObjectPlacements();
        if (!activeSession().selectedLayerIsEditable() &&
            tool_sidebar_->activeTool() != ToolSidebar::Tool::Select &&
            tool_sidebar_->activeTool() != ToolSidebar::Tool::Shapes) {
            deactivateCanvasTools();
        }
        updateSelectionContext();
    });
    connect(layer_panel_, &LayerPanel::groupSelected, this, [this](const QString& id) {
        const bool mask_changed = !selectedMaskLayerId().isEmpty();
        selectedMaskLayerId().clear();
        selectedObjectIds().clear();
        updateObjectPlacements();
        if (activeSession().selectGroup(id) || mask_changed) updateSelectionContext();
    });
    connect(layer_panel_, &LayerPanel::layerMaskSelected, this, [this](const QString& id) {
        static_cast<void>(activeSession().selectLayer(id));
        selectedMaskLayerId() = id;
        selectedObjectIds().clear();
        updateObjectPlacements();
        updateSelectionContext();
    });
    connect(layer_panel_, &LayerPanel::addLayerMaskRequested, this, [this](const QString& id) {
        if (!activeSession().addLayerMask(id)) return;
        static_cast<void>(activeSession().selectLayer(id));
        selectedMaskLayerId() = id;
        selectedObjectIds().clear();
        updateView(true);
    });
    connect(layer_panel_, &LayerPanel::removeLayerMaskRequested, this, [this](const QString& id) {
        if (activeSession().removeLayerMask(id)) updateView(true);
    });
    connect(layer_panel_, &LayerPanel::layerMaskEnabledChanged, this,
            [this](const QString& id, bool enabled) {
                if (activeSession().setLayerMaskEnabled(id, enabled)) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::layerVisibilityChanged, this,
            [this](const QString& id, bool visible) {
                if (activeSession().setLayerVisible(id, visible)) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::layerRenamed, this,
            [this](const QString& id, const QString& name) {
                QString error;
                if (activeSession().renameLayer(id, name, &error)) updateView(true);
                else {
                    updateView(true);
                    if (!error.isEmpty()) statusBar()->showMessage(error, 4000);
                }
            });
    connect(layer_panel_, &LayerPanel::groupVisibilityChanged, this,
            [this](const QString& id, bool visible) {
                if (activeSession().setGroupVisible(id, visible)) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::groupRenamed, this,
            [this](const QString& id, const QString& name) {
                QString error;
                if (activeSession().renameGroup(id, name, &error)) updateView(true);
                else {
                    updateView(true);
                    if (!error.isEmpty()) statusBar()->showMessage(error, 4000);
                }
            });
    connect(layer_panel_, &LayerPanel::addLayerRequested, this, [this]() {
        const QString id = activeSession().addLayer();
        if (id.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("A layer could not be added"), 3000);
            return;
        }
        selectedStackItems().clear();
        static_cast<void>(activeSession().selectLayer(id));
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Layer added"), 1800);
    });
    connect(layer_panel_, &LayerPanel::addGroupRequested, this, [this]() {
        QString error;
        const QString id = activeSession().addGroup(&error);
        if (id.isEmpty()) {
            if (!error.isEmpty()) statusBar()->showMessage(error, 4000);
            return;
        }
        selectedStackItems().clear();
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Group added"), 1800);
    });
    connect(layer_panel_, &LayerPanel::groupSelectedLayersRequested, this,
            [this](const QStringList& ids) {
                QString error;
                if (activeSession().groupLayers(ids, &error).isEmpty()) {
                    if (!error.isEmpty()) statusBar()->showMessage(error, 4000);
                    return;
                }
                selectedStackItems().clear();
                updateView(true);
                statusBar()->showMessage(QStringLiteral("Layers grouped"), 1800);
            });
    connect(layer_panel_, &LayerPanel::deleteStackItemsRequested, this,
            [this](const QVector<ImageStackItemData>& items) {
                activeCanvas()->commitTextEditing();
                if (!activeSession().deleteStackItems(items)) return;
                selectedStackItems().clear();
                selectedObjectIds().clear();
                selectedMaskLayerId().clear();
                if (!activeSession().selectedLayerIsEditable() &&
                    tool_sidebar_->activeTool() != ToolSidebar::Tool::Select &&
                    tool_sidebar_->activeTool() != ToolSidebar::Tool::Shapes) {
                    deactivateCanvasTools();
                }
                updateView(true);
                statusBar()->showMessage(QStringLiteral("Selected layers / groups deleted"), 1800);
            });
    connect(layer_panel_, &LayerPanel::ungroupRequested, this,
            [this](const QString& id) {
                if (!activeSession().ungroup(id)) return;
                selectedStackItems().clear();
                updateView(true);
            });
    connect(layer_panel_, &LayerPanel::moveStackItemRequested, this,
            [this](const QString& id, bool is_group, const QString& target_group_id,
                   qsizetype insertion_index) {
                if (activeSession().moveStackItem(id, is_group, target_group_id, insertion_index)) {
                    updateView(true);
                }
            });
    connect(layer_panel_, &LayerPanel::opacityEditStarted, this,
            [this]() { activeSession().beginLayerOpacityEdit(); });
    connect(layer_panel_, &LayerPanel::stackOpacityChanged, this,
            [this](const QString& id, bool is_group, int opacity) {
                const bool changed = is_group ? activeSession().setGroupOpacity(id, opacity)
                                              : activeSession().setLayerOpacity(id, opacity);
                if (changed) updateView(true);
            });
    connect(layer_panel_, &LayerPanel::opacityEditFinished, this, [this]() {
        activeSession().endLayerOpacityEdit();
        updateView(true);
    });

    autosave_timer_ = new QTimer(this);
    autosave_timer_->setObjectName(QStringLiteral("imageEditorRecoveryTimer"));
    autosave_timer_->setInterval(60'000);
    connect(autosave_timer_, &QTimer::timeout, this, [this]() {
        for (int index = 0; index < document_tabs_.size(); ++index) {
            const ImageDocumentSession& document = document_tabs_.at(index)->session;
            if (!document.isDirty() || !document.hasSource()) continue;
            QString error;
            if (!recovery_store_.save(document, &error)) {
                QString context = document.sourcePath();
                if (context.isEmpty()) context = document.documentPath();
                if (context.isEmpty()) context = document.recoverySessionId();
                logger_.logError(QStringLiteral("autosave"), error, context);
                statusBar()->showMessage(
                    QStringLiteral("Recovery snapshot could not be saved for %1")
                        .arg(context), 5000);
            }
        }
    });
    autosave_timer_->start();

    QTimer::singleShot(0, this, [this]() { maybeOfferRecovery(); });
    updateView(true);
}

ImageEditorWindow::~ImageEditorWindow() {
    for (auto* tab : document_tabs_) delete tab;
}

void ImageEditorWindow::connectCanvas(ImageCanvas* canvas) {
    if (canvas == nullptr) return;
    connect(canvas, &ImageCanvas::cropSelected, this,
            [this](const QRect& crop) { handleCrop(crop); });
    connect(canvas, &ImageCanvas::paintStrokeSelected, this,
            [this, canvas](const QVector<QPointF>& points, const QColor& color, int diameter) {
                handlePaintStroke(points, color, diameter, canvas->areaSelectionClipPath());
            });
    connect(canvas, &ImageCanvas::erasePreviewRequested, this,
            [this, canvas](const QVector<QPointF>& points, int diameter) {
                if (activeCanvas() == nullptr) return;
                activeCanvas()->setTransientImage(
                    editingMask()
                        ? activeSession().renderedImageWithMaskStroke(
                            points, Qt::black, diameter, canvas->areaSelectionClipPath())
                        : activeSession().renderedImageWithEraseStroke(
                            points, diameter, canvas->areaSelectionClipPath()));
            });
    connect(canvas, &ImageCanvas::maskPaintPreviewRequested, this,
            [this, canvas](const QVector<QPointF>& points, const QColor& color, int diameter) {
                if (activeCanvas() != nullptr)
                    activeCanvas()->setTransientImage(
                        activeSession().renderedImageWithMaskStroke(
                            points, color, diameter, canvas->areaSelectionClipPath()));
            });
    connect(canvas, &ImageCanvas::erasePreviewCleared, canvas, [canvas]() {
        canvas->setTransientImage({});
    });
    connect(canvas, &ImageCanvas::eraseStrokeSelected, this,
            [this, canvas](const QVector<QPointF>& points, int diameter) {
                handleEraseStroke(points, diameter, canvas->areaSelectionClipPath());
            });
    connect(canvas, &ImageCanvas::areaSelectionChanged, this,
            [this](bool) { updateDeleteActions(); });
    connect(canvas, &ImageCanvas::areaSelectionRejected, this,
            [this](const QString& reason) { statusBar()->showMessage(reason, 4000); });
    connect(canvas, &ImageCanvas::shapeCreated, this,
            [this](const ImageShapeData& shape) { handleShapeCreated(shape); });
    connect(canvas, &ImageCanvas::textCommitted, this,
            [this](const ImageTextData& text, bool existing) {
                handleTextCommitted(text, existing);
            });
    connect(canvas, &ImageCanvas::textEditingStarted, this,
            [this](const ImageTextData& text, bool existing) {
                handleTextEditingStarted(text, existing);
            });
    connect(canvas, &ImageCanvas::textEditingCancelled, canvas, [canvas]() {
        canvas->setTransientImage({});
    });
    connect(canvas, &ImageCanvas::objectsSelected, this,
            [this](const QStringList& object_ids, const QString& layer_id) {
                if (activeCanvas() == nullptr) return;
                selectedObjectIds() = object_ids;
                const bool mask_changed = !selectedMaskLayerId().isEmpty();
                selectedMaskLayerId().clear();
                activeCanvas()->setMaskEditing(false);
                const bool layer_changed = !layer_id.isEmpty() && activeSession().selectLayer(layer_id);
                if (layer_changed) {
                    selectedMaskLayerId().clear();
                    updateLayerPanel();
                    updateSelectionContext();
                }
                for (const auto& placement : activeSession().visibleObjects()) {
                    if (!selectedObjectIds().contains(imageObjectId(placement.operation))) continue;
                    if (placement.operation.kind == OperationKind::Shape) {
                        shape_style_ = placement.operation.shape;
                        activeCanvas()->setShapeStyle(shape_style_);
                        break;
                    }
                    if (placement.operation.kind == OperationKind::Text) {
                        text_style_ = placement.operation.text;
                        activeCanvas()->setTextStyle(text_style_);
                        break;
                    }
                }
                if (layer_changed || mask_changed) updateView(true);
                else {
                    updateObjectPlacements();
                    updateShapeOptions();
                    updateToolOptions();
                }
            });
    connect(canvas, &ImageCanvas::objectTransformStarted, this,
            [this](const QStringList& object_ids) {
                if (activeCanvas() == nullptr) return;
                for (const auto& object : activeSession().visibleObjects())
                    if (object_ids.contains(imageObjectId(object.operation)) &&
                        object.operation.kind == OperationKind::RasterImage) {
                        activeCanvas()->setTransientImage(activeSession().renderedImage());
                        return;
                    }
                activeCanvas()->setTransientImage(activeSession().renderedImageWithoutObjects(object_ids));
            });
    connect(canvas, &ImageCanvas::objectsPreviewRequested, this,
            [this](const QVector<ImageObjectPlacement>& objects) {
                if (activeCanvas() != nullptr)
                    activeCanvas()->setTransientImage(activeSession().renderedImageWithObjects(objects));
            });
    connect(canvas, &ImageCanvas::imagesDropped, this,
            [this](const QStringList& paths, const QPointF& center) {
                (void)importImagePaths(paths, center);
            });
    connect(canvas, &ImageCanvas::objectsGeometryChanged, this,
            [this](const QVector<ImageObjectPlacement>& objects) {
                handleObjectsGeometryChanged(objects);
            });
    connect(canvas, &ImageCanvas::brushDiameterChanged,
            brush_size_spin_, &QSpinBox::setValue);
}

int ImageEditorWindow::addDocumentTab(bool activate) {
    auto* tab = new ImageEditorDocumentTab;
    tab->page = new QWidget(document_stack_);
    auto* layout = new QHBoxLayout(tab->page);
    layout->setContentsMargins(0, 0, 0, 0);
    tab->canvas = new ImageCanvas(tab->page);
    layout->addWidget(tab->canvas);
    connectCanvas(tab->canvas);

    const int index = document_tabs_.size();
    document_tabs_.append(tab);
    document_stack_->addWidget(tab->page);
    {
        const QSignalBlocker blocker(document_tab_bar_);
        document_tab_bar_->addTab(QStringLiteral("Untitled Canvas"));
        if (activate) document_tab_bar_->setCurrentIndex(index);
    }
    if (activate) activateDocumentTab(index);
    return index;
}

void ImageEditorWindow::activateDocumentTab(int index) {
    if (index < 0 || index >= document_tabs_.size()) {
        if (active_document_tab_ >= 0 && activeCanvas() != nullptr)
            activeCanvas()->commitTextEditing();
        if (active_document_tab_ >= 0) {
            selectedStackItems() = layer_panel_->selectedStackItems();
        }
        active_document_tab_ = -1;
        resetActiveDocumentState();
        updateView();
        return;
    }
    if (index == active_document_tab_) {
        document_stack_->setCurrentWidget(document_tabs_.at(index)->page);
        updateView(true);
        return;
    }

    if (active_document_tab_ >= 0) {
        if (activeCanvas() != nullptr) activeCanvas()->commitTextEditing();
        selectedStackItems() = layer_panel_->selectedStackItems();
    }

    active_document_tab_ = index;
    auto* tab = document_tabs_.at(index);
    document_stack_->setCurrentWidget(tab->page);
    tab->canvas->setShapeStyle(shape_style_);
    tab->canvas->setTextStyle(text_style_);
    updateCanvasToolState(tool_sidebar_->activeTool(), true);
    updateView(true);
}

void ImageEditorWindow::closeDocumentTab(int index) {
    if (importing_ || index < 0 || index >= document_tabs_.size()) return;
    auto* closing = document_tabs_.at(index);
    if (!closing->linked_document_path.isEmpty() ||
        (index == active_document_tab_ && !linkedDocumentPath().isEmpty())) return;

    const int previous_active = active_document_tab_;
    if (index != active_document_tab_) {
        const QSignalBlocker blocker(document_tab_bar_);
        document_tab_bar_->setCurrentIndex(index);
        activateDocumentTab(index);
    }
    if (!confirmDiscardOrSave()) {
        if (previous_active >= 0 && previous_active < document_tabs_.size()) {
            const QSignalBlocker blocker(document_tab_bar_);
            document_tab_bar_->setCurrentIndex(previous_active);
            activateDocumentTab(previous_active);
        }
        return;
    }

    const QString recovery_path = recovery_store_.pathFor(activeSession());
    if (!recovery_path.isEmpty()) static_cast<void>(recovery_store_.remove(recovery_path));
    const int next_active = previous_active >= 0 && previous_active != index
        ? previous_active - (previous_active > index ? 1 : 0)
        : (document_tabs_.size() > 1
               ? static_cast<int>(std::min<qsizetype>(
                     index, document_tabs_.size() - 2))
               : -1);
    document_stack_->removeWidget(closing->page);
    {
        const QSignalBlocker blocker(document_tab_bar_);
        document_tab_bar_->removeTab(index);
    }
    document_tabs_.removeAt(index);
    delete closing->page;
    delete closing;
    active_document_tab_ = -1;
    resetActiveDocumentState();

    if (next_active >= 0 && next_active < document_tabs_.size()) {
        const QSignalBlocker blocker(document_tab_bar_);
        document_tab_bar_->setCurrentIndex(next_active);
        activateDocumentTab(next_active);
    } else {
        updateView();
    }
}

void ImageEditorWindow::openNewTabMenu() {
    if (!linkedDocumentPath().isEmpty() || importing_) return;
    QMenu menu(this);
    new_tab_canvas_action_->setEnabled(true);
    new_tab_open_image_action_->setEnabled(true);
    new_tab_open_document_action_->setEnabled(true);
    menu.addAction(new_tab_canvas_action_);
    menu.addAction(new_tab_open_image_action_);
    menu.addAction(new_tab_open_document_action_);
    menu.exec(new_document_tab_button_->mapToGlobal(
        QPoint(0, new_document_tab_button_->height())));
}

void ImageEditorWindow::updateDocumentTabLabel() {
    if (!hasActiveDocumentTab()) return;
    QString label;
    if (!activeSession().documentPath().isEmpty()) {
        label = QFileInfo(activeSession().documentPath()).fileName();
    } else if (!activeSession().sourcePath().isEmpty()) {
        label = QFileInfo(activeSession().sourcePath()).fileName();
    } else if (activeSession().hasDocument()) {
        label = QStringLiteral("Untitled Canvas");
    } else {
        label = QStringLiteral("New Document");
    }
    if (activeSession().isDirty()) label.prepend('*');
    document_tab_bar_->setTabText(active_document_tab_, label);
    QString tooltip = label;
    if (!activeSession().documentPath().isEmpty()) tooltip += QStringLiteral("\n") + activeSession().documentPath();
    else if (!activeSession().sourcePath().isEmpty()) tooltip += QStringLiteral("\n") + activeSession().sourcePath();
    document_tab_bar_->setTabToolTip(active_document_tab_, tooltip);
}

void ImageEditorWindow::resetActiveDocumentState() {
    activeSession() = ImageDocumentSession{};
    resetActiveDocumentSelection();
    linkedDocumentPath().clear();
    linkedOutputPath().clear();
    linkedDocumentFingerprint().clear();
}

void ImageEditorWindow::resetActiveDocumentSelection() {
    selectedObjectIds().clear();
    selectedStackItems().clear();
    selectedMaskLayerId().clear();
    loggedRasterProblems().clear();
    if (activeCanvas() != nullptr) activeCanvas()->clearAreaSelection();
}

bool ImageEditorWindow::hasActiveDocumentTab() const noexcept {
    return active_document_tab_ >= 0 && active_document_tab_ < document_tabs_.size();
}

ImageEditorDocumentTab& ImageEditorWindow::activeTabState() noexcept {
    return hasActiveDocumentTab()
        ? *document_tabs_.at(active_document_tab_)
        : *empty_document_state_;
}

const ImageEditorDocumentTab& ImageEditorWindow::activeTabState() const noexcept {
    return hasActiveDocumentTab()
        ? *document_tabs_.at(active_document_tab_)
        : *empty_document_state_;
}

ImageDocumentSession& ImageEditorWindow::activeSession() noexcept {
    return activeTabState().session;
}

ImageCanvas* ImageEditorWindow::activeCanvas() noexcept {
    return activeTabState().canvas;
}

QHash<QString, QString>& ImageEditorWindow::loggedRasterProblems() noexcept {
    return activeTabState().logged_raster_problems;
}

QStringList& ImageEditorWindow::selectedObjectIds() noexcept {
    return activeTabState().selected_object_ids;
}

QVector<ImageStackItemData>& ImageEditorWindow::selectedStackItems() noexcept {
    return activeTabState().selected_stack_items;
}

QString& ImageEditorWindow::selectedMaskLayerId() noexcept {
    return activeTabState().selected_mask_layer_id;
}

QString& ImageEditorWindow::linkedDocumentPath() noexcept {
    return activeTabState().linked_document_path;
}

QString& ImageEditorWindow::linkedOutputPath() noexcept {
    return activeTabState().linked_output_path;
}

QByteArray& ImageEditorWindow::linkedDocumentFingerprint() noexcept {
    return activeTabState().linked_document_fingerprint;
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

    tool_size_label_ = new QLabel(QStringLiteral("Brush Size"), paint_size_options_);
    tool_size_label_->setObjectName(QStringLiteral("paintBrushSizeLabel"));
    options_layout->addWidget(tool_size_label_);

    brush_size_slider_ = new QSlider(Qt::Horizontal, paint_size_options_);
    brush_size_slider_->setObjectName(QStringLiteral("paintBrushSizeSlider"));
    brush_size_slider_->setAccessibleName(QStringLiteral("Brush size"));
    brush_size_slider_->setRange(1, ImageDocumentStore::kMaximumPaintBrushDiameter);
    brush_size_slider_->setValue(12);
    brush_size_slider_->setMinimumWidth(140);
    brush_size_slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    options_layout->addWidget(brush_size_slider_, 1);

    brush_size_spin_ = new QSpinBox(paint_size_options_);
    brush_size_spin_->setObjectName(QStringLiteral("paintBrushSizeSpinBox"));
    brush_size_spin_->setAccessibleName(QStringLiteral("Brush size in pixels"));
    brush_size_spin_->setRange(1, ImageDocumentStore::kMaximumPaintBrushDiameter);
    brush_size_spin_->setValue(12);
    brush_size_spin_->setSuffix(QStringLiteral(" px"));
    brush_size_spin_->setFixedWidth(96);
    options_layout->addWidget(brush_size_spin_);

    eraser_preview_check_ = new QCheckBox(QStringLiteral("Preview"), paint_size_options_);
    eraser_preview_check_->setObjectName(QStringLiteral("eraserPreviewCheckBox"));
    eraser_preview_check_->setToolTip(
        QStringLiteral("Show a translucent erase preview and apply it when the stroke ends"));
    eraser_preview_check_->setChecked(false);
    eraser_preview_check_->setVisible(false);
    options_layout->addWidget(eraser_preview_check_);

    paint_options_action_ = new QWidgetAction(tool_options_toolbar_);
    paint_options_action_->setObjectName(QStringLiteral("paintBrushSizeAction"));
    paint_options_action_->setDefaultWidget(paint_size_options_);
    tool_options_toolbar_->addAction(paint_options_action_);
    paint_options_action_->setVisible(false);

    shape_options_widget_ = new QWidget(tool_options_toolbar_);
    shape_options_widget_->setObjectName(QStringLiteral("shapeOptionsWidget"));
    auto* shape_layout = new QHBoxLayout(shape_options_widget_);
    shape_layout->setContentsMargins(8, 3, 8, 3);
    shape_layout->setSpacing(7);
    shape_stroke_check_ = new QCheckBox(QStringLiteral("Stroke"), shape_options_widget_);
    shape_stroke_check_->setObjectName(QStringLiteral("shapeStrokeCheckBox"));
    shape_stroke_check_->setChecked(true);
    shape_layout->addWidget(shape_stroke_check_);
    shape_stroke_color_button_ = new QPushButton(QStringLiteral("Color"), shape_options_widget_);
    shape_stroke_color_button_->setObjectName(QStringLiteral("shapeStrokeColorButton"));
    shape_stroke_color_button_->setFixedWidth(62);
    shape_layout->addWidget(shape_stroke_color_button_);
    shape_fill_check_ = new QCheckBox(QStringLiteral("Fill"), shape_options_widget_);
    shape_fill_check_->setObjectName(QStringLiteral("shapeFillCheckBox"));
    shape_fill_check_->setChecked(true);
    shape_layout->addWidget(shape_fill_check_);
    shape_fill_color_button_ = new QPushButton(QStringLiteral("Color"), shape_options_widget_);
    shape_fill_color_button_->setObjectName(QStringLiteral("shapeFillColorButton"));
    shape_fill_color_button_->setFixedWidth(62);
    shape_layout->addWidget(shape_fill_color_button_);
    shape_layout->addWidget(new QLabel(QStringLiteral("Width"), shape_options_widget_));
    shape_stroke_width_spin_ = new QSpinBox(shape_options_widget_);
    shape_stroke_width_spin_->setObjectName(QStringLiteral("shapeStrokeWidthSpinBox"));
    shape_stroke_width_spin_->setRange(1, ImageDocumentStore::kMaximumShapeStrokeWidth);
    shape_stroke_width_spin_->setValue(2);
    shape_stroke_width_spin_->setSuffix(QStringLiteral(" px"));
    shape_stroke_width_spin_->setFixedWidth(82);
    shape_layout->addWidget(shape_stroke_width_spin_);
    shape_options_action_ = new QWidgetAction(tool_options_toolbar_);
    shape_options_action_->setObjectName(QStringLiteral("shapeOptionsAction"));
    shape_options_action_->setDefaultWidget(shape_options_widget_);
    tool_options_toolbar_->addAction(shape_options_action_);
    shape_options_action_->setVisible(false);

    text_options_widget_ = new QWidget(tool_options_toolbar_);
    text_options_widget_->setObjectName(QStringLiteral("textOptionsWidget"));
    auto* text_layout = new QHBoxLayout(text_options_widget_);
    text_layout->setContentsMargins(8, 3, 8, 3);
    text_layout->setSpacing(7);
    text_font_combo_ = new QFontComboBox(text_options_widget_);
    text_font_combo_->setObjectName(QStringLiteral("textFontComboBox"));
    text_font_combo_->setAccessibleName(QStringLiteral("Text font family"));
    text_font_combo_->setCurrentFont(QFont(text_style_.font_family));
    text_font_combo_->setMinimumWidth(150);
    text_layout->addWidget(text_font_combo_);
    text_size_spin_ = new QSpinBox(text_options_widget_);
    text_size_spin_->setObjectName(QStringLiteral("textSizeSpinBox"));
    text_size_spin_->setAccessibleName(QStringLiteral("Text size in pixels"));
    text_size_spin_->setRange(1, ImageDocumentStore::kMaximumTextFontPixelSize);
    text_size_spin_->setValue(text_style_.font_pixel_size);
    text_size_spin_->setSuffix(QStringLiteral(" px"));
    text_size_spin_->setFixedWidth(88);
    text_layout->addWidget(text_size_spin_);
    text_color_button_ = new QPushButton(QStringLiteral("Color"), text_options_widget_);
    text_color_button_->setObjectName(QStringLiteral("textColorButton"));
    text_color_button_->setAccessibleName(QStringLiteral("Text color"));
    text_color_button_->setFixedWidth(70);
    text_layout->addWidget(text_color_button_);
    text_alignment_combo_ = new QComboBox(text_options_widget_);
    text_alignment_combo_->setObjectName(QStringLiteral("textAlignmentComboBox"));
    text_alignment_combo_->setAccessibleName(QStringLiteral("Text alignment"));
    text_alignment_combo_->addItem(QStringLiteral("Left"),
        static_cast<int>(ImageTextAlignment::Left));
    text_alignment_combo_->addItem(QStringLiteral("Center"),
        static_cast<int>(ImageTextAlignment::Center));
    text_alignment_combo_->addItem(QStringLiteral("Right"),
        static_cast<int>(ImageTextAlignment::Right));
    text_layout->addWidget(text_alignment_combo_);
    text_options_action_ = new QWidgetAction(tool_options_toolbar_);
    text_options_action_->setObjectName(QStringLiteral("textOptionsAction"));
    text_options_action_->setDefaultWidget(text_options_widget_);
    tool_options_toolbar_->addAction(text_options_action_);
    text_options_action_->setVisible(false);

    auto* selection_options = new QWidget(tool_options_toolbar_);
    selection_options->setObjectName(QStringLiteral("selectionOptionsWidget"));
    auto* selection_layout = new QHBoxLayout(selection_options);
    selection_layout->setContentsMargins(8, 3, 8, 3);
    delete_selected_shape_button_ = new QPushButton(
        QStringLiteral("Delete Selected Objects"), selection_options);
    delete_selected_shape_button_->setObjectName(QStringLiteral("deleteSelectedShapeButton"));
    delete_selected_shape_button_->setAccessibleName(QStringLiteral("Delete selected objects"));
    selection_layout->addWidget(delete_selected_shape_button_);
    selection_options_action_ = new QWidgetAction(tool_options_toolbar_);
    selection_options_action_->setObjectName(QStringLiteral("selectionOptionsAction"));
    selection_options_action_->setDefaultWidget(selection_options);
    tool_options_toolbar_->addAction(selection_options_action_);
    selection_options_action_->setVisible(false);

    area_selection_options_widget_ = new QWidget(tool_options_toolbar_);
    area_selection_options_widget_->setObjectName(QStringLiteral("areaSelectionOptionsWidget"));
    auto* area_layout = new QHBoxLayout(area_selection_options_widget_);
    area_layout->setContentsMargins(8, 3, 8, 3);
    area_layout->setSpacing(7);
    area_layout->addWidget(new QLabel(QStringLiteral("Shape"), area_selection_options_widget_));
    area_selection_shape_combo_ = new QComboBox(area_selection_options_widget_);
    area_selection_shape_combo_->setObjectName(QStringLiteral("areaSelectionShapeComboBox"));
    area_selection_shape_combo_->setAccessibleName(QStringLiteral("Area selection shape"));
    area_selection_shape_combo_->addItem(QStringLiteral("Rectangle"), 0);
    area_selection_shape_combo_->addItem(QStringLiteral("Ellipse"), 1);
    area_layout->addWidget(area_selection_shape_combo_);
    area_layout->addWidget(new QLabel(QStringLiteral("Mode"), area_selection_options_widget_));
    area_selection_mode_combo_ = new QComboBox(area_selection_options_widget_);
    area_selection_mode_combo_->setObjectName(QStringLiteral("areaSelectionModeComboBox"));
    area_selection_mode_combo_->setAccessibleName(QStringLiteral("Area selection mode"));
    area_selection_mode_combo_->addItem(QStringLiteral("Replace"), 0);
    area_selection_mode_combo_->addItem(QStringLiteral("Add"), 1);
    area_selection_mode_combo_->addItem(QStringLiteral("Subtract"), 2);
    area_layout->addWidget(area_selection_mode_combo_);
    area_selection_options_action_ = new QWidgetAction(tool_options_toolbar_);
    area_selection_options_action_->setObjectName(QStringLiteral("areaSelectionOptionsAction"));
    area_selection_options_action_->setDefaultWidget(area_selection_options_widget_);
    tool_options_toolbar_->addAction(area_selection_options_action_);
    area_selection_options_action_->setVisible(false);

    const auto refreshColorButton = [](QPushButton* button, const QColor& color) {
        button->setStyleSheet(QStringLiteral("background-color: %1;").arg(
            color.name(QColor::HexArgb)));
        button->setToolTip(color.name(QColor::HexArgb));
    };
    refreshColorButton(shape_stroke_color_button_, shape_style_.stroke_color);
    refreshColorButton(shape_fill_color_button_, shape_style_.fill_color);
    const auto refreshTextColor = [this]() {
        text_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
            text_style_.color.name(QColor::HexArgb)));
        text_color_button_->setToolTip(text_style_.color.name(QColor::HexArgb));
    };
    refreshTextColor();

    connect(brush_size_slider_, &QSlider::valueChanged,
            brush_size_spin_, &QSpinBox::setValue);
    connect(brush_size_spin_, &QSpinBox::valueChanged, this, [this](int diameter) {
        brush_size_slider_->setValue(diameter);
        if (tool_sidebar_->activeTool() == ToolSidebar::Tool::Paint) {
            paint_diameter_ = diameter;
        } else if (tool_sidebar_->activeTool() == ToolSidebar::Tool::Eraser) {
            eraser_diameter_ = diameter;
        }
        updateCanvasBrush();
    });
    connect(eraser_preview_check_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (activeCanvas() != nullptr) activeCanvas()->setEraserPreviewEnabled(enabled);
    });
    const auto updateAreaSelectionOptions = [this]() {
        area_selection_shape_ = area_selection_shape_combo_->currentData().toInt();
        area_selection_mode_ = area_selection_mode_combo_->currentData().toInt();
        if (activeCanvas() != nullptr) {
            activeCanvas()->setAreaSelectionOptions(
                area_selection_shape_ == 1 ? ImageCanvas::AreaSelectionShape::Ellipse
                    : ImageCanvas::AreaSelectionShape::Rectangle,
                area_selection_mode_ == 1 ? ImageCanvas::AreaSelectionCombineMode::Add
                    : (area_selection_mode_ == 2
                        ? ImageCanvas::AreaSelectionCombineMode::Subtract
                        : ImageCanvas::AreaSelectionCombineMode::Replace));
        }
    };
    connect(area_selection_shape_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [updateAreaSelectionOptions](int) { updateAreaSelectionOptions(); });
    connect(area_selection_mode_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [updateAreaSelectionOptions](int) { updateAreaSelectionOptions(); });
    updateAreaSelectionOptions();
    connect(shape_stroke_check_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (shape_style_.kind == ImageShapeKind::Line && !enabled) enabled = true;
        if (!enabled && !shape_style_.fill_enabled) {
            shape_style_.fill_enabled = true;
            const QSignalBlocker blocker(shape_fill_check_);
            shape_fill_check_->setChecked(true);
        }
        shape_style_.stroke_enabled = enabled;
        updateShapeOptions();
        activeCanvas()->setShapeStyle(shape_style_);
        applyShapeStyleToSelection();
    });
    connect(shape_fill_check_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (shape_style_.kind == ImageShapeKind::Line) enabled = false;
        if (!enabled && !shape_style_.stroke_enabled) {
            shape_style_.stroke_enabled = true;
            const QSignalBlocker blocker(shape_stroke_check_);
            shape_stroke_check_->setChecked(true);
        }
        shape_style_.fill_enabled = enabled;
        updateShapeOptions();
        activeCanvas()->setShapeStyle(shape_style_);
        applyShapeStyleToSelection();
    });
    connect(shape_stroke_color_button_, &QPushButton::clicked, this, [this, refreshColorButton]() {
        const QColor color = QColorDialog::getColor(
            shape_style_.stroke_color, this, QStringLiteral("Shape Stroke Color"),
            QColorDialog::ShowAlphaChannel);
        if (!color.isValid()) return;
        shape_style_.stroke_color = color;
        refreshColorButton(shape_stroke_color_button_, color);
        activeCanvas()->setShapeStyle(shape_style_);
        applyShapeStyleToSelection();
    });
    connect(shape_fill_color_button_, &QPushButton::clicked, this, [this, refreshColorButton]() {
        const QColor color = QColorDialog::getColor(
            shape_style_.fill_color, this, QStringLiteral("Shape Fill Color"),
            QColorDialog::ShowAlphaChannel);
        if (!color.isValid()) return;
        shape_style_.fill_color = color;
        refreshColorButton(shape_fill_color_button_, color);
        activeCanvas()->setShapeStyle(shape_style_);
        applyShapeStyleToSelection();
    });
    connect(shape_stroke_width_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int width) {
                shape_style_.stroke_width = width;
                activeCanvas()->setShapeStyle(shape_style_);
                applyShapeStyleToSelection();
            });
    connect(text_font_combo_, &QFontComboBox::currentFontChanged, this,
            [this](const QFont& font) {
                text_style_.font_family = font.family();
                activeCanvas()->setTextStyle(text_style_);
                applyTextStyleToSelection();
            });
    connect(text_size_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int size) {
                text_style_.font_pixel_size = size;
                activeCanvas()->setTextStyle(text_style_);
                applyTextStyleToSelection();
            });
    connect(text_color_button_, &QPushButton::clicked, this,
            [this, refreshTextColor]() {
                const QColor color = QColorDialog::getColor(
                    text_style_.color, this, QStringLiteral("Text Color"),
                    QColorDialog::ShowAlphaChannel);
                if (!color.isValid()) return;
                text_style_.color = color;
                refreshTextColor();
                activeCanvas()->setTextStyle(text_style_);
                applyTextStyleToSelection();
            });
    connect(text_alignment_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                text_style_.alignment = static_cast<ImageTextAlignment>(
                    text_alignment_combo_->itemData(index).toInt());
                activeCanvas()->setTextStyle(text_style_);
                applyTextStyleToSelection();
            });
    connect(delete_selected_shape_button_, &QPushButton::clicked,
            this, [this]() { deleteSelectedObjects(); });

    createShapePalette();
}

void ImageEditorWindow::createShapePalette() {
    shape_palette_window_ = new QDialog(
        this, Qt::Tool | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            Qt::WindowCloseButtonHint);
    shape_palette_window_->setObjectName(QStringLiteral("shapePaletteWindow"));
    shape_palette_window_->setWindowTitle(QStringLiteral("Shapes"));
    shape_palette_window_->setModal(false);

    auto* layout = new QVBoxLayout(shape_palette_window_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    shape_palette_button_group_ = new QButtonGroup(shape_palette_window_);
    shape_palette_button_group_->setObjectName(QStringLiteral("shapePaletteButtonGroup"));
    shape_palette_button_group_->setExclusive(true);

    const auto addShapeButton = [this, layout](ImageShapeKind kind,
                                               const QString& text,
                                               const QString& object_name) {
        auto* button = new QToolButton(shape_palette_window_);
        button->setObjectName(object_name);
        button->setAccessibleName(text + QStringLiteral(" shape"));
        button->setIcon(shapePaletteIcon(kind));
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        const QString article = kind == ImageShapeKind::Ellipse
            ? QStringLiteral("an") : QStringLiteral("a");
        button->setToolTip(QStringLiteral("Draw %1 %2").arg(article, text.toLower()));
        button->setCheckable(true);
        button->setFixedSize(36, 36);
        shape_palette_button_group_->addButton(button, static_cast<int>(kind));
        shape_palette_buttons_.append(button);
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, kind]() { setShapeKind(kind); });
    };
    addShapeButton(ImageShapeKind::Line, QStringLiteral("Line"),
                   QStringLiteral("shapePaletteLineButton"));
    addShapeButton(ImageShapeKind::Rectangle, QStringLiteral("Rectangle"),
                   QStringLiteral("shapePaletteRectangleButton"));
    addShapeButton(ImageShapeKind::Ellipse, QStringLiteral("Ellipse"),
                   QStringLiteral("shapePaletteEllipseButton"));

    shape_palette_window_->adjustSize();
    updateShapePalette();
}

void ImageEditorWindow::updateToolOptions() {
    if (paint_options_action_ == nullptr || paint_size_options_ == nullptr ||
        tool_sidebar_ == nullptr || activeCanvas() == nullptr) return;
    const ToolSidebar::Tool active_tool = tool_sidebar_->activeTool();
    const bool tool_active = active_tool != ToolSidebar::Tool::None && activeSession().hasSource();
    const bool paint_active = active_tool == ToolSidebar::Tool::Paint && activeCanvas()->paintMode();
    const bool eraser_active = active_tool == ToolSidebar::Tool::Eraser && activeCanvas()->eraserMode();
    paint_options_action_->setVisible(tool_active && (paint_active || eraser_active));
    paint_size_options_->setVisible(tool_active && (paint_active || eraser_active));
    paint_size_options_->setEnabled(tool_active);
    tool_size_label_->setText(editingMask() ? QStringLiteral("Mask Brush Size")
        : (eraser_active ? QStringLiteral("Eraser Size") : QStringLiteral("Brush Size")));
    brush_size_slider_->setAccessibleName(
        eraser_active ? QStringLiteral("Eraser size") : QStringLiteral("Brush size"));
    brush_size_spin_->setAccessibleName(
        eraser_active ? QStringLiteral("Eraser size in pixels")
                      : QStringLiteral("Brush size in pixels"));
    eraser_preview_check_->setVisible(eraser_active && !editingMask());
    const auto tool = tool_sidebar_->activeTool();
    const auto placements = activeSession().visibleObjects();
    const bool has_selected_shape = std::any_of(
        placements.cbegin(), placements.cend(),
        [this](const ImageObjectPlacement& placement) {
            return placement.operation.kind == OperationKind::Shape &&
                selectedObjectIds().contains(placement.operation.shape.id);
        });
    const bool shapes_active = tool == ToolSidebar::Tool::Shapes ||
        (tool == ToolSidebar::Tool::Select && has_selected_shape);
    shape_options_action_->setVisible(tool_active && shapes_active);
    shape_options_widget_->setVisible(tool_active && shapes_active);
    const bool has_selected_text = std::any_of(
        placements.cbegin(), placements.cend(),
        [this](const ImageObjectPlacement& placement) {
            return placement.operation.kind == OperationKind::Text &&
                selectedObjectIds().contains(placement.operation.text.id);
        });
    const bool text_options_active = tool == ToolSidebar::Tool::Text ||
        (tool == ToolSidebar::Tool::Select && has_selected_text);
    text_options_action_->setVisible(tool_active && text_options_active);
    text_options_widget_->setVisible(tool_active && text_options_active);
    selection_options_action_->setVisible(tool_active && tool == ToolSidebar::Tool::Select);
    area_selection_options_action_->setVisible(
        tool_active && tool == ToolSidebar::Tool::AreaSelect);
    area_selection_options_widget_->setVisible(
        tool_active && tool == ToolSidebar::Tool::AreaSelect);
    updateTextOptions();
    updateShapeOptions();
}

void ImageEditorWindow::updateTextOptions() {
    if (text_options_widget_ == nullptr) return;
    const QSignalBlocker font_blocker(text_font_combo_);
    const QSignalBlocker size_blocker(text_size_spin_);
    const QSignalBlocker alignment_blocker(text_alignment_combo_);
    text_font_combo_->setCurrentFont(QFont(text_style_.font_family));
    text_size_spin_->setValue(text_style_.font_pixel_size);
    const int alignment_index = text_alignment_combo_->findData(
        static_cast<int>(text_style_.alignment));
    if (alignment_index >= 0) text_alignment_combo_->setCurrentIndex(alignment_index);
    text_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
        text_style_.color.name(QColor::HexArgb)));
    text_color_button_->setToolTip(text_style_.color.name(QColor::HexArgb));
}

void ImageEditorWindow::updateShapeOptions() {
    if (shape_options_widget_ == nullptr) return;
    const QSignalBlocker stroke_blocker(shape_stroke_check_);
    const QSignalBlocker fill_blocker(shape_fill_check_);
    const QSignalBlocker width_blocker(shape_stroke_width_spin_);
    updateShapePalette();
    shape_stroke_check_->setChecked(shape_style_.stroke_enabled);
    shape_fill_check_->setChecked(shape_style_.fill_enabled);
    shape_stroke_width_spin_->setValue(shape_style_.stroke_width);
    shape_stroke_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
        shape_style_.stroke_color.name(QColor::HexArgb)));
    shape_stroke_color_button_->setToolTip(shape_style_.stroke_color.name(QColor::HexArgb));
    shape_fill_color_button_->setStyleSheet(QStringLiteral("background-color: %1;").arg(
        shape_style_.fill_color.name(QColor::HexArgb)));
    shape_fill_color_button_->setToolTip(shape_style_.fill_color.name(QColor::HexArgb));
    const auto placements = activeSession().visibleObjects();
    const bool selecting_objects = tool_sidebar_->activeTool() == ToolSidebar::Tool::Select;
    const bool can_edit_selected_shape = selecting_objects &&
        std::any_of(placements.cbegin(), placements.cend(),
            [this](const ImageObjectPlacement& placement) {
                return placement.operation.kind == OperationKind::Shape &&
                    selectedObjectIds().contains(placement.operation.shape.id);
            });
    const bool has_selected_fillable_shape = selecting_objects &&
        std::any_of(placements.cbegin(), placements.cend(),
            [this](const ImageObjectPlacement& placement) {
                return placement.operation.kind == OperationKind::Shape &&
                    placement.operation.shape.kind != ImageShapeKind::Line &&
                    selectedObjectIds().contains(placement.operation.shape.id);
            });
    const bool shape_creation_active = tool_sidebar_->activeTool() == ToolSidebar::Tool::Shapes;
    const bool can_edit_fill_and_stroke = shape_creation_active
        ? shape_style_.kind != ImageShapeKind::Line
        : has_selected_fillable_shape;
    updateDeleteActions();
    shape_stroke_check_->setEnabled(can_edit_fill_and_stroke);
    shape_fill_check_->setEnabled(can_edit_fill_and_stroke);
    shape_stroke_color_button_->setEnabled(can_edit_selected_shape || shape_creation_active);
    shape_fill_color_button_->setEnabled(has_selected_fillable_shape ||
        (shape_creation_active && shape_style_.kind != ImageShapeKind::Line));
    shape_stroke_width_spin_->setEnabled(can_edit_selected_shape || shape_creation_active);
}

void ImageEditorWindow::updateShapePalette() {
    if (shape_palette_button_group_ == nullptr) return;
    const bool enabled = activeSession().hasSource();
    for (auto* button : shape_palette_buttons_) {
        if (button != nullptr) button->setEnabled(enabled);
    }
    if (auto* selected = shape_palette_button_group_->button(
            static_cast<int>(shape_style_.kind)); selected != nullptr) {
        selected->setChecked(true);
    }
}

void ImageEditorWindow::openShapePalette() {
    if (shape_palette_window_ == nullptr || tool_sidebar_ == nullptr) return;
    if (!shape_palette_positioned_) {
        auto* shapes_button = tool_sidebar_->findChild<QToolButton*>(
            QStringLiteral("shapesToolButton"));
        if (shapes_button != nullptr) {
            shape_palette_window_->adjustSize();
            const QPoint button_origin = shapes_button->mapToGlobal(QPoint(0, 0));
            QPoint position = shapes_button->mapToGlobal(
                QPoint(shapes_button->width() + 6, 0));
            QScreen* screen = QGuiApplication::screenAt(position);
            if (screen == nullptr) screen = QGuiApplication::primaryScreen();
            if (screen != nullptr) {
                const QRect available = screen->availableGeometry();
                if (position.x() + shape_palette_window_->width() > available.right() + 1) {
                    position.setX(button_origin.x() - shape_palette_window_->width() - 6);
                }
                const int max_x = std::max(available.left(),
                    available.right() - shape_palette_window_->width() + 1);
                const int max_y = std::max(available.top(),
                    available.bottom() - shape_palette_window_->height() + 1);
                position.setX(std::clamp(position.x(), available.left(), max_x));
                position.setY(std::clamp(position.y(), available.top(), max_y));
            }
            shape_palette_window_->move(position);
            shape_palette_positioned_ = true;
        }
    }
    updateShapePalette();
    shape_palette_window_->show();
    shape_palette_window_->raise();
}

void ImageEditorWindow::setShapeKind(ImageShapeKind kind) {
    if (tool_sidebar_ == nullptr || activeCanvas() == nullptr) return;
    tool_sidebar_->setActiveTool(ToolSidebar::Tool::Shapes);
    if (tool_sidebar_->activeTool() != ToolSidebar::Tool::Shapes) return;
    if (shape_style_.kind != kind) {
        shape_style_.kind = kind;
        if (kind == ImageShapeKind::Line) shape_style_.fill_enabled = false;
        activeCanvas()->setShapeStyle(shape_style_);
        applyShapeStyleToSelection(true);
    }
    updateShapePalette();
    updateShapeOptions();
}

void ImageEditorWindow::updateObjectPlacements() {
    if (relink_raster_action_) relink_raster_action_->setEnabled(activeSession().hasSource() && !importing_ &&
        selectedObjectIds().size() == 1 && activeSession().findRaster(selectedObjectIds().front(), nullptr));
    const auto placements = activeSession().visibleObjects();
    for (qsizetype index = selectedObjectIds().size(); index > 0; --index) {
        const QString& id = selectedObjectIds().at(index - 1);
        const bool visible = std::any_of(placements.cbegin(), placements.cend(),
            [&id](const ImageObjectPlacement& placement) {
                const auto& operation = placement.operation;
                const QString object_id = imageObjectId(operation);
                return object_id == id;
            });
        if (!visible) selectedObjectIds().removeAt(index - 1);
    }
    activeCanvas()->setObjectPlacements(placements, selectedObjectIds());
}

void ImageEditorWindow::applyShapeStyleToSelection(bool include_kind) {
    Q_UNUSED(include_kind);
    QStringList shape_ids;
    for (const auto& placement : activeSession().visibleObjects()) {
        if (placement.operation.kind == OperationKind::Shape &&
            selectedObjectIds().contains(placement.operation.shape.id)) {
            shape_ids.append(placement.operation.shape.id);
        }
    }
    if (shape_ids.isEmpty()) return;
    QString error;
    if (activeSession().updateShapeStyles(shape_ids, shape_style_, &error)) updateView(true);
    else if (!error.isEmpty()) reportError(QStringLiteral("update_shape_style"), error);
}

void ImageEditorWindow::applyTextStyleToSelection() {
    if (activeCanvas() == nullptr || activeCanvas()->textEditing()) return;
    QVector<ImageObjectPlacement> updated;
    for (auto placement : activeSession().visibleObjects()) {
        auto& operation = placement.operation;
        if (operation.kind != OperationKind::Text ||
            !selectedObjectIds().contains(operation.text.id)) continue;
        operation.text.font_family = text_style_.font_family;
        operation.text.font_pixel_size = text_style_.font_pixel_size;
        operation.text.color = text_style_.color;
        operation.text.alignment = text_style_.alignment;
        updated.append(std::move(placement));
    }
    if (updated.isEmpty()) return;
    QString error;
    if (activeSession().updateObjectsRendered(updated, &error)) updateView(true);
    else if (!error.isEmpty()) {
        reportError(QStringLiteral("update_text_style"), error);
        ImageTextData stored;
        if (activeSession().findText(updated.front().operation.text.id, &stored)) {
            text_style_ = stored;
            activeCanvas()->setTextStyle(text_style_);
            updateTextOptions();
        }
    }
}

void ImageEditorWindow::handleShapeCreated(const ImageShapeData& shape) {
    if (activeSession().data().layers.size() + activeSession().data().groups.size() >=
        ImageDocumentStore::kMaximumLayers) {
        statusBar()->showMessage(
            QStringLiteral("Cannot create a shape: the 512-layer limit has been reached."),
            4000);
        return;
    }
    QString error;
    const QString id = activeSession().addShape(shape, &error);
    if (id.isEmpty()) {
        if (!error.isEmpty()) reportError(QStringLiteral("create_shape"), error);
        return;
    }
    updateView(true);
    statusBar()->showMessage(QStringLiteral("Shape created"), 1800);
}

void ImageEditorWindow::handleTextEditingStarted(const ImageTextData& text, bool existing) {
    if (existing) {
        activeCanvas()->setTransientImage(activeSession().renderedImageWithoutObjects({text.id}));
    }
    text_style_.font_family = text.font_family;
    text_style_.font_pixel_size = text.font_pixel_size;
    text_style_.color = text.color;
    text_style_.alignment = text.alignment;
    activeCanvas()->setTextStyle(text_style_);
    updateTextOptions();
}

void ImageEditorWindow::handleTextCommitted(const ImageTextData& text, bool existing) {
    if (text.content.isEmpty()) {
        if (existing && activeSession().deleteObjects({text.id})) {
            selectedObjectIds().removeAll(text.id);
            updateView(true);
            statusBar()->showMessage(QStringLiteral("Text removed"), 1800);
        }
        return;
    }
    if (existing) {
        QVector<ImageObjectPlacement> updated;
        for (auto placement : activeSession().visibleObjects()) {
            if (placement.operation.kind != OperationKind::Text ||
                placement.operation.text.id != text.id) continue;
            placement.operation.text = text;
            updated.append(std::move(placement));
            break;
        }
        QString error;
        if (!activeSession().updateObjectsRendered(updated, &error)) {
            if (!error.isEmpty()) reportError(QStringLiteral("edit_text"), error);
            return;
        }
        selectedObjectIds() = {text.id};
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Text updated"), 1800);
        return;
    }
    if (activeSession().data().layers.size() + activeSession().data().groups.size() >=
        ImageDocumentStore::kMaximumLayers) {
        statusBar()->showMessage(
            QStringLiteral("Cannot create text: the 512-layer limit has been reached."), 4000);
        return;
    }
    QString error;
    const QString id = activeSession().addText(text, &error);
    if (id.isEmpty()) {
        if (!error.isEmpty()) reportError(QStringLiteral("create_text"), error);
        return;
    }
    selectedObjectIds() = {id};
    updateView(true);
    statusBar()->showMessage(QStringLiteral("Text created"), 1800);
}

void ImageEditorWindow::handleObjectsGeometryChanged(
    const QVector<ImageObjectPlacement>& objects) {
    QString error;
    if (activeSession().updateObjectsRendered(objects, &error)) {
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Selected objects transformed"), 1500);
    } else if (!error.isEmpty()) {
        reportError(QStringLiteral("transform_selected_objects"), error);
    }
}

void ImageEditorWindow::deleteSelectedObjects() {
    if (activeCanvas() != nullptr) activeCanvas()->commitTextEditing();
    if (selectedObjectIds().isEmpty() || !activeSession().deleteObjects(selectedObjectIds())) return;
    selectedObjectIds().clear();
    updateView(true);
    statusBar()->showMessage(QStringLiteral("Selected objects deleted"), 1800);
}

void ImageEditorWindow::deleteSelection() {
    if (editingFieldHasFocus()) return;
    QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr && layer_panel_->isAncestorOf(focus)) {
        layer_panel_->requestDeleteSelection();
    } else if (focus == activeCanvas() || focus == this) {
        deleteSelectedObjects();
    }
}

void ImageEditorWindow::updateDeleteActions() {
    const bool can_delete_objects = activeCanvas() != nullptr && tool_sidebar_ != nullptr &&
        activeSession().hasSource() && !selectedObjectIds().isEmpty() &&
        tool_sidebar_->activeTool() == ToolSidebar::Tool::Select && !activeCanvas()->textEditing();
    if (delete_selected_shape_button_ != nullptr) delete_selected_shape_button_->setEnabled(can_delete_objects);
    if (delete_objects_action_ != nullptr) delete_objects_action_->setEnabled(can_delete_objects);
    QWidget* focus = QApplication::focusWidget();
    const bool layers = focus != nullptr && layer_panel_->isAncestorOf(focus);
    const bool canvas = focus == activeCanvas() || focus == this;
    if (deselect_area_selection_action_ != nullptr) {
        deselect_area_selection_action_->setEnabled(activeCanvas() != nullptr &&
            activeCanvas()->hasAreaSelection() && !editingFieldHasFocus());
    }
    if (delete_selection_action_ == nullptr) return;
    delete_selection_action_->setEnabled(!editingFieldHasFocus() &&
        ((layers && activeSession().hasDocument() && !layer_panel_->selectedStackItems().isEmpty()) ||
         (canvas && can_delete_objects)));
}

void ImageEditorWindow::updateCanvasBrush() {
    if (activeCanvas() == nullptr || tool_sidebar_ == nullptr) return;
    const int diameter = tool_sidebar_->activeTool() == ToolSidebar::Tool::Eraser
        ? eraser_diameter_ : paint_diameter_;
    activeCanvas()->setBrush(tool_sidebar_->brushColor(), diameter);
}

void ImageEditorWindow::updateCanvasToolState(ToolSidebar::Tool tool, bool preserveSelection) {
    if (activeCanvas() == nullptr) return;
    if (!preserveSelection && tool == ToolSidebar::Tool::Shapes &&
        !selectedObjectIds().isEmpty()) {
        selectedObjectIds().clear();
        updateObjectPlacements();
    }
    if (tool != ToolSidebar::Tool::None && crop_action_ != nullptr &&
        crop_action_->isChecked()) {
        crop_action_->setChecked(false);
    }
    if (paint_tool_action_ != nullptr) {
        const QSignalBlocker blocker(paint_tool_action_);
        paint_tool_action_->setChecked(tool == ToolSidebar::Tool::Paint);
    }
    if (eraser_tool_action_ != nullptr) {
        const QSignalBlocker blocker(eraser_tool_action_);
        eraser_tool_action_->setChecked(tool == ToolSidebar::Tool::Eraser);
    }
    if (shapes_tool_action_ != nullptr) {
        const QSignalBlocker blocker(shapes_tool_action_);
        shapes_tool_action_->setChecked(tool == ToolSidebar::Tool::Shapes);
    }
    if (text_tool_action_ != nullptr) {
        const QSignalBlocker blocker(text_tool_action_);
        text_tool_action_->setChecked(tool == ToolSidebar::Tool::Text);
    }
    if (select_tool_action_ != nullptr) {
        const QSignalBlocker blocker(select_tool_action_);
        select_tool_action_->setChecked(tool == ToolSidebar::Tool::Select);
    }
    if (area_selection_tool_action_ != nullptr) {
        const QSignalBlocker blocker(area_selection_tool_action_);
        area_selection_tool_action_->setChecked(tool == ToolSidebar::Tool::AreaSelect);
    }

    if ((tool == ToolSidebar::Tool::Shapes || tool == ToolSidebar::Tool::Select) &&
        !shape_colors_initialized_) {
        shape_style_.stroke_color = tool_sidebar_->brushColor();
        shape_style_.fill_color = tool_sidebar_->brushColor();
        shape_colors_initialized_ = true;
    }

    const bool has_source = activeSession().hasSource();
    // Mode setters also update the cursor, so disable the other modes before
    // enabling the selected one; otherwise a later disable can hide its cursor.
    if (tool == ToolSidebar::Tool::Paint && has_source) {
        activeCanvas()->setTextCreationMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setAreaSelectionMode(false);
        activeCanvas()->setShapeCreationMode(false);
        activeCanvas()->setObjectSelectionMode(false);
        activeCanvas()->setPaintMode(true);
    } else if (tool == ToolSidebar::Tool::Eraser && has_source) {
        activeCanvas()->setTextCreationMode(false);
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setAreaSelectionMode(false);
        activeCanvas()->setShapeCreationMode(false);
        activeCanvas()->setObjectSelectionMode(false);
        activeCanvas()->setEraserMode(true);
    } else if (tool == ToolSidebar::Tool::Shapes && has_source) {
        activeCanvas()->setTextCreationMode(false);
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setAreaSelectionMode(false);
        activeCanvas()->setObjectSelectionMode(false);
        activeCanvas()->setShapeCreationMode(true);
    } else if (tool == ToolSidebar::Tool::Select && has_source) {
        activeCanvas()->setTextCreationMode(false);
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setAreaSelectionMode(false);
        activeCanvas()->setShapeCreationMode(false);
        activeCanvas()->setObjectSelectionMode(true);
    } else if (tool == ToolSidebar::Tool::AreaSelect && has_source) {
        activeCanvas()->setTextCreationMode(false);
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setShapeCreationMode(false);
        activeCanvas()->setObjectSelectionMode(false);
        activeCanvas()->setAreaSelectionMode(true);
    } else if (tool == ToolSidebar::Tool::Text && has_source) {
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setAreaSelectionMode(false);
        activeCanvas()->setShapeCreationMode(false);
        activeCanvas()->setObjectSelectionMode(false);
        activeCanvas()->setTextCreationMode(true);
    } else {
        activeCanvas()->setTextCreationMode(false);
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setAreaSelectionMode(false);
        activeCanvas()->setShapeCreationMode(false);
        activeCanvas()->setObjectSelectionMode(false);
    }
    activeCanvas()->setShapeStyle(shape_style_);
    activeCanvas()->setTextStyle(text_style_);
    activeCanvas()->setAreaSelectionOptions(
        area_selection_shape_ == 1 ? ImageCanvas::AreaSelectionShape::Ellipse
            : ImageCanvas::AreaSelectionShape::Rectangle,
        area_selection_mode_ == 1 ? ImageCanvas::AreaSelectionCombineMode::Add
            : (area_selection_mode_ == 2
                ? ImageCanvas::AreaSelectionCombineMode::Subtract
                : ImageCanvas::AreaSelectionCombineMode::Replace));
    activeCanvas()->setEraserPreviewEnabled(eraser_preview_check_->isChecked());
    const int diameter = tool == ToolSidebar::Tool::Eraser
        ? eraser_diameter_ : paint_diameter_;
    {
        const QSignalBlocker slider_blocker(brush_size_slider_);
        const QSignalBlocker spin_blocker(brush_size_spin_);
        brush_size_slider_->setValue(diameter);
        brush_size_spin_->setValue(diameter);
    }
    updateCanvasBrush();
    updateToolOptions();
}


bool ImageEditorWindow::importImagePaths(const QStringList& paths,
    std::optional<QPointF> center, const QString& relink_id) {
    if (importing_ || paths.isEmpty() || !activeSession().hasSource()) return false;
    if (relink_id.isEmpty() && paths.size() >
        ImageDocumentStore::kMaximumLayers - activeSession().data().layers.size() - activeSession().data().groups.size()) {
        reportError(QStringLiteral("import_images"), QStringLiteral("The batch exceeds the layer limit."));
        return false;
    }
    activeCanvas()->commitTextEditing();
    importing_ = true;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    RasterImportResult result;
    ImageExportProgressDialog dialog(this);
    dialog.setObjectName(QStringLiteral("imageImportProgressDialog"));
    dialog.setWindowTitle(relink_id.isEmpty() ? QStringLiteral("Importing Images") : QStringLiteral("Relinking Image"));
    dialog.setPhaseText(QStringLiteral("Decoding images…"));
    dialog.setCancellationText(QStringLiteral("Cancelling image import…"));
    connect(&dialog, &ImageExportProgressDialog::cancelRequested, this, [cancellation]() {
        cancellation->store(true, std::memory_order_relaxed);
    });
    std::unique_ptr<QThread> thread(QThread::create([&result, paths, cancellation]() {
        result = prepareRasterImport(paths, cancellation.get());
    }));
    connect(thread.get(), &QThread::finished, &dialog, &ImageExportProgressDialog::finish);
    thread->start();
    dialog.exec();
    thread->wait();
    importing_ = false;
    if (cancellation->load(std::memory_order_relaxed) || result.status == RasterImportStatus::Cancelled) {
        statusBar()->showMessage(QStringLiteral("Image import cancelled"), 3000);
        return false;
    }
    if (result.status != RasterImportStatus::Ready) {
        reportError(QStringLiteral("decode_imported_images"), result.cause, result.failed_path);
        return false;
    }
    QString error;
    const bool changed = relink_id.isEmpty()
        ? activeSession().importRasterImages(result.images, center, &error)
        : (result.images.size() == 1 && activeSession().relinkRaster(relink_id, result.images.front(), &error));
    if (!changed) {
        reportError(QStringLiteral("import_or_relink_images"), error, paths.front());
        return false;
    }
    selectedMaskLayerId().clear();
    if (relink_id.isEmpty()) {
        selectedObjectIds().clear();
        for (const auto& object : activeSession().visibleObjects())
            if (object.layer_id == activeSession().selectedLayerId() && object.operation.kind == OperationKind::RasterImage)
                selectedObjectIds() = {object.operation.raster.id};
    }
    tool_sidebar_->setActiveTool(ToolSidebar::Tool::Select);
    updateView(true);
    statusBar()->showMessage(relink_id.isEmpty() ? QStringLiteral("Images imported") : QStringLiteral("Image relinked"), 3000);
    return true;
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
    resize_canvas_action_ = makeAction(
        QStringLiteral("Canvas Size..."), {}, [this]() { resizeCanvas(); });
    resize_canvas_action_->setObjectName(QStringLiteral("resizeCanvasAction"));
    open_image_action_ = makeAction(
        QStringLiteral("Open Image..."), QKeySequence::Open, [this]() { openImage(); });
    open_image_action_->setObjectName(QStringLiteral("openImageAction"));
    open_document_action_ = makeAction(
        QStringLiteral("Open Editable Document..."), {}, [this]() { openDocument(); });
    open_document_action_->setObjectName(QStringLiteral("openEditableDocumentAction"));
    new_tab_canvas_action_ = makeAction(
        QStringLiteral("New Canvas in New Tab..."), {}, [this]() { createNewCanvas(true); });
    new_tab_canvas_action_->setObjectName(QStringLiteral("newTabCanvasAction"));
    new_tab_open_image_action_ = makeAction(
        QStringLiteral("Open Image in New Tab..."), {}, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Open Image in New Tab"), {}, imageFilter());
            if (!path.isEmpty()) static_cast<void>(openImagePathInTarget(path, OpenTarget::NewTab));
        });
    new_tab_open_image_action_->setObjectName(QStringLiteral("newTabOpenImageAction"));
    new_tab_open_document_action_ = makeAction(
        QStringLiteral("Open Editable Document in New Tab..."), {}, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Open Editable Image Document in New Tab"), {},
                QStringLiteral("Image Editor documents (*.cimg)"));
            if (!path.isEmpty()) static_cast<void>(openDocumentPathInTarget(path, OpenTarget::NewTab));
        });
    new_tab_open_document_action_->setObjectName(QStringLiteral("newTabOpenEditableDocumentAction"));
    import_layer_action_ = makeAction(QStringLiteral("Import Image as Layer..."), {}, [this]() {
        const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Import Image as Layer"), {},
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"));
        if (!paths.isEmpty()) (void)importImagePaths(paths);
    });
    import_layer_action_->setObjectName(QStringLiteral("importImageAsLayerAction"));
    registerShortcutAction(import_layer_action_, {});
    relink_raster_action_ = makeAction(QStringLiteral("Relink Image..."), {}, [this]() {
        if (selectedObjectIds().size() != 1) return;
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Relink Image"), {},
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"));
        if (!path.isEmpty()) (void)importImagePaths({path}, {}, selectedObjectIds().front());
    });
    relink_raster_action_->setObjectName(QStringLiteral("relinkRasterImageAction"));
    registerShortcutAction(relink_raster_action_, {});
    relink_action_ = makeAction(
        QStringLiteral("Relink Source Image..."), {}, [this]() { relinkSource(); });
    save_action_ = makeAction(
        QStringLiteral("Save Document"), QKeySequence::Save, [this]() { saveDocument(); });
    save_as_action_ = makeAction(
        QStringLiteral("Save Document As..."), QKeySequence::SaveAs, [this]() { saveDocumentAs(); });
    export_action_ = makeAction(
        QStringLiteral("Export Image..."), {}, [this]() { exportImage(); });
    quick_export_action_ = makeAction(
        QStringLiteral("Quick Export..."), {}, [this]() { exportImage(true); });
    auto* quit_action = makeAction(
        QStringLiteral("Quit"), QKeySequence::Quit, [this]() { close(); });
    close_document_tab_action_ = makeAction(
        QStringLiteral("Close Document Tab"), QKeySequence::Close, [this]() {
            closeDocumentTab(active_document_tab_);
        });
    close_document_tab_action_->setObjectName(QStringLiteral("closeDocumentTabAction"));
    next_document_tab_action_ = makeAction(
        QStringLiteral("Next Document Tab"), QKeySequence::NextChild, [this]() {
            if (document_tabs_.size() > 1)
                document_tab_bar_->setCurrentIndex((active_document_tab_ + 1) % document_tabs_.size());
        });
    next_document_tab_action_->setObjectName(QStringLiteral("nextDocumentTabAction"));
    previous_document_tab_action_ = makeAction(
        QStringLiteral("Previous Document Tab"), QKeySequence::PreviousChild, [this]() {
            if (document_tabs_.size() > 1)
                document_tab_bar_->setCurrentIndex(
                    (active_document_tab_ + document_tabs_.size() - 1) % document_tabs_.size());
        });
    previous_document_tab_action_->setObjectName(QStringLiteral("previousDocumentTabAction"));
    addAction(close_document_tab_action_);
    addAction(next_document_tab_action_);
    addAction(previous_document_tab_action_);

    undo_action_ = makeAction(
        QStringLiteral("Undo"), QKeySequence::Undo, [this]() {
            activeCanvas()->commitTextEditing();
            const QPoint old_offset = activeSession().data().canvas_base_offset;
            if (activeSession().undo()) {
                updateView(true);
                if (activeCanvas() != nullptr) {
                    activeCanvas()->translateAreaSelection(
                        activeSession().data().canvas_base_offset - old_offset);
                }
            }
        });
    undo_action_->setObjectName(QStringLiteral("undoAction"));
    redo_action_ = makeAction(
        QStringLiteral("Redo"), QKeySequence::Redo, [this]() {
            activeCanvas()->commitTextEditing();
            const QPoint old_offset = activeSession().data().canvas_base_offset;
            if (activeSession().redo()) {
                updateView(true);
                if (activeCanvas() != nullptr) {
                    activeCanvas()->translateAreaSelection(
                        activeSession().data().canvas_base_offset - old_offset);
                }
            }
        });
    redo_action_->setObjectName(QStringLiteral("redoAction"));
    rotate_left_action_ = makeAction(
        QStringLiteral("Rotate Left 90°"), {}, [this]() {
            activeCanvas()->commitTextEditing();
            if (activeSession().hasSource()) { activeSession().rotateLeft(); updateView(); }
        });
    rotate_right_action_ = makeAction(
        QStringLiteral("Rotate Right 90°"), {}, [this]() {
            activeCanvas()->commitTextEditing();
            if (activeSession().hasSource()) { activeSession().rotateRight(); updateView(); }
        });
    rotate_right_action_->setObjectName(QStringLiteral("rotateRightAction"));
    flip_horizontal_action_ = makeAction(
        QStringLiteral("Flip Horizontal"), {}, [this]() {
            activeCanvas()->commitTextEditing();
            if (activeSession().hasSource()) { activeSession().flipHorizontal(); updateView(); }
        });
    flip_vertical_action_ = makeAction(
        QStringLiteral("Flip Vertical"), {}, [this]() {
            activeCanvas()->commitTextEditing();
            if (activeSession().hasSource()) { activeSession().flipVertical(); updateView(); }
        });
    crop_action_ = new QAction(QStringLiteral("Crop Selection"), this);
    crop_action_->setObjectName(QStringLiteral("cropSelectionAction"));
    crop_action_->setCheckable(true);
    connect(crop_action_, &QAction::toggled, this, [this](bool enabled) {
        if (enabled && tool_sidebar_->activeTool() != ToolSidebar::Tool::None) {
            tool_sidebar_->setActiveTool(ToolSidebar::Tool::None);
        }
        updateToolOptions();
        activeCanvas()->setPaintMode(false);
        activeCanvas()->setEraserMode(false);
        activeCanvas()->setCropMode(enabled && activeSession().hasSource());
        if (cancel_crop_action_ != nullptr) {
            cancel_crop_action_->setEnabled(enabled && activeSession().hasSource());
        }
        statusBar()->showMessage(enabled
            ? QStringLiteral("Drag over the image to crop; press Esc to cancel")
            : QStringLiteral("Ready"));
    });
    fit_action_ = makeAction(
        QStringLiteral("Fit Image"), {}, [this]() { activeCanvas()->fitToWindow(); });
    cancel_crop_action_ = new QAction(QStringLiteral("Cancel Crop"), this);
    cancel_crop_action_->setObjectName(QStringLiteral("cancelCropAction"));
    cancel_crop_action_->setEnabled(false);
    addAction(cancel_crop_action_);
    connect(cancel_crop_action_, &QAction::triggered, this, [this]() {
        if (activeCanvas() != nullptr && activeCanvas()->areaSelectionGestureActive()) {
            activeCanvas()->cancelAreaSelectionGesture();
            statusBar()->showMessage(QStringLiteral("Area selection cancelled"), 2500);
            return;
        }
        if (!crop_action_->isChecked()) return;
        crop_action_->setChecked(false);
        activeCanvas()->setCropMode(false);
        statusBar()->showMessage(QStringLiteral("Crop cancelled"), 2500);
    });

    open_document_action_->setObjectName(QStringLiteral("openEditableDocumentAction"));
    relink_action_->setObjectName(QStringLiteral("relinkSourceAction"));
    save_action_->setObjectName(QStringLiteral("saveDocumentAction"));
    save_as_action_->setObjectName(QStringLiteral("saveDocumentAsAction"));
    export_action_->setObjectName(QStringLiteral("exportImageAction"));
    quick_export_action_->setObjectName(QStringLiteral("quickExportImageAction"));
    quit_action->setObjectName(QStringLiteral("quitAction"));
    rotate_left_action_->setObjectName(QStringLiteral("rotateLeftAction"));
    flip_horizontal_action_->setObjectName(QStringLiteral("flipHorizontalAction"));
    flip_vertical_action_->setObjectName(QStringLiteral("flipVerticalAction"));
    fit_action_->setObjectName(QStringLiteral("fitImageAction"));

    registerShortcutAction(new_canvas_action_, QKeySequence::New);
    registerShortcutAction(open_image_action_, QKeySequence::Open);
    registerShortcutAction(open_document_action_, {});
    registerShortcutAction(close_document_tab_action_, QKeySequence::Close);
    registerShortcutAction(next_document_tab_action_, QKeySequence::NextChild);
    registerShortcutAction(previous_document_tab_action_, QKeySequence::PreviousChild);
    registerShortcutAction(relink_action_, {});
    registerShortcutAction(save_action_, QKeySequence::Save);
    registerShortcutAction(save_as_action_, QKeySequence::SaveAs);
    registerShortcutAction(export_action_, {});
    registerShortcutAction(quick_export_action_, {});
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
        const auto current = tool_sidebar_->activeTool();
        tool_sidebar_->setActiveTool(active ? ToolSidebar::Tool::Paint
            : (current == ToolSidebar::Tool::Paint ? ToolSidebar::Tool::None : current));
    });

    eraser_tool_action_ = new QAction(QStringLiteral("Eraser"), this);
    eraser_tool_action_->setObjectName(QStringLiteral("eraserToolAction"));
    eraser_tool_action_->setCheckable(true);
    registerShortcutAction(eraser_tool_action_, QKeySequence(Qt::Key_E));
    addAction(eraser_tool_action_);
    connect(eraser_tool_action_, &QAction::toggled, this, [this](bool active) {
        const auto current = tool_sidebar_->activeTool();
        tool_sidebar_->setActiveTool(active ? ToolSidebar::Tool::Eraser
            : (current == ToolSidebar::Tool::Eraser ? ToolSidebar::Tool::None : current));
    });

    shapes_tool_action_ = new QAction(QStringLiteral("Shapes"), this);
    shapes_tool_action_->setObjectName(QStringLiteral("shapesToolAction"));
    shapes_tool_action_->setCheckable(true);
    registerShortcutAction(shapes_tool_action_, {});
    addAction(shapes_tool_action_);
    connect(shapes_tool_action_, &QAction::toggled, this, [this](bool active) {
        const auto current = tool_sidebar_->activeTool();
        tool_sidebar_->setActiveTool(active ? ToolSidebar::Tool::Shapes
            : (current == ToolSidebar::Tool::Shapes ? ToolSidebar::Tool::None : current));
    });

    text_tool_action_ = new QAction(QStringLiteral("Text"), this);
    text_tool_action_->setObjectName(QStringLiteral("textToolAction"));
    text_tool_action_->setCheckable(true);
    registerShortcutAction(text_tool_action_, {});
    addAction(text_tool_action_);
    connect(text_tool_action_, &QAction::toggled, this, [this](bool active) {
        const auto current = tool_sidebar_->activeTool();
        tool_sidebar_->setActiveTool(active ? ToolSidebar::Tool::Text
            : (current == ToolSidebar::Tool::Text ? ToolSidebar::Tool::None : current));
    });

    select_tool_action_ = new QAction(QStringLiteral("Selection"), this);
    // Keep the old preference key so customized shortcuts survive this rename.
    select_tool_action_->setObjectName(QStringLiteral("selectShapesToolAction"));
    select_tool_action_->setCheckable(true);
    registerShortcutAction(select_tool_action_, {});
    addAction(select_tool_action_);
    connect(select_tool_action_, &QAction::toggled, this, [this](bool active) {
        const auto current = tool_sidebar_->activeTool();
        tool_sidebar_->setActiveTool(active ? ToolSidebar::Tool::Select
            : (current == ToolSidebar::Tool::Select ? ToolSidebar::Tool::None : current));
    });

    area_selection_tool_action_ = new QAction(QStringLiteral("Area Selection"), this);
    area_selection_tool_action_->setObjectName(QStringLiteral("areaSelectionToolAction"));
    area_selection_tool_action_->setCheckable(true);
    registerShortcutAction(area_selection_tool_action_, QKeySequence(Qt::Key_M));
    addAction(area_selection_tool_action_);
    connect(area_selection_tool_action_, &QAction::toggled, this, [this](bool active) {
        const auto current = tool_sidebar_->activeTool();
        tool_sidebar_->setActiveTool(active ? ToolSidebar::Tool::AreaSelect
            : (current == ToolSidebar::Tool::AreaSelect ? ToolSidebar::Tool::None : current));
    });

    deselect_area_selection_action_ = new QAction(QStringLiteral("Deselect"), this);
    deselect_area_selection_action_->setObjectName(QStringLiteral("deselectAreaSelectionAction"));
    registerShortcutAction(deselect_area_selection_action_,
                           QKeySequence(Qt::CTRL | Qt::Key_D));
    addAction(deselect_area_selection_action_);
    connect(deselect_area_selection_action_, &QAction::triggered, this, [this]() {
        if (activeCanvas() == nullptr || editingFieldHasFocus()) return;
        activeCanvas()->clearAreaSelection();
        updateDeleteActions();
    });

    delete_objects_action_ = new QAction(QStringLiteral("Delete Selected Objects"), this);
    delete_objects_action_->setObjectName(QStringLiteral("deleteSelectedObjectsAction"));
    connect(delete_objects_action_, &QAction::triggered, this,
            [this]() { deleteSelectedObjects(); });

    delete_selection_action_ = new QAction(QStringLiteral("Delete Selection"), this);
    // Keep the preference key for any user-assigned Delete Selected Shape shortcut.
    delete_selection_action_->setObjectName(QStringLiteral("deleteSelectedShapeAction"));
    registerShortcutAction(delete_selection_action_, QKeySequence(Qt::Key_Delete));
    addAction(delete_selection_action_);
    connect(delete_selection_action_, &QAction::triggered, this,
            [this]() { deleteSelection(); });

    auto* file_menu = menuBar()->addMenu(QStringLiteral("File"));
    file_menu->addAction(new_canvas_action_);
    file_menu->addAction(close_document_tab_action_);
    file_menu->addSeparator();
    file_menu->addAction(open_image_action_);
    file_menu->addAction(open_document_action_);
    file_menu->addAction(import_layer_action_);
    file_menu->addAction(relink_raster_action_);
    file_menu->addAction(relink_action_);
    file_menu->addSeparator();
    file_menu->addAction(save_action_);
    file_menu->addAction(save_as_action_);
    file_menu->addAction(export_action_);
    file_menu->addAction(quick_export_action_);
    file_menu->addSeparator();
    file_menu->addAction(quit_action);

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("Edit"));
    edit_menu->addAction(undo_action_);
    edit_menu->addAction(redo_action_);
    edit_menu->addSeparator();
    edit_menu->addAction(delete_selection_action_);
    edit_menu->addAction(deselect_area_selection_action_);
    edit_menu->addAction(delete_objects_action_);
    edit_menu->addSeparator();
    edit_menu->addAction(crop_action_);
    edit_menu->addAction(rotate_left_action_);
    edit_menu->addAction(rotate_right_action_);
    edit_menu->addAction(flip_horizontal_action_);
    edit_menu->addAction(flip_vertical_action_);

    auto* image_menu = menuBar()->addMenu(QStringLiteral("Image"));
    image_menu->addAction(resize_canvas_action_);

    auto* view_menu = menuBar()->addMenu(QStringLiteral("View"));
    view_menu->addAction(fit_action_);
    view_menu->addAction(next_document_tab_action_);
    view_menu->addAction(previous_document_tab_action_);
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
                .arg(version.isEmpty() ? QStringLiteral("Beta 0.1.3") : version,
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
    shortcut_manager_.registerAction(
        action->objectName(), action->text(), action);
}

void ImageEditorWindow::loadShortcutPreferences() {
    // A newly introduced default must not displace an older saved assignment.
    QSettings settings;
    settings.beginGroup(QStringLiteral("ImageEditor/KeyboardShortcuts"));
    const QString deletion_key = delete_selection_action_->objectName();
    if (!settings.contains(deletion_key)) {
        for (const auto& entry : shortcut_manager_.entries()) {
            if (entry.id == deletion_key || !settings.contains(entry.id)) continue;
            if (QKeySequence::fromString(settings.value(entry.id).toString(), QKeySequence::PortableText)
                == delete_selection_action_->shortcut()) {
                settings.setValue(deletion_key, QString{});
                break;
            }
        }
    }
    if (settings.contains(deletion_key)) {
        const QString saved = settings.value(deletion_key).toString();
        delete_selection_action_->setShortcut(saved == QStringLiteral("<disabled>")
            ? QKeySequence{} : QKeySequence(saved, QKeySequence::PortableText));
    }
    settings.endGroup();
    settings.sync();
    QString error;
    const bool preferences_loaded = shortcut_manager_.load(&error);
    if (!preferences_loaded || settings.status() != QSettings::NoError) {
        if (error.isEmpty() && settings.status() != QSettings::NoError) {
            error = QStringLiteral("Keyboard shortcut preference migration could not be saved to %1.")
                .arg(settings.fileName());
        }
        logger_.logError(QStringLiteral("load_keyboard_shortcuts"),
                         error.isEmpty()
                             ? QStringLiteral("The keyboard shortcut preferences could not be read.")
                             : error);
        statusBar()->showMessage(
            QStringLiteral("Keyboard shortcut preferences could not be loaded; defaults may be in use."),
            5000);
    }
}

void ImageEditorWindow::openShortcutSettings() {
    QList<ShortcutBinding> bindings;
    const auto& entries = shortcut_manager_.entries();
    bindings.reserve(static_cast<qsizetype>(entries.size()));
    for (const auto& entry : entries) {
        ShortcutBinding binding;
        binding.id = entry.id;
        binding.label = entry.label;
        binding.default_sequence = entry.default_sequence;
        binding.sequence = entry.action->shortcut();
        bindings.append(binding);
    }

    ShortcutSettingsDialog dialog(bindings, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto updated_bindings = dialog.bindings();
    std::vector<creative_suite::shortcuts::ShortcutAssignment> assignments;
    assignments.reserve(static_cast<std::size_t>(updated_bindings.size()));
    for (const auto& binding : updated_bindings) {
        assignments.push_back({binding.id, binding.sequence});
    }
    QString error;
    if (!shortcut_manager_.applyShortcuts(assignments, &error)) {
        const QString cause = QStringLiteral(
            "The keyboard shortcut preferences could not be saved.");
        logger_.logError(QStringLiteral("save_keyboard_shortcuts"),
                         error.isEmpty() ? cause : error);
        QMessageBox::warning(this, QStringLiteral("Settings Error"), cause);
    }
}

bool ImageEditorWindow::editingMask() const {
    const auto& active_tab = activeTabState();
    if (active_tab.selected_mask_layer_id.isEmpty() ||
        active_tab.selected_mask_layer_id != active_tab.session.selectedLayerId()) return false;
    for (const auto& layer : active_tab.session.data().layers) {
        if (layer.id == active_tab.selected_mask_layer_id) return layer.mask.has_value();
    }
    return false;
}

void ImageEditorWindow::updateLayerPanel() {
    const auto problems = activeSession().rasterSourceProblems();
    for (auto it = problems.cbegin(); it != problems.cend(); ++it)
        if (loggedRasterProblems().value(it.key()) != it.value()) {
            ImageRasterData raster;
            (void)activeSession().findRaster(it.key(), &raster);
            logger_.logError(QStringLiteral("load_imported_image"), it.value(), raster.source_path);
        }
    loggedRasterProblems() = problems;
    const QSize size(LayerPanel::kThumbnailWidth, LayerPanel::kThumbnailHeight);
    layer_panel_->setDocument(activeSession().data(), activeSession().selectedLayerId(),
        activeSession().selectedGroupId(), activeSession().renderedLayerThumbnails(size),
        activeSession().renderedLayerMaskThumbnails(size), selectedMaskLayerId(), problems,
        selectedStackItems());
}

void ImageEditorWindow::updateView(bool preserveCanvasView) {
    updateDocumentTabLabel();
    const bool has_tab = hasActiveDocumentTab();
    const bool linked = !linkedDocumentPath().isEmpty();
    new_document_tab_button_->setEnabled(!linked && !importing_);
    document_tab_bar_->setTabsClosable(!linked);
    close_document_tab_action_->setEnabled(has_tab && !linked);
    next_document_tab_action_->setEnabled(document_tabs_.size() > 1 && !linked);
    previous_document_tab_action_->setEnabled(document_tabs_.size() > 1 && !linked);

    if (activeCanvas() == nullptr) {
        tool_sidebar_->setDocumentAvailable(false);
        tool_sidebar_->setPaintingAllowed(false);
        paint_options_action_->setVisible(false);
        paint_size_options_->setVisible(false);
        shape_options_action_->setVisible(false);
        shape_options_widget_->setVisible(false);
        text_options_action_->setVisible(false);
        text_options_widget_->setVisible(false);
        selection_options_action_->setVisible(false);
        area_selection_options_action_->setVisible(false);
        area_selection_options_widget_->setVisible(false);
        layer_panel_->setDocument(ImageDocumentData{}, {}, {}, {}, {}, {}, {});
        layer_panel_->setQuickExportEnabled(false);
        undo_action_->setEnabled(false);
        redo_action_->setEnabled(false);
        save_action_->setEnabled(false);
        save_as_action_->setEnabled(false);
        export_action_->setEnabled(false);
        quick_export_action_->setEnabled(false);
        relink_action_->setEnabled(false);
        import_layer_action_->setEnabled(false);
        relink_raster_action_->setEnabled(false);
        new_canvas_action_->setEnabled(!linked);
        resize_canvas_action_->setEnabled(false);
        open_image_action_->setEnabled(!linked);
        open_document_action_->setEnabled(!linked);
        fit_action_->setEnabled(false);
        status_label_->setText(QStringLiteral("No image open"));
        setWindowTitle(QStringLiteral("Image Editor"));
        updateSelectionContext();
        return;
    }
    if (!editingMask()) selectedMaskLayerId().clear();
    activeCanvas()->setMaskEditing(editingMask());
    const QImage rendered = activeSession().renderedImage();
    activeCanvas()->setImage(rendered, !preserveCanvasView);
    tool_sidebar_->setDocumentAvailable(activeSession().hasSource());
    tool_sidebar_->setPaintingAllowed(activeSession().hasSource() &&
                                      activeSession().selectedLayerIsEditable());
    updateLayerPanel();
    updateObjectPlacements();
    updateToolOptions();
    updateCanvasBrush();
    undo_action_->setEnabled(activeSession().canUndo());
    redo_action_->setEnabled(activeSession().canRedo());
    save_action_->setEnabled(activeSession().hasSource());
    save_as_action_->setEnabled(activeSession().hasSource());
    export_action_->setEnabled(activeSession().hasSource());
    quick_export_action_->setEnabled(activeSession().hasSource());
    layer_panel_->setQuickExportEnabled(activeSession().hasSource());
    relink_action_->setEnabled(activeSession().sourceIsMissing());
    import_layer_action_->setEnabled(activeSession().hasSource() && !importing_);
    relink_raster_action_->setEnabled(activeSession().hasSource() && !importing_ &&
        selectedObjectIds().size() == 1 && activeSession().findRaster(selectedObjectIds().front(), nullptr));
    new_canvas_action_->setEnabled(!linked);
    resize_canvas_action_->setEnabled(activeSession().hasSource());
    open_image_action_->setEnabled(!linked);
    open_document_action_->setEnabled(!linked);
    save_as_action_->setEnabled(activeSession().hasSource() && !linked);
    updateSelectionContext();

    QString title = QStringLiteral("Image Editor");
    if (!activeSession().documentPath().isEmpty()) {
        title = QFileInfo(activeSession().documentPath()).fileName() + QStringLiteral(" — Image Editor");
    } else if (!activeSession().sourcePath().isEmpty()) {
        title = QFileInfo(activeSession().sourcePath()).fileName() + QStringLiteral(" — Image Editor");
    } else if (activeSession().hasDocument()) {
        title = QStringLiteral("Untitled Canvas — Image Editor");
    }
    if (activeSession().isDirty()) title.prepend('*');
    if (linked) title += QStringLiteral(" [Linked]");
    setWindowTitle(title);

    if (!activeSession().hasSource()) {
        status_label_->setText(activeSession().sourceIsMissing()
            ? QStringLiteral("Source image is missing — relink it to continue")
            : QStringLiteral("No image open"));
        return;
    }
    const QSize size = rendered.size();
    status_label_->setText(QStringLiteral("%1 × %2 px  |  %3%")
        .arg(size.width()).arg(size.height())
        .arg(static_cast<int>(activeCanvas()->zoomFactor() * 100.0)));
    if (editingMask()) status_label_->setText(status_label_->text() +
        QStringLiteral("  |  Editing layer mask — black hides, white reveals"));
}

void ImageEditorWindow::updateSelectionContext() {
    if (activeCanvas() != nullptr) {
        activeCanvas()->setMaskEditing(editingMask());
        activeCanvas()->setEraserPreviewEnabled(eraser_preview_check_->isChecked());
    }
    if (layer_panel_ != nullptr) layer_panel_->setSelectedMask(selectedMaskLayerId());
    const QString mask_hint = QStringLiteral("  |  Editing layer mask — black hides, white reveals");
    QString status = status_label_->text();
    if (status.endsWith(mask_hint)) status.chop(mask_hint.size());
    if (editingMask()) status += mask_hint;
    status_label_->setText(status);
    if (activeCanvas() == nullptr || tool_sidebar_ == nullptr || !activeSession().hasSource()) {
        if (tool_sidebar_ != nullptr) tool_sidebar_->setPaintingAllowed(false);
        if (crop_action_ != nullptr) crop_action_->setEnabled(false);
        if (rotate_left_action_ != nullptr) rotate_left_action_->setEnabled(false);
        if (rotate_right_action_ != nullptr) rotate_right_action_->setEnabled(false);
        if (flip_horizontal_action_ != nullptr) flip_horizontal_action_->setEnabled(false);
        if (flip_vertical_action_ != nullptr) flip_vertical_action_->setEnabled(false);
        if (fit_action_ != nullptr) fit_action_->setEnabled(false);
        if (cancel_crop_action_ != nullptr) cancel_crop_action_->setEnabled(false);
        if (paint_tool_action_ != nullptr) paint_tool_action_->setEnabled(false);
        if (eraser_tool_action_ != nullptr) eraser_tool_action_->setEnabled(false);
        if (shapes_tool_action_ != nullptr) shapes_tool_action_->setEnabled(false);
        if (text_tool_action_ != nullptr) text_tool_action_->setEnabled(false);
        if (select_tool_action_ != nullptr) select_tool_action_->setEnabled(false);
        if (area_selection_tool_action_ != nullptr) area_selection_tool_action_->setEnabled(false);
        if (delete_objects_action_ != nullptr) delete_objects_action_->setEnabled(false);
        updateDeleteActions();
        return;
    }
    const bool selected_layer_editable = activeSession().selectedLayerIsEditable();
    const bool selected_item_transformable = selected_layer_editable ||
        activeSession().selectedGroupIsActive();
    tool_sidebar_->setPaintingAllowed(selected_layer_editable);
    crop_action_->setEnabled(selected_item_transformable);
    rotate_left_action_->setEnabled(selected_item_transformable);
    rotate_right_action_->setEnabled(selected_item_transformable);
    flip_horizontal_action_->setEnabled(selected_item_transformable);
    flip_vertical_action_->setEnabled(selected_item_transformable);
    fit_action_->setEnabled(true);
    cancel_crop_action_->setEnabled((crop_action_->isChecked() && selected_item_transformable) ||
        tool_sidebar_->activeTool() == ToolSidebar::Tool::AreaSelect);
    paint_tool_action_->setEnabled(selected_layer_editable);
    eraser_tool_action_->setEnabled(selected_layer_editable);
    shapes_tool_action_->setEnabled(true);
    text_tool_action_->setEnabled(true);
    select_tool_action_->setEnabled(true);
    area_selection_tool_action_->setEnabled(true);
    updateDeleteActions();
    updateToolOptions();
}

void ImageEditorWindow::createNewCanvas(bool new_tab) {
    if (new_tab && !linkedDocumentPath().isEmpty()) return;
    NewCanvasDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!new_tab && hasActiveDocumentTab() && !confirmDiscardOrSave()) return;
    if (!linkedDocumentPath().isEmpty()) return;
    const bool created_tab = new_tab || !hasActiveDocumentTab();
    if (created_tab) static_cast<void>(addDocumentTab(true));

    QString error;
    if (!activeSession().createCanvas(dialog.canvasSize(), dialog.backgroundColor(), &error)) {
        reportError(QStringLiteral("create_canvas"), error);
        if (created_tab) closeDocumentTab(active_document_tab_);
        return;
    }
    resetActiveDocumentSelection();
    deactivateCanvasTools();
    updateView();
    statusBar()->showMessage(QStringLiteral("New canvas created"), 3000);
}

void ImageEditorWindow::resizeCanvas() {
    if (activeCanvas() == nullptr || !activeSession().hasSource()) return;
    activeCanvas()->commitTextEditing();
    CanvasSizeDialog dialog(activeSession().renderedImage().size(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    const QPoint old_offset = activeSession().data().canvas_base_offset;
    if (!activeSession().resizeCanvas(dialog.canvasSize(), dialog.anchor(), &error)) {
        if (!error.isEmpty()) reportError(QStringLiteral("resize_canvas"), error);
        return;
    }
    selectedObjectIds().clear();
    updateView(true);
    activeCanvas()->translateAreaSelection(activeSession().data().canvas_base_offset - old_offset);
    statusBar()->showMessage(QStringLiteral("Canvas resized"), 3000);
}

bool ImageEditorWindow::confirmDiscardOrSave(bool clearRecoveryOnDiscard) {
    if (activeCanvas() != nullptr) activeCanvas()->commitTextEditing();
    if (!activeSession().isDirty()) return true;
    QMessageBox prompt(QMessageBox::Warning,
                       QStringLiteral("Unsaved image edits"),
                       QStringLiteral("Save the changes to this image document?"),
                       QMessageBox::NoButton, this);
    auto* save = prompt.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    auto* discard = prompt.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.exec();
    if (prompt.clickedButton() == cancel) return false;
    if (prompt.clickedButton() == discard) {
        if (clearRecoveryOnDiscard) {
            const QString recovery_path = recovery_store_.pathFor(activeSession());
            if (!recovery_path.isEmpty()) static_cast<void>(recovery_store_.remove(recovery_path));
        }
        return true;
    }
    return prompt.clickedButton() == save && saveToPath();
}

void ImageEditorWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Image"), {}, imageFilter());
    if (path.isEmpty()) return;
    static_cast<void>(openImagePath(path));
}

bool ImageEditorWindow::openImagePath(const QString& path) {
    return openImagePathInTarget(path, OpenTarget::CurrentTab);
}

bool ImageEditorWindow::openImagePathInTarget(const QString& path, OpenTarget target) {
    if (importing_ || path.isEmpty()) return false;
    if (!linkedDocumentPath().isEmpty()) return false;
    if (target == OpenTarget::CurrentTab && hasActiveDocumentTab() &&
        !confirmDiscardOrSave()) return false;
    const bool created_tab = target == OpenTarget::NewTab || !hasActiveDocumentTab();
    if (created_tab) static_cast<void>(addDocumentTab(true));
    QString error;
    if (!activeSession().openImage(path, &error)) {
        reportError(QStringLiteral("open_image"), error, path);
        if (created_tab) closeDocumentTab(active_document_tab_);
        return false;
    }
    resetActiveDocumentSelection();
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
    return openDocumentPathInTarget(path, OpenTarget::CurrentTab);
}

bool ImageEditorWindow::openDocumentPathInTarget(const QString& path, OpenTarget target) {
    if (importing_ || path.isEmpty()) return false;
    if (!linkedDocumentPath().isEmpty()) return false;
    for (int index = 0; index < document_tabs_.size(); ++index) {
        const QString open_path = index == active_document_tab_
            ? activeSession().documentPath() : document_tabs_.at(index)->session.documentPath();
        if (open_path.isEmpty() || !sameLinkedPath(open_path, path)) continue;
        {
            const QSignalBlocker blocker(document_tab_bar_);
            document_tab_bar_->setCurrentIndex(index);
        }
        activateDocumentTab(index);
        return true;
    }
    if (target == OpenTarget::CurrentTab && hasActiveDocumentTab() &&
        !confirmDiscardOrSave()) return false;
    const bool created_tab = target == OpenTarget::NewTab || !hasActiveDocumentTab();
    if (created_tab) static_cast<void>(addDocumentTab(true));
    QString error;
    if (!activeSession().openDocument(path, &error)) {
        reportError(QStringLiteral("open_document"), error, path);
        if (created_tab) closeDocumentTab(active_document_tab_);
        return false;
    }
    resetActiveDocumentSelection();
    deactivateCanvasTools();
    updateView();
    if (activeSession().sourceIsMissing()) {
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

bool ImageEditorWindow::openLinkedImage(
    const QString& source_path,
    const QString& document_path,
    const QString& published_output_path) {
    if (source_path.isEmpty() || document_path.isEmpty() ||
        published_output_path.isEmpty()) {
        reportError(QStringLiteral("open_linked_image"),
                    QStringLiteral("The linked image paths are incomplete."), document_path);
        return false;
    }
    if (document_tabs_.size() > 1) {
        reportError(QStringLiteral("open_linked_image"),
                    QStringLiteral("Linked editing requires a single open document tab."),
                    document_path);
        return false;
    }
    if (QFileInfo(document_path).suffix().compare(
            QStringLiteral("cimg"), Qt::CaseInsensitive) != 0 ||
        QFileInfo(published_output_path).suffix().compare(
            QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        reportError(QStringLiteral("open_linked_image"),
                    QStringLiteral("Linked mode requires a .cimg document and a .png published output."),
                    document_path);
        return false;
    }
    const QString normalized_document_path = QFileInfo(document_path).absoluteFilePath();
    const QString normalized_output_path = QFileInfo(published_output_path).absoluteFilePath();
    const bool reopening_linked_tab = !linkedDocumentPath().isEmpty();
    if (reopening_linked_tab &&
        !sameLinkedPath(linkedDocumentPath(), normalized_document_path)) {
        reportError(QStringLiteral("open_linked_image"),
                    QStringLiteral("A different linked document is already open in this tab."),
                    normalized_document_path);
        return false;
    }
    if (sameLinkedPath(normalized_document_path, normalized_output_path) ||
        sameLinkedPath(normalized_document_path, source_path) ||
        sameLinkedPath(normalized_output_path, source_path)) {
        reportError(QStringLiteral("open_linked_image"),
                    QStringLiteral("The source, editable document, and published image must use different files."),
                    document_path);
        return false;
    }
    if (!QDir().mkpath(QFileInfo(normalized_document_path).absolutePath()) ||
        !QDir().mkpath(QFileInfo(normalized_output_path).absolutePath())) {
        reportError(QStringLiteral("prepare_linked_image"),
                    QStringLiteral("The linked image directories could not be created."),
                    normalized_document_path);
        return false;
    }

    bool opened = false;
    if (reopening_linked_tab) {
        if (!confirmDiscardOrSave()) return false;
        QString error;
        if (!activeSession().openDocument(normalized_document_path, &error)) {
            reportError(QStringLiteral("open_document"), error, normalized_document_path);
            return false;
        }
        resetActiveDocumentSelection();
        deactivateCanvasTools();
        updateView();
        if (activeSession().sourceIsMissing()) {
            QMessageBox prompt(QMessageBox::Warning,
                               QStringLiteral("Source image is missing"),
                               QStringLiteral("The editable document is open, but its source image could not be found."),
                               QMessageBox::NoButton, this);
            auto* relink = prompt.addButton(QStringLiteral("Relink Source..."), QMessageBox::AcceptRole);
            prompt.addButton(QMessageBox::Close);
            prompt.exec();
            if (prompt.clickedButton() == relink) relinkSource();
        }
        opened = true;
    } else if (QFileInfo::exists(normalized_document_path)) {
        opened = openDocumentPath(normalized_document_path);
    } else if (openImagePath(source_path)) {
        linkedDocumentPath() = normalized_document_path;
        linkedOutputPath() = normalized_output_path;
        opened = saveToPath(linkedDocumentPath());
    }
    if (!opened) {
        linkedDocumentPath().clear();
        linkedOutputPath().clear();
        linkedDocumentFingerprint().clear();
        updateView();
        return false;
    }

    linkedDocumentPath() = normalized_document_path;
    linkedOutputPath() = normalized_output_path;

    linkedDocumentFingerprint() = documentFingerprint(linkedDocumentPath());
    if (linkedDocumentFingerprint().isEmpty()) {
        reportError(QStringLiteral("open_linked_image"),
                    QStringLiteral("The linked document could not be fingerprinted."),
                    linkedDocumentPath());
        linkedDocumentPath().clear();
        linkedOutputPath().clear();
        updateView();
        return false;
    }
    updateView(true);
    return true;
}

void ImageEditorWindow::relinkSource() {
    if (!activeSession().sourceIsMissing()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Relink Source Image"), {}, imageFilter());
    if (path.isEmpty()) return;
    QString error;
    if (!activeSession().relinkSource(path, &error)) {
        reportError(QStringLiteral("relink_source"), error, path);
        return;
    }
    deactivateCanvasTools();
    updateView();
    statusBar()->showMessage(QStringLiteral("Source image relinked"), 3000);
}

bool ImageEditorWindow::saveToPath(QString path) {
    if (activeCanvas() != nullptr) activeCanvas()->commitTextEditing();
    const QString previous_recovery = recovery_store_.pathFor(activeSession());
    std::unique_ptr<QLockFile> linked_write_lock;
    if (!linkedDocumentPath().isEmpty()) {
        path = linkedDocumentPath();
        linked_write_lock = std::make_unique<QLockFile>(
            linkedDocumentPath() + QStringLiteral(".lock"));
        if (!linked_write_lock->tryLock()) {
            reportError(
                QStringLiteral("save_linked_document_conflict"),
                QStringLiteral("Another Image Editor instance is saving this linked document. Try again after it finishes."),
                linkedDocumentPath());
            return false;
        }
        if (QFileInfo::exists(linkedDocumentPath())) {
            const auto current_fingerprint = documentFingerprint(linkedDocumentPath());
            if (current_fingerprint.isEmpty() ||
                current_fingerprint != linkedDocumentFingerprint()) {
                reportError(
                    QStringLiteral("save_linked_document_conflict"),
                    QStringLiteral("The linked document changed outside this Image Editor window. Reopen it before saving to avoid overwriting a newer revision."),
                    linkedDocumentPath());
                return false;
            }
        } else if (!linkedDocumentFingerprint().isEmpty()) {
            reportError(QStringLiteral("save_linked_document_conflict"),
                        QStringLiteral("The linked document was removed outside this Image Editor window."),
                        linkedDocumentPath());
            return false;
        }
    }
    if (path.isEmpty()) path = activeSession().documentPath();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Editable Image Document"), {},
            QStringLiteral("Image Editor document (*.cimg)"));
    }
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("cimg"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".cimg");
    }

    QString error;
    if (!activeSession().saveDocument(path, &error)) {
        reportError(QStringLiteral("save_document"), error, path);
        return false;
    }
    if (!linkedDocumentPath().isEmpty()) {
        linkedDocumentFingerprint() = documentFingerprint(linkedDocumentPath());
        if (linkedDocumentFingerprint().isEmpty()) {
            reportError(QStringLiteral("save_linked_document"),
                        QStringLiteral("The saved linked document could not be verified."),
                        linkedDocumentPath());
            return false;
        }
        if (!activeSession().exportImage(linkedOutputPath(), &error)) {
            reportError(QStringLiteral("publish_linked_image"), error,
                        linkedOutputPath());
            return false;
        }
    }
    static_cast<void>(recovery_store_.remove(previous_recovery));
    updateView();
    statusBar()->showMessage(
        linkedDocumentPath().isEmpty()
            ? QStringLiteral("Editable document saved")
            : QStringLiteral("Linked document saved and image published"), 3000);
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

void ImageEditorWindow::exportImage(bool quick_export) {
    activeCanvas()->commitTextEditing();
    QString selected_filter;
    const QString path = QFileDialog::getSaveFileName(
        this, quick_export ? QStringLiteral("Quick Export Selected Layer")
                           : QStringLiteral("Export Flattened Image"), {},
        QStringLiteral("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"),
        &selected_filter);
    if (path.isEmpty()) return;
    QString output = path;
    if (QFileInfo(output).suffix().isEmpty()) {
        output += selected_filter.startsWith(QStringLiteral("JPEG"), Qt::CaseInsensitive)
            ? QStringLiteral(".jpg") : QStringLiteral(".png");
    }

    ImageExportOptions options;
    const QString suffix = QFileInfo(output).suffix().toLower();
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
        bool preferences_read = false;
        QString preferences_path;
        options = loadJpegExportPreferences(&preferences_read, &preferences_path);
        if (!preferences_read) {
            const QString cause = QStringLiteral(
                "JPEG export preferences could not be read; default values are in use.");
            logger_.logError(QStringLiteral("load_export_preferences"), cause,
                             preferences_path);
            statusBar()->showMessage(cause, 5000);
        }

        if (!quick_export) {
            JpegExportOptionsDialog options_dialog(options, this);
            if (options_dialog.exec() != QDialog::Accepted) return;
            options = options_dialog.options();

            QString preferences_error;
            if (!saveJpegExportPreferences(options, &preferences_error, &preferences_path)) {
                logger_.logError(QStringLiteral("save_export_preferences"),
                                 preferences_error, preferences_path);
                QMessageBox::warning(this, QStringLiteral("Image Editor"),
                                     preferences_error + QStringLiteral(
                                         " This export will continue with the selected options."));
            }
        }
    }
    options.scope = quick_export
        ? (activeSession().selectedGroupIsActive() ? ImageExportScope::SelectedGroup
                                            : ImageExportScope::SelectedLayer)
        : ImageExportScope::Composite;

    const ImageExportSnapshot snapshot = activeSession().exportSnapshot();
    const ImageExportResult result = ImageExportController::run(
        this, snapshot, output, options);

    if (result.status == ImageExportStatus::Cancelled) {
        statusBar()->showMessage(QStringLiteral("Image export cancelled"), 3000);
        return;
    }
    if (result.status != ImageExportStatus::Succeeded) {
        reportError(QStringLiteral("export_image"), result.error, output);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Image exported"), 3000);
}

void ImageEditorWindow::handleCrop(const QRect& crop) {
    crop_action_->setChecked(false);
    QString error;
    if (activeSession().applyCrop(crop, &error)) {
        updateView();
        statusBar()->showMessage(QStringLiteral("Image cropped"), 3000);
    } else if (!error.isEmpty()) {
        reportError(QStringLiteral("crop_image"), error, activeSession().sourcePath());
    }
}

void ImageEditorWindow::handlePaintStroke(const QVector<QPointF>& points,
                                          const QColor& color,
                                          int diameter,
                                          std::optional<QPainterPath> clipping_path) {
    if (clipping_path.has_value() && clipping_path->isEmpty()) return;
    QString error;
    if (editingMask() ? activeSession().applyLayerMaskStroke(
                            points, color, diameter, &error, std::move(clipping_path))
                      : activeSession().applyPaintStroke(
                            points, color, diameter, &error, std::move(clipping_path))) {
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Paint stroke applied"), 1800);
    } else if (!error.isEmpty()) {
        reportError(QStringLiteral("paint_stroke"), error, activeSession().sourcePath());
    }
}

void ImageEditorWindow::handleEraseStroke(const QVector<QPointF>& points, int diameter,
                                          std::optional<QPainterPath> clipping_path) {
    if (clipping_path.has_value() && clipping_path->isEmpty()) return;
    QString error;
    if (editingMask() ? activeSession().applyLayerMaskEraseStroke(
                            points, diameter, &error, std::move(clipping_path))
                      : activeSession().applyEraseStroke(
                            points, diameter, &error, std::move(clipping_path))) {
        updateView(true);
        statusBar()->showMessage(QStringLiteral("Erase stroke applied"), 1800);
    } else if (!error.isEmpty()) {
        reportError(QStringLiteral("erase_stroke"), error, activeSession().sourcePath());
    }
}

void ImageEditorWindow::deactivateCanvasTools() {
    if (activeCanvas() == nullptr || tool_sidebar_ == nullptr) return;
    tool_sidebar_->setActiveTool(ToolSidebar::Tool::None);
    updateToolOptions();
    if (crop_action_ != nullptr && crop_action_->isChecked()) {
        crop_action_->setChecked(false);
    }
    activeCanvas()->setPaintMode(false);
    activeCanvas()->setEraserMode(false);
    activeCanvas()->setAreaSelectionMode(false);
    activeCanvas()->setCropMode(false);
    activeCanvas()->setShapeCreationMode(false);
    activeCanvas()->setObjectSelectionMode(false);
}

void ImageEditorWindow::maybeOfferRecovery() {
    const auto snapshots = recovery_store_.snapshots();
    for (const QString& snapshot : snapshots) {
        QMessageBox prompt(QMessageBox::Question,
                           QStringLiteral("Recover image edits?"),
                           QStringLiteral("Restore this recovery snapshot in a new tab?\n\n%1")
                               .arg(QFileInfo(snapshot).completeBaseName()),
                           QMessageBox::NoButton, this);
        auto* restore = prompt.addButton(QStringLiteral("Restore in New Tab"), QMessageBox::AcceptRole);
        auto* discard = prompt.addButton(QStringLiteral("Discard Snapshot"), QMessageBox::DestructiveRole);
        auto* later = prompt.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
        prompt.exec();
        if (prompt.clickedButton() == discard) {
            static_cast<void>(recovery_store_.remove(snapshot));
            continue;
        }
        if (prompt.clickedButton() == later) break;
        if (prompt.clickedButton() != restore) continue;

        const int index = addDocumentTab(true);
        QString error;
        if (!activeSession().restoreRecovery(snapshot, &error)) {
            reportError(QStringLiteral("restore_recovery"), error, snapshot);
            closeDocumentTab(index);
            continue;
        }
        deactivateCanvasTools();
        updateView();
        if (activeSession().sourceIsMissing()) {
            QMessageBox::information(this, QStringLiteral("Source image is missing"),
                QStringLiteral("The recovered document needs its source image to be relinked."));
            relinkSource();
        }
    }
}

void ImageEditorWindow::reportError(const QString& operation,
                                    const QString& cause,
                                    const QString& path) {
    logger_.logError(operation, cause, path);
    QMessageBox::critical(this, QStringLiteral("Image Editor"), cause);
}

void ImageEditorWindow::closeEvent(QCloseEvent* event) {
    if (importing_) { event->ignore(); return; }
    const int previous_active = active_document_tab_;
    QStringList recovery_paths;
    for (int index = 0; index < document_tabs_.size(); ++index) {
        if (index != active_document_tab_) {
            const QSignalBlocker blocker(document_tab_bar_);
            document_tab_bar_->setCurrentIndex(index);
            activateDocumentTab(index);
        }
        if (!confirmDiscardOrSave(false)) {
            if (previous_active >= 0 && previous_active < document_tabs_.size()) {
                const QSignalBlocker blocker(document_tab_bar_);
                document_tab_bar_->setCurrentIndex(previous_active);
                activateDocumentTab(previous_active);
            }
            event->ignore();
            return;
        }
        const QString recovery_path = recovery_store_.pathFor(activeSession());
        if (!recovery_path.isEmpty()) recovery_paths.append(recovery_path);
    }
    for (const QString& recovery_path : recovery_paths)
        static_cast<void>(recovery_store_.remove(recovery_path));
    event->accept();
}

} // namespace image_editor
