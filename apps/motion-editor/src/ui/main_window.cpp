#include "main_window.h"

#include "composition_viewer.h"
#include "media_pool_widget.h"
#include "new_composition_dialog.h"
#include "timeline_navigator.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <filesystem>
#include <utility>
#include <vector>

namespace motion::ui {
namespace {

std::filesystem::path pathFromQString(const QString& value)
{
    const auto utf8 = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first, first + utf8.size()));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Motion Studio"));
    setWindowState(windowState() | Qt::WindowMaximized);

    auto* empty_state_container = new QWidget(this);
    empty_state_container->setObjectName(QStringLiteral("motion-empty-state-container"));
    auto* empty_state_layout = new QVBoxLayout(empty_state_container);
    empty_state_layout->setContentsMargins(16, 16, 16, 16);
    empty_state_layout->setSpacing(10);
    empty_state_layout->addStretch(1);

    empty_state_ = new QLabel(QStringLiteral("No composition open"), empty_state_container);
    empty_state_->setObjectName(QStringLiteral("motion-empty-state"));
    empty_state_->setAlignment(Qt::AlignCenter);
    empty_state_layout->addWidget(empty_state_, 0, Qt::AlignHCenter);

    empty_state_new_composition_button_ = new QPushButton(
        QStringLiteral("New Composition..."), empty_state_container);
    empty_state_new_composition_button_->setObjectName(
        QStringLiteral("motion-empty-new-composition-button"));
    empty_state_layout->addWidget(empty_state_new_composition_button_, 0, Qt::AlignHCenter);
    empty_state_layout->addStretch(1);
    setCentralWidget(empty_state_container);

    QMenu* file_menu = menuBar()->addMenu(QStringLiteral("File"));
    QAction* new_composition_action = file_menu->addAction(QStringLiteral("New Composition..."));
    new_composition_action->setObjectName(QStringLiteral("motion-new-composition-action"));
    connect(new_composition_action, &QAction::triggered, this, [this] {
        createNewComposition();
    });
    connect(empty_state_new_composition_button_, &QPushButton::clicked,
            new_composition_action, &QAction::trigger);
    import_media_action_ = file_menu->addAction(QStringLiteral("Import Media..."));
    import_media_action_->setObjectName(QStringLiteral("motion-import-media-action"));
    import_media_action_->setEnabled(false);
    connect(import_media_action_, &QAction::triggered, this, [this] { openMedia(); });
}

const model::CompositionDocument* MainWindow::compositionDocument() const noexcept
{
    return document_.has_value() ? &*document_ : nullptr;
}

MediaPoolWidget* MainWindow::mediaPoolWidget() const noexcept
{
    return media_pool_;
}

void MainWindow::createNewComposition()
{
    if (document_.has_value()) {
        QMessageBox replace_prompt(
            QMessageBox::Warning,
            QStringLiteral("Replace Composition"),
            QStringLiteral("The current composition has not been saved. Replace it?"),
            QMessageBox::Yes | QMessageBox::No,
            this);
        replace_prompt.setObjectName(QStringLiteral("motion-replace-composition-prompt"));
        replace_prompt.setDefaultButton(QMessageBox::No);
        if (replace_prompt.exec() != QMessageBox::Yes) return;
    }

    NewCompositionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto settings = dialog.compositionSettings();
    if (!settings.has_value()) return;

    document_.emplace(settings->canvas_size.width,
                      settings->canvas_size.height,
                      settings->frame_rate);
    if (workspace_ == nullptr) createWorkspace();
    else media_pool_->clear();
    import_media_action_->setEnabled(true);
    timeline_->setCompositionTiming(settings->frame_rate);
    viewer_->setComposition(document_->canvasSize(), std::nullopt);
    media_details_->setMedia(nullptr);
}

void MainWindow::createWorkspace()
{
    const bool was_maximized = isMaximized();
    const bool was_full_screen = isFullScreen();
    const auto previous_geometry = geometry();

    composition_splitter_ = new QSplitter(Qt::Vertical, this);
    composition_splitter_->setObjectName(QStringLiteral("motion-composition-splitter"));
    composition_splitter_->setChildrenCollapsible(false);

    workspace_ = new QSplitter(Qt::Horizontal, composition_splitter_);
    workspace_->setObjectName(QStringLiteral("motion-workspace"));
    workspace_->setChildrenCollapsible(false);

    media_pool_ = new MediaPoolWidget(workspace_);
    media_pool_->setMinimumWidth(220);
    media_pool_->setImportRequestedHandler([this] { openMedia(); });
    media_pool_->setSelectionChangedHandler([this] { updateMediaDetails(); });
    viewer_ = new CompositionViewer(workspace_);
    viewer_->setObjectName(QStringLiteral("motion-composition-viewer"));
    media_details_ = new MediaDetailsWidget(workspace_);

    timeline_ = new TimelineNavigator(composition_splitter_);
    workspace_->addWidget(media_pool_);
    workspace_->addWidget(viewer_);
    workspace_->addWidget(media_details_);
    workspace_->setStretchFactor(0, 0);
    workspace_->setStretchFactor(1, 1);
    workspace_->setStretchFactor(2, 0);
    workspace_->setSizes({270, 800, 300});
    composition_splitter_->addWidget(workspace_);
    composition_splitter_->addWidget(timeline_);
    composition_splitter_->setStretchFactor(0, 1);
    composition_splitter_->setStretchFactor(1, 0);
    composition_splitter_->setSizes({570, 150});
    setCentralWidget(composition_splitter_);
    empty_state_ = nullptr;
    empty_state_new_composition_button_ = nullptr;
    if (!was_maximized && !was_full_screen) setGeometry(previous_geometry);

}

void MainWindow::openMedia()
{
    if (!document_.has_value() || media_pool_ == nullptr) return;
    QFileDialog dialog(this, QStringLiteral("Import Media"));
    dialog.setObjectName(QStringLiteral("motion-import-media-dialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters({
        QStringLiteral("Supported media (*.avi *.mkv *.mov *.mp4 *.mxf *.webm *.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"),
        QStringLiteral("Video files (*.avi *.mkv *.mov *.mp4 *.mxf *.webm)"),
        QStringLiteral("Image files (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"),
        QStringLiteral("All files (*)"),
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const auto files = dialog.selectedFiles();
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<std::size_t>(files.size()));
    for (const auto& file : files) paths.push_back(pathFromQString(file));
    media_pool_->importFiles(std::move(paths));
}

void MainWindow::updateMediaDetails()
{
    if (media_pool_ == nullptr || media_details_ == nullptr) return;
    media_details_->setMedia(media_pool_->selectedMedia());
}

} // namespace motion::ui
