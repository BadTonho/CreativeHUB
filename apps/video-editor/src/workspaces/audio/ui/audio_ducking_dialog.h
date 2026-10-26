#pragma once
#include "timeline/audio_processing.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <array>

namespace ui {
class AudioDuckingDialog final : public QDialog {
public:
    AudioDuckingDialog(const timeline::AudioDuckingSettings& settings,
        const std::vector<std::pair<std::uint64_t, QString>>& sources, std::uint64_t target, QWidget* parent)
        : QDialog(parent), original_(settings) {
        setObjectName("audioDuckingDialog"); setWindowTitle(tr("Ducking"));
        auto* layout = new QVBoxLayout(this); auto* form = new QFormLayout;
        layout->addLayout(form);
        mode_ = new QComboBox(this); mode_->setObjectName("audioDuckingMode");
        mode_->addItems({tr("Off"), tr("Automatic"), tr("Curve")});
        mode_->setCurrentIndex(static_cast<int>(settings.mode)); form->addRow(tr("Mode"), mode_);
        source_ = new QComboBox(this); source_->setObjectName("audioDuckingSource");
        source_->addItem(tr("Choose a voice track"), QVariant::fromValue<qulonglong>(0));
        for (const auto& [id, name] : sources) if (id != target) source_->addItem(name, QVariant::fromValue<qulonglong>(id));
        source_->setCurrentIndex(std::max(0, source_->findData(QVariant::fromValue<qulonglong>(settings.source_track_id.value_or(0)))));
        form->addRow(tr("Voice source"), source_);
        static constexpr const char* labels[] = {"Threshold", "Reduction", "Attack", "Hold", "Release"};
        static constexpr const char* objects[] = {"audioDuckingThreshold", "audioDuckingReduction", "audioDuckingAttack", "audioDuckingHold", "audioDuckingRelease"};
        const double values[] = {settings.threshold_db, settings.reduction_db, settings.attack_ms, settings.hold_ms, settings.release_ms};
        const double minima[] = {-60, 0, 1, 0, 10}, maxima[] = {0, 36, 1000, 2000, 5000};
        for (std::size_t index = 0; index < parameters_.size(); ++index) {
            auto* spin = new QDoubleSpinBox(this); spin->setObjectName(objects[index]);
            spin->setRange(minima[index], maxima[index]); spin->setDecimals(1); spin->setValue(values[index]);
            spin->setSuffix(index < 2 ? QStringLiteral(" dB") : QStringLiteral(" ms"));
            form->addRow(tr(labels[index]), spin); parameters_[index] = spin;
        }
        auto* hint = new QLabel(tr("Generate a curve to edit ducking points on the music track. Regenerating replaces the saved curve and can be undone."), this);
        hint->setWordWrap(true); layout->addWidget(hint);
        generate_ = new QPushButton(tr("Generate curve"), this); generate_->setObjectName("audioDuckingGenerateCurve");
        layout->addWidget(generate_);
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(buttons_);
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(generate_, &QPushButton::clicked, this, [this] { generate_requested_ = true; accept(); });
        const auto update = [this] {
            const bool has_source = source_->currentData().toULongLong() != 0;
            generate_->setEnabled(has_source);
            buttons_->button(QDialogButtonBox::Ok)->setEnabled(mode_->currentIndex() != 1 || has_source);
        };
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
        connect(source_, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
        update();
    }
    timeline::AudioDuckingSettings settings() const {
        auto value = original_; value.mode = static_cast<timeline::AudioDuckingMode>(mode_->currentIndex());
        const auto source = source_->currentData().toULongLong();
        value.source_track_id = source ? std::optional<std::uint64_t>(source) : std::nullopt;
        value.threshold_db = parameters_[0]->value(); value.reduction_db = parameters_[1]->value();
        value.attack_ms = parameters_[2]->value(); value.hold_ms = parameters_[3]->value(); value.release_ms = parameters_[4]->value();
        return value;
    }
    bool generateRequested() const noexcept { return generate_requested_; }
private:
    static QString tr(const char* text) { return QCoreApplication::translate("AudioWorkspace", text); }
    timeline::AudioDuckingSettings original_;
    QComboBox* mode_ = nullptr; QComboBox* source_ = nullptr;
    std::array<QDoubleSpinBox*, 5> parameters_{};
    QPushButton* generate_ = nullptr; QDialogButtonBox* buttons_ = nullptr;
    bool generate_requested_ = false;
};
} // namespace ui
