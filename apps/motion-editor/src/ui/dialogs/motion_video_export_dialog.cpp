#include "ui/translation_helpers.h"
#include "motion_video_export_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <string>
#include <utility>

namespace motion::ui {
namespace {

QString pathToQString(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()),
                             static_cast<qsizetype>(value.size()));
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* begin = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(begin, begin + bytes.size()));
}

QString extensionFilter(const creative_suite::media::VideoContainerOption& container)
{
    QStringList patterns;
    for (const auto& extension : QString::fromStdString(container.extensions).split(
             QLatin1Char(','), Qt::SkipEmptyParts)) {
        patterns.push_back(QStringLiteral("*.%1").arg(extension.trimmed()));
    }
    if (patterns.isEmpty()) patterns.push_back(QStringLiteral("*.*"));
    return QStringLiteral("%1 (%2)")
        .arg(QString::fromStdString(container.display_name), patterns.join(QLatin1Char(' ')));
}

std::int64_t rateNumerator(const QDoubleSpinBox* field)
{
    return static_cast<std::int64_t>(std::llround(field->value() * 1000.0));
}

using VideoOutputProfile = creative_suite::media::VideoOutputProfile;

bool isAlphaProfile(VideoOutputProfile profile)
{
    return profile != VideoOutputProfile::Bitrate;
}

int alphaProfileRank(VideoOutputProfile profile)
{
    switch (profile) {
    case VideoOutputProfile::Ffv1LosslessAlpha: return 0;
    case VideoOutputProfile::ProRes4444Alpha: return 1;
    case VideoOutputProfile::ProRes4444XqAlpha: return 2;
    case VideoOutputProfile::Vp9Alpha: return 3;
    case VideoOutputProfile::Bitrate: return 100;
    }
    return 100;
}

bool hasAlphaProfile(const creative_suite::media::VideoEncoderOption& encoder)
{
    return !encoder.alpha_profiles.empty();
}

std::optional<VideoOutputProfile> selectedAlphaProfile(const QComboBox* quality)
{
    if (quality == nullptr || !quality->currentData(Qt::UserRole + 1).toBool()) return {};
    const auto value = static_cast<VideoOutputProfile>(quality->currentData().toInt());
    return isAlphaProfile(value) ? std::optional<VideoOutputProfile>(value) : std::nullopt;
}

} // namespace

MotionVideoExportDialog::MotionVideoExportDialog(
    model::CanvasSize canvas_size,
    model::FrameRate composition_rate,
    QWidget* parent)
    : QDialog(parent), canvas_size_(canvas_size), composition_rate_(composition_rate)
{
    setObjectName(QStringLiteral("motion-video-export-dialog"));
    setWindowTitle(MotionUiTranslation::tr("Export Video"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* output_row = new QWidget(this);
    auto* output_layout = new QHBoxLayout(output_row);
    output_layout->setContentsMargins(0, 0, 0, 0);
    output_path_ = new QLineEdit(output_row);
    output_path_->setObjectName(QStringLiteral("motion-export-output-path"));
    output_path_->setAccessibleName(MotionUiTranslation::tr("Video output file"));
    auto* browse = new QPushButton(MotionUiTranslation::tr("Browse..."), output_row);
    browse->setObjectName(QStringLiteral("motion-export-browse-output"));
    output_layout->addWidget(output_path_, 1);
    output_layout->addWidget(browse);
    form->addRow(MotionUiTranslation::tr("Output file"), output_row);

    output_profile_combo_ = new QComboBox(this);
    output_profile_combo_->setObjectName(QStringLiteral("motion-export-output-profile"));
    output_profile_combo_->addItem(MotionUiTranslation::tr("Opaque"),
        static_cast<int>(VideoOutputProfile::Bitrate));
    output_profile_combo_->addItem(MotionUiTranslation::tr("Transparent (alpha)"),
        static_cast<int>(VideoOutputProfile::Ffv1LosslessAlpha));
    form->addRow(MotionUiTranslation::tr("Output profile"), output_profile_combo_);

    container_combo_ = new QComboBox(this);
    container_combo_->setObjectName(QStringLiteral("motion-export-container"));
    form->addRow(MotionUiTranslation::tr("Container"), container_combo_);
    encoder_combo_ = new QComboBox(this);
    encoder_combo_->setObjectName(QStringLiteral("motion-export-video-encoder"));
    form->addRow(MotionUiTranslation::tr("Video encoder"), encoder_combo_);

    resolution_combo_ = new QComboBox(this);
    resolution_combo_->setObjectName(QStringLiteral("motion-export-resolution"));
    resolution_combo_->addItem(
        MotionUiTranslation::tr("Composition (%1 x %2)").arg(canvas_size.width).arg(canvas_size.height),
        QSize(canvas_size.width, canvas_size.height));
    const std::vector<QSize> common_resolutions{
        QSize(1280, 720), QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160)};
    for (const auto& size : common_resolutions) {
        if (size.width() == canvas_size.width && size.height() == canvas_size.height) continue;
        resolution_combo_->addItem(
            MotionUiTranslation::tr("%1 x %2").arg(size.width()).arg(size.height()), size);
    }
    resolution_combo_->addItem(MotionUiTranslation::tr("Custom dimensions"), QVariant{});
    form->addRow(MotionUiTranslation::tr("Resolution"), resolution_combo_);

    custom_width_ = new QSpinBox(this);
    custom_width_->setObjectName(QStringLiteral("motion-export-custom-width"));
    custom_width_->setRange(1, 16384);
    custom_width_->setValue(canvas_size.width);
    custom_width_->setSuffix(QStringLiteral(" px"));
    custom_width_label_ = new QLabel(MotionUiTranslation::tr("Custom width"), this);
    form->addRow(custom_width_label_, custom_width_);
    custom_height_ = new QSpinBox(this);
    custom_height_->setObjectName(QStringLiteral("motion-export-custom-height"));
    custom_height_->setRange(1, 16384);
    custom_height_->setValue(canvas_size.height);
    custom_height_->setSuffix(QStringLiteral(" px"));
    custom_height_label_ = new QLabel(MotionUiTranslation::tr("Custom height"), this);
    form->addRow(custom_height_label_, custom_height_);

    frame_rate_ = new QDoubleSpinBox(this);
    frame_rate_->setObjectName(QStringLiteral("motion-export-frame-rate"));
    frame_rate_->setRange(0.001, 1000.0);
    frame_rate_->setDecimals(3);
    frame_rate_->setSingleStep(0.001);
    frame_rate_->setSuffix(QStringLiteral(" fps"));
    frame_rate_->setValue(composition_rate.asDouble());
    form->addRow(MotionUiTranslation::tr("Output frame rate"), frame_rate_);

    quality_combo_ = new QComboBox(this);
    quality_combo_->setObjectName(QStringLiteral("motion-export-quality"));
    quality_combo_->addItem(MotionUiTranslation::tr("Low"), 0);
    quality_combo_->addItem(MotionUiTranslation::tr("Standard"), 1);
    quality_combo_->addItem(MotionUiTranslation::tr("High"), 2);
    quality_combo_->addItem(MotionUiTranslation::tr("Custom"), 3);
    quality_combo_->setCurrentIndex(1);
    quality_label_ = new QLabel(MotionUiTranslation::tr("Quality profile"), this);
    form->addRow(quality_label_, quality_combo_);

    bitrate_ = new QDoubleSpinBox(this);
    bitrate_->setObjectName(QStringLiteral("motion-export-video-bitrate"));
    bitrate_->setRange(0.1, 500.0);
    bitrate_->setDecimals(1);
    bitrate_->setSingleStep(0.5);
    bitrate_->setSuffix(QStringLiteral(" Mbps"));
    bitrate_label_ = new QLabel(MotionUiTranslation::tr("Video bitrate"), this);
    form->addRow(bitrate_label_, bitrate_);
    vp9_crf_ = new QSpinBox(this);
    vp9_crf_->setObjectName(QStringLiteral("motion-export-vp9-crf"));
    vp9_crf_->setRange(0, 63);
    vp9_crf_->setValue(32);
    vp9_crf_label_ = new QLabel(MotionUiTranslation::tr("VP9 constant quality (CRF)"), this);
    form->addRow(vp9_crf_label_, vp9_crf_);
    layout->addLayout(form);

    note_ = new QLabel(this);
    note_->setWordWrap(true);
    layout->addWidget(note_);
    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("motion-export-buttons"));
    buttons_->button(QDialogButtonBox::Save)->setText(MotionUiTranslation::tr("Export"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(MotionUiTranslation::tr("Cancel"));
    buttons_->button(QDialogButtonBox::Save)->setObjectName(
        QStringLiteral("motion-export-accept"));
    buttons_->button(QDialogButtonBox::Cancel)->setObjectName(
        QStringLiteral("motion-export-cancel"));
    layout->addWidget(buttons_);

    connect(browse, &QPushButton::clicked, this, [this] { browseOutput(); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(output_path_, &QLineEdit::textChanged, this, [this] { updateAcceptState(); });
    connect(container_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
                updateEncoders({}, selectedAlphaProfile(quality_combo_));
                updateDefaultExtension();
            });
    connect(encoder_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { updateQualityControls(selectedAlphaProfile(quality_combo_)); });
    connect(output_profile_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
                const auto previous_container = container_combo_->currentData().toString();
                const auto previous_encoder = encoder_combo_->currentData().toString();
                const auto previous_profile = selectedAlphaProfile(quality_combo_);
                rebuildContainerChoices(previous_container, previous_encoder, previous_profile);
                updateDefaultExtension();
                updateAcceptState();
            });
    connect(resolution_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { updateResolutionFields(); updateQualitySuggestion(); });
    connect(custom_width_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] { updateQualitySuggestion(); });
    connect(custom_height_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] { updateQualitySuggestion(); });
    connect(frame_rate_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
        frame_rate_overridden_ = true;
        updateQualitySuggestion();
    });
    connect(quality_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { updateQualitySuggestion(); });
    connect(bitrate_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
        if (!applying_quality_suggestion_ && quality_combo_->currentIndex() < 3) {
            const QSignalBlocker blocker(quality_combo_);
            quality_combo_->setCurrentIndex(3);
        }
    });
    connect(vp9_crf_, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
        updateAcceptState();
    });

    populateContainers();
    updateResolutionFields();
    updateQualitySuggestion();
    updateAcceptState();
    resize(520, sizeHint().height());
}

std::optional<MotionExportSettings> MotionVideoExportDialog::exportSettings() const
{
    const auto path = output_path_->text().trimmed();
    if (path.isEmpty() || container_combo_->currentIndex() < 0 ||
        encoder_combo_->currentIndex() < 0) return std::nullopt;
    const int visible_container = container_combo_->currentIndex();
    if (visible_container >= static_cast<int>(container_indices_.size())) return std::nullopt;
    const auto& selected_container = containers_[container_indices_[static_cast<std::size_t>(visible_container)]];
    const QSize selected_size = resolution_combo_->currentData().toSize();
    model::FrameRate output_rate = composition_rate_;
    if (frame_rate_overridden_) {
        auto numerator = rateNumerator(frame_rate_);
        constexpr std::int64_t denominator = 1000;
        const auto divisor = std::gcd(numerator, denominator);
        numerator /= divisor;
        output_rate = {numerator, denominator / divisor};
    }
    MotionExportSettings settings{
        pathFromQString(path),
        selected_container.name,
        encoder_combo_->currentData().toString().toStdString(),
        selected_size.isValid() ? selected_size.width() : custom_width_->value(),
        selected_size.isValid() ? selected_size.height() : custom_height_->value(),
        output_rate,
        bitrate_->value()};
    if (output_profile_combo_->currentData().toInt() !=
        static_cast<int>(VideoOutputProfile::Bitrate)) {
        const auto profile = selectedAlphaProfile(quality_combo_);
        if (!profile) return std::nullopt;
        settings.profile = *profile;
        settings.quality_value = vp9_crf_->value();
        const auto encoder = std::find_if(selected_container.video_encoders.begin(),
            selected_container.video_encoders.end(), [&](const auto& option) {
                return option.name == settings.video_encoder_name;
            });
        if (encoder == selected_container.video_encoders.end()) return std::nullopt;
        const auto alpha = std::find_if(encoder->alpha_profiles.begin(),
            encoder->alpha_profiles.end(), [&](const auto& option) {
                return option.profile == *profile;
            });
        if (alpha == encoder->alpha_profiles.end()) return std::nullopt;
        settings.alpha_pixel_format_name = alpha->pixel_format_name;
    }
    return settings;
}

void MotionVideoExportDialog::browseOutput()
{
    const int index = container_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(container_indices_.size())) return;
    const auto& container = containers_[container_indices_[static_cast<std::size_t>(index)]];
    const auto selected = QFileDialog::getSaveFileName(
        this, MotionUiTranslation::tr("Choose Video Output"), output_path_->text(),
        extensionFilter(container));
    if (!selected.isEmpty()) output_path_->setText(selected);
}

void MotionVideoExportDialog::populateContainers()
{
    containers_ = creative_suite::media::availableVideoContainers();
    transparent_available_ = std::any_of(containers_.begin(), containers_.end(), [](const auto& container) {
        return std::any_of(container.video_encoders.begin(), container.video_encoders.end(),
            [](const auto& encoder) { return hasAlphaProfile(encoder); });
    });
    if (auto* model = qobject_cast<QStandardItemModel*>(output_profile_combo_->model())) {
        if (auto* item = model->item(1)) {
            item->setEnabled(transparent_available_);
            if (!transparent_available_) {
            output_profile_combo_->setItemData(1, MotionUiTranslation::tr(
                "No supported software alpha encoder is available in this FFmpeg installation."),
                Qt::ToolTipRole);
            }
        }
    }
    note_->setText(transparent_available_
        ? MotionUiTranslation::tr("Exports the full layer range as video. Transparent alpha is available with supported software profiles. Audio is not included.")
        : MotionUiTranslation::tr("Exports the full layer range as opaque video. No supported software alpha encoder is available. Audio is not included."));
    if (containers_.empty()) {
        container_combo_->setEnabled(false);
        encoder_combo_->setEnabled(false);
        buttons_->button(QDialogButtonBox::Save)->setEnabled(false);
        return;
    }
    rebuildContainerChoices();
}

void MotionVideoExportDialog::rebuildContainerChoices(
    const QString& preferred_container,
    const QString& preferred_encoder,
    std::optional<VideoOutputProfile> preferred_profile)
{
    const bool transparent = output_profile_combo_->currentData().toInt() !=
        static_cast<int>(VideoOutputProfile::Bitrate);
    const QSignalBlocker container_signals(container_combo_);
    container_indices_.clear();
    container_combo_->clear();
    for (std::size_t index = 0; index < containers_.size(); ++index) {
        const auto& container = containers_[index];
        const bool has_alpha = std::any_of(container.video_encoders.begin(), container.video_encoders.end(),
            [](const auto& encoder) { return hasAlphaProfile(encoder); });
        if (transparent && !has_alpha) continue;
        QString label = QString::fromStdString(container.display_name);
        if (!container.extensions.empty()) {
            label += QStringLiteral(" (%1)").arg(
                QString::fromStdString(container.extensions).section(QLatin1Char(','), 0, 0));
        }
        container_indices_.push_back(index);
        container_combo_->addItem(label, QString::fromStdString(container.name));
    }
    if (container_indices_.empty()) {
        encoder_combo_->clear();
        encoder_combo_->setEnabled(false);
        updateQualityControls();
        updateAcceptState();
        return;
    }

    int selected = -1;
    if (!preferred_container.isEmpty() && !preferred_encoder.isEmpty()) {
        for (int index = 0; index < static_cast<int>(container_indices_.size()); ++index) {
            const auto& container = containers_[container_indices_[static_cast<std::size_t>(index)]];
            if (container.name != preferred_container.toStdString()) continue;
            const auto encoder = std::find_if(container.video_encoders.begin(), container.video_encoders.end(),
                [&](const auto& option) { return option.name == preferred_encoder.toStdString(); });
            if (encoder != container.video_encoders.end() &&
                (!transparent || hasAlphaProfile(*encoder))) {
                selected = index;
                break;
            }
        }
    }
    if (selected < 0 && transparent) {
        int best_rank = 100;
        for (int index = 0; index < static_cast<int>(container_indices_.size()); ++index) {
            const auto& container = containers_[container_indices_[static_cast<std::size_t>(index)]];
            for (const auto& encoder : container.video_encoders) {
                for (const auto& alpha : encoder.alpha_profiles) {
                    const auto rank = alphaProfileRank(alpha.profile);
                    if (rank < best_rank) { best_rank = rank; selected = index; }
                }
            }
        }
    }
    if (selected < 0 && !preferred_container.isEmpty())
        selected = container_combo_->findData(preferred_container);
    if (selected < 0 && !transparent) {
        for (int index = 0; index < static_cast<int>(container_indices_.size()); ++index) {
            const auto& container = containers_[container_indices_[static_cast<std::size_t>(index)]];
            const auto extensions = QString::fromStdString(container.extensions).split(
                QLatin1Char(','), Qt::SkipEmptyParts);
            if (container.name == "mp4" || extensions.contains(QStringLiteral("mp4"), Qt::CaseInsensitive)) {
                selected = index;
                break;
            }
        }
    }
    if (selected < 0) selected = 0;
    container_combo_->setCurrentIndex(selected);
    updateEncoders(preferred_encoder, preferred_profile);
    updateDefaultExtension();
}

void MotionVideoExportDialog::updateEncoders(
    const QString& preferred_encoder,
    std::optional<VideoOutputProfile> preferred_profile)
{
    const QSignalBlocker encoder_signals(encoder_combo_);
    encoder_combo_->clear();
    const int index = container_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(container_indices_.size())) {
        encoder_combo_->setEnabled(false);
        updateQualityControls();
        updateAcceptState();
        return;
    }
    const auto& container = containers_[container_indices_[static_cast<std::size_t>(index)]];
    const bool transparent = output_profile_combo_->currentData().toInt() !=
        static_cast<int>(VideoOutputProfile::Bitrate);
    const auto& encoders = container.video_encoders;
    for (const auto& encoder : encoders) {
        if (transparent && !hasAlphaProfile(encoder)) continue;
        encoder_combo_->addItem(
            QStringLiteral("%1 (%2)").arg(QString::fromStdString(encoder.display_name),
                                            QString::fromStdString(encoder.name)),
            QString::fromStdString(encoder.name));
        encoder_combo_->setItemData(encoder_combo_->count() - 1, encoder.experimental, Qt::UserRole + 1);
    }
    int preferred = encoder_combo_->findData(preferred_encoder);
    if (preferred >= 0 && transparent) {
        const auto encoder = std::find_if(encoders.begin(), encoders.end(), [&](const auto& option) {
            return option.name == preferred_encoder.toStdString();
        });
        const bool profile_matches = encoder != encoders.end() &&
            (!preferred_profile || std::any_of(encoder->alpha_profiles.begin(), encoder->alpha_profiles.end(),
                [&](const auto& option) { return option.profile == *preferred_profile; }));
        if (!profile_matches) preferred = -1;
    }
    if (preferred < 0 && !transparent) preferred = encoder_combo_->findData(QStringLiteral("libx264"));
    if (preferred < 0 && transparent) {
        int best_rank = 100;
        for (int option = 0; option < encoder_combo_->count(); ++option) {
            const auto name = encoder_combo_->itemData(option).toString().toStdString();
            const auto encoder = std::find_if(encoders.begin(), encoders.end(), [&](const auto& value) {
                return value.name == name;
            });
            if (encoder == encoders.end()) continue;
            for (const auto& alpha : encoder->alpha_profiles) {
                const auto rank = alphaProfileRank(alpha.profile);
                if (rank < best_rank) { best_rank = rank; preferred = option; }
            }
        }
    }
    if (preferred < 0) {
        for (int option = 0; option < encoder_combo_->count(); ++option) {
            if (encoder_combo_->itemData(option, Qt::UserRole + 1).toBool()) continue;
            const auto name = encoder_combo_->itemData(option).toString();
            if (name.contains(QStringLiteral("264"), Qt::CaseInsensitive) ||
                name.contains(QStringLiteral("h264"), Qt::CaseInsensitive)) {
                preferred = option;
                break;
            }
        }
    }
    if (preferred < 0) {
        for (int option = 0; option < encoder_combo_->count(); ++option)
            if (!encoder_combo_->itemData(option, Qt::UserRole + 1).toBool()) { preferred = option; break; }
    }
    if (preferred >= 0) encoder_combo_->setCurrentIndex(preferred);
    encoder_combo_->setEnabled(encoder_combo_->count() > 0);
    updateQualityControls(preferred_profile);
    updateAcceptState();
}

void MotionVideoExportDialog::updateQualityControls(
    std::optional<VideoOutputProfile> preferred_profile)
{
    const QSignalBlocker quality_signals(quality_combo_);
    const bool transparent = output_profile_combo_->currentData().toInt() !=
        static_cast<int>(VideoOutputProfile::Bitrate);
    const bool vp9 = transparent && encoder_combo_->currentData().toString() == QStringLiteral("libvpx-vp9");
    quality_combo_->clear();
    if (!transparent) {
        quality_combo_->addItem(MotionUiTranslation::tr("Low"), 0);
        quality_combo_->addItem(MotionUiTranslation::tr("Standard"), 1);
        quality_combo_->addItem(MotionUiTranslation::tr("High"), 2);
        quality_combo_->addItem(MotionUiTranslation::tr("Custom"), 3);
        quality_combo_->setCurrentIndex(1);
        quality_combo_->setEnabled(true);
    } else {
        const int container_index = container_combo_->currentIndex();
        if (container_index >= 0 && container_index < static_cast<int>(container_indices_.size())) {
            const auto& container = containers_[container_indices_[static_cast<std::size_t>(container_index)]];
            const auto encoder_name = encoder_combo_->currentData().toString().toStdString();
            const auto encoder = std::find_if(container.video_encoders.begin(), container.video_encoders.end(),
                [&](const auto& option) { return option.name == encoder_name; });
            if (encoder != container.video_encoders.end()) {
                for (const auto& alpha : encoder->alpha_profiles) {
                    QString label = QString::fromStdString(alpha.display_name);
                    if (alpha.profile == VideoOutputProfile::ProRes4444Alpha ||
                        alpha.profile == VideoOutputProfile::ProRes4444XqAlpha)
                        label = MotionUiTranslation::tr("%1 (16-bit alpha)").arg(label);
                    else if (alpha.profile == VideoOutputProfile::Ffv1LosslessAlpha)
                        label = MotionUiTranslation::tr("Lossless");
                    quality_combo_->addItem(label, static_cast<int>(alpha.profile));
                    quality_combo_->setItemData(
                        quality_combo_->count() - 1, true, Qt::UserRole + 1);
                }
            }
        }
        int selected = -1;
        if (preferred_profile) {
            for (int index = 0; index < quality_combo_->count(); ++index)
                if (quality_combo_->itemData(index).toInt() == static_cast<int>(*preferred_profile)) {
                    selected = index; break;
                }
        }
        if (selected < 0 && quality_combo_->count() > 0) {
            int best_rank = 100;
            for (int index = 0; index < quality_combo_->count(); ++index) {
                const auto profile = static_cast<VideoOutputProfile>(quality_combo_->itemData(index).toInt());
                const auto rank = alphaProfileRank(profile);
                if (rank < best_rank) { best_rank = rank; selected = index; }
            }
        }
        if (selected >= 0) quality_combo_->setCurrentIndex(selected);
        quality_combo_->setEnabled(quality_combo_->count() > 1 && !vp9);
    }
    quality_label_->setVisible(!vp9);
    quality_combo_->setVisible(!vp9);
    bitrate_label_->setVisible(!transparent);
    bitrate_->setVisible(!transparent);
    vp9_crf_label_->setVisible(vp9);
    vp9_crf_->setVisible(vp9);
    if (transparent) {
        note_->setText(MotionUiTranslation::tr(
            "Exports transparent alpha with the selected software codec profile. Empty areas remain transparent; audio is not included."));
    } else {
        note_->setText(transparent_available_
            ? MotionUiTranslation::tr("Exports the full layer range as opaque video. Transparent alpha is available with supported software profiles. Audio is not included.")
            : MotionUiTranslation::tr("Exports the full layer range as opaque video. No supported software alpha encoder is available. Audio is not included."));
    }
    updateQualitySuggestion();
}

void MotionVideoExportDialog::updateResolutionFields()
{
    const bool custom = resolution_combo_->currentData().toSize().isEmpty();
    custom_width_->setVisible(custom);
    custom_height_->setVisible(custom);
    custom_width_label_->setVisible(custom);
    custom_height_label_->setVisible(custom);
}

void MotionVideoExportDialog::updateQualitySuggestion()
{
    if (output_profile_combo_->currentData().toInt() !=
            static_cast<int>(VideoOutputProfile::Bitrate) ||
        quality_combo_->currentIndex() >= 3) return;
    int width = canvas_size_.width;
    int height = canvas_size_.height;
    const QSize preset = resolution_combo_->currentData().toSize();
    if (preset.isValid()) {
        width = preset.width();
        height = preset.height();
    } else {
        width = custom_width_->value();
        height = custom_height_->value();
    }
    const double rate = frame_rate_->value();
    const double scale = (static_cast<double>(width) * height * rate) /
        (1920.0 * 1080.0 * 30.0);
    constexpr double base_bitrates[]{5.0, 10.0, 20.0};
    const int index = std::clamp(quality_combo_->currentIndex(), 0, 2);
    const auto suggested = std::clamp(
        std::round(base_bitrates[index] * scale * 10.0) / 10.0, 0.1, 500.0);
    applying_quality_suggestion_ = true;
    bitrate_->setValue(suggested);
    applying_quality_suggestion_ = false;
}

void MotionVideoExportDialog::updateAcceptState()
{
    if (buttons_ == nullptr) return;
    const bool available = container_combo_->currentIndex() >= 0 &&
        encoder_combo_->currentIndex() >= 0;
    buttons_->button(QDialogButtonBox::Save)->setEnabled(
        available && !output_path_->text().trimmed().isEmpty());
}

void MotionVideoExportDialog::updateDefaultExtension()
{
    const int index = container_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(container_indices_.size())) return;
    const auto& container = containers_[container_indices_[static_cast<std::size_t>(index)]];
    const QStringList extensions = QString::fromStdString(container.extensions).split(
        QLatin1Char(','), Qt::SkipEmptyParts);
    if (extensions.isEmpty()) return;
    const auto path = output_path_->text();
    if (path.isEmpty()) return;
    QFileInfo info(path);
    const auto current_extension = info.suffix();
    if (!current_extension.isEmpty() && extensions.contains(
            current_extension, Qt::CaseInsensitive)) return;
    const auto base = info.path() == QStringLiteral(".")
        ? info.completeBaseName() : info.path() + QLatin1Char('/') + info.completeBaseName();
    output_path_->setText(base + QLatin1Char('.') + extensions.front().trimmed());
}

} // namespace motion::ui
