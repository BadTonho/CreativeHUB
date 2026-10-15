#include "ui/translation_helpers.h"
#include "general_settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace motion::ui {

GeneralSettingsDialog::GeneralSettingsDialog(
    bool preview_metrics_enabled,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(MotionUiTranslation::tr("General Settings"));
    setObjectName(QStringLiteral("motion-general-settings-dialog"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    preview_metrics_checkbox_ = new QCheckBox(
        MotionUiTranslation::tr("Enable preview performance metrics"), this);
    preview_metrics_checkbox_->setObjectName(
        QStringLiteral("motion-preview-performance-metrics-checkbox"));
    preview_metrics_checkbox_->setChecked(preview_metrics_enabled);
    layout->addWidget(preview_metrics_checkbox_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("motion-general-settings-buttons"));
    buttons->button(QDialogButtonBox::Close)->setText(MotionUiTranslation::tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    connect(preview_metrics_checkbox_, &QCheckBox::toggled,
            this, &GeneralSettingsDialog::previewMetricsEnabledChanged);
}

bool GeneralSettingsDialog::previewMetricsEnabled() const noexcept
{
    return preview_metrics_checkbox_->isChecked();
}

} // namespace motion::ui
