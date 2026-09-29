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

} // namespace

MotionVideoExportDialog::MotionVideoExportDialog(
    model::CanvasSize canvas_size,
    model::FrameRate composition_rate,
    QWidget* parent)
    : QDialog(parent), canvas_size_(canvas_size), composition_rate_(composition_rate)
{
    setObjectName(QStringLiteral("motion-video-export-dialog"));
    setWindowTitle(QStringLiteral("Export Video"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* output_row = new QWidget(this);
    auto* output_layout = new QHBoxLayout(output_row);
    output_layout->setContentsMargins(0, 0, 0, 0);
    output_path_ = new QLineEdit(output_row);
    output_path_->setObjectName(QStringLiteral("motion-export-output-path"));
    output_path_->setAccessibleName(QStringLiteral("Video output file"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), output_row);
    browse->setObjectName(QStringLiteral("motion-export-browse-output"));
    output_layout->addWidget(output_path_, 1);
    output_layout->addWidget(browse);
    form->addRow(QStringLiteral("Output file"), output_row);

    container_combo_ = new QComboBox(this);
    container_combo_->setObjectName(QStringLiteral("motion-export-container"));
    form->addRow(QStringLiteral("Container"), container_combo_);
    encoder_combo_ = new QComboBox(this);
    encoder_combo_->setObjectName(QStringLiteral("motion-export-video-encoder"));
    form->addRow(QStringLiteral("Video encoder"), encoder_combo_);

    resolution_combo_ = new QComboBox(this);
    resolution_combo_->setObjectName(QStringLiteral("motion-export-resolution"));
    resolution_combo_->addItem(
        QStringLiteral("Composition (%1 x %2)").arg(canvas_size.width).arg(canvas_size.height),
        QSize(canvas_size.width, canvas_size.height));
    const std::vector<QSize> common_resolutions{
        QSize(1280, 720), QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160)};
    for (const auto& size : common_resolutions) {
        if (size.width() == canvas_size.width && size.height() == canvas_size.height) continue;
        resolution_combo_->addItem(
            QStringLiteral("%1 x %2").arg(size.width()).arg(size.height()), size);
    }
    resolution_combo_->addItem(QStringLiteral("Custom dimensions"), QVariant{});
    form->addRow(QStringLiteral("Resolution"), resolution_combo_);

    custom_width_ = new QSpinBox(this);
    custom_width_->setObjectName(QStringLiteral("motion-export-custom-width"));
    custom_width_->setRange(1, 16384);
    custom_width_->setValue(canvas_size.width);
    custom_width_->setSuffix(QStringLiteral(" px"));
    custom_width_label_ = new QLabel(QStringLiteral("Custom width"), this);
    form->addRow(custom_width_label_, custom_width_);
    custom_height_ = new QSpinBox(this);
    custom_height_->setObjectName(QStringLiteral("motion-export-custom-height"));
    custom_height_->setRange(1, 16384);
    custom_height_->setValue(canvas_size.height);
    custom_height_->setSuffix(QStringLiteral(" px"));
    custom_height_label_ = new QLabel(QStringLiteral("Custom height"), this);
    form->addRow(custom_height_label_, custom_height_);

    frame_rate_ = new QDoubleSpinBox(this);
    frame_rate_->setObjectName(QStringLiteral("motion-export-frame-rate"));
    frame_rate_->setRange(0.001, 1000.0);
    frame_rate_->setDecimals(3);
    frame_rate_->setSingleStep(0.001);
    frame_rate_->setSuffix(QStringLiteral(" fps"));
    frame_rate_->setValue(composition_rate.asDouble());
    form->addRow(QStringLiteral("Output frame rate"), frame_rate_);

    quality_combo_ = new QComboBox(this);
    quality_combo_->setObjectName(QStringLiteral("motion-export-quality"));
    quality_combo_->addItem(QStringLiteral("Low"), 0);
    quality_combo_->addItem(QStringLiteral("Standard"), 1);
    quality_combo_->addItem(QStringLiteral("High"), 2);
    quality_combo_->addItem(QStringLiteral("Custom"), 3);
    quality_combo_->setCurrentIndex(1);
    form->addRow(QStringLiteral("Quality profile"), quality_combo_);

    bitrate_ = new QDoubleSpinBox(this);
    bitrate_->setObjectName(QStringLiteral("motion-export-video-bitrate"));
    bitrate_->setRange(0.1, 500.0);
    bitrate_->setDecimals(1);
    bitrate_->setSingleStep(0.5);
    bitrate_->setSuffix(QStringLiteral(" Mbps"));
    form->addRow(QStringLiteral("Video bitrate"), bitrate_);
    layout->addLayout(form);

    auto* note = new QLabel(
        QStringLiteral("Exports the full layer range as opaque video. Audio is not included."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("motion-export-buttons"));
    buttons_->button(QDialogButtonBox::Save)->setText(QStringLiteral("Export"));
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
            this, [this] { updateEncoders(); updateDefaultExtension(); });
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
    const QSize selected_size = resolution_combo_->currentData().toSize();
    model::FrameRate output_rate = composition_rate_;
    if (frame_rate_overridden_) {
        auto numerator = rateNumerator(frame_rate_);
        constexpr std::int64_t denominator = 1000;
        const auto divisor = std::gcd(numerator, denominator);
        numerator /= divisor;
        output_rate = {numerator, denominator / divisor};
    }
    return MotionExportSettings{
        pathFromQString(path),
        container_combo_->currentData().toString().toStdString(),
        encoder_combo_->currentData().toString().toStdString(),
        selected_size.isValid() ? selected_size.width() : custom_width_->value(),
        selected_size.isValid() ? selected_size.height() : custom_height_->value(),
        output_rate,
        bitrate_->value()};
}

void MotionVideoExportDialog::browseOutput()
{
    const int index = container_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(containers_.size())) return;
    const auto& container = containers_[static_cast<std::size_t>(index)];
    const auto selected = QFileDialog::getSaveFileName(
        this, QStringLiteral("Choose Video Output"), output_path_->text(),
        extensionFilter(container));
    if (!selected.isEmpty()) output_path_->setText(selected);
}

void MotionVideoExportDialog::populateContainers()
{
    containers_ = creative_suite::media::availableVideoContainers();
    container_combo_->clear();
    for (const auto& container : containers_) {
        QString label = QString::fromStdString(container.display_name);
        if (!container.extensions.empty()) {
            label += QStringLiteral(" (%1)").arg(
                QString::fromStdString(container.extensions).section(QLatin1Char(','), 0, 0));
        }
        container_combo_->addItem(label, QString::fromStdString(container.name));
    }
    if (containers_.empty()) {
        container_combo_->setEnabled(false);
        encoder_combo_->setEnabled(false);
        buttons_->button(QDialogButtonBox::Save)->setEnabled(false);
        return;
    }
    int preferred = 0;
    for (int index = 0; index < static_cast<int>(containers_.size()); ++index) {
        const auto& container = containers_[static_cast<std::size_t>(index)];
        const auto extensions = QString::fromStdString(container.extensions).split(
            QLatin1Char(','), Qt::SkipEmptyParts);
        if (container.name == "mp4" || extensions.contains(QStringLiteral("mp4"), Qt::CaseInsensitive)) {
            preferred = index;
            break;
        }
    }
    container_combo_->setCurrentIndex(preferred);
    updateEncoders();
    updateDefaultExtension();
}

void MotionVideoExportDialog::updateEncoders()
{
    encoder_combo_->clear();
    const int index = container_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(containers_.size())) {
        encoder_combo_->setEnabled(false);
        updateAcceptState();
        return;
    }
    const auto& encoders = containers_[static_cast<std::size_t>(index)].video_encoders;
    for (const auto& encoder : encoders) {
        encoder_combo_->addItem(
            QStringLiteral("%1 (%2)").arg(QString::fromStdString(encoder.display_name),
                                            QString::fromStdString(encoder.name)),
            QString::fromStdString(encoder.name));
    }
    int preferred = encoder_combo_->findData(QStringLiteral("libx264"));
    if (preferred < 0) {
        for (int option = 0; option < encoder_combo_->count(); ++option) {
            const auto name = encoder_combo_->itemData(option).toString();
            if (name.contains(QStringLiteral("264"), Qt::CaseInsensitive) ||
                name.contains(QStringLiteral("h264"), Qt::CaseInsensitive)) {
                preferred = option;
                break;
            }
        }
    }
    if (preferred < 0 && encoder_combo_->count() > 0) preferred = 0;
    if (preferred >= 0) encoder_combo_->setCurrentIndex(preferred);
    encoder_combo_->setEnabled(encoder_combo_->count() > 0);
    updateAcceptState();
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
    if (quality_combo_->currentIndex() >= 3) return;
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
    if (index < 0 || index >= static_cast<int>(containers_.size())) return;
    const auto& container = containers_[static_cast<std::size_t>(index)];
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
