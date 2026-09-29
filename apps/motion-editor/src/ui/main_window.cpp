#include "main_window.h"

#include "composition_viewer.h"
#include "autosave_recovery_dialog.h"
#include "media_pool_widget.h"
#include "new_composition_dialog.h"
#include "../persistence/motion_document_store.h"
#include "preview_renderer.h"
#include "shortcut_settings_dialog.h"
#include "timeline_navigator.h"
#include "../settings/autosave_preferences.h"

#include <creative_suite/animation/animation.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QKeySequence>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDesktopServices>
#include <QEvent>
#include <QFontComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QRunnable>
#include <QThreadPool>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
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

QString pathForDisplay(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(encoded.data()),
                             static_cast<qsizetype>(encoded.size()));
}

QString layerName(const model::CompositionLayer& layer)
{
    return QString::fromUtf8(layer.name.data(), static_cast<qsizetype>(layer.name.size()));
}

QColor qColor(const model::ColorRgba& color)
{
    return QColor(color[0], color[1], color[2], color[3]);
}

model::ColorRgba modelColor(const QColor& color)
{
    return {static_cast<std::uint8_t>(color.red()),
            static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue()),
            static_cast<std::uint8_t>(color.alpha())};
}

void setColorButton(QPushButton* button, const model::ColorRgba& color)
{
    if (button == nullptr) return;
    const QColor value = qColor(color);
    button->setText(value.name(QColor::HexArgb));
    button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; }")
                              .arg(value.name(QColor::HexArgb)));
}

QString qString(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string utf8String(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

using TransformProperty = creative_suite::animation::TransformProperty;

constexpr std::array<TransformProperty, 5> kTransformProperties{{
    TransformProperty::PositionX,
    TransformProperty::PositionY,
    TransformProperty::Scale,
    TransformProperty::Rotation,
    TransformProperty::Opacity,
}};

double transformPropertyValue(
    const creative_suite::animation::Transform2D& transform,
    TransformProperty property) noexcept
{
    switch (property) {
    case TransformProperty::PositionX: return transform.position_x;
    case TransformProperty::PositionY: return transform.position_y;
    case TransformProperty::Scale: return transform.scale;
    case TransformProperty::Rotation: return transform.rotation_degrees;
    case TransformProperty::Opacity: return transform.opacity;
    }
    return 0.0;
}

void setTransformPropertyValue(
    creative_suite::animation::Transform2D& transform,
    TransformProperty property,
    double value) noexcept
{
    switch (property) {
    case TransformProperty::PositionX: transform.position_x = value; break;
    case TransformProperty::PositionY: transform.position_y = value; break;
    case TransformProperty::Scale: transform.scale = value; break;
    case TransformProperty::Rotation: transform.rotation_degrees = value; break;
    case TransformProperty::Opacity: transform.opacity = value; break;
    }
}

bool containsKeyframeAt(
    const creative_suite::animation::TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame) noexcept
{
    const auto& frames = creative_suite::animation::keyframesFor(keyframes, property);
    const auto found = std::lower_bound(
        frames.begin(), frames.end(), local_frame,
        [](const creative_suite::animation::Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    return found != frames.end() && found->frame == local_frame;
}

class OpenMediaStageTask final : public QRunnable {
public:
    using ProgressHandler = std::function<void(
        std::uint64_t, std::size_t, std::size_t, const std::filesystem::path&)>;
    using FinishedHandler = std::function<void(
        std::uint64_t, std::filesystem::path, model::MotionProjectData,
        creative_suite::media::MediaImportBatchResult, bool,
        std::filesystem::path)>;

    OpenMediaStageTask(QObject* receiver,
                       std::uint64_t generation,
                       std::filesystem::path document_path,
                       model::MotionProjectData project,
                       std::shared_ptr<std::atomic_bool> cancel,
                       bool recovered,
                       std::filesystem::path recovery_snapshot_path,
                       ProgressHandler progress_handler,
                       FinishedHandler finished_handler)
        : receiver_(receiver), generation_(generation),
          document_path_(std::move(document_path)), project_(std::move(project)),
          cancel_(std::move(cancel)), recovered_(recovered),
          recovery_snapshot_path_(std::move(recovery_snapshot_path)),
          progress_handler_(std::move(progress_handler)),
          finished_handler_(std::move(finished_handler))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        std::vector<std::filesystem::path> paths;
        paths.reserve(project_.media.size());
        for (const auto& media : project_.media) paths.push_back(media.source_path);
        QPointer<QObject> receiver = receiver_;
        const auto generation = generation_;
        const auto progress_handler = progress_handler_;
        auto result = creative_suite::media::MediaImporter{}.process(
            paths, *cancel_, [receiver, generation, progress_handler](
                std::size_t completed, std::size_t total,
                const std::filesystem::path& path) {
                if (receiver.isNull()) return;
                QMetaObject::invokeMethod(receiver.data(),
                    [receiver, generation, completed, total, path, progress_handler] {
                        if (!receiver.isNull())
                            progress_handler(generation, completed, total, path);
                    }, Qt::QueuedConnection);
            });
        if (receiver.isNull()) return;
        const auto finished_handler = finished_handler_;
        auto document_path = document_path_;
        auto recovery_snapshot_path = recovery_snapshot_path_;
        const auto recovered = recovered_;
        auto project = std::move(project_);
        QMetaObject::invokeMethod(receiver.data(),
            [receiver, generation, document_path = std::move(document_path),
             project = std::move(project), result = std::move(result),
             recovered, recovery_snapshot_path = std::move(recovery_snapshot_path),
             finished_handler]() mutable {
                if (!receiver.isNull())
                    finished_handler(generation, std::move(document_path),
                                     std::move(project), std::move(result), recovered,
                                     std::move(recovery_snapshot_path));
            }, Qt::QueuedConnection);
    }

private:
    QPointer<QObject> receiver_;
    std::uint64_t generation_ = 0;
    std::filesystem::path document_path_;
    model::MotionProjectData project_;
    std::shared_ptr<std::atomic_bool> cancel_;
    bool recovered_ = false;
    std::filesystem::path recovery_snapshot_path_;
    ProgressHandler progress_handler_;
    FinishedHandler finished_handler_;
};

} // namespace

MainWindow::MainWindow(QWidget* parent,
                       std::filesystem::path recovery_root,
                       std::string recovery_session_id)
    : QMainWindow(parent),
      recovery_store_(std::move(recovery_root), std::move(recovery_session_id))
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
    new_composition_action_ = file_menu->addAction(QStringLiteral("New Composition..."));
    new_composition_action_->setObjectName(QStringLiteral("motion-new-composition-action"));
    new_composition_action_->setShortcut(QKeySequence::New);
    new_composition_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(new_composition_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.new_composition"), QStringLiteral("New Composition"),
        new_composition_action_);
    connect(new_composition_action_, &QAction::triggered, this, [this] {
        createNewComposition();
    });
    connect(empty_state_new_composition_button_, &QPushButton::clicked,
            new_composition_action_, &QAction::trigger);

    open_composition_action_ = file_menu->addAction(QStringLiteral("Open Composition..."));
    open_composition_action_->setObjectName(QStringLiteral("motion-open-composition-action"));
    open_composition_action_->setShortcut(QKeySequence::Open);
    open_composition_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(open_composition_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.open_composition"), QStringLiteral("Open Composition"),
        open_composition_action_);
    connect(open_composition_action_, &QAction::triggered, this, [this] {
        openComposition();
    });

    save_composition_action_ = file_menu->addAction(QStringLiteral("Save"));
    save_composition_action_->setObjectName(QStringLiteral("motion-save-composition-action"));
    save_composition_action_->setShortcut(QKeySequence::Save);
    save_composition_action_->setShortcutContext(Qt::WindowShortcut);
    save_composition_action_->setEnabled(false);
    addAction(save_composition_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.save_composition"), QStringLiteral("Save Composition"),
        save_composition_action_);
    connect(save_composition_action_, &QAction::triggered, this, [this] {
        (void)saveComposition();
    });

    save_composition_as_action_ = file_menu->addAction(QStringLiteral("Save As..."));
    save_composition_as_action_->setObjectName(
        QStringLiteral("motion-save-composition-as-action"));
#if defined(Q_OS_MACOS)
    save_composition_as_action_->setShortcut(
        QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S));
#else
    save_composition_as_action_->setShortcut(
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
#endif
    save_composition_as_action_->setShortcutContext(Qt::WindowShortcut);
    save_composition_as_action_->setEnabled(false);
    addAction(save_composition_as_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.save_composition_as"), QStringLiteral("Save Composition As"),
        save_composition_as_action_);
    connect(save_composition_as_action_, &QAction::triggered, this, [this] {
        (void)saveCompositionAs();
    });

    file_menu->addSeparator();
    import_media_action_ = file_menu->addAction(QStringLiteral("Import Media..."));
    import_media_action_->setObjectName(QStringLiteral("motion-import-media-action"));
    import_media_action_->setEnabled(false);
    const auto import_sequence =
#if defined(Q_OS_MACOS)
        QKeySequence(Qt::META | Qt::Key_I);
#else
        QKeySequence(Qt::CTRL | Qt::Key_I);
#endif
    import_media_action_->setShortcut(import_sequence);
    import_media_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(import_media_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("media.import"), QStringLiteral("Import Media"),
        import_media_action_);
    connect(import_media_action_, &QAction::triggered, this, [this] { openMedia(); });

    auto* layer_menu = menuBar()->addMenu(QStringLiteral("Layer"));
    new_text_layer_action_ = layer_menu->addAction(QStringLiteral("New Text"));
    new_text_layer_action_->setObjectName(QStringLiteral("motion-new-text-layer-action"));
    new_text_layer_action_->setEnabled(false);
    connect(new_text_layer_action_, &QAction::triggered, this, [this] {
        createContentLayer(model::LayerKind::Text, model::ShapeKind::Rectangle);
    });
    new_rectangle_layer_action_ = layer_menu->addAction(QStringLiteral("New Rectangle"));
    new_rectangle_layer_action_->setObjectName(
        QStringLiteral("motion-new-rectangle-layer-action"));
    new_rectangle_layer_action_->setEnabled(false);
    connect(new_rectangle_layer_action_, &QAction::triggered, this, [this] {
        createContentLayer(model::LayerKind::Shape, model::ShapeKind::Rectangle);
    });
    new_ellipse_layer_action_ = layer_menu->addAction(QStringLiteral("New Ellipse"));
    new_ellipse_layer_action_->setObjectName(
        QStringLiteral("motion-new-ellipse-layer-action"));
    new_ellipse_layer_action_->setEnabled(false);
    connect(new_ellipse_layer_action_, &QAction::triggered, this, [this] {
        createContentLayer(model::LayerKind::Shape, model::ShapeKind::Ellipse);
    });

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("Edit"));
    undo_action_ = edit_menu->addAction(QStringLiteral("Undo"));
    undo_action_->setObjectName(QStringLiteral("motion-undo-action"));
    undo_action_->setShortcut(QKeySequence::Undo);
    undo_action_->setShortcutContext(Qt::WindowShortcut);
    undo_action_->setEnabled(false);
    addAction(undo_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("edit.undo"), QStringLiteral("Undo"), undo_action_);
    connect(undo_action_, &QAction::triggered, this, [this] { undoComposition(); });

    redo_action_ = edit_menu->addAction(QStringLiteral("Redo"));
    redo_action_->setObjectName(QStringLiteral("motion-redo-action"));
    redo_action_->setShortcut(QKeySequence::Redo);
    redo_action_->setShortcutContext(Qt::WindowShortcut);
    redo_action_->setEnabled(false);
    addAction(redo_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("edit.redo"), QStringLiteral("Redo"), redo_action_);
    connect(redo_action_, &QAction::triggered, this, [this] { redoComposition(); });

    auto* settings_menu = menuBar()->addMenu(QStringLiteral("Settings"));
    settings_action_ = settings_menu->addAction(QStringLiteral("Keyboard Shortcuts..."));
    settings_action_->setObjectName(QStringLiteral("motion-shortcut-settings-action"));
    connect(settings_action_, &QAction::triggered,
            this, [this] { openShortcutSettings(); });
    autosave_settings_action_ = settings_menu->addAction(
        QStringLiteral("Autosave & Recovery..."));
    autosave_settings_action_->setObjectName(
        QStringLiteral("motion-autosave-recovery-action"));
    connect(autosave_settings_action_, &QAction::triggered,
            this, [this] { openAutosaveRecoverySettings(); });

    const auto register_timeline_action = [this](
        QAction*& action, const QString& object_name, const QString& id,
        const QString& label, const QKeySequence& default_sequence) {
        action = new QAction(label, this);
        action->setObjectName(object_name);
        action->setShortcut(default_sequence);
        action->setShortcutContext(Qt::WindowShortcut);
        addAction(action);
        shortcut_manager_.registerAction(id, label, action);
    };
    register_timeline_action(
        play_pause_action_, QStringLiteral("motion-play-pause-action"),
        QStringLiteral("timeline.play_pause"), QStringLiteral("Play/Pause"),
        QKeySequence(Qt::Key_Space));
    register_timeline_action(
        previous_frame_action_, QStringLiteral("motion-previous-frame-action"),
        QStringLiteral("timeline.previous_frame"), QStringLiteral("Previous frame"),
        QKeySequence(Qt::Key_Left));
    register_timeline_action(
        next_frame_action_, QStringLiteral("motion-next-frame-action"),
        QStringLiteral("timeline.next_frame"), QStringLiteral("Next frame"),
        QKeySequence(Qt::Key_Right));
    register_timeline_action(
        loop_action_, QStringLiteral("motion-loop-action"),
        QStringLiteral("timeline.toggle_loop"), QStringLiteral("Loop"), QKeySequence{});
    loop_action_->setCheckable(true);
    register_timeline_action(
        zoom_in_action_, QStringLiteral("motion-zoom-in-action"),
        QStringLiteral("timeline.zoom_in"), QStringLiteral("Zoom In"), QKeySequence{});
    register_timeline_action(
        zoom_out_action_, QStringLiteral("motion-zoom-out-action"),
        QStringLiteral("timeline.zoom_out"), QStringLiteral("Zoom Out"), QKeySequence{});
    previous_frame_action_->setEnabled(false);
    next_frame_action_->setEnabled(false);
    play_pause_action_->setEnabled(false);
    loop_action_->setEnabled(false);
    zoom_in_action_->setEnabled(false);
    zoom_out_action_->setEnabled(false);

    QString shortcut_error;
    if (!shortcut_manager_.load(&shortcut_error)) {
        const auto detail = shortcut_error.toStdString();
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_settings", "load_shortcuts", detail,
            {{"settings_group", shortcut_manager_.settingsGroup().toStdString()}});
    }

    autosave_timer_ = new QTimer(this);
    autosave_timer_->setObjectName(QStringLiteral("motion-autosave-timer"));
    connect(autosave_timer_, &QTimer::timeout,
            this, [this] { autosaveProject(); });
    configureAutosaveTimer();
    QTimer::singleShot(0, this, [this] { maybeOfferUnsavedRecovery(); });
}

MainWindow::~MainWindow()
{
    if (autosave_timer_ != nullptr) autosave_timer_->stop();
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
    finishPendingTransformEdit();
    NewCompositionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto settings = dialog.compositionSettings();
    if (!settings.has_value()) return;
    if (!confirmReplaceDocument()) return;

    cleanupCurrentUnsavedSnapshots("new_composition_cleanup");
    cleanupRecoveredUnsavedSnapshot("new_recovered_snapshot_cleanup");
    last_autosaved_data_.reset();
    composition_history_.clear();
    active_transform_edit_.reset();
    document_.emplace(settings->canvas_size.width,
                      settings->canvas_size.height,
                      settings->frame_rate);
    document_path_.reset();
    saved_data_.reset();
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
    updateDocumentState();
    requestPreview();
}

void MainWindow::openComposition()
{
    finishPendingTransformEdit();
    if (open_cancel_requested_) return;
    QFileDialog dialog(this, QStringLiteral("Open Composition"));
    dialog.setObjectName(QStringLiteral("motion-open-composition-dialog"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters({QStringLiteral("Motion Studio documents (*.motion)"),
                           QStringLiteral("All files (*)")});
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;

    const auto path = pathFromQString(dialog.selectedFiles().front());
    try {
        const auto recoverable = recovery_store_.recoverableSnapshotsForProject(path);
        if (!recoverable.empty()) {
            const auto selected = chooseRecoverySnapshot(
                recoverable, QFileInfo(pathForDisplay(path)).fileName());
            if (selected.has_value()) {
                restoreRecoverySnapshot(*selected);
                return;
            }
        }
        auto project = persistence::MotionDocumentStore::load(path);
        stageOpenProject(path, std::move(project));
    } catch (const persistence::MotionDocumentError& error) {
        reportDocumentError("open_document", path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
        return;
    } catch (const std::exception& error) {
        reportDocumentError("open_document", path, error);
    }
}

void MainWindow::stageOpenProject(
    std::filesystem::path target_path,
    model::MotionProjectData project,
    bool recovered,
    std::filesystem::path recovery_snapshot_path)
{
    const auto generation = ++open_generation_;
    open_cancel_requested_ = std::make_shared<std::atomic_bool>(false);
    if (project.media.empty()) {
        finishOpen(generation, std::move(target_path), std::move(project), {},
                   recovered, std::move(recovery_snapshot_path));
        return;
    }

    open_progress_ = new QProgressDialog(
        QStringLiteral("Preparing linked media..."), QStringLiteral("Cancel"),
        0, static_cast<int>(project.media.size()), this);
    open_progress_->setObjectName(QStringLiteral("motion-open-media-progress"));
    open_progress_->setWindowTitle(QStringLiteral("Open Composition"));
    open_progress_->setWindowModality(Qt::WindowModal);
    open_progress_->setMinimumDuration(250);
    open_progress_->setValue(0);
    connect(open_progress_, &QProgressDialog::canceled, this, [this] {
        if (open_cancel_requested_)
            open_cancel_requested_->store(true, std::memory_order_relaxed);
    });
    new_composition_action_->setEnabled(false);
    open_composition_action_->setEnabled(false);
    open_progress_->show();

    QPointer<MainWindow> owner(this);
    auto progress_handler = [owner](std::uint64_t task_generation,
                                    std::size_t completed,
                                    std::size_t total,
                                    const std::filesystem::path& media_path) {
        if (owner.isNull() || task_generation != owner->open_generation_ ||
            owner->open_progress_ == nullptr) return;
        owner->open_progress_->setRange(0, static_cast<int>(total));
        owner->open_progress_->setValue(static_cast<int>(completed));
        owner->open_progress_->setLabelText(QStringLiteral("Loading %1")
            .arg(QFileInfo(pathForDisplay(media_path)).fileName()));
    };
    auto finished_handler = [owner](std::uint64_t task_generation,
                                    std::filesystem::path document_path,
                                    model::MotionProjectData staged_project,
                                    creative_suite::media::MediaImportBatchResult result,
                                    bool was_recovered,
                                    std::filesystem::path snapshot_path) {
        if (!owner.isNull())
            owner->finishOpen(task_generation, std::move(document_path),
                              std::move(staged_project), std::move(result),
                              was_recovered, std::move(snapshot_path));
    };
    QThreadPool::globalInstance()->start(new OpenMediaStageTask(
        this, generation, std::move(target_path), std::move(project),
        open_cancel_requested_, recovered, std::move(recovery_snapshot_path),
        std::move(progress_handler), std::move(finished_handler)));
}

bool MainWindow::saveComposition()
{
    finishPendingTransformEdit();
    if (!document_) return false;
    return document_path_.has_value()
        ? saveToPath(*document_path_)
        : saveCompositionAs();
}

bool MainWindow::saveCompositionAs()
{
    finishPendingTransformEdit();
    if (!document_) return false;
    QFileDialog dialog(this, QStringLiteral("Save Composition As"));
    dialog.setObjectName(QStringLiteral("motion-save-composition-dialog"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({QStringLiteral("Motion Studio documents (*.motion)")});
    dialog.setDefaultSuffix(QStringLiteral("motion"));
    if (document_path_.has_value())
        dialog.selectFile(QString::fromUtf8(pathForLog(*document_path_)));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    return saveToPath(pathFromQString(dialog.selectedFiles().front()));
}

bool MainWindow::saveToPath(const std::filesystem::path& path)
{
    finishPendingTransformEdit();
    if (!document_) return false;
    const bool was_untitled = !document_path_.has_value();
    try {
        const auto project_snapshot = projectData();
        persistence::MotionDocumentStore::save(path, project_snapshot);
        document_path_ = creative_suite::media::MediaLibrary::canonicalPath(path);
        saved_data_ = project_snapshot;
        last_autosaved_data_.reset();
        if (was_untitled) {
            cleanupCurrentUnsavedSnapshots("save_as_unsaved_recovery_cleanup");
            cleanupRecoveredUnsavedSnapshot("save_as_recovered_snapshot_cleanup");
        }
        updateDocumentState();
        statusBar()->showMessage(QStringLiteral("Composition saved."), 4000);
        return true;
    } catch (const persistence::MotionDocumentError& error) {
        reportDocumentError("save_document", path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
    } catch (const std::exception& error) {
        reportDocumentError("save_document", path, error);
    }
    return false;
}

bool MainWindow::confirmReplaceDocument()
{
    if (!documentIsDirty()) return true;
    QMessageBox prompt(QMessageBox::Warning,
                       QStringLiteral("Unsaved Changes"),
                       QStringLiteral("Save changes to the current composition before continuing?"),
                       QMessageBox::NoButton, this);
    prompt.setObjectName(QStringLiteral("motion-unsaved-changes-prompt"));
    auto* save = prompt.addButton(QMessageBox::Save);
    auto* discard = prompt.addButton(QMessageBox::Discard);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    save->setObjectName(QStringLiteral("motion-unsaved-save-button"));
    discard->setObjectName(QStringLiteral("motion-unsaved-discard-button"));
    cancel->setObjectName(QStringLiteral("motion-unsaved-cancel-button"));
    prompt.setDefaultButton(save);
    prompt.exec();
    if (prompt.clickedButton() == save) return saveComposition();
    return prompt.clickedButton() == discard;
}

model::MotionProjectData MainWindow::projectData() const
{
    if (!document_) throw std::logic_error("There is no open Motion Studio composition");
    model::MotionProjectData snapshot;
    snapshot.composition = {document_->canvasSize(), document_->frameRate()};
    snapshot.layers = document_->layers();
    if (media_pool_ != nullptr) {
        snapshot.bins = media_pool_->library().bins();
        snapshot.media.reserve(media_pool_->library().items().size());
        for (const auto& item : media_pool_->library().items()) {
            snapshot.media.push_back({item.metadata.source_path,
                                      item.metadata.kind,
                                      item.display_name,
                                      item.bin_path});
        }
    }
    return snapshot;
}

bool MainWindow::documentIsDirty() const
{
    if (!document_) return false;
    if (!saved_data_.has_value()) return true;
    try {
        return projectData() != *saved_data_;
    } catch (...) {
        return true;
    }
}

void MainWindow::updateDocumentState()
{
    const bool has_document = document_.has_value();
    const bool dirty = documentIsDirty();
    if (import_media_action_ != nullptr) import_media_action_->setEnabled(has_document);
    if (new_text_layer_action_ != nullptr) new_text_layer_action_->setEnabled(has_document);
    if (new_rectangle_layer_action_ != nullptr)
        new_rectangle_layer_action_->setEnabled(has_document);
    if (new_ellipse_layer_action_ != nullptr)
        new_ellipse_layer_action_->setEnabled(has_document);
    if (save_composition_action_ != nullptr) save_composition_action_->setEnabled(has_document);
    if (save_composition_as_action_ != nullptr)
        save_composition_as_action_->setEnabled(has_document);
    updateHistoryActions();
    if (!has_document) {
        setWindowModified(false);
        setWindowTitle(QStringLiteral("Motion Studio"));
        return;
    }
    const QString document_name = document_path_.has_value()
        ? QFileInfo(pathForDisplay(*document_path_)).fileName()
        : QStringLiteral("Untitled");
    setWindowTitle(QStringLiteral("%1 — Motion Studio[*]").arg(document_name));
    setWindowModified(dirty);
}

CompositionEditState MainWindow::captureEditState() const
{
    if (!document_) throw std::logic_error("There is no open Motion Studio composition");
    return {*document_, selected_layer_id_};
}

bool MainWindow::recordCompositionEdit(CompositionEditState before)
{
    if (!document_) return false;
    const bool unchanged = before.document.canvasSize() == document_->canvasSize() &&
        before.document.frameRate() == document_->frameRate() &&
        before.document.layers() == document_->layers();
    if (unchanged) return false;
    composition_history_.recordBeforeEdit(std::move(before));
    updateHistoryActions();
    return true;
}

void MainWindow::finishPendingTransformEdit()
{
    if (!active_transform_edit_.has_value()) return;
    active_transform_edit_.reset();
    if (document_) {
        composition_history_.finishCoalescedEdit(captureEditState());
    }
    updateHistoryActions();
    finishPendingContentEdit();
}

void MainWindow::finishPendingContentEdit()
{
    if (!active_content_edit_layer_.has_value()) return;
    active_content_edit_layer_.reset();
    if (document_) {
        composition_history_.finishCoalescedEdit(captureEditState());
    }
    updateHistoryActions();
}

void MainWindow::updateHistoryActions()
{
    const bool has_document = document_.has_value();
    if (undo_action_ != nullptr)
        undo_action_->setEnabled(has_document && composition_history_.canUndo());
    if (redo_action_ != nullptr)
        redo_action_->setEnabled(has_document && composition_history_.canRedo());
}

void MainWindow::undoComposition()
{
    finishPendingTransformEdit();
    if (!document_) return;
    auto state = composition_history_.undo(captureEditState());
    if (!state.has_value()) {
        updateHistoryActions();
        return;
    }
    applyEditState(std::move(*state));
}

void MainWindow::redoComposition()
{
    finishPendingTransformEdit();
    if (!document_) return;
    auto state = composition_history_.redo(captureEditState());
    if (!state.has_value()) {
        updateHistoryActions();
        return;
    }
    applyEditState(std::move(*state));
}

void MainWindow::applyEditState(CompositionEditState state)
{
    if (!document_) return;
    *document_ = std::move(state.document);
    const auto selected = std::find_if(
        document_->layers().begin(), document_->layers().end(),
        [&state](const auto& layer) { return layer.id == state.selected_layer_id; });
    selected_layer_id_ = selected == document_->layers().end() ? 0 : state.selected_layer_id;
    refreshTimeline();
    syncTransformInspector();
    updateDocumentState();
    requestPreview();
}

void MainWindow::reportDocumentError(const char* operation,
                                     const std::filesystem::path& path,
                                     const std::exception& error,
                                     int error_code,
                                     int system_error)
{
    creative_suite::diagnostics::Context context{
        {"path", pathForLog(path)}, {"error_code", std::to_string(error_code)}};
    if (system_error >= 0)
        context.emplace_back("system_error", std::to_string(system_error));
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Error,
        "motion_document", operation, error.what(), context);
    QMessageBox::warning(
        this, QStringLiteral("Document Error"),
        QStringLiteral("The composition could not be %1. Check the Motion Studio log for details.")
            .arg(QString::fromUtf8(operation).contains(QStringLiteral("save"))
                     ? QStringLiteral("saved") : QStringLiteral("opened")));
}

void MainWindow::finishOpen(std::uint64_t generation,
                            std::filesystem::path path,
                            model::MotionProjectData project,
                            creative_suite::media::MediaImportBatchResult result,
                            bool recovered,
                            std::filesystem::path recovery_snapshot_path)
{
    if (generation != open_generation_) return;
    if (open_progress_ != nullptr) open_progress_->hide();
    open_cancel_requested_.reset();
    new_composition_action_->setEnabled(true);
    open_composition_action_->setEnabled(true);
    if (result.cancelled) return;

    try {
        auto staged_document = model::CompositionDocument(
            project.composition.canvas_size.width,
            project.composition.canvas_size.height,
            project.composition.frame_rate,
            project.layers);
        creative_suite::media::MediaLibrary staged_library;
        for (const auto& bin : project.bins) {
            if (bin == creative_suite::media::default_bin) continue;
            (void)staged_library.createBin(bin);
        }

        std::unordered_map<std::filesystem::path, std::size_t> imported_by_path;
        imported_by_path.reserve(result.files.size());
        for (std::size_t i = 0; i < result.files.size(); ++i) {
            imported_by_path.emplace(
                creative_suite::media::MediaLibrary::canonicalPath(result.files[i].path), i);
        }
        std::size_t offline_count = 0;
        auto& logger = creative_suite::diagnostics::Logger::instance();
        for (const auto& media : project.media) {
            const auto found = imported_by_path.find(
                creative_suite::media::MediaLibrary::canonicalPath(media.source_path));
            bool restored = false;
            if (found != imported_by_path.end()) {
                auto& file = result.files[found->second];
                if (file.status == creative_suite::media::MediaImportFileStatus::Imported &&
                    file.item && file.item->metadata.kind == media.kind) {
                    auto imported = std::move(*file.item);
                    const auto mutation = staged_library.addOnline(
                        std::move(imported.metadata), std::move(imported.first_frame),
                        media.display_name, media.bin_path);
                    restored = mutation == creative_suite::media::MediaMutationResult::Changed;
                    if (!restored) {
                        logger.log(creative_suite::diagnostics::Level::Warning,
                                   "motion_document", "restore_media",
                                   "The imported source could not be added to the staged Media Pool",
                                   {{"path", pathForLog(media.source_path)},
                                    {"media_mutation", std::to_string(static_cast<int>(mutation))}});
                    }
                } else if (file.status == creative_suite::media::MediaImportFileStatus::Failed) {
                    logger.log(creative_suite::diagnostics::Level::Warning,
                               "motion_document", "restore_media", file.cause,
                               {{"path", pathForLog(media.source_path)},
                                {"error_code", file.error_code.has_value()
                                    ? std::to_string(*file.error_code) : std::string{}}});
                } else if (file.status == creative_suite::media::MediaImportFileStatus::Imported) {
                    logger.log(creative_suite::diagnostics::Level::Warning,
                               "motion_document", "restore_media",
                               "The source media kind does not match the saved Media Pool entry",
                               {{"path", pathForLog(media.source_path)},
                                {"saved_kind", std::to_string(static_cast<int>(media.kind))},
                                {"imported_kind", file.item.has_value()
                                    ? std::to_string(static_cast<int>(file.item->metadata.kind))
                                    : std::string{}}});
                }
            }
            if (!restored) {
                (void)staged_library.addOffline(media.source_path, media.display_name,
                                                media.bin_path, media.kind);
                ++offline_count;
            }
        }

        if (!confirmReplaceDocument()) return;
        const auto current_unsaved_directory = recovery_store_.recoveryRoot() /
            "unsaved" / recovery_store_.sessionId();
        const bool recovered_from_current_session = recovered && path.empty() &&
            recovery_snapshot_path.parent_path() == current_unsaved_directory;
        const auto previous_recovered_snapshot = recovered_untitled_snapshot_path_;
        composition_history_.clear();
        active_transform_edit_.reset();
        document_.emplace(std::move(staged_document));
        document_path_ = path.empty()
            ? std::nullopt
            : std::optional<std::filesystem::path>(
                  creative_suite::media::MediaLibrary::canonicalPath(path));
        last_autosaved_data_.reset();
        selected_layer_id_ = 0;
        if (workspace_ == nullptr) createWorkspace();
        else if (preview_renderer_) preview_renderer_->resetSessions();
        media_pool_->replaceLibrary(std::move(staged_library));
        timeline_->setCompositionTiming(document_->frameRate());
        timeline_->setLayers(document_->layers());
        timeline_->setSelectedLayerId(0);
        viewer_->setComposition(document_->canvasSize(), std::nullopt);
        media_details_->setMedia(nullptr);
        inspector_tabs_->setCurrentWidget(media_details_);
        syncTransformInspector();
        if (recovered) saved_data_.reset();
        else saved_data_ = projectData();
        updateDocumentState();
        requestPreview();
        if (!recovered_from_current_session)
            cleanupCurrentUnsavedSnapshots("open_unsaved_recovery_cleanup");
        if (previous_recovered_snapshot.has_value() &&
            (!recovered || recovery_snapshot_path != *previous_recovered_snapshot)) {
            cleanupRecoveredUnsavedSnapshot("open_recovered_snapshot_cleanup");
        } else {
            recovered_untitled_snapshot_path_.reset();
        }
        if (recovered && !document_path_.has_value())
            recovered_untitled_snapshot_path_ = recovery_snapshot_path;
        if (recovered) {
            last_autosaved_data_ = projectData();
        }
        statusBar()->showMessage(offline_count == 0
            ? QStringLiteral("Composition opened.")
            : QStringLiteral("Composition opened; %1 media item(s) are offline.")
                  .arg(offline_count), 7000);
    } catch (const std::exception& error) {
        reportDocumentError("open_document", path, error);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmReplaceDocument()) {
        event->ignore();
        return;
    }
    if (autosave_timer_ != nullptr) autosave_timer_->stop();
    cleanupCurrentUnsavedSnapshots("close_unsaved_recovery_cleanup");
    cleanupRecoveredUnsavedSnapshot("close_recovered_snapshot_cleanup");
    event->accept();
}

void MainWindow::openShortcutSettings()
{
    ShortcutSettingsDialog dialog(shortcut_manager_.entries(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    QString error;
    if (!shortcut_manager_.applyShortcuts(dialog.assignments(), &error)) {
        const auto detail = error.toStdString();
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_settings", "save_shortcuts", detail,
            {{"settings_group", shortcut_manager_.settingsGroup().toStdString()}});
        QMessageBox::warning(
            this, QStringLiteral("Settings Error"),
            QStringLiteral("Keyboard shortcut preferences could not be saved."));
    }
}

void MainWindow::configureAutosaveTimer()
{
    if (autosave_timer_ == nullptr) return;
    autosave_timer_->setInterval(settings::autosaveIntervalSeconds() * 1000);
    if (settings::autosaveEnabled()) autosave_timer_->start();
    else autosave_timer_->stop();
}

void MainWindow::autosaveProject()
{
    if (!settings::autosaveEnabled() || !document_ || !documentIsDirty()) return;

    try {
        auto snapshot = projectData();
        if (last_autosaved_data_.has_value() &&
            *last_autosaved_data_ == snapshot) return;
        if (recovery_store_.containsSnapshotData(
                snapshot, document_path_.value_or(std::filesystem::path{}))) {
            last_autosaved_data_ = std::move(snapshot);
            return;
        }
        if (document_path_.has_value()) {
            recovery_store_.saveSnapshot(
                snapshot, *document_path_, settings::recoveryRetention());
        } else {
            recovery_store_.saveSnapshot(snapshot, settings::recoveryRetention());
        }
        last_autosaved_data_ = std::move(snapshot);
        if (recovered_untitled_snapshot_path_.has_value()) {
            cleanupRecoveredUnsavedSnapshot("autosave_recovered_snapshot_cleanup");
        }
        statusBar()->showMessage(QStringLiteral("Recovery snapshot saved."), 2500);
    } catch (const persistence::MotionDocumentError& error) {
        creative_suite::diagnostics::Context context{
            {"path", pathForLog(error.path())},
            {"error_code", std::to_string(static_cast<int>(error.code()))}};
        if (error.systemError().has_value())
            context.emplace_back("system_error", std::to_string(*error.systemError()));
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", "autosave", error.what(), context);
        statusBar()->showMessage(
            QStringLiteral("Autosave failed. Check the Motion Studio log."), 5000);
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", "autosave", error.what(),
            {{"document_path", document_path_.has_value()
                ? pathForLog(*document_path_) : std::string{}}});
        statusBar()->showMessage(
            QStringLiteral("Autosave failed. Check the Motion Studio log."), 5000);
    }
}

void MainWindow::cleanupCurrentUnsavedSnapshots(const char* operation) noexcept
{
    try {
        recovery_store_.removeCurrentUnsavedSnapshots();
    } catch (const persistence::MotionDocumentError& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"path", pathForLog(error.path())},
             {"error_code", std::to_string(static_cast<int>(error.code()))}});
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"session_id", recovery_store_.sessionId()}});
    }
}

void MainWindow::cleanupRecoveredUnsavedSnapshot(const char* operation) noexcept
{
    if (!recovered_untitled_snapshot_path_.has_value()) return;
    const auto snapshot_path = *recovered_untitled_snapshot_path_;
    try {
        const auto current_directory = recovery_store_.recoveryRoot() /
            "unsaved" / recovery_store_.sessionId();
        if (snapshot_path.parent_path() == current_directory)
            recovery_store_.removeSnapshot(snapshot_path);
        else
            recovery_store_.removeUnsavedSnapshotsForSession(snapshot_path);
        recovered_untitled_snapshot_path_.reset();
    } catch (const persistence::MotionDocumentError& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"path", pathForLog(error.path())},
             {"error_code", std::to_string(static_cast<int>(error.code()))}});
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", operation, error.what(),
            {{"snapshot_path", pathForLog(snapshot_path)}});
    }
}

std::optional<std::filesystem::path> MainWindow::chooseRecoverySnapshot(
    std::vector<persistence::MotionRecoverySnapshot> snapshots,
    const QString& project_label)
{
    if (snapshots.empty()) return std::nullopt;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("motion-recovery-choice-dialog"));
    dialog.setWindowTitle(QStringLiteral("Composition Recovery"));
    dialog.setModal(true);
    dialog.resize(620, 360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(
        QStringLiteral("Recovery snapshots were found for %1. Restore one or continue without recovery.")
            .arg(project_label), &dialog);
    description->setWordWrap(true);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("motion-recovery-choice-list"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    const auto append_snapshot = [list](const persistence::MotionRecoverySnapshot& snapshot) {
        const QFileInfo info(pathForDisplay(snapshot.path));
        const auto modified = info.lastModified().isValid()
            ? info.lastModified().toLocalTime().toString(Qt::TextDate)
            : QStringLiteral("Unknown time");
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(modified, info.fileName()), list);
        item->setData(Qt::UserRole, pathForDisplay(snapshot.path));
    };
    for (const auto& snapshot : snapshots) append_snapshot(snapshot);
    if (list->count() > 0) list->setCurrentRow(0);

    auto* buttons = new QHBoxLayout();
    auto* restore = new QPushButton(QStringLiteral("Restore"), &dialog);
    restore->setObjectName(QStringLiteral("motion-recovery-choice-restore"));
    auto* remove = new QPushButton(QStringLiteral("Delete"), &dialog);
    remove->setObjectName(QStringLiteral("motion-recovery-choice-delete"));
    auto* ignore = new QPushButton(QStringLiteral("Ignore"), &dialog);
    ignore->setObjectName(QStringLiteral("motion-recovery-choice-ignore"));
    buttons->addWidget(restore);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(ignore);
    layout->addWidget(description);
    layout->addWidget(list, 1);
    layout->addLayout(buttons);

    std::optional<std::filesystem::path> selected_path;
    const auto current_path = [list]() -> std::filesystem::path {
        const auto* item = list->currentItem();
        return item == nullptr ? std::filesystem::path{}
                               : pathFromQString(item->data(Qt::UserRole).toString());
    };
    connect(restore, &QPushButton::clicked, &dialog, [&] {
        const auto path = current_path();
        if (path.empty()) return;
        selected_path = path;
        dialog.accept();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        auto* item = list->currentItem();
        if (item == nullptr) return;
        if (QMessageBox::question(
                &dialog, QStringLiteral("Delete Recovery Snapshot"),
                QStringLiteral("Delete the selected recovery snapshot?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto path = current_path();
        try {
            recovery_store_.removeSnapshot(path);
            delete list->takeItem(list->row(item));
            if (list->count() > 0) list->setCurrentRow(0);
        } catch (const persistence::MotionDocumentError& error) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Warning,
                "motion_recovery", "delete_snapshot", error.what(),
                {{"path", pathForLog(path)},
                 {"error_code", std::to_string(static_cast<int>(error.code()))},
                 {"system_error", error.systemError().has_value()
                     ? std::to_string(*error.systemError()) : std::string{}}});
            statusBar()->showMessage(
                QStringLiteral("Recovery snapshot could not be deleted."), 5000);
        }
    });
    connect(ignore, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&] {
        const auto path = current_path();
        if (path.empty()) return;
        selected_path = path;
        dialog.accept();
    });
    connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, restore, remove] {
        const bool selected = list->currentItem() != nullptr;
        restore->setEnabled(selected);
        remove->setEnabled(selected);
    });
    restore->setEnabled(list->currentItem() != nullptr);
    remove->setEnabled(list->currentItem() != nullptr);
    static_cast<void>(dialog.exec());
    return selected_path;
}

void MainWindow::maybeOfferUnsavedRecovery()
{
    auto snapshots = recovery_store_.unsavedSnapshots();
    if (snapshots.empty()) return;
    const auto selected = chooseRecoverySnapshot(
        std::move(snapshots), QStringLiteral("an untitled composition"));
    if (selected.has_value()) restoreRecoverySnapshot(*selected);
}

void MainWindow::restoreRecoverySnapshot(
    const std::filesystem::path& snapshot_path)
{
    try {
        const auto recovery = persistence::MotionDocumentStore::loadRecovery(snapshot_path);
        stageOpenProject(recovery.target_document_path, recovery.document,
                         true, snapshot_path);
    } catch (const persistence::MotionDocumentError& error) {
        reportDocumentError("restore_recovery", snapshot_path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
    } catch (const std::exception& error) {
        reportDocumentError("restore_recovery", snapshot_path, error);
    }
}

void MainWindow::refreshAutosaveRecoveryDialog(
    AutosaveRecoveryDialog& dialog) const
{
    std::vector<persistence::MotionRecoverySnapshot> snapshots;
    if (document_path_.has_value()) {
        snapshots = recovery_store_.validSnapshotsForProject(*document_path_);
    }
    auto unsaved = recovery_store_.unsavedSnapshots();
    snapshots.insert(snapshots.end(),
                     std::make_move_iterator(unsaved.begin()),
                     std::make_move_iterator(unsaved.end()));
    std::sort(snapshots.begin(), snapshots.end(),
        [](const auto& left, const auto& right) {
            if (left.modified_time != right.modified_time)
                return left.modified_time > right.modified_time;
            return left.path > right.path;
        });

    std::vector<AutosaveSnapshotRow> rows;
    rows.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        const QFileInfo file_info(pathForDisplay(snapshot.path));
        const bool has_project = !snapshot.target_document_path.empty();
        const QFileInfo project_info(pathForDisplay(snapshot.target_document_path));
        const auto modified = file_info.lastModified().isValid()
            ? file_info.lastModified().toLocalTime().toString(Qt::TextDate)
            : QStringLiteral("Unknown time");
        rows.push_back({
            has_project ? project_info.fileName() : QStringLiteral("Untitled composition"),
            has_project ? QStringLiteral("Saved project") : QStringLiteral("Untitled project"),
            modified,
            file_info.fileName(),
            pathForDisplay(snapshot.path),
            has_project ? pathForDisplay(snapshot.target_document_path) : QString{},
            pathForDisplay(snapshot.path.parent_path())});
    }
    dialog.setSnapshots(rows);
}

void MainWindow::openAutosaveRecoverySettings()
{
    AutosaveRecoveryDialog dialog(settings::autosaveEnabled(),
                                  settings::autosaveIntervalSeconds(),
                                  settings::recoveryRetention(), this);
    refreshAutosaveRecoveryDialog(dialog);
    connect(&dialog, &AutosaveRecoveryDialog::autosaveSettingsChanged,
            this, [this](bool enabled, int interval, int retention) {
        settings::setAutosaveEnabled(enabled);
        settings::setAutosaveIntervalSeconds(interval);
        settings::setRecoveryRetention(retention);
        configureAutosaveTimer();
    });
    connect(&dialog, &AutosaveRecoveryDialog::refreshRequested,
            this, [this, &dialog] { refreshAutosaveRecoveryDialog(dialog); });
    connect(&dialog, &AutosaveRecoveryDialog::deleteSnapshotRequested,
            this, [this, &dialog](const QString& encoded_path) {
        if (QMessageBox::question(
                this, QStringLiteral("Delete Recovery Snapshot"),
                QStringLiteral("Delete the selected recovery snapshot?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto path = pathFromQString(encoded_path);
        try {
            recovery_store_.removeSnapshot(path);
            refreshAutosaveRecoveryDialog(dialog);
        } catch (const persistence::MotionDocumentError& error) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Warning,
                "motion_recovery", "delete_snapshot", error.what(),
                {{"path", pathForLog(path)},
                 {"error_code", std::to_string(static_cast<int>(error.code()))}});
            statusBar()->showMessage(
                QStringLiteral("Recovery snapshot could not be deleted."), 5000);
        }
    });
    connect(&dialog, &AutosaveRecoveryDialog::openFolderRequested,
            this, [this](const QString& encoded_folder) {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(encoded_folder))) return;
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Warning,
            "motion_recovery", "open_snapshot_folder",
            "The recovery snapshot folder could not be opened.",
            {{"path", pathForLog(pathFromQString(encoded_folder))}});
        statusBar()->showMessage(
            QStringLiteral("The recovery folder could not be opened."), 5000);
    });

    if (dialog.exec() == AutosaveRecoveryDialog::restore_snapshot_result) {
        const auto path = pathFromQString(dialog.selectedSnapshotPath());
        if (!path.empty()) restoreRecoverySnapshot(path);
    }
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
    media_pool_->setContentChangedHandler([this] { updateDocumentState(); });
    viewer_ = new CompositionViewer(workspace_);
    viewer_->setObjectName(QStringLiteral("motion-composition-viewer"));
    media_details_ = new MediaDetailsWidget(workspace_);

    inspector_tabs_ = new QTabWidget(workspace_);
    inspector_tabs_->setObjectName(QStringLiteral("motion-inspector-tabs"));
    inspector_tabs_->setMinimumWidth(250);
    inspector_tabs_->addTab(media_details_, QStringLiteral("Media"));

    layer_content_inspector_ = new QWidget(inspector_tabs_);
    layer_content_inspector_->setObjectName(QStringLiteral("motion-layer-content-inspector"));
    auto* content_layout = new QVBoxLayout(layer_content_inspector_);
    auto* content_title = new QLabel(QStringLiteral("Selected Layer Content"),
                                     layer_content_inspector_);
    content_title->setObjectName(QStringLiteral("motion-layer-content-title"));
    content_layout->addWidget(content_title);
    layer_content_pages_ = new QStackedWidget(layer_content_inspector_);
    layer_content_pages_->setObjectName(QStringLiteral("motion-layer-content-pages"));
    auto* empty_content_page = new QLabel(
        QStringLiteral("Select a text or shape layer to edit its content."),
        layer_content_pages_);
    empty_content_page->setObjectName(QStringLiteral("motion-layer-content-empty"));
    empty_content_page->setWordWrap(true);
    empty_content_page->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layer_content_pages_->addWidget(empty_content_page);

    text_content_page_ = new QWidget(layer_content_pages_);
    text_content_page_->setObjectName(QStringLiteral("motion-text-content-page"));
    auto* text_layout = new QVBoxLayout(text_content_page_);
    auto* text_form = new QFormLayout();
    text_content_field_ = new QTextEdit(text_content_page_);
    text_content_field_->setObjectName(QStringLiteral("motion-text-content"));
    text_content_field_->setAcceptRichText(false);
    text_content_field_->setMaximumHeight(112);
    text_form->addRow(QStringLiteral("Text"), text_content_field_);

    text_font_field_ = new QFontComboBox(text_content_page_);
    text_font_field_->setObjectName(QStringLiteral("motion-text-font-family"));
    text_form->addRow(QStringLiteral("Font"), text_font_field_);

    text_font_size_field_ = new QSpinBox(text_content_page_);
    text_font_size_field_->setObjectName(QStringLiteral("motion-text-font-size"));
    text_font_size_field_->setRange(1, 4096);
    text_font_size_field_->setSuffix(QStringLiteral(" px"));
    text_form->addRow(QStringLiteral("Size"), text_font_size_field_);

    text_color_button_ = new QPushButton(text_content_page_);
    text_color_button_->setObjectName(QStringLiteral("motion-text-color"));
    text_form->addRow(QStringLiteral("Color"), text_color_button_);

    text_alignment_field_ = new QComboBox(text_content_page_);
    text_alignment_field_->setObjectName(QStringLiteral("motion-text-alignment"));
    text_alignment_field_->addItem(QStringLiteral("Left"),
        static_cast<int>(model::TextAlignment::Left));
    text_alignment_field_->addItem(QStringLiteral("Center"),
        static_cast<int>(model::TextAlignment::Center));
    text_alignment_field_->addItem(QStringLiteral("Right"),
        static_cast<int>(model::TextAlignment::Right));
    text_form->addRow(QStringLiteral("Alignment"), text_alignment_field_);

    text_box_width_field_ = new QSpinBox(text_content_page_);
    text_box_width_field_->setObjectName(QStringLiteral("motion-text-box-width"));
    text_box_width_field_->setRange(1, 32768);
    text_box_width_field_->setSuffix(QStringLiteral(" px"));
    text_form->addRow(QStringLiteral("Box width"), text_box_width_field_);

    text_box_height_field_ = new QSpinBox(text_content_page_);
    text_box_height_field_->setObjectName(QStringLiteral("motion-text-box-height"));
    text_box_height_field_->setRange(1, 32768);
    text_box_height_field_->setSuffix(QStringLiteral(" px"));
    text_form->addRow(QStringLiteral("Box height"), text_box_height_field_);
    text_layout->addLayout(text_form);
    text_layout->addStretch(1);
    layer_content_pages_->addWidget(text_content_page_);

    shape_content_page_ = new QWidget(layer_content_pages_);
    shape_content_page_->setObjectName(QStringLiteral("motion-shape-content-page"));
    auto* shape_layout = new QVBoxLayout(shape_content_page_);
    auto* shape_form = new QFormLayout();
    shape_width_field_ = new QSpinBox(shape_content_page_);
    shape_width_field_->setObjectName(QStringLiteral("motion-shape-width"));
    shape_width_field_->setRange(1, 32768);
    shape_width_field_->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(QStringLiteral("Width"), shape_width_field_);
    shape_height_field_ = new QSpinBox(shape_content_page_);
    shape_height_field_->setObjectName(QStringLiteral("motion-shape-height"));
    shape_height_field_->setRange(1, 32768);
    shape_height_field_->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(QStringLiteral("Height"), shape_height_field_);
    shape_fill_button_ = new QPushButton(shape_content_page_);
    shape_fill_button_->setObjectName(QStringLiteral("motion-shape-fill-color"));
    shape_form->addRow(QStringLiteral("Fill"), shape_fill_button_);
    shape_stroke_button_ = new QPushButton(shape_content_page_);
    shape_stroke_button_->setObjectName(QStringLiteral("motion-shape-stroke-color"));
    shape_form->addRow(QStringLiteral("Stroke"), shape_stroke_button_);
    shape_stroke_width_field_ = new QSpinBox(shape_content_page_);
    shape_stroke_width_field_->setObjectName(QStringLiteral("motion-shape-stroke-width"));
    shape_stroke_width_field_->setRange(0, 4096);
    shape_stroke_width_field_->setSuffix(QStringLiteral(" px"));
    shape_form->addRow(QStringLiteral("Stroke width"), shape_stroke_width_field_);
    shape_layout->addLayout(shape_form);
    shape_layout->addStretch(1);
    layer_content_pages_->addWidget(shape_content_page_);
    content_layout->addWidget(layer_content_pages_, 1);
    layer_content_tab_index_ = inspector_tabs_->addTab(
        layer_content_inspector_, QStringLiteral("Layer"));
    inspector_tabs_->setTabEnabled(layer_content_tab_index_, false);

    text_content_field_->installEventFilter(this);
    text_font_field_->installEventFilter(this);
    text_alignment_field_->installEventFilter(this);
    for (auto* field : {text_font_size_field_, text_box_width_field_,
                        text_box_height_field_, shape_width_field_,
                        shape_height_field_, shape_stroke_width_field_}) {
        field->installEventFilter(this);
        connect(field, &QSpinBox::valueChanged, this, [this](int) {
            editSelectedLayerContent();
        });
        connect(field, &QSpinBox::editingFinished,
                this, [this] { finishPendingContentEdit(); });
    }
    connect(text_content_field_, &QTextEdit::textChanged,
            this, [this] { editSelectedLayerContent(); });
    connect(text_font_field_, &QFontComboBox::currentFontChanged,
            this, [this](const QFont&) { editSelectedLayerContent(); });
    connect(text_alignment_field_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { editSelectedLayerContent(); });
    connect(text_color_button_, &QPushButton::clicked, this, [this] {
        chooseSelectedLayerColor(true, false);
    });
    connect(shape_fill_button_, &QPushButton::clicked, this, [this] {
        chooseSelectedLayerColor(false, false);
    });
    connect(shape_stroke_button_, &QPushButton::clicked, this, [this] {
        chooseSelectedLayerColor(false, true);
    });

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
        auto* property_row = new QWidget(transform_inspector_);
        auto* property_layout = new QHBoxLayout(property_row);
        property_layout->setContentsMargins(0, 0, 0, 0);
        property_layout->setSpacing(4);
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
        property_layout->addWidget(field, 1);
        auto* key_button = new QToolButton(property_row);
        key_button->setObjectName(QStringLiteral("motion-transform-keyframe-%1")
            .arg(transform_rows[index].second.mid(QStringLiteral("motion-transform-").size())));
        key_button->setText(QStringLiteral("◇"));
        key_button->setToolTip(QStringLiteral("Add or remove a keyframe at the current frame"));
        transform_key_buttons_[index] = key_button;
        property_layout->addWidget(key_button);
        transform_form->addRow(transform_rows[index].first, property_row);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, index](double) {
            editSelectedLayerTransform(index);
        });
        connect(field, &QDoubleSpinBox::editingFinished,
                this, [this] { finishPendingTransformEdit(); });
        connect(key_button, &QToolButton::clicked, this, [this, index] {
            toggleSelectedLayerKeyframe(index);
        });
    }
    transform_layout->addLayout(transform_form);
    transform_layout->addStretch(1);
    inspector_tabs_->addTab(transform_inspector_, QStringLiteral("Transform"));

    timeline_ = new TimelineNavigator(composition_splitter_);
    timeline_->setShortcutActions(
        play_pause_action_, previous_frame_action_, next_frame_action_, loop_action_,
        zoom_in_action_, zoom_out_action_);
    timeline_->setMediaDropHandler([this](const std::filesystem::path& path,
                                          std::int64_t frame,
                                          model::LayerId before) {
        handleMediaDrop(path, frame, before);
    });
    timeline_->setLayerSelectedHandler([this](model::LayerId id) { selectLayer(id); });
    timeline_->setKeyframeSelectedHandler(
        [this](model::LayerId id, TransformProperty, std::int64_t local_frame) {
            if (!document_ || timeline_ == nullptr) return;
            const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
                [id](const auto& layer) { return layer.id == id; });
            if (found == document_->layers().end() || local_frame < 0 ||
                local_frame >= found->duration_frames) return;
            selectLayer(id);
            const auto composition_frame = found->timeline_start_frame + local_frame;
            timeline_->setCurrentFrame(composition_frame);
        });
    timeline_->setKeyframeMoveHandler(
        [this](model::LayerId id, TransformProperty property,
               std::int64_t from_local_frame, std::int64_t to_local_frame) {
            finishPendingTransformEdit();
            if (!document_) return false;
            auto before = captureEditState();
            if (!document_ || !document_->moveLayerKeyframe(
                    id, property, from_local_frame, to_local_frame)) {
                if (statusBar() != nullptr) {
                    statusBar()->showMessage(
                        QStringLiteral("A keyframe already exists at that frame."), 4000);
                }
                return false;
            }
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            syncTransformInspector();
            requestPreview();
            return true;
        });
    timeline_->setLayerMoveHandler([this](model::LayerId id, std::int64_t frame) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->moveLayerInTimeline(id, frame)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerResizeHandler([this](model::LayerId id, std::int64_t duration) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->resizeLayerDuration(id, duration)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerReorderHandler([this](model::LayerId id, std::size_t front_index) {
        finishPendingTransformEdit();
        if (!document_ || front_index >= document_->layers().size()) return;
        auto before = captureEditState();
        const auto model_index = document_->layers().size() - 1 - front_index;
        if (document_->moveLayer(id, model_index)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerVisibilityHandler([this](model::LayerId id, bool visible) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->setLayerVisible(id, visible)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            if (selected_layer_id_ == id) syncTransformInspector();
            requestPreview();
        }
    });
    timeline_->setLayerRemoveHandler([this](model::LayerId id) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (!document_->removeLayer(id)) return;
        (void)recordCompositionEdit(std::move(before));
        updateDocumentState();
        if (selected_layer_id_ == id) selected_layer_id_ = 0;
        refreshTimeline();
        syncTransformInspector();
        requestPreview();
    });
    connect(timeline_, &TimelineNavigator::currentFrameChanged,
            this, [this] {
                finishPendingTransformEdit();
                syncTransformInspector();
                requestPreview(timeline_ != nullptr && timeline_->isPlaying());
            });

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
        [this](std::uint64_t generation,
               PreviewRequestMode mode,
               std::uint64_t cancellation_generation,
               creative_suite::media::RgbaFramePtr frame) {
            const bool may_present = preview_renderer_ &&
                preview_renderer_->canPresentResult(
                    generation, mode, cancellation_generation) &&
                (mode != PreviewRequestMode::Playback ||
                 (timeline_ != nullptr && timeline_->isPlaying()));
            if (may_present && viewer_ != nullptr) {
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
    finishPendingTransformEdit();
    if (!document_) return;
    const auto& layers = document_->layers();
    const auto found = std::find_if(layers.begin(), layers.end(), [id](const auto& layer) {
        return layer.id == id;
    });
    if (found == layers.end()) return;
    selected_layer_id_ = id;
    timeline_->setSelectedLayerId(id);
    syncTransformInspector();
    if (found->kind == model::LayerKind::Text || found->kind == model::LayerKind::Shape) {
        inspector_tabs_->setCurrentWidget(layer_content_inspector_);
    } else {
        inspector_tabs_->setCurrentWidget(transform_inspector_);
    }
}

void MainWindow::createContentLayer(model::LayerKind kind, model::ShapeKind shape)
{
    finishPendingTransformEdit();
    if (!document_ || timeline_ == nullptr) return;

    auto before = captureEditState();
    const std::size_t same_kind_count = static_cast<std::size_t>(std::count_if(
        document_->layers().begin(), document_->layers().end(),
        [kind, shape](const model::CompositionLayer& layer) {
            if (layer.kind != kind) return false;
            if (kind != model::LayerKind::Shape) return true;
            const auto* content = std::get_if<model::ShapeLayerContent>(&layer.content);
            return content != nullptr && content->shape == shape;
        }));
    QString name;
    if (kind == model::LayerKind::Text) {
        name = QStringLiteral("Text %1").arg(same_kind_count + 1);
    } else {
        name = QStringLiteral("%1 %2")
            .arg(shape == model::ShapeKind::Ellipse
                    ? QStringLiteral("Ellipse") : QStringLiteral("Rectangle"))
            .arg(same_kind_count + 1);
    }

    model::LayerId added_id = 0;
    if (!document_->addContentLayer(kind, utf8String(name), timeline_->currentFrame(), &added_id)) {
        statusBar()->showMessage(
            QStringLiteral("The layer could not be added at the current frame."), 4000);
        return;
    }
    if (kind == model::LayerKind::Shape && shape == model::ShapeKind::Ellipse) {
        auto layer = std::find_if(
            document_->layers().begin(), document_->layers().end(),
            [added_id](const model::CompositionLayer& item) { return item.id == added_id; });
        if (layer != document_->layers().end()) {
            auto content = std::get<model::ShapeLayerContent>(layer->content);
            content.shape = model::ShapeKind::Ellipse;
            (void)document_->setShapeLayerContent(added_id, content);
        }
    }
    selected_layer_id_ = added_id;
    (void)recordCompositionEdit(std::move(before));
    refreshTimeline();
    syncTransformInspector();
    inspector_tabs_->setCurrentWidget(layer_content_inspector_);
    updateDocumentState();
    requestPreview();
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
    syncLayerContentInspector(selected);
    if (selected == nullptr) {
        viewer_->setSelectedLayerAnchor(std::nullopt);
        return;
    }
    const auto frame = timeline_ != nullptr ? timeline_->currentFrame() : 0;
    const auto raw_local_frame = frame - selected->timeline_start_frame;
    const auto local_frame = selected->duration_frames > 0
        ? std::clamp(raw_local_frame, std::int64_t{0}, selected->duration_frames - 1)
        : std::int64_t{0};
    const auto evaluated = creative_suite::animation::evaluateTransform(
        selected->transform, selected->keyframes, local_frame);
    const bool current_frame_in_layer = raw_local_frame >= 0 &&
        raw_local_frame < selected->duration_frames;
    for (std::size_t index = 0; index < transform_fields_.size(); ++index) {
        const auto property = kTransformProperties[index];
        const auto& frames = creative_suite::animation::keyframesFor(
            selected->keyframes, property);
        const bool current_key = current_frame_in_layer &&
            containsKeyframeAt(selected->keyframes, property, raw_local_frame);
        const QSignalBlocker blocker(transform_fields_[index]);
        transform_fields_[index]->setValue(transformPropertyValue(evaluated, property));
        transform_fields_[index]->setReadOnly(!frames.empty() && !current_key);
        transform_key_buttons_[index]->setEnabled(current_frame_in_layer);
        transform_key_buttons_[index]->setText(current_key
            ? QStringLiteral("◆") : QStringLiteral("◇"));
        transform_key_buttons_[index]->setToolTip(current_key
            ? QStringLiteral("Remove the keyframe at the current frame")
            : QStringLiteral("Add a keyframe at the current frame"));
    }
    viewer_->setSelectedLayerAnchor(selected->visible
        ? std::optional<QPointF>(QPointF(evaluated.position_x, evaluated.position_y))
        : std::nullopt);
}

void MainWindow::syncLayerContentInspector(const model::CompositionLayer* selected)
{
    if (layer_content_pages_ == nullptr || inspector_tabs_ == nullptr) return;
    const auto* text = selected != nullptr
        ? std::get_if<model::TextLayerContent>(&selected->content) : nullptr;
    const auto* shape = selected != nullptr
        ? std::get_if<model::ShapeLayerContent>(&selected->content) : nullptr;
    const bool supports_content = text != nullptr || shape != nullptr;
    inspector_tabs_->setTabEnabled(layer_content_tab_index_, supports_content);
    if (text != nullptr) {
        layer_content_pages_->setCurrentIndex(1);
        const QSignalBlocker text_blocker(text_content_field_);
        const QSignalBlocker font_blocker(text_font_field_);
        const QSignalBlocker size_blocker(text_font_size_field_);
        const QSignalBlocker alignment_blocker(text_alignment_field_);
        const QSignalBlocker width_blocker(text_box_width_field_);
        const QSignalBlocker height_blocker(text_box_height_field_);
        text_content_field_->setPlainText(qString(text->text));
        text_font_field_->setCurrentFont(QFont(qString(text->font_family)));
        text_font_size_field_->setValue(text->font_size_pixels);
        text_alignment_field_->setCurrentIndex(text_alignment_field_->findData(
            static_cast<int>(text->alignment)));
        text_box_width_field_->setValue(text->box_width);
        text_box_height_field_->setValue(text->box_height);
        setColorButton(text_color_button_, text->color);
        return;
    }
    if (shape != nullptr) {
        layer_content_pages_->setCurrentIndex(2);
        const QSignalBlocker width_blocker(shape_width_field_);
        const QSignalBlocker height_blocker(shape_height_field_);
        const QSignalBlocker stroke_width_blocker(shape_stroke_width_field_);
        shape_width_field_->setValue(shape->width);
        shape_height_field_->setValue(shape->height);
        shape_stroke_width_field_->setValue(shape->stroke_width_pixels);
        setColorButton(shape_fill_button_, shape->fill_color);
        setColorButton(shape_stroke_button_, shape->stroke_color);
        return;
    }
    layer_content_pages_->setCurrentIndex(0);
}

void MainWindow::editSelectedLayerContent()
{
    if (!document_ || selected_layer_id_ == 0) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;
    const bool is_text = found->kind == model::LayerKind::Text;
    const bool is_shape = found->kind == model::LayerKind::Shape;
    if (!is_text && !is_shape) return;

    const bool coalescing = active_content_edit_layer_ == selected_layer_id_;
    std::optional<CompositionEditState> before;
    if (!coalescing) {
        finishPendingTransformEdit();
        before.emplace(captureEditState());
    }

    bool changed = false;
    bool rejected = false;
    if (is_text) {
        auto content = std::get<model::TextLayerContent>(found->content);
        content.text = utf8String(text_content_field_->toPlainText());
        content.font_family = utf8String(text_font_field_->currentFont().family());
        content.font_size_pixels = text_font_size_field_->value();
        content.alignment = static_cast<model::TextAlignment>(
            text_alignment_field_->currentData().toInt());
        content.box_width = text_box_width_field_->value();
        content.box_height = text_box_height_field_->value();
        if (content != std::get<model::TextLayerContent>(found->content)) {
            changed = document_->setTextLayerContent(selected_layer_id_, content);
            rejected = !changed;
        }
    } else {
        auto content = std::get<model::ShapeLayerContent>(found->content);
        content.width = shape_width_field_->value();
        content.height = shape_height_field_->value();
        content.stroke_width_pixels = shape_stroke_width_field_->value();
        if (content != std::get<model::ShapeLayerContent>(found->content)) {
            changed = document_->setShapeLayerContent(selected_layer_id_, content);
            rejected = !changed;
        }
    }
    if (rejected) {
        syncLayerContentInspector(&*found);
        statusBar()->showMessage(
            QStringLiteral("The layer content value is outside the supported range."), 4000);
        return;
    }
    if (!changed) return;
    if (!coalescing) {
        composition_history_.beginCoalescedEdit(std::move(*before));
        active_content_edit_layer_ = selected_layer_id_;
        updateHistoryActions();
    }
    updateDocumentState();
    requestPreview();
}

void MainWindow::chooseSelectedLayerColor(bool text_color, bool stroke_color)
{
    finishPendingTransformEdit();
    if (!document_ || selected_layer_id_ == 0) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;

    model::ColorRgba old_color{};
    if (text_color && found->kind == model::LayerKind::Text) {
        old_color = std::get<model::TextLayerContent>(found->content).color;
    } else if (!text_color && found->kind == model::LayerKind::Shape) {
        const auto& shape = std::get<model::ShapeLayerContent>(found->content);
        old_color = stroke_color ? shape.stroke_color : shape.fill_color;
    } else {
        return;
    }
    const QColor chosen = QColorDialog::getColor(
        qColor(old_color), this,
        text_color ? QStringLiteral("Text Color")
            : stroke_color ? QStringLiteral("Stroke Color") : QStringLiteral("Fill Color"),
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;
    const auto new_color = modelColor(chosen);
    if (new_color == old_color) return;

    auto before = captureEditState();
    bool changed = false;
    if (text_color) {
        auto content = std::get<model::TextLayerContent>(found->content);
        content.color = new_color;
        changed = document_->setTextLayerContent(selected_layer_id_, content);
    } else {
        auto content = std::get<model::ShapeLayerContent>(found->content);
        if (stroke_color) content.stroke_color = new_color;
        else content.fill_color = new_color;
        changed = document_->setShapeLayerContent(selected_layer_id_, content);
    }
    if (!changed) return;
    (void)recordCompositionEdit(std::move(before));
    syncLayerContentInspector(found == document_->layers().end() ? nullptr : &*found);
    updateDocumentState();
    requestPreview();
}

void MainWindow::editSelectedLayerTransform(std::size_t property_index)
{
    if (!document_ || selected_layer_id_ == 0 || property_index >= transform_fields_.size()) return;
    const auto edit_identity = std::pair{selected_layer_id_, property_index};
    const bool coalescing = active_transform_edit_ == edit_identity;
    if (!coalescing) finishPendingTransformEdit();
    std::optional<CompositionEditState> before;
    if (!coalescing) before.emplace(captureEditState());
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;

    const auto property = kTransformProperties[property_index];
    const auto raw_local_frame = (timeline_ != nullptr ? timeline_->currentFrame() : 0) -
        found->timeline_start_frame;
    const auto& frames = creative_suite::animation::keyframesFor(found->keyframes, property);
    const double new_value = transform_fields_[property_index]->value();
    bool changed = false;
    if (frames.empty()) {
        auto transform = found->transform;
        setTransformPropertyValue(transform, property, new_value);
        if (transformPropertyValue(found->transform, property) != new_value)
            changed = document_->setLayerTransform(selected_layer_id_, transform);
    } else if (raw_local_frame >= 0 && raw_local_frame < found->duration_frames &&
               containsKeyframeAt(found->keyframes, property, raw_local_frame)) {
        const auto key = std::lower_bound(
            frames.begin(), frames.end(), raw_local_frame,
            [](const creative_suite::animation::Keyframe& item, std::int64_t frame) {
                return item.frame < frame;
            });
        if (key != frames.end() && key->frame == raw_local_frame && key->value != new_value)
            changed = document_->setLayerKeyframe(
                selected_layer_id_, property, raw_local_frame, new_value);
    }
    if (!changed) {
        syncTransformInspector();
        return;
    }
    if (!coalescing) {
        composition_history_.beginCoalescedEdit(std::move(*before));
        active_transform_edit_ = edit_identity;
        updateHistoryActions();
    }
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}

void MainWindow::toggleSelectedLayerKeyframe(std::size_t property_index)
{
    finishPendingTransformEdit();
    if (!document_ || selected_layer_id_ == 0 || property_index >= kTransformProperties.size() ||
        timeline_ == nullptr) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;
    const auto local_frame = timeline_->currentFrame() - found->timeline_start_frame;
    if (local_frame < 0 || local_frame >= found->duration_frames) return;
    auto before = captureEditState();

    const auto property = kTransformProperties[property_index];
    const bool has_key = containsKeyframeAt(found->keyframes, property, local_frame);
    bool changed = false;
    if (has_key) {
        changed = document_->removeLayerKeyframe(selected_layer_id_, property, local_frame);
    } else {
        const auto value = creative_suite::animation::evaluateProperty(
            found->transform, found->keyframes, property, local_frame);
        changed = document_->setLayerKeyframe(
            selected_layer_id_, property, local_frame, value);
        if (changed) {
            timeline_->setLayerExpanded(selected_layer_id_, true);
            timeline_->setTransformGroupExpanded(selected_layer_id_, true);
        }
    }
    if (!changed) return;
    (void)recordCompositionEdit(std::move(before));
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}

void MainWindow::handleMediaDrop(const std::filesystem::path& path,
                                std::int64_t start_frame,
                                model::LayerId before_layer_id)
{
    finishPendingTransformEdit();
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

    auto before = captureEditState();
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
    (void)recordCompositionEdit(std::move(before));
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    inspector_tabs_->setCurrentWidget(transform_inspector_);
    requestPreview();
}

void MainWindow::requestPreview(bool playback_tick)
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
        snapshot.id = layer.id;
        snapshot.kind = layer.kind;
        snapshot.source_path = layer.source_path;
        snapshot.local_frame = frame - layer.timeline_start_frame;
        snapshot.source_frame_count = layer.source_frame_count;
        snapshot.source_frame_rate = layer.source_frame_rate;
        snapshot.transform = layer.transform;
        snapshot.keyframes = layer.keyframes;
        snapshot.content = layer.content;
        if (layer.kind == model::LayerKind::Image) {
            snapshot.still_frame = media_pool_->sharedFirstFrameForPath(layer.source_path);
        }
        request.layers.push_back(std::move(snapshot));
    }
    (void)preview_renderer_->submit(
        std::move(request),
        playback_tick ? PreviewRequestMode::Playback : PreviewRequestMode::Interactive);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::FocusOut &&
        (watched == text_content_field_ || watched == text_font_field_ ||
         watched == text_alignment_field_ || watched == text_font_size_field_ ||
         watched == text_box_width_field_ || watched == text_box_height_field_ ||
         watched == shape_width_field_ || watched == shape_height_field_ ||
         watched == shape_stroke_width_field_)) {
        finishPendingContentEdit();
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace motion::ui
