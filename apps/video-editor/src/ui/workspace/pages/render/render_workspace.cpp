#include "ui/workspace/pages/render/render_workspace.h"

#include "ui/workspace/pages/render/render_job.h"
#include "ui/workspace/pages/render/render_queue_controller.h"
#include "ui/workspace/pages/render/render_queue_model.h"
#include "logging/logger.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace ui {
namespace {

constexpr int kCompactRenderLayoutWidth = 1100;

void configureCompactCombo(QComboBox* combo) {
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(14);
    combo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    QObject::connect(
        combo,
        &QComboBox::currentTextChanged,
        combo,
        [combo](const QString& text) { combo->setToolTip(text); });
}

QString extensionFilter(const RenderContainerOption& container) {
    QStringList patterns;
    const auto extensions = QString::fromStdString(container.extensions).split(
        ',', Qt::SkipEmptyParts);
    for (auto extension : extensions) {
        extension = extension.trimmed();
        if (!extension.isEmpty()) patterns.push_back(QStringLiteral("*.") + extension);
    }
    if (patterns.isEmpty()) return QStringLiteral("All files (*)");
    return QStringLiteral("%1 (%2)")
        .arg(QString::fromStdString(container.display_name), patterns.join(' '));
}

QString containerLabel(const RenderContainerOption& container) {
    auto label = QString::fromStdString(container.display_name);
    if (container.extensions.empty()) return label;

    QStringList extensions;
    for (auto extension : QString::fromStdString(container.extensions).split(
             ',', Qt::SkipEmptyParts)) {
        extension = extension.trimmed();
        if (!extension.isEmpty()) extensions.push_back(QStringLiteral(".") + extension);
    }
    if (!extensions.isEmpty()) {
        label += QStringLiteral(" (%1)").arg(extensions.join(QStringLiteral(", ")));
    }
    return label;
}

QString normalizedOutputPath(const QString& path) {
    const QFileInfo info(path);
    auto normalized = info.canonicalFilePath();
    if (normalized.isEmpty()) {
        auto parent = QFileInfo(info.absolutePath()).canonicalFilePath();
        if (parent.isEmpty()) parent = info.absolutePath();
        normalized = QDir::cleanPath(QDir(parent).filePath(info.fileName()));
    }
#ifdef _WIN32
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

}  // namespace

RenderWorkspace::RenderWorkspace(
    TimelineReadOnlyHandler timeline_read_only_handler,
    ProjectSnapshotProvider project_snapshot_provider,
    FrameRateProvider frame_rate_provider,
    QObject* parent)
    : QObject(parent),
      timeline_read_only_handler_(std::move(timeline_read_only_handler)),
      project_snapshot_provider_(std::move(project_snapshot_provider)),
      frame_rate_provider_(std::move(frame_rate_provider)) {}

void RenderWorkspace::createPanels(QWidget* parent) {
    if (central_page_ != nullptr || parent == nullptr) return;

    project::ProjectDocument initial_project;
    if (project_snapshot_provider_) {
        initial_project = project_snapshot_provider_();
    }
    project_width_ = std::max(1, initial_project.canvas_width);
    project_height_ = std::max(1, initial_project.canvas_height);

    central_page_ = new QWidget(parent);
    central_page_->setObjectName("renderWorkspacePage");
    auto* page_layout = new QVBoxLayout(central_page_);
    page_layout->setContentsMargins(8, 8, 8, 8);

    page_scroll_area_ = new QScrollArea(central_page_);
    page_scroll_area_->setObjectName("renderWorkspaceScrollArea");
    page_scroll_area_->setWidgetResizable(true);
    page_scroll_area_->setFrameShape(QFrame::NoFrame);
    page_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    page_scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    page_layout->addWidget(page_scroll_area_);

    splitter_ = new QSplitter(Qt::Horizontal, page_scroll_area_);
    splitter_->setObjectName("renderWorkspaceSplitter");
    splitter_->setChildrenCollapsible(false);
    splitter_->setMinimumWidth(0);
    page_scroll_area_->setWidget(splitter_);
    central_page_->installEventFilter(this);

    queue_model_ = new RenderQueueModel(this);
    queue_controller_ = new RenderQueueController(this);
    connect(queue_controller_, &RenderQueueController::jobStarted,
            this, [this](qulonglong id) {
                static_cast<void>(queue_model_->setJobStatus(
                    static_cast<std::uint64_t>(id), RenderJobStatus::Rendering, 0));
            });
    connect(queue_controller_, &RenderQueueController::jobProgress,
            this, [this](qulonglong id, int progress) {
                static_cast<void>(queue_model_->setJobStatus(
                    static_cast<std::uint64_t>(id), RenderJobStatus::Rendering, progress));
            });
    connect(queue_controller_, &RenderQueueController::jobCompleted,
            this, [this](qulonglong id) {
                static_cast<void>(queue_model_->setJobStatus(
                    static_cast<std::uint64_t>(id), RenderJobStatus::Completed, 100));
                updateQueueActions();
            });
    connect(queue_controller_, &RenderQueueController::jobFailed,
            this, [this](qulonglong id, const QString& message, const QString& error_code) {
                const auto id_value = static_cast<std::uint64_t>(id);
                static_cast<void>(queue_model_->setJobStatus(
                    id_value, RenderJobStatus::Failed, 0, message));
                for (int row = 0; row < queue_model_->jobCount(); ++row) {
                    const auto* failed = queue_model_->jobAt(row);
                    if (failed == nullptr || failed->id != id_value) continue;
                    failed_job_names_.push_back(failed->display_name);
                    std::string media_paths;
                    for (const auto& track : failed->project_snapshot.timeline_tracks) {
                        for (const auto& clip : track.clips) {
                            if (clip.kind == timeline::ClipKind::Text || clip.source_path.empty()) continue;
                            if (!media_paths.empty()) media_paths += "; ";
                            const auto path = clip.source_path.u8string();
                            media_paths.append(
                                reinterpret_cast<const char*>(path.data()), path.size());
                        }
                    }
                    logging::Context context{
                        {"job_id", std::to_string(id_value)},
                        {"output_path", failed->settings.output_path.toUtf8().constData()},
                        {"media_paths", media_paths},
                        {"container", failed->settings.container_name.toUtf8().constData()},
                        {"video_encoder", failed->settings.video_encoder_name.toUtf8().constData()},
                        {"audio_encoder", failed->settings.export_audio
                             ? failed->settings.audio_encoder_name.toUtf8().constData()
                             : "disabled"}};
                    if (!error_code.isEmpty()) {
                        context.emplace_back("error_code", error_code.toStdString());
                    }
                    logging::Logger::instance().log(
                        logging::Level::Error,
                        "render",
                        "export_job",
                        message.toUtf8().constData(),
                        context);
                    break;
                }
                updateQueueActions();
            });
    connect(queue_controller_, &RenderQueueController::jobCanceled,
            this, [this](qulonglong id) {
                static_cast<void>(queue_model_->setJobStatus(
                    static_cast<std::uint64_t>(id), RenderJobStatus::Canceled, 0));
                updateQueueActions();
            });
    connect(queue_controller_, &RenderQueueController::queueFinished,
            this, [this](bool canceled) { handleQueueFinished(canceled); });
    createSettingsPanel();
    createPreviewPanel();
    createQueuePanel();
    splitter_->addWidget(settings_panel_);
    splitter_->addWidget(preview_panel_);
    splitter_->addWidget(queue_panel_);
    splitter_->setStretchFactor(0, 5);
    splitter_->setStretchFactor(1, 9);
    splitter_->setStretchFactor(2, 6);
    splitter_->setSizes({480, 860, 570});
    settings_panel_->setMinimumWidth(0);
    queue_panel_->setMinimumWidth(0);

    populateContainerOptions();
    updateResolutionFields();
    updateDefaultFrameRate();
    updateQualitySuggestions();
    updateAddAction();
}

bool RenderWorkspace::eventFilter(QObject* watched, QEvent* event) {
    if (watched == central_page_ && event->type() == QEvent::Resize) {
        updateResponsiveLayout();
    }

    if (event->type() == QEvent::Wheel && settings_scroll_area_ != nullptr) {
        const std::array<QWidget*, 5> combo_controls{
            container_combo_, video_encoder_combo_, audio_encoder_combo_,
            resolution_combo_, quality_preset_combo_};
        bool is_settings_control = std::find(
            combo_controls.begin(), combo_controls.end(), watched) !=
            combo_controls.end();

        const std::array<QAbstractSpinBox*, 5> spin_controls{
            custom_width_, custom_height_, frame_rate_spin_,
            video_bitrate_spin_, audio_bitrate_spin_};
        auto* watched_widget = qobject_cast<QWidget*>(watched);
        for (auto* spin_control : spin_controls) {
            if (spin_control != nullptr &&
                (watched == spin_control ||
                 (watched_widget != nullptr &&
                  spin_control->isAncestorOf(watched_widget)))) {
                is_settings_control = true;
                break;
            }
        }

        if (is_settings_control) {
            const auto* wheel_event = static_cast<const QWheelEvent*>(event);
            auto* viewport = settings_scroll_area_->viewport();
            const QPointF viewport_position = viewport->mapFromGlobal(
                wheel_event->globalPosition().toPoint());
            QWheelEvent forwarded_event(
                viewport_position,
                wheel_event->globalPosition(),
                wheel_event->pixelDelta(),
                wheel_event->angleDelta(),
                wheel_event->buttons(),
                wheel_event->modifiers(),
                wheel_event->phase(),
                wheel_event->inverted(),
                wheel_event->source());
            QCoreApplication::sendEvent(viewport, &forwarded_event);
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

void RenderWorkspace::updateResponsiveLayout() {
    if (central_page_ == nullptr || splitter_ == nullptr) return;

    const bool use_compact_layout =
        central_page_->width() < kCompactRenderLayoutWidth;
    if (use_compact_layout == compact_layout_) return;

    const auto current_sizes = splitter_->sizes();
    if (current_sizes.size() == 3 &&
        (current_sizes[0] > 0 || current_sizes[1] > 0 || current_sizes[2] > 0)) {
        auto& saved_sizes = compact_layout_
            ? vertical_splitter_sizes_
            : horizontal_splitter_sizes_;
        for (std::size_t index = 0; index < saved_sizes.size(); ++index) {
            saved_sizes[index] = current_sizes[static_cast<qsizetype>(index)];
        }
    }

    compact_layout_ = use_compact_layout;
    splitter_->setOrientation(
        compact_layout_ ? Qt::Vertical : Qt::Horizontal);
    const auto& restored_sizes = compact_layout_
        ? vertical_splitter_sizes_
        : horizontal_splitter_sizes_;
    splitter_->setSizes({
        restored_sizes[0], restored_sizes[1], restored_sizes[2]});
}

void RenderWorkspace::createSettingsPanel() {
    settings_panel_ = new QWidget(splitter_);
    settings_panel_->setObjectName("renderSettingsPanel");
    auto* panel_layout = new QVBoxLayout(settings_panel_);
    panel_layout->setContentsMargins(0, 0, 8, 0);

    auto* heading = new QLabel(QStringLiteral("Render Settings"), settings_panel_);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setPointSize(heading_font.pointSize() + 1);
    heading->setFont(heading_font);
    panel_layout->addWidget(heading);

    settings_scroll_area_ = new QScrollArea(settings_panel_);
    settings_scroll_area_->setObjectName("renderSettingsScrollArea");
    settings_scroll_area_->setWidgetResizable(true);
    settings_scroll_area_->setFrameShape(QFrame::NoFrame);
    settings_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settings_scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    panel_layout->addWidget(settings_scroll_area_, 1);

    auto* content = new QWidget(settings_scroll_area_);
    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 8, 0);

    auto* output_group = new QGroupBox(QStringLiteral("Output"), content);
    output_group->setObjectName("renderOutputSettingsGroup");
    output_group->setMinimumWidth(0);
    output_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* output_form = new QFormLayout(output_group);
    output_form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    auto* output_path_row = new QWidget(output_group);
    output_path_row->setMinimumWidth(0);
    output_path_row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* output_path_layout = new QHBoxLayout(output_path_row);
    output_path_layout->setContentsMargins(0, 0, 0, 0);
    output_path_ = new QLineEdit(output_path_row);
    output_path_->setObjectName("renderOutputPath");
    output_path_->setMinimumWidth(0);
    output_path_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    output_path_->setPlaceholderText(QStringLiteral("Choose an output file"));
    output_path_->setAccessibleName(QStringLiteral("Render output file"));
    auto* browse_button = new QPushButton(QStringLiteral("Browse…"), output_path_row);
    browse_button->setObjectName("renderBrowseOutputButton");
    browse_button->setAccessibleName(QStringLiteral("Choose render output file"));
    output_path_layout->addWidget(output_path_, 1);
    output_path_layout->addWidget(browse_button);
    output_form->addRow(QStringLiteral("File"), output_path_row);

    container_combo_ = new QComboBox(output_group);
    container_combo_->setObjectName("renderContainerCombo");
    container_combo_->setAccessibleName(QStringLiteral("Output container"));
    configureCompactCombo(container_combo_);
    output_form->addRow(QStringLiteral("Container"), container_combo_);
    capability_warning_ = new QLabel(output_group);
    capability_warning_->setObjectName("renderCapabilitiesWarning");
    capability_warning_->setWordWrap(true);
    capability_warning_->setText(QStringLiteral(
        "This FFmpeg build does not expose a compatible video output format."));
    capability_warning_->hide();
    output_form->addRow(capability_warning_);
    content_layout->addWidget(output_group);

    auto* video_group = new QGroupBox(QStringLiteral("Video"), content);
    video_group->setObjectName("renderVideoSettingsGroup");
    video_group->setMinimumWidth(0);
    video_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* video_form = new QFormLayout(video_group);
    video_form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    video_encoder_combo_ = new QComboBox(video_group);
    video_encoder_combo_->setObjectName("renderVideoEncoderCombo");
    video_encoder_combo_->setAccessibleName(QStringLiteral("Video encoder"));
    configureCompactCombo(video_encoder_combo_);
    video_form->addRow(QStringLiteral("Encoder"), video_encoder_combo_);

    resolution_combo_ = new QComboBox(video_group);
    resolution_combo_->setObjectName("renderResolutionCombo");
    resolution_combo_->setAccessibleName(QStringLiteral("Output resolution"));
    configureCompactCombo(resolution_combo_);
    resolution_combo_->addItem(
        QStringLiteral("Project (%1 × %2)").arg(project_width_).arg(project_height_),
        QSize(project_width_, project_height_));
    const std::vector<QSize> common_resolutions{
        QSize(1280, 720), QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160)};
    for (const auto& resolution : common_resolutions) {
        if (resolution.width() == project_width_ &&
            resolution.height() == project_height_) {
            continue;
        }
        resolution_combo_->addItem(
            QStringLiteral("%1 × %2").arg(resolution.width()).arg(resolution.height()),
            resolution);
    }
    resolution_combo_->addItem(QStringLiteral("Custom dimensions"), QVariant{});
    video_form->addRow(QStringLiteral("Resolution"), resolution_combo_);

    custom_width_ = new QSpinBox(video_group);
    custom_width_->setObjectName("renderCustomWidth");
    custom_width_->setRange(1, 16384);
    custom_width_->setValue(project_width_);
    custom_width_->setSuffix(QStringLiteral(" px"));
    video_form->addRow(QStringLiteral("Custom width"), custom_width_);
    custom_height_ = new QSpinBox(video_group);
    custom_height_->setObjectName("renderCustomHeight");
    custom_height_->setRange(1, 16384);
    custom_height_->setValue(project_height_);
    custom_height_->setSuffix(QStringLiteral(" px"));
    video_form->addRow(QStringLiteral("Custom height"), custom_height_);

    frame_rate_spin_ = new QDoubleSpinBox(video_group);
    frame_rate_spin_->setObjectName("renderFrameRate");
    frame_rate_spin_->setAccessibleName(QStringLiteral("Output frame rate"));
    frame_rate_spin_->setRange(0.001, 1000.0);
    frame_rate_spin_->setDecimals(3);
    frame_rate_spin_->setSingleStep(0.001);
    frame_rate_spin_->setSuffix(QStringLiteral(" fps"));
    video_form->addRow(QStringLiteral("Frame rate"), frame_rate_spin_);

    quality_preset_combo_ = new QComboBox(video_group);
    quality_preset_combo_->setObjectName("renderQualityPresetCombo");
    configureCompactCombo(quality_preset_combo_);
    quality_preset_combo_->addItem(QStringLiteral("Low"),
                                   static_cast<int>(RenderQualityPreset::Low));
    quality_preset_combo_->addItem(QStringLiteral("Standard"),
                                   static_cast<int>(RenderQualityPreset::Standard));
    quality_preset_combo_->addItem(QStringLiteral("High"),
                                   static_cast<int>(RenderQualityPreset::High));
    quality_preset_combo_->addItem(QStringLiteral("Custom"),
                                   static_cast<int>(RenderQualityPreset::Custom));
    quality_preset_combo_->setCurrentIndex(1);
    video_form->addRow(QStringLiteral("Quality profile"), quality_preset_combo_);

    video_bitrate_spin_ = new QDoubleSpinBox(video_group);
    video_bitrate_spin_->setObjectName("renderVideoBitrate");
    video_bitrate_spin_->setRange(0.1, 500.0);
    video_bitrate_spin_->setDecimals(1);
    video_bitrate_spin_->setSingleStep(0.5);
    video_bitrate_spin_->setSuffix(QStringLiteral(" Mbps"));
    video_form->addRow(QStringLiteral("Video bitrate"), video_bitrate_spin_);
    content_layout->addWidget(video_group);

    auto* audio_group = new QGroupBox(QStringLiteral("Audio"), content);
    audio_group->setObjectName("renderAudioSettingsGroup");
    audio_group->setMinimumWidth(0);
    audio_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* audio_layout = new QVBoxLayout(audio_group);
    export_audio_check_ = new QCheckBox(QStringLiteral("Export audio"), audio_group);
    export_audio_check_->setObjectName("renderExportAudioCheck");
    audio_layout->addWidget(export_audio_check_);
    auto* audio_form_widget = new QWidget(audio_group);
    auto* audio_form = new QFormLayout(audio_form_widget);
    audio_form->setContentsMargins(0, 0, 0, 0);
    audio_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    audio_encoder_combo_ = new QComboBox(audio_form_widget);
    audio_encoder_combo_->setObjectName("renderAudioEncoderCombo");
    audio_encoder_combo_->setAccessibleName(QStringLiteral("Audio encoder"));
    configureCompactCombo(audio_encoder_combo_);
    audio_form->addRow(QStringLiteral("Encoder"), audio_encoder_combo_);
    audio_bitrate_spin_ = new QSpinBox(audio_form_widget);
    audio_bitrate_spin_->setObjectName("renderAudioBitrate");
    audio_bitrate_spin_->setRange(8, 1024);
    audio_bitrate_spin_->setSingleStep(16);
    audio_bitrate_spin_->setSuffix(QStringLiteral(" kbps"));
    audio_form->addRow(QStringLiteral("Audio bitrate"), audio_bitrate_spin_);
    audio_layout->addWidget(audio_form_widget);
    content_layout->addWidget(audio_group);
    content_layout->addStretch(1);

    settings_scroll_area_->setWidget(content);

    add_job_button_ = new QPushButton(QStringLiteral("Add to Queue"), settings_panel_);
    add_job_button_->setObjectName("renderAddToQueueButton");
    add_job_button_->setAccessibleName(QStringLiteral("Add render job to queue"));
    panel_layout->addWidget(add_job_button_);

    const std::array<QWidget*, 5> combo_controls{
        container_combo_, video_encoder_combo_, audio_encoder_combo_,
        resolution_combo_, quality_preset_combo_};
    for (auto* control : combo_controls) {
        control->installEventFilter(this);
    }
    const std::array<QAbstractSpinBox*, 5> spin_controls{
        custom_width_, custom_height_, frame_rate_spin_,
        video_bitrate_spin_, audio_bitrate_spin_};
    for (auto* control : spin_controls) {
        control->installEventFilter(this);
        for (auto* child : control->findChildren<QWidget*>()) {
            child->installEventFilter(this);
        }
    }

    connect(browse_button, &QPushButton::clicked,
            this, [this] { browseOutputPath(); });
    connect(add_job_button_, &QPushButton::clicked,
            this, [this] { addCurrentJob(); });
    connect(output_path_, &QLineEdit::textChanged,
            this, [this] { updateAddAction(); });
    connect(container_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
                updateEncoderOptions();
                updateOutputPathExtension();
                updateAddAction();
            });
    connect(video_encoder_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { updateAddAction(); });
    connect(audio_encoder_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { updateAddAction(); });
    connect(export_audio_check_, &QCheckBox::toggled,
            this, [this](bool enabled) {
                audio_encoder_combo_->setEnabled(enabled);
                audio_bitrate_spin_->setEnabled(enabled);
                updateAddAction();
            });
    connect(resolution_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
                updateResolutionFields();
                updateQualitySuggestions();
                updateAddAction();
            });
    connect(custom_width_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] {
                updateQualitySuggestions();
                updateAddAction();
            });
    connect(custom_height_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] {
                updateQualitySuggestions();
                updateAddAction();
            });
    connect(frame_rate_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] {
                frame_rate_user_modified_ = true;
                updateQualitySuggestions();
                updateAddAction();
            });
    connect(quality_preset_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index >= 0 && index < 3) updateQualitySuggestions();
                updateAddAction();
            });
    connect(video_bitrate_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] {
                if (!applying_quality_suggestion_) {
                    const QSignalBlocker blocker(quality_preset_combo_);
                    quality_preset_combo_->setCurrentIndex(3);
                }
                updateAddAction();
            });
    connect(audio_bitrate_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] {
                if (!applying_quality_suggestion_) {
                    const QSignalBlocker blocker(quality_preset_combo_);
                    quality_preset_combo_->setCurrentIndex(3);
                }
                updateAddAction();
            });
}

void RenderWorkspace::createQueuePanel() {
    queue_panel_ = new QWidget(splitter_);
    queue_panel_->setObjectName("renderQueuePanel");
    auto* layout = new QVBoxLayout(queue_panel_);
    layout->setContentsMargins(8, 0, 0, 0);

    auto* heading = new QLabel(QStringLiteral("Render Queue"), queue_panel_);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setPointSize(heading_font.pointSize() + 1);
    heading->setFont(heading_font);
    layout->addWidget(heading);

    empty_queue_label_ = new QLabel(
        QStringLiteral("No jobs in the queue. Configure an output in Render Settings and add it to the queue."),
        queue_panel_);
    empty_queue_label_->setObjectName("renderQueueEmptyLabel");
    empty_queue_label_->setWordWrap(true);
    layout->addWidget(empty_queue_label_);

    queue_view_ = new QListView(queue_panel_);
    queue_view_->setObjectName("renderQueueList");
    queue_view_->setAccessibleName(QStringLiteral("Prepared render jobs"));
    queue_view_->setModel(queue_model_);
    queue_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    queue_view_->setUniformItemSizes(false);
    layout->addWidget(queue_view_, 1);

    auto* run_actions = new QWidget(queue_panel_);
    auto* run_layout = new QHBoxLayout(run_actions);
    run_layout->setContentsMargins(0, 0, 0, 0);
    start_queue_button_ = new QPushButton(QStringLiteral("Start Queue"), run_actions);
    start_queue_button_->setObjectName("renderStartQueueButton");
    start_queue_button_->setAccessibleName(QStringLiteral("Start render queue"));
    cancel_queue_button_ = new QPushButton(QStringLiteral("Cancel"), run_actions);
    cancel_queue_button_->setObjectName("renderCancelQueueButton");
    cancel_queue_button_->setAccessibleName(QStringLiteral("Cancel current render queue"));
    run_layout->addWidget(start_queue_button_);
    run_layout->addWidget(cancel_queue_button_);
    run_layout->addStretch(1);
    layout->addWidget(run_actions);

    auto* queue_actions = new QWidget(queue_panel_);
    auto* action_layout = new QHBoxLayout(queue_actions);
    action_layout->setContentsMargins(0, 0, 0, 0);
    remove_job_button_ = new QPushButton(QStringLiteral("Remove"), queue_actions);
    remove_job_button_->setObjectName("renderRemoveQueueJobButton");
    move_job_up_button_ = new QPushButton(QStringLiteral("Move Up"), queue_actions);
    move_job_up_button_->setObjectName("renderMoveQueueJobUpButton");
    move_job_down_button_ = new QPushButton(QStringLiteral("Move Down"), queue_actions);
    move_job_down_button_->setObjectName("renderMoveQueueJobDownButton");
    action_layout->addWidget(remove_job_button_);
    action_layout->addStretch(1);
    action_layout->addWidget(move_job_up_button_);
    action_layout->addWidget(move_job_down_button_);
    layout->addWidget(queue_actions);

    connect(remove_job_button_, &QPushButton::clicked, this, [this] {
        const int row = queue_view_->currentIndex().row();
        if (queue_model_->removeJobAt(row)) {
            queue_view_->clearSelection();
            updateQueueActions();
        }
    });
    connect(start_queue_button_, &QPushButton::clicked,
            this, [this] { startQueue(); });
    connect(cancel_queue_button_, &QPushButton::clicked,
            this, [this] {
                if (queue_controller_ != nullptr) {
                    queue_controller_->cancel();
                    cancel_queue_button_->setEnabled(false);
                }
            });
    connect(move_job_up_button_, &QPushButton::clicked, this, [this] {
        const int row = queue_view_->currentIndex().row();
        if (queue_model_->moveJob(row, row - 1)) {
            queue_view_->setCurrentIndex(queue_model_->index(row - 1, 0));
        }
    });
    connect(move_job_down_button_, &QPushButton::clicked, this, [this] {
        const int row = queue_view_->currentIndex().row();
        if (queue_model_->moveJob(row, row + 1)) {
            queue_view_->setCurrentIndex(queue_model_->index(row + 1, 0));
        }
    });
    connect(queue_view_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this] { updateQueueActions(); });
    connect(queue_model_, &QAbstractItemModel::rowsInserted,
            this, [this] {
                empty_queue_label_->setVisible(queue_model_->jobCount() == 0);
                updateQueueActions();
            });
    connect(queue_model_, &QAbstractItemModel::rowsRemoved,
            this, [this] {
                empty_queue_label_->setVisible(queue_model_->jobCount() == 0);
                updateQueueActions();
            });
    connect(queue_model_, &QAbstractItemModel::rowsMoved,
            this, [this] { updateQueueActions(); });
    updateQueueActions();
}

void RenderWorkspace::createPreviewPanel() {
    preview_panel_ = new QWidget(splitter_);
    preview_panel_->setObjectName("renderPreviewPanel");
    preview_panel_->setMinimumWidth(320);
    auto* panel_layout = new QVBoxLayout(preview_panel_);
    panel_layout->setContentsMargins(8, 0, 8, 0);
    panel_layout->setSpacing(6);

    auto* heading = new QLabel(QStringLiteral("Preview"), preview_panel_);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setPointSize(heading_font.pointSize() + 1);
    heading->setFont(heading_font);
    panel_layout->addWidget(heading);
    panel_layout->addStretch(1);
}

void RenderWorkspace::populateContainerOptions() {
    containers_ = RenderOutputCapabilities::availableContainers();
    container_combo_->clear();
    for (const auto& container : containers_) {
        container_combo_->addItem(
            containerLabel(container), QString::fromStdString(container.name));
    }

    if (containers_.empty()) {
        container_combo_->setEnabled(false);
        video_encoder_combo_->setEnabled(false);
        capability_warning_->show();
        add_job_button_->setEnabled(false);
        return;
    }
    capability_warning_->hide();

    int preferred_index = 0;
    for (int index = 0; index < static_cast<int>(containers_.size()); ++index) {
        const auto& container = containers_[static_cast<std::size_t>(index)];
        const bool has_mp4_extension =
            QString::fromStdString(container.extensions)
                .split(',', Qt::SkipEmptyParts)
                .contains(QStringLiteral("mp4"), Qt::CaseInsensitive);
        if (container.name == "mp4" || has_mp4_extension) {
            preferred_index = index;
            break;
        }
    }
    container_combo_->setCurrentIndex(preferred_index);
    updateEncoderOptions();
}

void RenderWorkspace::updateEncoderOptions() {
    const auto selected_name = container_combo_->currentData().toString().toStdString();
    const auto container = std::find_if(
        containers_.begin(), containers_.end(), [&selected_name](const auto& option) {
            return option.name == selected_name;
        });
    video_encoder_combo_->clear();
    audio_encoder_combo_->clear();
    if (container == containers_.end()) {
        export_audio_check_->setChecked(false);
        export_audio_check_->setEnabled(false);
        updateAddAction();
        return;
    }

    for (const auto& encoder : container->video_encoders) {
        const auto label = QStringLiteral("%1 (%2)")
            .arg(QString::fromStdString(encoder.display_name),
                 QString::fromStdString(encoder.name));
        video_encoder_combo_->addItem(label, QString::fromStdString(encoder.name));
    }
    for (const auto& encoder : container->audio_encoders) {
        const auto label = QStringLiteral("%1 (%2)")
            .arg(QString::fromStdString(encoder.display_name),
                 QString::fromStdString(encoder.name));
        audio_encoder_combo_->addItem(label, QString::fromStdString(encoder.name));
    }

    const auto chooseEncoder = [](QComboBox* combo, const QString& preferred) {
        int chosen = combo->findData(preferred);
        if (chosen < 0) chosen = 0;
        if (combo->count() > 0) combo->setCurrentIndex(chosen);
    };
    chooseEncoder(video_encoder_combo_, QStringLiteral("libx264"));
    if (video_encoder_combo_->currentIndex() < 0 ||
        video_encoder_combo_->currentData().toString().contains(QStringLiteral("264")) == false) {
        int h264_index = -1;
        for (int index = 0; index < video_encoder_combo_->count(); ++index) {
            if (video_encoder_combo_->itemData(index).toString().contains(
                    QStringLiteral("h264"), Qt::CaseInsensitive)) {
                h264_index = index;
                break;
            }
        }
        if (h264_index >= 0) video_encoder_combo_->setCurrentIndex(h264_index);
    }
    chooseEncoder(audio_encoder_combo_, QStringLiteral("aac"));

    video_encoder_combo_->setEnabled(video_encoder_combo_->count() > 0);
    const bool audio_available = audio_encoder_combo_->count() > 0;
    export_audio_check_->setEnabled(audio_available);
    if (!audio_available) {
        export_audio_check_->setChecked(false);
        export_audio_check_->setToolTip(
            QStringLiteral("No compatible audio encoder is available for this container."));
    } else {
        export_audio_check_->setChecked(true);
        export_audio_check_->setToolTip({});
    }
    audio_encoder_combo_->setEnabled(audio_available && export_audio_check_->isChecked());
    audio_bitrate_spin_->setEnabled(audio_available && export_audio_check_->isChecked());
    updateAddAction();
}

void RenderWorkspace::updateResolutionFields() {
    const bool custom = resolution_combo_->currentIndex() == resolution_combo_->count() - 1;
    custom_width_->setVisible(custom);
    custom_height_->setVisible(custom);
}

void RenderWorkspace::updateQualitySuggestions() {
    if (quality_preset_combo_ == nullptr || video_bitrate_spin_ == nullptr ||
        audio_bitrate_spin_ == nullptr || frame_rate_spin_ == nullptr) {
        return;
    }
    const int preset_index = quality_preset_combo_->currentIndex();
    if (preset_index < 0 || preset_index >= 3) return;

    int width = project_width_;
    int height = project_height_;
    const QSize preset_resolution = resolution_combo_->currentData().toSize();
    if (preset_resolution.isValid()) {
        width = preset_resolution.width();
        height = preset_resolution.height();
    } else if (custom_width_ != nullptr && custom_height_ != nullptr) {
        width = custom_width_->value();
        height = custom_height_->value();
    }

    const double baseline_scale =
        (static_cast<double>(width) * static_cast<double>(height) *
         frame_rate_spin_->value()) /
        (1920.0 * 1080.0 * 30.0);
    constexpr double baseline_video_mbps[]{5.0, 10.0, 20.0};
    constexpr int audio_bitrate_kbps[]{128, 192, 320};
    const double suggested_video = std::clamp(
        std::round(baseline_video_mbps[preset_index] * baseline_scale * 10.0) / 10.0,
        0.1,
        500.0);

    applying_quality_suggestion_ = true;
    video_bitrate_spin_->setValue(suggested_video);
    audio_bitrate_spin_->setValue(audio_bitrate_kbps[preset_index]);
    applying_quality_suggestion_ = false;
}

void RenderWorkspace::updateQueueActions() {
    if (queue_model_ == nullptr || queue_view_ == nullptr) return;
    if (empty_queue_label_ != nullptr) {
        empty_queue_label_->setVisible(queue_model_->jobCount() == 0);
    }
    const int row = queue_view_->currentIndex().row();
    const bool selected = row >= 0 && row < queue_model_->jobCount();
    const bool running = queue_controller_ != nullptr && queue_controller_->isRunning();
    remove_job_button_->setEnabled(selected && !running);
    move_job_up_button_->setEnabled(selected && row > 0 && !running);
    move_job_down_button_->setEnabled(
        selected && row + 1 < queue_model_->jobCount() && !running);
    if (start_queue_button_ != nullptr) {
        bool has_pending = false;
        for (int index = 0; index < queue_model_->jobCount(); ++index) {
            const auto* job = queue_model_->jobAt(index);
            if (job != nullptr && job->status != RenderJobStatus::Completed) {
                has_pending = true;
                break;
            }
        }
        start_queue_button_->setEnabled(has_pending && !running);
    }
    if (cancel_queue_button_ != nullptr) cancel_queue_button_->setEnabled(running);
    if (add_job_button_ != nullptr) add_job_button_->setEnabled(
        add_job_button_->property("renderSettingsValid").toBool() && !running);
}

void RenderWorkspace::startQueue() {
    if (queue_controller_ == nullptr || queue_controller_->isRunning() ||
        queue_model_ == nullptr) return;

    std::vector<RenderJob> jobs;
    QStringList existing_outputs;
    QStringList invalid_outputs;
    std::vector<QString> targets;
    for (int row = 0; row < queue_model_->jobCount(); ++row) {
        const auto* job = queue_model_->jobAt(row);
        if (job == nullptr) continue;
        const auto normalized = normalizedOutputPath(job->settings.output_path);
        if (targets.end() != std::find(targets.begin(), targets.end(), normalized)) {
            QMessageBox::warning(
                central_page_, QStringLiteral("Duplicate output paths"),
                QStringLiteral("Two or more queue jobs use the same output file. Change the paths before starting the queue."));
            return;
        }
        targets.push_back(normalized);
        const QFileInfo output(job->settings.output_path);
        if (output.absoluteFilePath().trimmed().isEmpty()) {
            invalid_outputs.push_back(job->settings.output_path);
            continue;
        }
        if (job->status == RenderJobStatus::Completed) continue;
        if (!QFileInfo::exists(output.absolutePath()) ||
            !QFileInfo(output.absolutePath()).isDir() || output.isDir()) {
            invalid_outputs.push_back(job->settings.output_path);
            continue;
        }
        if (output.exists()) existing_outputs.push_back(output.absoluteFilePath());
        jobs.push_back(*job);
    }
    if (!invalid_outputs.empty()) {
        QMessageBox::warning(
            central_page_, QStringLiteral("Invalid output folder"),
            QStringLiteral("The output folder is unavailable for: %1")
                .arg(invalid_outputs.join(QStringLiteral("\n"))));
        return;
    }
    if (jobs.empty()) {
        updateQueueActions();
        return;
    }
    if (!existing_outputs.empty()) {
        const auto answer = QMessageBox::question(
            central_page_, QStringLiteral("Replace existing files?"),
            QStringLiteral("The following output files already exist. Replace them after each job completes?\n\n%1")
                .arg(existing_outputs.join(QStringLiteral("\n"))),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    failed_job_names_.clear();
    for (const auto& job : jobs) {
        static_cast<void>(queue_model_->setJobStatus(
            job.id, RenderJobStatus::Prepared, 0));
    }
    queue_model_->setLocked(true);
    if (!queue_controller_->start(std::move(jobs))) {
        queue_model_->setLocked(false);
        updateQueueActions();
        return;
    }
    updateQueueActions();
}

void RenderWorkspace::handleQueueFinished(bool canceled) {
    if (queue_model_ != nullptr) queue_model_->setLocked(false);
    updateQueueActions();
    if (!failed_job_names_.empty()) {
        QMessageBox::warning(
            central_page_, QStringLiteral("Render queue finished with errors"),
            QStringLiteral("%1 render job(s) failed. See the queue rows and application log for details.")
                .arg(failed_job_names_.size()));
    }
    Q_UNUSED(canceled);
}

void RenderWorkspace::updateAddAction() {
    if (add_job_button_ == nullptr) return;
    const bool has_container = container_combo_ != nullptr &&
        container_combo_->currentIndex() >= 0;
    const bool has_video_encoder = video_encoder_combo_ != nullptr &&
        video_encoder_combo_->currentIndex() >= 0;
    const bool has_audio_encoder = !export_audio_check_->isChecked() ||
        (audio_encoder_combo_ != nullptr && audio_encoder_combo_->currentIndex() >= 0);
    const bool valid_dimensions = customWidth() > 0 && customHeight() > 0;
    const bool valid_frame_rate = frame_rate_spin_ != nullptr &&
        std::isfinite(frame_rate_spin_->value()) && frame_rate_spin_->value() > 0.0;
    const bool valid =
        !output_path_->text().trimmed().isEmpty() && has_container &&
        has_video_encoder && has_audio_encoder && valid_dimensions && valid_frame_rate;
    add_job_button_->setProperty("renderSettingsValid", valid);
    const bool running = queue_controller_ != nullptr && queue_controller_->isRunning();
    add_job_button_->setEnabled(valid && !running);
}

void RenderWorkspace::updateDefaultFrameRate() {
    if (frame_rate_spin_ == nullptr || frame_rate_user_modified_) return;
    double frame_rate = frame_rate_provider_ ? frame_rate_provider_() : 30.0;
    if (!std::isfinite(frame_rate) || frame_rate < 0.001 || frame_rate > 1000.0) {
        frame_rate = 30.0;
    }
    {
        const QSignalBlocker blocker(frame_rate_spin_);
        frame_rate_spin_->setValue(frame_rate);
    }
    updateQualitySuggestions();
    updateAddAction();
}

void RenderWorkspace::updateOutputPathExtension() {
    if (output_path_ == nullptr || output_path_->text().trimmed().isEmpty()) return;
    const auto selected_name = container_combo_->currentData().toString().toStdString();
    const auto container = std::find_if(
        containers_.begin(), containers_.end(), [&selected_name](const auto& option) {
            return option.name == selected_name;
        });
    if (container == containers_.end()) return;

    QStringList extensions;
    for (auto extension : QString::fromStdString(container->extensions).split(
             ',', Qt::SkipEmptyParts)) {
        extensions.push_back(extension.trimmed().toLower());
    }
    if (extensions.isEmpty()) return;

    const QFileInfo path_info(output_path_->text());
    const auto suffix = path_info.suffix().toLower();
    if (extensions.contains(suffix)) return;

    auto base_name = path_info.completeBaseName();
    if (base_name.isEmpty()) base_name = path_info.fileName();
    if (base_name.isEmpty()) return;
    output_path_->setText(path_info.dir().filePath(
        base_name + QStringLiteral(".") + extensions.front()));
}

void RenderWorkspace::browseOutputPath() {
    const int index = container_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(containers_.size())) return;
    const auto& container = containers_[static_cast<std::size_t>(index)];
    const auto path = QFileDialog::getSaveFileName(
        central_page_,
        QStringLiteral("Choose Render Output"),
        output_path_->text(),
        extensionFilter(container));
    if (path.isEmpty()) return;
    output_path_->setText(path);
    updateOutputPathExtension();
}

void RenderWorkspace::addCurrentJob() {
    if (add_job_button_ == nullptr || !add_job_button_->isEnabled()) return;
    updateOutputPathExtension();

    RenderJob job;
    job.settings.output_path = output_path_->text().trimmed();
    job.settings.container_name = container_combo_->currentData().toString();
    job.settings.video_encoder_name = video_encoder_combo_->currentData().toString();
    job.settings.export_audio = export_audio_check_->isChecked();
    if (job.settings.export_audio) {
        job.settings.audio_encoder_name = audio_encoder_combo_->currentData().toString();
    }

    QSize resolution = resolution_combo_->currentData().toSize();
    if (!resolution.isValid()) {
        resolution = QSize(custom_width_->value(), custom_height_->value());
    }
    job.settings.width = resolution.width();
    job.settings.height = resolution.height();
    job.settings.frame_rate = frame_rate_spin_->value();
    job.settings.video_bitrate_mbps = video_bitrate_spin_->value();
    job.settings.audio_bitrate_kbps = audio_bitrate_spin_->value();
    job.settings.quality_preset = static_cast<RenderQualityPreset>(
        quality_preset_combo_->currentData().toInt());

    if (project_snapshot_provider_) {
        job.project_snapshot = project_snapshot_provider_();
    }
    job.display_name = QFileInfo(job.settings.output_path).completeBaseName();
    if (job.display_name.isEmpty()) job.display_name = QStringLiteral("Render Job");

    const int row = queue_model_->jobCount();
    const auto queued_job_id = queue_model_->addJob(std::move(job));
    if (queued_job_id == 0) return;
    Q_UNUSED(queued_job_id);
    queue_view_->setCurrentIndex(queue_model_->index(row, 0));
    updateQueueActions();
}

int RenderWorkspace::customWidth() const noexcept {
    if (resolution_combo_ == nullptr) return project_width_;
    const auto size = resolution_combo_->currentData().toSize();
    return size.isValid() ? size.width() : custom_width_->value();
}

int RenderWorkspace::customHeight() const noexcept {
    if (resolution_combo_ == nullptr) return project_height_;
    const auto size = resolution_combo_->currentData().toSize();
    return size.isValid() ? size.height() : custom_height_->value();
}

void RenderWorkspace::setActive(bool active) {
    if (active_ == active) return;

    active_ = active;
    if (active_) updateDefaultFrameRate();
    if (timeline_read_only_handler_) {
        timeline_read_only_handler_(active_);
    }
}

void RenderWorkspace::setPreviewWidget(QWidget* preview_widget) {
    if (preview_widget_ != nullptr || preview_panel_ == nullptr ||
        preview_widget == nullptr) {
        return;
    }

    auto* layout = static_cast<QVBoxLayout*>(preview_panel_->layout());
    preview_widget_ = preview_widget;
    layout->insertWidget(1, preview_widget_, 1);
    preview_widget_->show();
}

QWidget* RenderWorkspace::takePreviewWidget() {
    if (preview_widget_ == nullptr) return nullptr;
    auto* preview_widget = preview_widget_;
    if (preview_panel_ != nullptr && preview_panel_->layout() != nullptr) {
        preview_panel_->layout()->removeWidget(preview_widget);
    }
    preview_widget->setParent(nullptr);
    preview_widget_ = nullptr;
    return preview_widget;
}

}  // namespace ui
