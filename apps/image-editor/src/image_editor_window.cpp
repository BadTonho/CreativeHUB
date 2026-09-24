#include "image_editor_window.h"

#include "image_canvas.h"
#include "image_document_store.h"
#include "new_canvas_dialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

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
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    canvas_ = new ImageCanvas(central);
    layout->addWidget(canvas_, 1);
    setCentralWidget(central);

    status_label_ = new QLabel(QStringLiteral("No image open"), this);
    status_label_->setObjectName(QStringLiteral("imageStatusLabel"));
    statusBar()->addWidget(status_label_, 1);

    createActions();
    connect(canvas_, &ImageCanvas::cropSelected, this,
            [this](const QRect& crop) { handleCrop(crop); });
    connect(canvas_, &ImageCanvas::cropModeCancelled, this, [this]() {
        crop_action_->setChecked(false);
        statusBar()->showMessage(QStringLiteral("Crop cancelled"), 2500);
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
    crop_action_->setCheckable(true);
    connect(crop_action_, &QAction::toggled, this, [this](bool enabled) {
        canvas_->setCropMode(enabled && session_.hasSource());
        statusBar()->showMessage(enabled
            ? QStringLiteral("Drag over the image to crop; press Esc to cancel")
            : QStringLiteral("Ready"));
    });
    fit_action_ = makeAction(
        QStringLiteral("Fit Image"), {}, [this]() { canvas_->fitToWindow(); });

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

    auto* toolbar = addToolBar(QStringLiteral("Image Editing"));
    toolbar->setMovable(false);
    toolbar->addAction(new_canvas_action_);
    toolbar->addAction(open_image_action);
    toolbar->addAction(open_document_action);
    toolbar->addSeparator();
    toolbar->addAction(save_action_);
    toolbar->addAction(export_action_);
    toolbar->addAction(crop_action_);
    toolbar->addAction(rotate_left_action_);
    toolbar->addAction(rotate_right_action_);
    toolbar->addAction(flip_horizontal_action_);
    toolbar->addAction(flip_vertical_action_);
    toolbar->addAction(fit_action_);
}

void ImageEditorWindow::updateView() {
    const QImage rendered = session_.renderedImage();
    canvas_->setImage(rendered);
    undo_action_->setEnabled(session_.canUndo());
    redo_action_->setEnabled(session_.canRedo());
    save_action_->setEnabled(session_.hasSource());
    save_as_action_->setEnabled(session_.hasSource());
    export_action_->setEnabled(session_.hasSource());
    relink_action_->setEnabled(session_.sourceIsMissing());
    crop_action_->setEnabled(session_.hasSource());
    rotate_left_action_->setEnabled(session_.hasSource());
    rotate_right_action_->setEnabled(session_.hasSource());
    flip_horizontal_action_->setEnabled(session_.hasSource());
    flip_vertical_action_->setEnabled(session_.hasSource());
    fit_action_->setEnabled(session_.hasSource());

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
