#include "main_window.h"

#include "composition_viewer.h"
#include "media_pool_widget.h"
#include "new_composition_dialog.h"
#include "preview_renderer.h"
#include "timeline_navigator.h"

#include <creative_suite/diagnostics/logger.h>

#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
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

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

QString layerName(const model::CompositionLayer& layer)
{
    return QString::fromUtf8(layer.name.data(), static_cast<qsizetype>(layer.name.size()));
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

MainWindow::~MainWindow()
{
    if (preview_renderer_) preview_renderer_->stopAndWait();
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
    selected_layer_id_ = 0;
    if (workspace_ == nullptr) createWorkspace();
    else {
        media_pool_->clear();
        preview_renderer_->resetSessions();
    }
    import_media_action_->setEnabled(true);
    timeline_->setCompositionTiming(settings->frame_rate);
    timeline_->setLayers(document_->layers());
    timeline_->setSelectedLayerId(0);
    viewer_->setComposition(document_->canvasSize(), std::nullopt);
    media_details_->setMedia(nullptr);
    inspector_tabs_->setCurrentWidget(media_details_);
    syncTransformInspector();
    requestPreview();
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

    inspector_tabs_ = new QTabWidget(workspace_);
    inspector_tabs_->setObjectName(QStringLiteral("motion-inspector-tabs"));
    inspector_tabs_->setMinimumWidth(250);
    inspector_tabs_->addTab(media_details_, QStringLiteral("Media"));

    transform_inspector_ = new QWidget(inspector_tabs_);
    transform_inspector_->setObjectName(QStringLiteral("motion-transform-inspector"));
    auto* transform_layout = new QVBoxLayout(transform_inspector_);
    auto* transform_title = new QLabel(QStringLiteral("Selected Layer Transform"),
                                       transform_inspector_);
    transform_title->setObjectName(QStringLiteral("motion-transform-inspector-title"));
    transform_layout->addWidget(transform_title);
    auto* transform_form = new QFormLayout();
    const std::array<std::pair<QString, QString>, 5> transform_rows{{
        {QStringLiteral("Position X (normalized)"), QStringLiteral("motion-transform-position-x")},
        {QStringLiteral("Position Y (normalized)"), QStringLiteral("motion-transform-position-y")},
        {QStringLiteral("Scale"), QStringLiteral("motion-transform-scale")},
        {QStringLiteral("Rotation (degrees)"), QStringLiteral("motion-transform-rotation")},
        {QStringLiteral("Opacity (0-1)"), QStringLiteral("motion-transform-opacity")},
    }};
    for (std::size_t index = 0; index < transform_rows.size(); ++index) {
        auto* field = new QDoubleSpinBox(transform_inspector_);
        field->setObjectName(transform_rows[index].second);
        field->setDecimals(6);
        field->setKeyboardTracking(false);
        if (index == 0 || index == 1) {
            field->setRange(-1'000'000.0, 1'000'000.0);
        } else if (index == 2) {
            field->setRange(0.000001, 1'000'000.0);
        } else if (index == 3) {
            field->setRange(-1'000'000'000.0, 1'000'000'000.0);
        } else {
            field->setRange(0.0, 1.0);
        }
        transform_fields_[index] = field;
        transform_form->addRow(transform_rows[index].first, field);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this](double) {
            editSelectedLayerTransform();
        });
    }
    transform_layout->addLayout(transform_form);
    transform_layout->addStretch(1);
    inspector_tabs_->addTab(transform_inspector_, QStringLiteral("Transform"));

    timeline_ = new TimelineNavigator(composition_splitter_);
    timeline_->setMediaDropHandler([this](const std::filesystem::path& path,
                                          std::int64_t frame,
                                          model::LayerId before) {
        handleMediaDrop(path, frame, before);
    });
    timeline_->setLayerSelectedHandler([this](model::LayerId id) { selectLayer(id); });
    timeline_->setLayerMoveHandler([this](model::LayerId id, std::int64_t frame) {
        if (document_ && document_->moveLayerInTimeline(id, frame)) {
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerResizeHandler([this](model::LayerId id, std::int64_t duration) {
        if (document_ && document_->resizeLayerDuration(id, duration)) {
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerReorderHandler([this](model::LayerId id, std::size_t front_index) {
        if (!document_ || front_index >= document_->layers().size()) return;
        const auto model_index = document_->layers().size() - 1 - front_index;
        if (document_->moveLayer(id, model_index)) {
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerVisibilityHandler([this](model::LayerId id, bool visible) {
        if (document_ && document_->setLayerVisible(id, visible)) {
            refreshTimeline();
            if (selected_layer_id_ == id) syncTransformInspector();
            requestPreview();
        }
    });
    timeline_->setLayerRemoveHandler([this](model::LayerId id) {
        if (!document_ || !document_->removeLayer(id)) return;
        if (selected_layer_id_ == id) selected_layer_id_ = 0;
        refreshTimeline();
        syncTransformInspector();
        requestPreview();
    });
    connect(timeline_, &TimelineNavigator::currentFrameChanged,
            this, [this] { requestPreview(); });

    workspace_->addWidget(media_pool_);
    workspace_->addWidget(viewer_);
    workspace_->addWidget(inspector_tabs_);
    workspace_->setStretchFactor(0, 0);
    workspace_->setStretchFactor(1, 1);
    workspace_->setStretchFactor(2, 0);
    workspace_->setSizes({270, 800, 300});
    composition_splitter_->addWidget(workspace_);
    composition_splitter_->addWidget(timeline_);
    composition_splitter_->setStretchFactor(0, 1);
    composition_splitter_->setStretchFactor(1, 0);
    composition_splitter_->setSizes({570, 220});
    setCentralWidget(composition_splitter_);
    empty_state_ = nullptr;
    empty_state_new_composition_button_ = nullptr;

    preview_renderer_ = std::make_unique<PreviewRenderer>(this,
        [this](std::uint64_t generation, creative_suite::media::RgbaFramePtr frame) {
            if (preview_renderer_ && generation == preview_renderer_->generation() &&
                viewer_ != nullptr) {
                viewer_->setRenderedFrame(std::move(frame));
            }
        });
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
    if (inspector_tabs_ != nullptr) inspector_tabs_->setCurrentWidget(media_details_);
}

void MainWindow::refreshTimeline()
{
    if (timeline_ == nullptr || !document_) return;
    timeline_->setLayers(document_->layers());
    timeline_->setSelectedLayerId(selected_layer_id_);
}

void MainWindow::selectLayer(model::LayerId id)
{
    if (!document_) return;
    const auto& layers = document_->layers();
    const auto found = std::find_if(layers.begin(), layers.end(), [id](const auto& layer) {
        return layer.id == id;
    });
    if (found == layers.end()) return;
    selected_layer_id_ = id;
    timeline_->setSelectedLayerId(id);
    syncTransformInspector();
    inspector_tabs_->setCurrentWidget(transform_inspector_);
    viewer_->setSelectedLayerAnchor(found->visible
        ? std::optional<QPointF>(QPointF(found->transform.position_x,
                                         found->transform.position_y))
        : std::nullopt);
}

void MainWindow::syncTransformInspector()
{
    const model::CompositionLayer* selected = nullptr;
    if (document_ && selected_layer_id_ != 0) {
        const auto& layers = document_->layers();
        const auto found = std::find_if(layers.begin(), layers.end(), [this](const auto& layer) {
            return layer.id == selected_layer_id_;
        });
        if (found != layers.end()) selected = &*found;
    }
    if (transform_inspector_ != nullptr) transform_inspector_->setEnabled(selected != nullptr);
    if (selected == nullptr) {
        viewer_->setSelectedLayerAnchor(std::nullopt);
        return;
    }
    const auto& transform = selected->transform;
    const std::array<double, 5> values{{transform.position_x, transform.position_y,
        transform.scale, transform.rotation_degrees, transform.opacity}};
    for (std::size_t index = 0; index < transform_fields_.size(); ++index) {
        const QSignalBlocker blocker(transform_fields_[index]);
        transform_fields_[index]->setValue(values[index]);
    }
    viewer_->setSelectedLayerAnchor(selected->visible
        ? std::optional<QPointF>(QPointF(transform.position_x, transform.position_y))
        : std::nullopt);
}

void MainWindow::editSelectedLayerTransform()
{
    if (!document_ || selected_layer_id_ == 0) return;
    creative_suite::animation::Transform2D transform{
        transform_fields_[0]->value(),
        transform_fields_[1]->value(),
        transform_fields_[2]->value(),
        transform_fields_[3]->value(),
        transform_fields_[4]->value()};
    if (!document_->setLayerTransform(selected_layer_id_, transform)) {
        syncTransformInspector();
        return;
    }
    syncTransformInspector();
    requestPreview();
}

void MainWindow::handleMediaDrop(const std::filesystem::path& path,
                                std::int64_t start_frame,
                                model::LayerId before_layer_id)
{
    if (!document_ || media_pool_ == nullptr || timeline_ == nullptr) return;
    const auto index = media_pool_->library().indexForPath(path);
    if (index >= media_pool_->library().size()) {
        statusBar()->showMessage(QStringLiteral("This media item is no longer in the Media Pool."),
                                 5000);
        return;
    }
    const auto& item = media_pool_->library().items()[index];
    if (item.offline) {
        statusBar()->showMessage(QStringLiteral("Restore this offline media item before adding it."),
                                 5000);
        return;
    }

    model::LayerId added_id = 0;
    model::AddMediaLayerResult result = model::AddMediaLayerResult::InvalidTimingMetadata;
    try {
        result = document_->addMediaLayer(item.metadata, start_frame, &added_id);
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_timeline", "insert_media_layer", error.what(),
            {{"path", pathForLog(path)}, {"frame", std::to_string(start_frame)}});
        QMessageBox::warning(this, QStringLiteral("Cannot Add Media"),
            QStringLiteral("The selected media could not be added to this composition."));
        return;
    } catch (...) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_timeline", "insert_media_layer", "Unknown media layer insertion failure",
            {{"path", pathForLog(path)}, {"frame", std::to_string(start_frame)}});
        QMessageBox::warning(this, QStringLiteral("Cannot Add Media"),
            QStringLiteral("The selected media could not be added to this composition."));
        return;
    }
    if (result != model::AddMediaLayerResult::Added) {
        const std::string cause = result == model::AddMediaLayerResult::InvalidTimingMetadata
            ? "Media does not contain enough valid timing metadata to create a timeline layer"
            : "Media layer could not be added to the Motion Studio composition";
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_timeline", "insert_media_layer", cause,
            {{"path", pathForLog(path)}, {"frame", std::to_string(start_frame)}});
        QMessageBox::warning(this, QStringLiteral("Cannot Add Media"),
            result == model::AddMediaLayerResult::InvalidTimingMetadata
                ? QStringLiteral("This video needs a valid frame rate and positive duration or frame count.")
                : QStringLiteral("The selected media could not be added to this composition."));
        return;
    }

    if (before_layer_id != 0) {
        const auto target_layer = std::find_if(
            document_->layers().begin(), document_->layers().end(),
            [before_layer_id](const auto& layer) { return layer.id == before_layer_id; });
        if (target_layer != document_->layers().end()) {
            const auto target_model_index = static_cast<std::size_t>(
                std::distance(document_->layers().begin(), target_layer));
            const auto destination = target_model_index + 1;
            (void)document_->moveLayer(added_id, destination);
        }
    }
    selected_layer_id_ = added_id;
    refreshTimeline();
    syncTransformInspector();
    inspector_tabs_->setCurrentWidget(transform_inspector_);
    requestPreview();
}

void MainWindow::requestPreview()
{
    if (!document_ || !preview_renderer_ || !media_pool_ || !timeline_) return;
    PreviewRequest request;
    request.canvas_size = document_->canvasSize();
    request.frame_rate = document_->frameRate();
    const auto frame = timeline_->currentFrame();
    for (const auto& layer : document_->layers()) {
        if (!layer.visible || layer.duration_frames <= 0 || frame < layer.timeline_start_frame ||
            frame - layer.timeline_start_frame >= layer.duration_frames) {
            continue;
        }
        PreviewLayerSnapshot snapshot;
        snapshot.kind = layer.kind;
        snapshot.source_path = layer.source_path;
        snapshot.local_frame = frame - layer.timeline_start_frame;
        snapshot.source_frame_count = layer.source_frame_count;
        snapshot.source_frame_rate = layer.source_frame_rate;
        snapshot.transform = layer.transform;
        if (layer.kind == model::LayerKind::Image) {
            snapshot.still_frame = media_pool_->sharedFirstFrameForPath(layer.source_path);
        }
        request.layers.push_back(std::move(snapshot));
    }
    (void)preview_renderer_->submit(std::move(request));
}

} // namespace motion::ui
