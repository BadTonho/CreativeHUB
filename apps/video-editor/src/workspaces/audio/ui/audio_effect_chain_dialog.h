#pragma once

#include "timeline/timeline_model.h"

#include <QCheckBox>
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
    return std::holds_alternative<timeline::AudioEqualizerParameters>(
        effect.parameters) ? translated("Equalizer") : translated("Compressor");
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
        QWidget* parent)
        : QDialog(parent), effects_(std::move(effects)) {
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
            static_cast<std::size_t>(row) < effects_.size();
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