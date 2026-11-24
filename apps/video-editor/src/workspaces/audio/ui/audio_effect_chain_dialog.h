#pragma once

#include "timeline/timeline_model.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace ui {
namespace audio_effect_dialog_detail {
inline QString translated(const char* text) {
    return QCoreApplication::translate("AudioWorkspace", text);
}
inline QString effectName(const timeline::AudioEffectInstance& effect) {
    static constexpr const char* names[] = {
        "Equalizer", "Compressor", "Dialogue cleanup", "Noise reduction", "Audio repair",
        "Gain", "Limiter", "LFE Filter", "Delay", "Reverb",
        "Modulation", "Distortion", "Stereo", "Pitch"
    };
    return translated(names[effect.parameters.index()]);
}
inline void clearLayout(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) {
        if (item->widget() != nullptr) item->widget()->deleteLater();
        delete item;
    }
}
}  // namespace audio_effect_dialog_detail
class AudioEffectChainDialog final : public QDialog {
public:
    AudioEffectChainDialog(
        QString title,
        std::vector<timeline::AudioEffectInstance> effects,
        QWidget* parent, bool advanced = true)
        : QDialog(parent), effects_(std::move(effects)), advanced_(advanced) {
        setObjectName("audioTrackEffectsDialog");
        setWindowTitle(title);
        setMinimumSize(700, 460);

        auto* outer = new QVBoxLayout(this);
        auto* body = new QHBoxLayout;
        auto* left = new QWidget(this);
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 0, 0);
        effects_list_ = new QListWidget(left);
        effects_list_->setObjectName("audioEffectChain");
        left_layout->addWidget(effects_list_, 1);
        add_equalizer_ = new QPushButton(audio_effect_dialog_detail::translated("Add EQ"), left);
        add_equalizer_->setObjectName("audioEffectAddEqualizer");
        add_compressor_ = new QPushButton(audio_effect_dialog_detail::translated("Add compressor"), left);
        add_compressor_->setObjectName("audioEffectAddCompressor");
        auto* add_row = new QHBoxLayout;
        add_row->addWidget(add_equalizer_);
        add_row->addWidget(add_compressor_);
        left_layout->addLayout(add_row);
        static constexpr const char* labels[] = {"Add dialogue cleanup", "Add noise reduction", "Add audio repair"};
        static constexpr const char* objects[] = {"audioEffectAddCleanup", "audioEffectAddNoiseReduction", "audioEffectAddRepair"};
        for (std::size_t index = 0; index < advanced_add_.size(); ++index) {
            auto* button = new QPushButton(audio_effect_dialog_detail::translated(labels[index]), left);
            button->setObjectName(objects[index]);
            button->setVisible(advanced_);
            advanced_add_[index] = button;
            left_layout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, index] {
                timeline::AudioEffectInstance effect;
                if (index == 0) effect.parameters = timeline::AudioDialogueCleanupParameters{};
                if (index == 1) effect.parameters = timeline::AudioNoiseReductionParameters{};
                if (index == 2) effect.parameters = timeline::AudioRepairParameters{};
                addEffect(std::move(effect));
            });
        }
        auto* order_row = new QHBoxLayout;
        move_up_ = new QPushButton(audio_effect_dialog_detail::translated("Move up"), left);
        move_up_->setObjectName("audioEffectMoveUp");
        move_down_ = new QPushButton(audio_effect_dialog_detail::translated("Move down"), left);
        move_down_->setObjectName("audioEffectMoveDown");
        remove_ = new QPushButton(audio_effect_dialog_detail::translated("Remove"), left);
        remove_->setObjectName("audioEffectRemove");
        order_row->addWidget(move_up_);
        order_row->addWidget(move_down_);
        order_row->addWidget(remove_);
        left_layout->addLayout(order_row);

        auto* editor_scroll = new QScrollArea(this);
        editor_scroll->setWidgetResizable(true);
        editor_ = new QWidget(editor_scroll);
        editor_layout_ = new QVBoxLayout(editor_);
        editor_scroll->setWidget(editor_);
        body->addWidget(left, 1);
        body->addWidget(editor_scroll, 2);
        outer->addLayout(body, 1);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        outer->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(effects_list_, &QListWidget::currentRowChanged,
                this, [this](int) { rebuildEditor(); });
        connect(add_equalizer_, &QPushButton::clicked, this, [this] {
            timeline::AudioEffectInstance effect;
            effect.parameters = timeline::AudioEqualizerParameters{};
            addEffect(std::move(effect));
        });
        connect(add_compressor_, &QPushButton::clicked, this, [this] {
            timeline::AudioEffectInstance effect;
            effect.parameters = timeline::AudioCompressorParameters{};
            addEffect(std::move(effect));
        });
        connect(move_up_, &QPushButton::clicked, this,
                [this] { moveEffect(-1); });
        connect(move_down_, &QPushButton::clicked, this,
                [this] { moveEffect(1); });
        connect(remove_, &QPushButton::clicked, this, [this] {
            const auto row = effects_list_->currentRow();
            if (row < 0 || static_cast<std::size_t>(row) >= effects_.size()) return;
            effects_.erase(effects_.begin() + row);
            rebuildList(std::max(0, row - 1));
        });
        rebuildList(effects_.empty() ? -1 : 0);
    }

    [[nodiscard]] const std::vector<timeline::AudioEffectInstance>& effects() const noexcept {
        return effects_;
    }

private:
    void addEffect(timeline::AudioEffectInstance effect) {
        const bool duplicate = std::any_of(effects_.begin(), effects_.end(),
            [&effect](const auto& existing) {
                return existing.parameters.index() == effect.parameters.index();
            });
        if (duplicate) return;
        effects_.push_back(std::move(effect));
        rebuildList(static_cast<int>(effects_.size()) - 1);
    }

    void moveEffect(int delta) {
        const auto row = effects_list_->currentRow();
        const auto destination = row + delta;
        if (row < 0 || destination < 0 ||
            destination >= static_cast<int>(effects_.size())) return;
        std::swap(effects_[static_cast<std::size_t>(row)],
                  effects_[static_cast<std::size_t>(destination)]);
        rebuildList(destination);
    }

    void rebuildList(int selected_row) {
        const QSignalBlocker blocker(effects_list_);
        effects_list_->clear();
        for (const auto& effect : effects_) {
            effects_list_->addItem(audio_effect_dialog_detail::effectName(effect) + QStringLiteral(" — ") +
                (effect.enabled ? audio_effect_dialog_detail::translated("On") : audio_effect_dialog_detail::translated("Off")));
        }
        if (selected_row >= 0 && selected_row < effects_list_->count())
            effects_list_->setCurrentRow(selected_row);
        else
            effects_list_->setCurrentRow(-1);
        updateActions();
        rebuildEditor();
    }

    void updateActions() {
        const auto row = effects_list_->currentRow();
        const bool has_selection = row >= 0 &&
            static_cast<std::size_t>(row) < effects_.size() &&
            (advanced_ || effects_[static_cast<std::size_t>(row)].parameters.index() < 2);
        const bool has_eq = std::any_of(effects_.begin(), effects_.end(),
            [](const auto& effect) {
                return std::holds_alternative<timeline::AudioEqualizerParameters>(
                    effect.parameters);
            });
        const bool has_compressor = std::any_of(effects_.begin(), effects_.end(),
            [](const auto& effect) {
                return std::holds_alternative<timeline::AudioCompressorParameters>(
                    effect.parameters);
            });
        add_equalizer_->setEnabled(!has_eq);
        add_compressor_->setEnabled(!has_compressor);
        for (std::size_t kind = 0; kind < advanced_add_.size(); ++kind)
            advanced_add_[kind]->setEnabled(advanced_ && std::none_of(effects_.begin(), effects_.end(),
                [kind](const auto& effect) { return effect.parameters.index() == kind + 2; }));
        remove_->setEnabled(has_selection);
        move_up_->setEnabled(has_selection && row > 0);
        move_down_->setEnabled(has_selection && row + 1 <
            static_cast<int>(effects_.size()));
    }

    void addDoubleControl(
        QFormLayout* form,
        const char* label,
        const char* object_name,
        double minimum,
        double maximum,
        int decimals,
        const QString& suffix,
        double value,
        std::function<void(double)> setter) {
        auto* control = new QDoubleSpinBox(editor_);
        control->setObjectName(object_name);
        control->setRange(minimum, maximum);
        control->setDecimals(decimals);
        control->setSingleStep(decimals == 0 ? 1.0 : 0.1);
        control->setSuffix(suffix);
        control->setValue(value);
        form->addRow(audio_effect_dialog_detail::translated(label), control);
        connect(control, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [setter = std::move(setter)](double next) { setter(next); });
    }

    void rebuildEditor() {
        audio_effect_dialog_detail::clearLayout(editor_layout_);
        const auto row = effects_list_->currentRow();
        if (row < 0 || static_cast<std::size_t>(row) >= effects_.size()) {
            editor_layout_->addWidget(new QLabel(
                audio_effect_dialog_detail::translated("Add an effect to edit its settings."), editor_));
            editor_layout_->addStretch(1);
            updateActions();
            return;
        }
        const auto index = static_cast<std::size_t>(row);
        if (!advanced_ && effects_[index].parameters.index() >= 2) {
            editor_layout_->addWidget(new QLabel(audio_effect_dialog_detail::translated("Edit this effect in the Audio workspace."), editor_));
            editor_layout_->addStretch(1); updateActions(); return;
        }
        auto* enabled = new QCheckBox(audio_effect_dialog_detail::translated("Enabled"), editor_);
        enabled->setObjectName("audioEffectEnabled");
        enabled->setChecked(effects_[index].enabled);
        editor_layout_->addWidget(enabled);
        connect(enabled, &QCheckBox::toggled, this, [this, index](bool value) {
            if (index >= effects_.size()) return;
            effects_[index].enabled = value;
            updateEffectItem(index);
        });

        auto* form = new QFormLayout;
        editor_layout_->addLayout(form);
        if (auto* eq = std::get_if<timeline::AudioEqualizerParameters>(
                &effects_[index].parameters)) {
            addDoubleControl(form, "Low shelf frequency", "audioEqLowFrequency",
                20.0, 500.0, 1, QStringLiteral(" Hz"), eq->low_shelf_frequency_hz,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).low_shelf_frequency_hz = value;
                });
            addDoubleControl(form, "Low shelf gain", "audioEqLowGain",
                -18.0, 18.0, 1, QStringLiteral(" dB"), eq->low_shelf_gain_db,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).low_shelf_gain_db = value;
                });
            addDoubleControl(form, "Mid frequency", "audioEqMidFrequency",
                20.0, 20000.0, 1, QStringLiteral(" Hz"), eq->mid_frequency_hz,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).mid_frequency_hz = value;
                });
            addDoubleControl(form, "Mid gain", "audioEqMidGain",
                -18.0, 18.0, 1, QStringLiteral(" dB"), eq->mid_gain_db,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).mid_gain_db = value;
                });
            addDoubleControl(form, "Mid Q", "audioEqMidQ",
                0.1, 10.0, 2, {}, eq->mid_q,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).mid_q = value;
                });
            addDoubleControl(form, "High shelf frequency", "audioEqHighFrequency",
                500.0, 20000.0, 1, QStringLiteral(" Hz"), eq->high_shelf_frequency_hz,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).high_shelf_frequency_hz = value;
                });
            addDoubleControl(form, "High shelf gain", "audioEqHighGain",
                -18.0, 18.0, 1, QStringLiteral(" dB"), eq->high_shelf_gain_db,
                [this, index](double value) {
                    std::get<timeline::AudioEqualizerParameters>(
                        effects_[index].parameters).high_shelf_gain_db = value;
                });
        } else if (auto* compressor =
                       std::get_if<timeline::AudioCompressorParameters>(
                           &effects_[index].parameters)) {
            addDoubleControl(form, "Threshold", "audioCompressorThreshold",
                -60.0, 0.0, 1, QStringLiteral(" dB"), compressor->threshold_db,
                [this, index](double value) {
                    std::get<timeline::AudioCompressorParameters>(
                        effects_[index].parameters).threshold_db = value;
                });
            addDoubleControl(form, "Ratio", "audioCompressorRatio",
                1.0, 20.0, 1, QStringLiteral(":1"), compressor->ratio,
                [this, index](double value) {
                    std::get<timeline::AudioCompressorParameters>(
                        effects_[index].parameters).ratio = value;
                });
            addDoubleControl(form, "Attack", "audioCompressorAttack",
                0.1, 200.0, 1, QStringLiteral(" ms"), compressor->attack_ms,
                [this, index](double value) {
                    std::get<timeline::AudioCompressorParameters>(
                        effects_[index].parameters).attack_ms = value;
                });
            addDoubleControl(form, "Release", "audioCompressorRelease",
                10.0, 1000.0, 1, QStringLiteral(" ms"), compressor->release_ms,
                [this, index](double value) {
                    std::get<timeline::AudioCompressorParameters>(
                        effects_[index].parameters).release_ms = value;
                });
            addDoubleControl(form, "Makeup gain", "audioCompressorMakeup",
                0.0, 24.0, 1, QStringLiteral(" dB"), compressor->makeup_gain_db,
                [this, index](double value) {
                    std::get<timeline::AudioCompressorParameters>(
                        effects_[index].parameters).makeup_gain_db = value;
                });
        }
        if (auto* cleanup = std::get_if<timeline::AudioDialogueCleanupParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "High-pass frequency", "audioCleanupHighpass", 20, 300, 1, QStringLiteral(" Hz"),
                cleanup->highpass_frequency_hz, [this, index](double value) { std::get<timeline::AudioDialogueCleanupParameters>(effects_[index].parameters).highpass_frequency_hz = value; });
            addDoubleControl(form, "De-esser intensity", "audioCleanupDeesser", 0, 100, 0, QStringLiteral("%"),
                cleanup->deesser_intensity * 100, [this, index](double value) { std::get<timeline::AudioDialogueCleanupParameters>(effects_[index].parameters).deesser_intensity = value / 100; });
        } else if (auto* noise = std::get_if<timeline::AudioNoiseReductionParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Noise reduction", "audioNoiseReduction", 0, 30, 1, QStringLiteral(" dB"),
                noise->reduction_db, [this, index](double value) { std::get<timeline::AudioNoiseReductionParameters>(effects_[index].parameters).reduction_db = value; });
            addDoubleControl(form, "Noise floor", "audioNoiseFloor", -80, -20, 1, QStringLiteral(" dB"),
                noise->noise_floor_db, [this, index](double value) { std::get<timeline::AudioNoiseReductionParameters>(effects_[index].parameters).noise_floor_db = value; });
        } else if (auto* repair = std::get_if<timeline::AudioRepairParameters>(&effects_[index].parameters)) {
            auto* hum = new QComboBox(editor_); hum->setObjectName("audioRepairHum");
            hum->addItem(audio_effect_dialog_detail::translated("Off"), 0); hum->addItem("50 Hz", 50); hum->addItem("60 Hz", 60);
            hum->setCurrentIndex(hum->findData(repair->hum_frequency_hz));
            form->addRow(audio_effect_dialog_detail::translated("Hum removal"), hum);
            connect(hum, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, hum](int) {
                std::get<timeline::AudioRepairParameters>(effects_[index].parameters).hum_frequency_hz = hum->currentData().toInt();
            });
            auto* declick = new QCheckBox(audio_effect_dialog_detail::translated("Remove clicks"), editor_);
            declick->setObjectName("audioRepairDeclick"); declick->setChecked(repair->declick_enabled); form->addRow(declick);
            connect(declick, &QCheckBox::toggled, this, [this, index](bool value) {
                std::get<timeline::AudioRepairParameters>(effects_[index].parameters).declick_enabled = value;
            });
            addDoubleControl(form, "Click detection threshold", "audioRepairSensitivity", 1, 10, 1, {},
                repair->declick_sensitivity, [this, index](double value) { std::get<timeline::AudioRepairParameters>(effects_[index].parameters).declick_sensitivity = value; });
        } else if (auto* gain = std::get_if<timeline::AudioGainParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Gain", "audioFxGainDb", -24.0, 24.0, 1, QStringLiteral(" dB"),
                gain->gain_db, [this, index](double v) { std::get<timeline::AudioGainParameters>(effects_[index].parameters).gain_db = v; });
            auto* inv = new QCheckBox(audio_effect_dialog_detail::translated("Invert phase"), editor_);
            inv->setChecked(gain->invert_phase);
            form->addRow(inv);
            connect(inv, &QCheckBox::toggled, this, [this, index](bool v) {
                std::get<timeline::AudioGainParameters>(effects_[index].parameters).invert_phase = v;
            });
        } else if (auto* limiter = std::get_if<timeline::AudioLimiterParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Ceiling", "audioFxLimiterCeiling", -12.0, 0.0, 1, QStringLiteral(" dB"),
                limiter->ceiling_db, [this, index](double v) { std::get<timeline::AudioLimiterParameters>(effects_[index].parameters).ceiling_db = v; });
            addDoubleControl(form, "Release", "audioFxLimiterRelease", 5.0, 500.0, 0, QStringLiteral(" ms"),
                limiter->release_ms, [this, index](double v) { std::get<timeline::AudioLimiterParameters>(effects_[index].parameters).release_ms = v; });
            auto* soft = new QCheckBox(audio_effect_dialog_detail::translated("Soft clip"), editor_);
            soft->setChecked(limiter->soft_clip);
            form->addRow(soft);
            connect(soft, &QCheckBox::toggled, this, [this, index](bool v) {
                std::get<timeline::AudioLimiterParameters>(effects_[index].parameters).soft_clip = v;
            });
        } else if (auto* lfe = std::get_if<timeline::AudioLfeFilterParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Cutoff frequency", "audioFxLfeCutoff", 40.0, 250.0, 0, QStringLiteral(" Hz"),
                lfe->cutoff_hz, [this, index](double v) { std::get<timeline::AudioLfeFilterParameters>(effects_[index].parameters).cutoff_hz = v; });
        } else if (auto* delay = std::get_if<timeline::AudioDelayParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Delay time", "audioFxDelayMs", 1.0, 2000.0, 0, QStringLiteral(" ms"),
                delay->delay_ms, [this, index](double v) { std::get<timeline::AudioDelayParameters>(effects_[index].parameters).delay_ms = v; });
            addDoubleControl(form, "Feedback", "audioFxDelayFeedback", 0.0, 90.0, 0, QStringLiteral("%"),
                delay->feedback * 100.0, [this, index](double v) { std::get<timeline::AudioDelayParameters>(effects_[index].parameters).feedback = v / 100.0; });
            addDoubleControl(form, "Mix", "audioFxDelayMix", 0.0, 100.0, 0, QStringLiteral("%"),
                delay->mix * 100.0, [this, index](double v) { std::get<timeline::AudioDelayParameters>(effects_[index].parameters).mix = v / 100.0; });
        } else if (auto* reverb = std::get_if<timeline::AudioReverbParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Room size", "audioFxReverbRoom", 0.0, 100.0, 0, QStringLiteral("%"),
                reverb->room_size * 100.0, [this, index](double v) { std::get<timeline::AudioReverbParameters>(effects_[index].parameters).room_size = v / 100.0; });
            addDoubleControl(form, "Damping", "audioFxReverbDamping", 0.0, 100.0, 0, QStringLiteral("%"),
                reverb->damping * 100.0, [this, index](double v) { std::get<timeline::AudioReverbParameters>(effects_[index].parameters).damping = v / 100.0; });
            addDoubleControl(form, "Mix", "audioFxReverbMix", 0.0, 100.0, 0, QStringLiteral("%"),
                reverb->mix * 100.0, [this, index](double v) { std::get<timeline::AudioReverbParameters>(effects_[index].parameters).mix = v / 100.0; });
        } else if (auto* mod = std::get_if<timeline::AudioModulationParameters>(&effects_[index].parameters)) {
            auto* mode = new QComboBox(editor_);
            mode->addItem(audio_effect_dialog_detail::translated("Chorus"), 0);
            mode->addItem(audio_effect_dialog_detail::translated("Flanger"), 1);
            mode->addItem(audio_effect_dialog_detail::translated("Modulation"), 2);
            mode->setCurrentIndex(mode->findData(mod->mode));
            form->addRow(audio_effect_dialog_detail::translated("Mode"), mode);
            connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, mode](int) {
                std::get<timeline::AudioModulationParameters>(effects_[index].parameters).mode = mode->currentData().toInt();
            });
            addDoubleControl(form, "Rate", "audioFxModRate", 0.1, 10.0, 1, QStringLiteral(" Hz"),
                mod->rate_hz, [this, index](double v) { std::get<timeline::AudioModulationParameters>(effects_[index].parameters).rate_hz = v; });
            addDoubleControl(form, "Depth", "audioFxModDepth", 0.0, 100.0, 0, QStringLiteral("%"),
                mod->depth * 100.0, [this, index](double v) { std::get<timeline::AudioModulationParameters>(effects_[index].parameters).depth = v / 100.0; });
            addDoubleControl(form, "Mix", "audioFxModMix", 0.0, 100.0, 0, QStringLiteral("%"),
                mod->mix * 100.0, [this, index](double v) { std::get<timeline::AudioModulationParameters>(effects_[index].parameters).mix = v / 100.0; });
        } else if (auto* dist = std::get_if<timeline::AudioDistortionParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Drive", "audioFxDistDrive", 0.0, 100.0, 0, QStringLiteral("%"),
                dist->drive * 100.0, [this, index](double v) { std::get<timeline::AudioDistortionParameters>(effects_[index].parameters).drive = v / 100.0; });
            addDoubleControl(form, "Mix", "audioFxDistMix", 0.0, 100.0, 0, QStringLiteral("%"),
                dist->mix * 100.0, [this, index](double v) { std::get<timeline::AudioDistortionParameters>(effects_[index].parameters).mix = v / 100.0; });
        } else if (auto* stereo = std::get_if<timeline::AudioStereoParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Stereo width", "audioFxStereoWidth", 0.0, 200.0, 0, QStringLiteral("%"),
                stereo->width * 100.0, [this, index](double v) { std::get<timeline::AudioStereoParameters>(effects_[index].parameters).width = v / 100.0; });
            addDoubleControl(form, "Balance", "audioFxStereoBalance", -100.0, 100.0, 0, QStringLiteral("%"),
                stereo->balance * 100.0, [this, index](double v) { std::get<timeline::AudioStereoParameters>(effects_[index].parameters).balance = v / 100.0; });
            auto* mono = new QCheckBox(audio_effect_dialog_detail::translated("Mono sum"), editor_);
            mono->setChecked(stereo->mono_sum);
            form->addRow(mono);
            connect(mono, &QCheckBox::toggled, this, [this, index](bool v) {
                std::get<timeline::AudioStereoParameters>(effects_[index].parameters).mono_sum = v;
            });
        } else if (auto* pitch = std::get_if<timeline::AudioPitchParameters>(&effects_[index].parameters)) {
            addDoubleControl(form, "Pitch shift", "audioFxPitchSemitones", -12.0, 12.0, 1, QStringLiteral(" st"),
                pitch->semitones, [this, index](double v) { std::get<timeline::AudioPitchParameters>(effects_[index].parameters).semitones = v; });
        }
        editor_layout_->addStretch(1);
        updateActions();
    }

    void updateEffectItem(std::size_t index) {
        if (index >= effects_.size()) return;
        auto* item = effects_list_->item(static_cast<int>(index));
        if (item != nullptr) {
            item->setText(audio_effect_dialog_detail::effectName(effects_[index]) + QStringLiteral(" — ") +
                (effects_[index].enabled ? audio_effect_dialog_detail::translated("On") : audio_effect_dialog_detail::translated("Off")));
        }
    }

    std::vector<timeline::AudioEffectInstance> effects_;
    bool advanced_ = true;
    std::array<QPushButton*, 3> advanced_add_{};
    QListWidget* effects_list_ = nullptr;
    QWidget* editor_ = nullptr;
    QVBoxLayout* editor_layout_ = nullptr;
    QPushButton* add_equalizer_ = nullptr;
    QPushButton* add_compressor_ = nullptr;
    QPushButton* move_up_ = nullptr;
    QPushButton* move_down_ = nullptr;
    QPushButton* remove_ = nullptr;
};
}  // namespace ui