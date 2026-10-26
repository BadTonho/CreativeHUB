#include "workspaces/audio/ui/audio_workspace.h"
#include "workspaces/audio/ui/audio_effect_chain_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace ui {
namespace {

QString translated(const char* text) {
    return QCoreApplication::translate("AudioWorkspace", text);
}

QString gainText(int percent) {
    if (percent == 0) return QStringLiteral("-inf dB");
    return QString::number(20.0 * std::log10(percent / 100.0), 'f', 1) +
        QStringLiteral(" dB");
}

QString panText(int percent) {
    if (percent == 0) return translated("Center");
    return (percent < 0 ? translated("L") : translated("R")) +
        QStringLiteral(" ") + QString::number(std::abs(percent)) + QStringLiteral("%");
}

QProgressBar* createMeter(QWidget* parent, const char* object_name) {
    auto* meter = new QProgressBar(parent);
    meter->setObjectName(object_name);
    meter->setRange(0, 100);
    meter->setValue(0);
    meter->setOrientation(Qt::Vertical);
    meter->setTextVisible(false);
    meter->setFixedWidth(22);
    meter->setStyleSheet(
        "QProgressBar { background: #172028; border: 1px solid #344451; }"
        "QProgressBar::chunk { background: #39ad77; }");
    return meter;
}

void setGainSlider(QSlider* slider, QLabel* label, double gain) {
    const auto percent = static_cast<int>(std::lround(
        std::clamp(gain, 0.0, 2.0) * 100.0));
    const QSignalBlocker blocker(slider);
    slider->setValue(percent);
    label->setText(gainText(percent));
}

void setPanSlider(QSlider* slider, QLabel* label, double pan) {
    const auto percent = static_cast<int>(std::lround(
        std::clamp(pan, -1.0, 1.0) * 100.0));
    const QSignalBlocker blocker(slider);
    slider->setValue(percent);
    label->setText(panText(percent));
}

QSlider* createGainSlider(QWidget* parent, const char* object_name) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(object_name);
    slider->setRange(0, 200);
    return slider;
}

QSlider* createPanSlider(QWidget* parent, const char* object_name) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(object_name);
    slider->setRange(-100, 100);
    slider->setValue(0);
    return slider;
}

void clearLayout(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) {
        if (item->widget() != nullptr) item->widget()->deleteLater();
        delete item;
    }
}

void fillBusCombo(QComboBox* combo,
                  const std::vector<timeline::AudioBus>& buses,
                  std::optional<timeline::AudioBusId> selected,
                  std::optional<timeline::AudioBusId> excluded = std::nullopt) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(translated("Master"), QVariant::fromValue<qulonglong>(0));
    for (const auto& bus : buses) {
        if (excluded == bus.bus_id) continue;
        combo->addItem(QString::fromUtf8(bus.name.c_str()),
                       QVariant::fromValue<qulonglong>(bus.bus_id));
    }
    const auto desired = selected.value_or(0);
    const auto index = combo->findData(QVariant::fromValue<qulonglong>(desired));
    combo->setCurrentIndex(index >= 0 ? index : 0);
}



}  // namespace

AudioWorkspace::AudioWorkspace(QObject* parent) : QObject(parent) {}

void AudioWorkspace::createPanels(QWidget* parent) {
    meter_page_ = new QWidget(parent);
    meter_page_->setObjectName("audioMeterPage");
    auto* meter_outer = new QVBoxLayout(meter_page_);
    meter_outer->setContentsMargins(12, 12, 12, 12);
    meter_outer->addWidget(new QLabel(translated("Audio levels"), meter_page_));
    auto* meter_scroll = new QScrollArea(meter_page_);
    meter_scroll->setWidgetResizable(true);
    auto* meter_content = new QWidget(meter_scroll);
    meter_layout_ = new QHBoxLayout(meter_content);
    meter_layout_->setAlignment(Qt::AlignLeft);
    meter_scroll->setWidget(meter_content);
    meter_outer->addWidget(meter_scroll, 1);

    auto* master_card = new QGroupBox(translated("Master"), meter_content);
    auto* master_layout = new QVBoxLayout(master_card);
    master_meter_ = createMeter(master_card, "audioOutputMeter");
    master_layout->addWidget(master_meter_, 1, Qt::AlignHCenter);
    meter_layout_->addWidget(master_card);

    mixer_panel_ = new QScrollArea(parent);
    mixer_panel_->setObjectName("audioMixerPanel");
    auto* mixer_scroll = static_cast<QScrollArea*>(mixer_panel_);
    mixer_scroll->setWidgetResizable(true);
    auto* mixer_content = new QWidget(mixer_scroll);
    mixer_layout_ = new QVBoxLayout(mixer_content);
    mixer_layout_->setAlignment(Qt::AlignTop);
    mixer_layout_->addWidget(new QLabel(translated("Mixer"), mixer_content));

    auto* output_card = new QGroupBox(translated("Master"), mixer_content);
    output_card->setObjectName("audioOutputMixerCard");
    auto* output_layout = new QVBoxLayout(output_card);
    mixer_master_meter_ = new QProgressBar(output_card);
    mixer_master_meter_->setObjectName("audioOutputMixerMeter");
    mixer_master_meter_->setRange(0, 100);
    mixer_master_meter_->setValue(0);
    mixer_master_meter_->setTextVisible(false);
    output_layout->addWidget(mixer_master_meter_);
    master_gain_ = createGainSlider(output_card, "audioMasterGain");
    output_layout->addWidget(master_gain_);
    master_gain_value_ = new QLabel(gainText(100), output_card);
    output_layout->addWidget(master_gain_value_);
    master_mute_ = new QCheckBox(translated("Mute"), output_card);
    master_mute_->setObjectName("audioMasterMute");
    output_layout->addWidget(master_mute_);
    mixer_layout_->addWidget(output_card);
    connect(master_gain_, &QSlider::sliderPressed, this,
            &AudioWorkspace::gainEditStarted);
    connect(master_gain_, &QSlider::valueChanged, this, [this](int value) {
        master_gain_value_->setText(gainText(value));
        emit masterMixChanged(value / 100.0, master_mute_->isChecked());
    });
    connect(master_gain_, &QSlider::sliderReleased, this,
            &AudioWorkspace::gainEditFinished);
    connect(master_mute_, &QCheckBox::toggled, this, [this](bool muted) {
        emit masterMixChanged(master_gain_->value() / 100.0, muted);
    });

    auto* bus_section = new QWidget(mixer_content);
    auto* bus_section_layout = new QVBoxLayout(bus_section);
    bus_section_layout->setContentsMargins(0, 0, 0, 0);
    auto* bus_header = new QHBoxLayout;
    bus_header->addWidget(new QLabel(translated("Submix buses"), bus_section));
    add_bus_button_ = new QPushButton(translated("Add bus"), bus_section);
    add_bus_button_->setObjectName("audioAddBus");
    bus_header->addWidget(add_bus_button_);
    bus_section_layout->addLayout(bus_header);
    bus_mixer_layout_ = new QVBoxLayout;
    bus_section_layout->addLayout(bus_mixer_layout_);
    mixer_layout_->addWidget(bus_section);
    connect(add_bus_button_, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const auto name = QInputDialog::getText(
            mixer_panel_, translated("Add bus"), translated("Bus name"),
            QLineEdit::Normal, translated("Submix"), &accepted);
        if (accepted && !name.trimmed().isEmpty())
            emit createBusRequested(name.trimmed());
    });

    auto* tracks_section = new QWidget(mixer_content);
    auto* tracks_section_layout = new QVBoxLayout(tracks_section);
    tracks_section_layout->setContentsMargins(0, 0, 0, 0);
    tracks_section_layout->addWidget(new QLabel(translated("Audio tracks"), tracks_section));
    track_mixer_layout_ = new QVBoxLayout;
    tracks_section_layout->addLayout(track_mixer_layout_);
    mixer_layout_->addWidget(tracks_section);

    auto* effects_section = new QWidget(mixer_content);
    effects_section->setObjectName("audioTrackEffectsSection");
    auto* effects_section_layout = new QVBoxLayout(effects_section);
    effects_section_layout->setContentsMargins(0, 0, 0, 0);
    effects_section_layout->addWidget(
        new QLabel(translated("Track audio effects"), effects_section));
    track_effects_layout_ = new QVBoxLayout;
    effects_section_layout->addLayout(track_effects_layout_);
    mixer_layout_->addWidget(effects_section);
    mixer_scroll->setWidget(mixer_content);
}

void AudioWorkspace::refreshTracks(
    const std::vector<timeline::TimelineTrack>& tracks,
    const std::vector<timeline::AudioBus>& buses,
    const timeline::MasterAudioMix& master) {
    if (meter_page_ == nullptr || mixer_panel_ == nullptr) return;
    std::vector<const timeline::TimelineTrack*> audio_tracks;
    for (const auto& track : tracks) {
        if (track.kind == timeline::TrackKind::Audio) audio_tracks.push_back(&track);
    }

    const bool same_tracks = audio_tracks.size() == strips_.size() &&
        std::equal(audio_tracks.begin(), audio_tracks.end(), strips_.begin(),
            [](const auto* track, const Strip& strip) {
                return track->track_id == strip.id;
            });
    if (!same_tracks) {
        for (auto& strip : strips_) {
            delete strip.meter_card;
            delete strip.mixer_card;
        }
        strips_.clear();
        for (const auto* track : audio_tracks) {
            Strip strip;
            strip.id = track->track_id;
            strip.meter_card = new QGroupBox(meter_page_);
            strip.meter_card->setObjectName("audioTrackMeterCard");
            auto* meter_layout = new QVBoxLayout(strip.meter_card);
            strip.meter_name = new QLabel(strip.meter_card);
            meter_layout->addWidget(strip.meter_name);
            strip.meter = createMeter(strip.meter_card, "audioTrackMeter");
            meter_layout->addWidget(strip.meter, 1, Qt::AlignHCenter);
            meter_layout_->addWidget(strip.meter_card);

            strip.mixer_card = new QGroupBox(mixer_panel_);
            strip.mixer_card->setObjectName("audioTrackMixerCard");
            auto* layout = new QVBoxLayout(strip.mixer_card);
            strip.mixer_name = new QLabel(strip.mixer_card);
            layout->addWidget(strip.mixer_name);
            strip.mixer_meter = createMeter(strip.mixer_card, "audioTrackMixerMeter");
            strip.mixer_meter->setOrientation(Qt::Horizontal);
            strip.mixer_meter->setFixedWidth(160);
            layout->addWidget(strip.mixer_meter);
            strip.gain = createGainSlider(strip.mixer_card, "audioTrackGain");
            layout->addWidget(strip.gain);
            strip.gain_value = new QLabel(strip.mixer_card);
            layout->addWidget(strip.gain_value);
            strip.pan = createPanSlider(strip.mixer_card, "audioTrackPan");
            layout->addWidget(strip.pan);
            strip.pan_value = new QLabel(strip.mixer_card);
            layout->addWidget(strip.pan_value);
            strip.mute = new QCheckBox(translated("Mute"), strip.mixer_card);
            strip.mute->setObjectName("audioTrackMute");
            strip.solo = new QCheckBox(translated("Solo"), strip.mixer_card);
            strip.solo->setObjectName("audioTrackSolo");
            auto* toggle_row = new QHBoxLayout;
            toggle_row->addWidget(strip.mute);
            toggle_row->addWidget(strip.solo);
            layout->addLayout(toggle_row);
            layout->addWidget(new QLabel(translated("Output"), strip.mixer_card));
            strip.output = new QComboBox(strip.mixer_card);
            strip.output->setObjectName("audioTrackOutput");
            layout->addWidget(strip.output);
            auto* sends_header = new QHBoxLayout;
            sends_header->addWidget(new QLabel(translated("Sends"), strip.mixer_card));
            strip.send_destination = new QComboBox(strip.mixer_card);
            strip.send_destination->setObjectName("audioTrackSendDestination");
            sends_header->addWidget(strip.send_destination, 1);
            strip.add_send = new QPushButton(translated("Add send"), strip.mixer_card);
            strip.add_send->setObjectName("audioTrackAddSend");
            sends_header->addWidget(strip.add_send);
            layout->addLayout(sends_header);
            strip.sends_layout = new QVBoxLayout;
            layout->addLayout(strip.sends_layout);

            connect(strip.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.gain, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(strips_.begin(), strips_.end(),
                        [id](const Strip& item) { return item.id == id; });
                    if (found == strips_.end()) return;
                    found->gain_value->setText(gainText(value));
                    emitTrackMix(id);
                });
            connect(strip.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.pan, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.pan, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(strips_.begin(), strips_.end(),
                        [id](const Strip& item) { return item.id == id; });
                    if (found == strips_.end()) return;
                    found->pan_value->setText(panText(value));
                    emitTrackMix(id);
                });
            connect(strip.pan, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.mute, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool) { emitTrackMix(id); });
            connect(strip.solo, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool checked) {
                        auto& ids = solo_track_ids_;
                        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                        if (checked) ids.push_back(id);
                        emitSoloState();
                    });
            connect(strip.output, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this, id = strip.id](int) { emitTrackMix(id); });
            connect(strip.add_send, &QPushButton::clicked, this,
                    [this, id = strip.id] {
                        const auto found = std::find_if(strips_.begin(), strips_.end(),
                            [id](const Strip& item) { return item.id == id; });
                        if (found == strips_.end() || found->send_destination->currentIndex() < 0)
                            return;
                        const auto bus_id = found->send_destination->currentData().toULongLong();
                        const auto existing = std::find_if(found->sends.begin(), found->sends.end(),
                            [bus_id](const SendRow& row) { return row.bus_id == bus_id; });
                        if (existing != found->sends.end()) return;
                        const auto output_value = found->output->currentData().toULongLong();
                        const auto output = output_value == 0 ? std::nullopt :
                            std::optional<timeline::AudioBusId>{
                                static_cast<timeline::AudioBusId>(output_value)};
                        std::vector<timeline::AudioSend> sends;
                        for (const auto& row : found->sends) {
                            sends.push_back(timeline::AudioSend{
                                static_cast<timeline::AudioBusId>(
                                    row.destination->currentData().toULongLong()),
                                row.gain->value() / 100.0,
                                row.pre_fader->isChecked()
                                    ? timeline::AudioSendTap::PreFader
                                    : timeline::AudioSendTap::PostFader});
                        }
                        if (!output.has_value() || *output != bus_id) {
                            sends.push_back(timeline::AudioSend{bus_id, 1.0,
                                timeline::AudioSendTap::PostFader});
                            emit trackMixChanged(id, found->gain->value() / 100.0,
                                found->mute->isChecked(), found->pan->value() / 100.0,
                                output, std::move(sends));
                        }
                    });
            track_mixer_layout_->addWidget(strip.mixer_card);
            strips_.push_back(std::move(strip));
        }
    }

    std::vector<const timeline::AudioBus*> audio_buses;
    for (const auto& bus : buses) audio_buses.push_back(&bus);
    const bool same_buses = audio_buses.size() == bus_strips_.size() &&
        std::equal(audio_buses.begin(), audio_buses.end(), bus_strips_.begin(),
            [](const auto* bus, const BusStrip& strip) { return bus->bus_id == strip.id; });
    if (!same_buses) {
        for (auto& strip : bus_strips_) delete strip.meter_card;
        clearLayout(bus_mixer_layout_);
        bus_strips_.clear();
        for (const auto* bus : audio_buses) {
            BusStrip strip;
            strip.id = bus->bus_id;
            strip.meter_card = new QGroupBox(meter_page_);
            auto* meter_layout = new QVBoxLayout(strip.meter_card);
            strip.meter_name = new QLabel(strip.meter_card);
            meter_layout->addWidget(strip.meter_name);
            strip.meter = createMeter(strip.meter_card, "audioBusMeter");
            meter_layout->addWidget(strip.meter, 1, Qt::AlignHCenter);
            meter_layout_->addWidget(strip.meter_card);

            strip.mixer_card = new QGroupBox(mixer_panel_);
            strip.mixer_card->setObjectName("audioBusMixerCard");
            auto* layout = new QVBoxLayout(strip.mixer_card);
            strip.mixer_name = new QLabel(strip.mixer_card);
            layout->addWidget(strip.mixer_name);
            strip.mixer_meter = createMeter(strip.mixer_card, "audioBusMixerMeter");
            strip.mixer_meter->setOrientation(Qt::Horizontal);
            strip.mixer_meter->setFixedWidth(160);
            layout->addWidget(strip.mixer_meter);
            strip.gain = createGainSlider(strip.mixer_card, "audioBusGain");
            layout->addWidget(strip.gain);
            strip.gain_value = new QLabel(strip.mixer_card);
            layout->addWidget(strip.gain_value);
            strip.pan = createPanSlider(strip.mixer_card, "audioBusPan");
            layout->addWidget(strip.pan);
            strip.pan_value = new QLabel(strip.mixer_card);
            layout->addWidget(strip.pan_value);
            auto* toggle_row = new QHBoxLayout;
            strip.mute = new QCheckBox(translated("Mute"), strip.mixer_card);
            strip.mute->setObjectName("audioBusMute");
            strip.solo = new QCheckBox(translated("Solo"), strip.mixer_card);
            strip.solo->setObjectName("audioBusSolo");
            toggle_row->addWidget(strip.mute);
            toggle_row->addWidget(strip.solo);
            layout->addLayout(toggle_row);
            auto* actions = new QHBoxLayout;
            auto* rename = new QPushButton(translated("Rename"), strip.mixer_card);
            rename->setObjectName("audioBusRename");
            auto* remove = new QPushButton(translated("Remove"), strip.mixer_card);
            remove->setObjectName("audioBusRemove");
            actions->addWidget(rename);
            actions->addWidget(remove);
            layout->addLayout(actions);
            connect(strip.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.gain, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
                        [id](const BusStrip& item) { return item.id == id; });
                    if (found == bus_strips_.end()) return;
                    found->gain_value->setText(gainText(value));
                    emitBusMix(id);
                });
            connect(strip.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.pan, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.pan, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
                        [id](const BusStrip& item) { return item.id == id; });
                    if (found == bus_strips_.end()) return;
                    found->pan_value->setText(panText(value));
                    emitBusMix(id);
                });
            connect(strip.pan, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.mute, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool) { emitBusMix(id); });
            connect(strip.solo, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool checked) {
                        auto& ids = solo_bus_ids_;
                        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                        if (checked) ids.push_back(id);
                        emitSoloState();
                    });
            connect(rename, &QPushButton::clicked, this, [this, id = strip.id] {
                const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
                    [id](const BusStrip& item) { return item.id == id; });
                if (found == bus_strips_.end()) return;
                bool accepted = false;
                const auto name = QInputDialog::getText(
                    mixer_panel_, translated("Rename bus"), translated("Bus name"),
                    QLineEdit::Normal, found->mixer_name->text(), &accepted);
                if (accepted && !name.trimmed().isEmpty())
                    emit renameBusRequested(id, name.trimmed());
            });
            connect(remove, &QPushButton::clicked, this,
                    [this, id = strip.id] { emit removeBusRequested(id); });
            bus_mixer_layout_->addWidget(strip.mixer_card);
            bus_strips_.push_back(std::move(strip));
        }
    }

    const QSignalBlocker master_mute_blocker(master_mute_);
    setGainSlider(master_gain_, master_gain_value_, master.audio_gain);
    master_mute_->setChecked(master.audio_muted);
    for (auto* track : audio_tracks) {
        auto found = std::find_if(strips_.begin(), strips_.end(),
            [track](const Strip& strip) { return strip.id == track->track_id; });
        if (found == strips_.end()) continue;
        found->meter_name->setText(QString::fromUtf8(track->name.c_str()));
        found->mixer_name->setText(QString::fromUtf8(track->name.c_str()));
        setGainSlider(found->gain, found->gain_value, track->audio_gain);
        setPanSlider(found->pan, found->pan_value, track->audio_pan);
        { const QSignalBlocker blocker(found->mute); found->mute->setChecked(track->audio_muted); }
        { const QSignalBlocker blocker(found->solo); found->solo->setChecked(
            std::find(solo_track_ids_.begin(), solo_track_ids_.end(), track->track_id) !=
                solo_track_ids_.end()); }
        fillBusCombo(found->output, buses, track->output_bus_id);
        {
            const QSignalBlocker blocker(found->send_destination);
            fillBusCombo(found->send_destination, buses, std::nullopt,
                         track->output_bus_id);
            found->send_destination->removeItem(0);
        }
        found->send_destination->setEnabled(found->send_destination->count() > 0);
        found->add_send->setEnabled(found->send_destination->count() > 0);
        rebuildTrackSendRows(*found, *track, buses);
    }

    const bool same_effect_tracks = tracks.size() == track_effects_rows_.size() &&
        std::equal(tracks.begin(), tracks.end(), track_effects_rows_.begin(),
            [](const timeline::TimelineTrack& track, const TrackEffectsRow& row) {
                return track.track_id == row.id;
            });
    if (!same_effect_tracks) {
        clearLayout(track_effects_layout_);
        track_effects_rows_.clear();
        for (const auto& track : tracks) {
            TrackEffectsRow row;
            row.id = track.track_id;
            row.effects = track.audio_effects;
            auto* widget = new QWidget(mixer_panel_);
            auto* layout = new QHBoxLayout(widget);
            layout->setContentsMargins(0, 0, 0, 0);
            row.name = new QLabel(QString::fromUtf8(track.name.c_str()), widget);
            row.edit = new QPushButton(translated("Effects…"), widget);
            row.edit->setObjectName("audioTrackEffects");
            row.edit->setProperty("trackId",
                QVariant::fromValue<qulonglong>(track.track_id));
            layout->addWidget(row.name, 1);
            layout->addWidget(row.edit);
            connect(row.edit, &QPushButton::clicked, this, [this, id = row.id] {
                const auto found = std::find_if(
                    track_effects_rows_.begin(), track_effects_rows_.end(),
                    [id](const TrackEffectsRow& item) { return item.id == id; });
                if (found == track_effects_rows_.end()) return;
                AudioEffectChainDialog dialog(
                    translated("Track audio effects — %1").arg(found->name->text()),
                    found->effects, mixer_panel_);
                if (dialog.exec() == QDialog::Accepted &&
                    timeline::validAudioEffectChain(dialog.effects())) {
                    emit trackAudioEffectsChanged(id, dialog.effects());
                }
            });
            track_effects_layout_->addWidget(widget);
            track_effects_rows_.push_back(std::move(row));
        }
    }
    for (const auto& track : tracks) {
        const auto found = std::find_if(track_effects_rows_.begin(),
            track_effects_rows_.end(), [&track](const TrackEffectsRow& row) {
                return row.id == track.track_id;
            });
        if (found == track_effects_rows_.end()) continue;
        found->name->setText(QString::fromUtf8(track.name.c_str()));
        found->effects = track.audio_effects;
    }

    for (const auto& bus : buses) {
        auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
            [&bus](const BusStrip& strip) { return strip.id == bus.bus_id; });
        if (found == bus_strips_.end()) continue;
        const auto name = QString::fromUtf8(bus.name.c_str());
        found->meter_name->setText(name);
        found->mixer_name->setText(name);
        static_cast<QGroupBox*>(found->mixer_card)->setTitle(name);
        setGainSlider(found->gain, found->gain_value, bus.audio_gain);
        setPanSlider(found->pan, found->pan_value, bus.audio_pan);
        { const QSignalBlocker blocker(found->mute); found->mute->setChecked(bus.audio_muted); }
        { const QSignalBlocker blocker(found->solo); found->solo->setChecked(
            std::find(solo_bus_ids_.begin(), solo_bus_ids_.end(), bus.bus_id) !=
                solo_bus_ids_.end()); }
    }

    const auto valid_track_solo = std::remove_if(
        solo_track_ids_.begin(), solo_track_ids_.end(), [&audio_tracks](auto id) {
            return std::none_of(audio_tracks.begin(), audio_tracks.end(),
                [id](const auto* track) { return track->track_id == id; });
        });
    const bool removed_track_solo = valid_track_solo != solo_track_ids_.end();
    solo_track_ids_.erase(valid_track_solo, solo_track_ids_.end());
    const auto valid_bus_solo = std::remove_if(
        solo_bus_ids_.begin(), solo_bus_ids_.end(), [&buses](auto id) {
            return std::none_of(buses.begin(), buses.end(),
                [id](const auto& bus) { return bus.bus_id == id; });
        });
    const bool removed_bus_solo = valid_bus_solo != solo_bus_ids_.end();
    solo_bus_ids_.erase(valid_bus_solo, solo_bus_ids_.end());
    if (removed_track_solo || removed_bus_solo) emitSoloState();
}

void AudioWorkspace::rebuildTrackSendRows(
    Strip& strip, const timeline::TimelineTrack& track,
    const std::vector<timeline::AudioBus>& buses) {
    const bool same_rows = strip.sends.size() == track.audio_sends.size() &&
        std::equal(strip.sends.begin(), strip.sends.end(), track.audio_sends.begin(),
            [](const SendRow& row, const timeline::AudioSend& send) {
                return row.bus_id == send.destination_bus_id;
            });
    if (!same_rows) {
        clearLayout(strip.sends_layout);
        strip.sends.clear();
        for (const auto& send : track.audio_sends) {
            SendRow row;
            row.bus_id = send.destination_bus_id;
            row.widget = new QWidget(strip.mixer_card);
            auto* layout = new QHBoxLayout(row.widget);
            layout->setContentsMargins(0, 0, 0, 0);
            row.destination = new QComboBox(row.widget);
            row.destination->setObjectName("audioTrackSendBus");
            fillBusCombo(row.destination, buses, send.destination_bus_id);
            row.destination->removeItem(0);
            row.gain = createGainSlider(row.widget, "audioTrackSendGain");
            row.gain->setRange(0, 200);
            row.gain_value = new QLabel(row.widget);
            row.gain_value->setMinimumWidth(48);
            row.pre_fader = new QCheckBox(translated("Pre"), row.widget);
            row.pre_fader->setObjectName("audioTrackSendPreFader");
            row.remove = new QPushButton(QStringLiteral("×"), row.widget);
            row.remove->setObjectName("audioTrackRemoveSend");
            layout->addWidget(row.destination, 1);
            layout->addWidget(row.gain, 2);
            layout->addWidget(row.gain_value);
            layout->addWidget(row.pre_fader);
            layout->addWidget(row.remove);
            strip.sends_layout->addWidget(row.widget);
            const auto track_id = strip.id;
            auto* destination_control = row.destination;
            connect(row.destination, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this, track_id, destination_control](int) {
                        const auto found = std::find_if(strips_.begin(), strips_.end(),
                            [track_id](const Strip& item) { return item.id == track_id; });
                        if (found == strips_.end()) return;
                        const auto row = std::find_if(found->sends.begin(), found->sends.end(),
                            [destination_control](const SendRow& value) {
                                return value.destination == destination_control;
                            });
                        if (row != found->sends.end()) row->bus_id = static_cast<timeline::AudioBusId>(
                            row->destination->currentData().toULongLong());
                        emitTrackMix(track_id);
                    });
            connect(row.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(row.gain, &QSlider::valueChanged, this,
                    [this, track_id = strip.id](int) { emitTrackMix(track_id); });
            connect(row.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(row.pre_fader, &QCheckBox::toggled, this,
                    [this, track_id = strip.id](bool) { emitTrackMix(track_id); });
            auto* row_widget = row.widget;
            connect(row.remove, &QPushButton::clicked, this,
                    [this, track_id = strip.id, row_widget] {
                        auto found = std::find_if(strips_.begin(), strips_.end(),
                            [track_id](const Strip& item) { return item.id == track_id; });
                        if (found == strips_.end()) return;
                        std::erase_if(found->sends, [row_widget](const SendRow& value) {
                            return value.widget == row_widget;
                        });
                        emitTrackMix(track_id);
                    });
            strip.sends.push_back(row);
        }
    }
    for (std::size_t index = 0; index < strip.sends.size(); ++index) {
        auto& row = strip.sends[index];
        const auto& send = track.audio_sends[index];
        {
            const QSignalBlocker blocker(row.destination);
            fillBusCombo(row.destination, buses, send.destination_bus_id);
            row.destination->removeItem(0);
        }
        setGainSlider(row.gain, row.gain_value, send.gain);
        { const QSignalBlocker blocker(row.pre_fader);
          row.pre_fader->setChecked(send.tap == timeline::AudioSendTap::PreFader); }
    }
}

void AudioWorkspace::emitTrackMix(timeline::TrackId track_id) {
    const auto found = std::find_if(strips_.begin(), strips_.end(),
        [track_id](const Strip& strip) { return strip.id == track_id; });
    if (found == strips_.end()) return;
    const auto output_id = found->output->currentData().toULongLong();
    const std::optional<timeline::AudioBusId> output = output_id == 0
        ? std::nullopt
        : std::optional<timeline::AudioBusId>{static_cast<timeline::AudioBusId>(output_id)};
    std::vector<timeline::AudioSend> sends;
    sends.reserve(found->sends.size());
    for (const auto& row : found->sends) {
        if (row.destination->currentIndex() < 0) continue;
        const auto destination = static_cast<timeline::AudioBusId>(
            row.destination->currentData().toULongLong());
        if (destination == 0 || output == destination) continue;
        sends.push_back(timeline::AudioSend{
            destination, row.gain->value() / 100.0,
            row.pre_fader->isChecked() ? timeline::AudioSendTap::PreFader
                                       : timeline::AudioSendTap::PostFader});
    }
    emit trackMixChanged(track_id, found->gain->value() / 100.0,
        found->mute->isChecked(), found->pan->value() / 100.0, output,
        std::move(sends));
}

void AudioWorkspace::emitBusMix(timeline::AudioBusId bus_id) {
    const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
        [bus_id](const BusStrip& strip) { return strip.id == bus_id; });
    if (found == bus_strips_.end()) return;
    emit busMixChanged(bus_id, found->gain->value() / 100.0,
        found->mute->isChecked(), found->pan->value() / 100.0);
}

void AudioWorkspace::emitSoloState() {
    emit soloStateChanged(solo_track_ids_, solo_bus_ids_);
}

void AudioWorkspace::setPeaks(
    const std::vector<std::pair<timeline::TrackId, float>>& track_peaks,
    const std::vector<std::pair<timeline::AudioBusId, float>>& bus_peaks,
    float master_peak) {
    const auto set_peak = [](QProgressBar* meter, float peak) {
        if (meter != nullptr) meter->setValue(static_cast<int>(std::lround(
            std::clamp(peak, 0.0F, 1.0F) * 100.0F)));
    };
    set_peak(master_meter_, master_peak);
    set_peak(mixer_master_meter_, master_peak);
    for (auto& strip : strips_) {
        const auto found = std::find_if(track_peaks.begin(), track_peaks.end(),
            [&strip](const auto& peak) { return peak.first == strip.id; });
        const auto peak = found == track_peaks.end() ? 0.0F : found->second;
        set_peak(strip.meter, peak);
        set_peak(strip.mixer_meter, peak);
    }
    for (auto& strip : bus_strips_) {
        const auto found = std::find_if(bus_peaks.begin(), bus_peaks.end(),
            [&strip](const auto& peak) { return peak.first == strip.id; });
        const auto peak = found == bus_peaks.end() ? 0.0F : found->second;
        set_peak(strip.meter, peak);
        set_peak(strip.mixer_meter, peak);
    }
}

void AudioWorkspace::clearPeaks() { setPeaks({}, {}, 0.0F); }

void AudioWorkspace::clearSoloState() {
    if (solo_track_ids_.empty() && solo_bus_ids_.empty()) return;
    solo_track_ids_.clear();
    solo_bus_ids_.clear();
    for (auto& strip : strips_) {
        const QSignalBlocker blocker(strip.solo);
        strip.solo->setChecked(false);
    }
    for (auto& strip : bus_strips_) {
        const QSignalBlocker blocker(strip.solo);
        strip.solo->setChecked(false);
    }
    emitSoloState();
}

}  // namespace ui
