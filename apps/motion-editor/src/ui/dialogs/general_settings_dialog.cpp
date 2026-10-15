#include "ui/translation_helpers.h"
#include "general_settings_dialog.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/translations/language_preference.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
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
    using creative_suite::translations::LanguagePreference;
    const auto saved_language = creative_suite::translations::loadLanguagePreference(
        QStringLiteral("motion-editor"));
    auto* language_row = new QWidget(this);
    auto* language_layout = new QHBoxLayout(language_row);
    language_layout->setContentsMargins(0, 0, 0, 0);
    auto* language_label = new QLabel(MotionUiTranslation::tr("Language:"), language_row);
    language_combo_ = new QComboBox(language_row);
    language_combo_->setObjectName(QStringLiteral("motion-language-preference-combo"));
    language_combo_->addItem(MotionUiTranslation::tr("System language"),
                             static_cast<int>(LanguagePreference::System));
    language_combo_->addItem(MotionUiTranslation::tr("English"),
                             static_cast<int>(LanguagePreference::English));
    language_combo_->addItem(MotionUiTranslation::tr("Português (Brasil)"),
                             static_cast<int>(LanguagePreference::PortugueseBrazil));
    language_combo_->setCurrentIndex(language_combo_->findData(
        static_cast<int>(saved_language)));
    language_layout->addWidget(language_label);
    language_layout->addWidget(language_combo_, 1);
    layout->addWidget(language_row);
    auto* language_note = new QLabel(
        MotionUiTranslation::tr("Changes take effect the next time you open this app."), this);
    language_note->setObjectName(QStringLiteral("motion-language-preference-note"));
    language_note->setWordWrap(true);
    layout->addWidget(language_note);
    connect(language_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, language_note](int index) {
        const auto selected = static_cast<LanguagePreference>(
            language_combo_->itemData(index).toInt());
        QString error;
        if (!creative_suite::translations::saveLanguagePreference(
                QStringLiteral("motion-editor"), selected, &error)) {
            const auto detail = error.toUtf8();
            auto& logger = creative_suite::diagnostics::Logger::instance();
            if (!logger.is_initialized()) {
                static_cast<void>(logger.initialize_default("motion-studio"));
            }
            logger.log(
                creative_suite::diagnostics::Level::Warning,
                "settings", "save_language_preference", detail.constData(),
                {{"application", "motion-editor"}});
            const QSignalBlocker blocker(language_combo_);
            const auto saved_language = creative_suite::translations::loadLanguagePreference(
                QStringLiteral("motion-editor"));
            language_combo_->setCurrentIndex(language_combo_->findData(
                static_cast<int>(saved_language)));
            language_note->setText(MotionUiTranslation::tr(
                "The language preference could not be saved. See the application log."));
            return;
        }
        language_note->setText(MotionUiTranslation::tr(
            "Changes take effect the next time you open this app."));
    });

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
