#pragma once
#include "timeline/audio_processing.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QPointer>
#include <QSlider>
#include <QVBoxLayout>
#include <functional>

namespace ui {
class VoiceIsolationPanel final : public QGroupBox {
public:
    explicit VoiceIsolationPanel(QWidget* parent) : QGroupBox(tr("Voice isolation"), parent) {
        setObjectName("audioVoiceIsolationPanel");
        auto* layout = new QVBoxLayout(this);
        state_ = new QLabel(tr("Off"), this); state_->setObjectName("audioVoiceIsolationState"); layout->addWidget(state_);
        progress_ = new QProgressBar(this); progress_->setObjectName("audioVoiceIsolationProgress"); progress_->setRange(0, 100); layout->addWidget(progress_);
        auto* row = new QHBoxLayout; layout->addLayout(row);
        auto* configure = new QPushButton(tr("Configure voice isolation…"), this); configure->setObjectName("audioVoiceIsolationConfigure"); row->addWidget(configure);
        regenerate_ = new QPushButton(tr("Generate again"), this); regenerate_->setObjectName("audioVoiceIsolationRegenerate"); row->addWidget(regenerate_);
        cancel_ = new QPushButton(tr("Cancel processing"), this); cancel_->setObjectName("audioVoiceIsolationCancel"); row->addWidget(cancel_);
        cancel_->setToolTip(tr("Cancel the current voice isolation queue."));
        connect(regenerate_, &QPushButton::clicked, this, [this] { if (regenerate) regenerate(); });
        connect(cancel_, &QPushButton::clicked, this, [this] { if (cancel) cancel(); });
        connect(configure, &QPushButton::clicked, this, [this] {
            const auto original = settings_; const auto submit = apply; QPointer<VoiceIsolationPanel> guard(this);
            QDialog dialog(window()); dialog.setObjectName("audioVoiceIsolationDialog"); dialog.setWindowTitle(tr("Voice isolation"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* enabled = new QCheckBox(tr("Enabled"), &dialog); enabled->setObjectName("audioVoiceIsolationEnabled"); enabled->setChecked(settings_.enabled); layout->addWidget(enabled);
            auto* amount = new QSlider(Qt::Horizontal, &dialog); amount->setObjectName("audioVoiceIsolationAmount"); amount->setRange(0, 100); amount->setValue(qRound(settings_.amount * 100));
            auto* value = new QLabel(&dialog); layout->addWidget(value); layout->addWidget(amount);
            const auto update = [value](int percent) { value->setText(tr("Intensity: %1%").arg(percent)); };
            update(amount->value()); connect(amount, &QSlider::valueChanged, &dialog, update);
            auto* hint = new QLabel(tr("Reduces background sounds around speech. The original audio is preserved."), &dialog); hint->setWordWrap(true); layout->addWidget(hint);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() == QDialog::Accepted && guard && submit) { auto next = original; next.enabled = enabled->isChecked(); next.amount = amount->value() / 100.0; submit(std::move(next)); }
        });
        setState("Off", 0);
    }
    void setSettings(timeline::VoiceIsolationSettings settings) { settings_ = std::move(settings); regenerate_->setEnabled(settings_.enabled); }
    void setState(const QString& state, int progress) {
        state_->setText(tr(state.toUtf8().constData())); progress_->setValue(state == "Ready" ? 100 : progress);
        progress_->setVisible(state == "Processing"); cancel_->setEnabled(state == "Processing");
    }
    std::function<void(timeline::VoiceIsolationSettings)> apply;
    std::function<void()> regenerate, cancel;
private:
    static QString tr(const char* text) { return QCoreApplication::translate("AudioWorkspace", text); }
    timeline::VoiceIsolationSettings settings_;
    QLabel* state_; QProgressBar* progress_; QPushButton *regenerate_, *cancel_;
};
} // namespace ui
