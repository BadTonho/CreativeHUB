#include "general_settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace motion::ui {

GeneralSettingsDialog::GeneralSettingsDialog(
    bool preview_metrics_enabled,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("General Settings"));
    setObjectName(QStringLiteral("motion-general-settings-dialog"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    preview_metrics_checkbox_ = new QCheckBox(
        QStringLiteral("Enable preview performance metrics"), this);
    preview_metrics_checkbox_->setObjectName(
        QStringLiteral("motion-preview-performance-metrics-checkbox"));
    preview_metrics_checkbox_->setChecked(preview_metrics_enabled);
    layout->addWidget(preview_metrics_checkbox_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("motion-general-settings-buttons"));
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
