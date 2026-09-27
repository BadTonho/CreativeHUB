#include "image_export_dialog.h"

#include <QColorDialog>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace image_editor {

JpegExportOptionsDialog::JpegExportOptionsDialog(
    const ImageExportOptions& options, QWidget* parent)
    : QDialog(parent), background_color_(options.jpeg_background) {
    setObjectName(QStringLiteral("jpegExportOptionsDialog"));
    setWindowTitle(QStringLiteral("JPEG Export Options"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* quality_layout = new QHBoxLayout;

    quality_slider_ = new QSlider(Qt::Horizontal, this);
    quality_slider_->setObjectName(QStringLiteral("jpegQualitySlider"));
    quality_slider_->setRange(0, 100);
    quality_slider_->setValue(qBound(0, options.jpeg_quality, 100));
    quality_slider_->setTickPosition(QSlider::NoTicks);

    quality_spin_ = new QSpinBox(this);
    quality_spin_->setObjectName(QStringLiteral("jpegQualitySpinBox"));
    quality_spin_->setRange(0, 100);
    quality_spin_->setValue(qBound(0, options.jpeg_quality, 100));
    quality_spin_->setSuffix(QStringLiteral("%"));
    quality_spin_->setMinimumWidth(78);

    connect(quality_slider_, &QSlider::valueChanged,
            quality_spin_, &QSpinBox::setValue);
    connect(quality_spin_, qOverload<int>(&QSpinBox::valueChanged),
            quality_slider_, &QSlider::setValue);

    quality_layout->addWidget(quality_slider_, 1);
    quality_layout->addWidget(quality_spin_);
    form->addRow(QStringLiteral("Quality:"), quality_layout);

    background_button_ = new QPushButton(this);
    background_button_->setObjectName(QStringLiteral("jpegBackgroundButton"));
    connect(background_button_, &QPushButton::clicked,
            this, &JpegExportOptionsDialog::chooseBackgroundColor);
    updateBackgroundButton();
    form->addRow(QStringLiteral("Transparent areas:"), background_button_);

    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("jpegExportOptionsButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ImageExportOptions JpegExportOptionsDialog::options() const {
    ImageExportOptions result;
    result.jpeg_quality = quality_spin_->value();
    result.jpeg_background = background_color_;
    return result;
}

void JpegExportOptionsDialog::chooseBackgroundColor() {
    const QColor selected = QColorDialog::getColor(
        background_color_, this, QStringLiteral("JPEG Background Color"));
    if (!selected.isValid()) return;
    setBackgroundColor(selected);
}

void JpegExportOptionsDialog::setBackgroundColor(const QColor& color) {
    if (!color.isValid()) return;
    background_color_ = QColor(color.red(), color.green(), color.blue());
    updateBackgroundButton();
}

void JpegExportOptionsDialog::updateBackgroundButton() {
    background_button_->setText(background_color_.name(QColor::HexRgb));
    background_button_->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; }")
            .arg(background_color_.name(QColor::HexRgb)));
    background_button_->setAccessibleName(
        QStringLiteral("JPEG transparency background color %1")
            .arg(background_color_.name(QColor::HexRgb)));
}

ImageExportProgressDialog::ImageExportProgressDialog(QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("imageExportProgressDialog"));
    setWindowTitle(QStringLiteral("Exporting Image"));
    setWindowModality(Qt::WindowModal);
    setModal(true);
    setMinimumWidth(360);

    auto* layout = new QVBoxLayout(this);
    label_ = new QLabel(QStringLiteral("Rendering image…"), this);
    label_->setObjectName(QStringLiteral("imageExportProgressLabel"));
    layout->addWidget(label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setObjectName(QStringLiteral("imageExportProgressBar"));
    progress_bar_->setRange(0, 0);
    progress_bar_->setTextVisible(false);
    layout->addWidget(progress_bar_);

    auto* button_layout = new QHBoxLayout;
    button_layout->addStretch(1);
    cancel_button_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_button_->setObjectName(QStringLiteral("imageExportCancelButton"));
    connect(cancel_button_, &QPushButton::clicked,
            this, &ImageExportProgressDialog::requestCancellation);
    button_layout->addWidget(cancel_button_);
    layout->addLayout(button_layout);
}

void ImageExportProgressDialog::setPhaseText(const QString& text) {
    phase_text_ = text;
    if (!cancellation_pending_) label_->setText(text);
}

void ImageExportProgressDialog::setCancellationPending() {
    if (cancellation_pending_) return;
    cancellation_pending_ = true;
    cancel_button_->setEnabled(false);
    const QString message = phase_text_.startsWith(
        QStringLiteral("Encoding"), Qt::CaseInsensitive)
        ? QStringLiteral(
              "Finishing the current encoding so the temporary output can be discarded…")
        : QStringLiteral("Cancelling export…");
    label_->setText(message);
}

void ImageExportProgressDialog::finish() {
    finished_ = true;
    accept();
}

void ImageExportProgressDialog::reject() {
    if (finished_) {
        QDialog::reject();
        return;
    }
    requestCancellation();
}

void ImageExportProgressDialog::closeEvent(QCloseEvent* event) {
    if (finished_) {
        event->accept();
        return;
    }
    event->ignore();
    requestCancellation();
}

void ImageExportProgressDialog::requestCancellation() {
    if (cancellation_pending_ || finished_) return;
    setCancellationPending();
    emit cancelRequested();
}

} // namespace image_editor
