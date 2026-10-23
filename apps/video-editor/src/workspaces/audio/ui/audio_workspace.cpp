#include "workspaces/audio/ui/audio_workspace.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace ui {
namespace {

QString gainText(int percent) {
    if (percent == 0) return QStringLiteral("-∞ dB");
    return QString::number(20.0 * std::log10(percent / 100.0), 'f', 1) +
        QStringLiteral(" dB");
}

QProgressBar* createMeter(QWidget* parent) {
    auto* meter = new QProgressBar(parent);
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

}  // namespace

AudioWorkspace::AudioWorkspace(QObject* parent) : QObject(parent) {}

void AudioWorkspace::createPanels(QWidget* parent) {
    meter_page_ = new QWidget(parent);
    meter_page_->setObjectName("audioMeterPage");
    auto* meter_outer = new QVBoxLayout(meter_page_);
    meter_outer->setContentsMargins(12, 12, 12, 12);
    meter_outer->addWidget(new QLabel(
        QCoreApplication::translate("AudioWorkspace", "Audio levels"), meter_page_));
    auto* meter_scroll = new QScrollArea(meter_page_);
    meter_scroll->setWidgetResizable(true);
    auto* meter_content = new QWidget(meter_scroll);
    meter_layout_ = new QHBoxLayout(meter_content);
    meter_layout_->setAlignment(Qt::AlignLeft);
    meter_scroll->setWidget(meter_content);
    meter_outer->addWidget(meter_scroll, 1);

    auto* master_card = new QGroupBox(
        QCoreApplication::translate("AudioWorkspace", "Output"), meter_content);
    auto* master_layout = new QVBoxLayout(master_card);
    master_meter_ = createMeter(master_card);
    master_meter_->setObjectName("audioOutputMeter");
    master_layout->addWidget(master_meter_, 1, Qt::AlignHCenter);
    meter_layout_->addWidget(master_card);

    mixer_panel_ = new QScrollArea(parent);
    mixer_panel_->setObjectName("audioMixerPanel");
    auto* mixer_scroll = static_cast<QScrollArea*>(mixer_panel_);
    mixer_scroll->setWidgetResizable(true);
    auto* mixer_content = new QWidget(mixer_scroll);
    mixer_layout_ = new QVBoxLayout(mixer_content);
    mixer_layout_->setAlignment(Qt::AlignTop);
    mixer_layout_->addWidget(new QLabel(
        QCoreApplication::translate("AudioWorkspace", "Mixer"), mixer_content));
    auto* output_card = new QGroupBox(
        QCoreApplication::translate("AudioWorkspace", "Output"), mixer_content);
    output_card->setObjectName("audioOutputMixerCard");
    auto* output_layout = new QVBoxLayout(output_card);
    mixer_master_meter_ = new QProgressBar(output_card);
    mixer_master_meter_->setObjectName("audioOutputMixerMeter");
    mixer_master_meter_->setRange(0, 100);
    mixer_master_meter_->setValue(0);
    mixer_master_meter_->setTextVisible(false);
    output_layout->addWidget(mixer_master_meter_);
    mixer_layout_->addWidget(output_card);
    mixer_scroll->setWidget(mixer_content);
}

void AudioWorkspace::refreshTracks(
    const std::vector<timeline::TimelineTrack>& tracks) {
    if (meter_page_ == nullptr || mixer_panel_ == nullptr) return;
    std::vector<const timeline::TimelineTrack*> audio_tracks;
    for (const auto& track : tracks) {
        if (track.kind == timeline::TrackKind::Audio) audio_tracks.push_back(&track);
    }
    const bool same_ids = audio_tracks.size() == strips_.size() &&
        std::equal(audio_tracks.begin(), audio_tracks.end(), strips_.begin(),
            [](const auto* track, const Strip& strip) {
                return track->track_id == strip.id;
            });
    if (!same_ids) {
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
            auto* meter_card_layout = new QVBoxLayout(strip.meter_card);
            strip.meter_name = new QLabel(strip.meter_card);
            meter_card_layout->addWidget(strip.meter_name);
            strip.meter = createMeter(strip.meter_card);
            strip.meter->setObjectName("audioTrackMeter");
            meter_card_layout->addWidget(strip.meter, 1, Qt::AlignHCenter);
            meter_layout_->insertWidget(meter_layout_->count() - 1, strip.meter_card);

            strip.mixer_card = new QGroupBox(mixer_panel_);
            strip.mixer_card->setObjectName("audioTrackMixerCard");
            auto* mixer_card_layout = new QVBoxLayout(strip.mixer_card);
            strip.mixer_name = new QLabel(strip.mixer_card);
            mixer_card_layout->addWidget(strip.mixer_name);
            strip.gain = new QSlider(Qt::Horizontal, strip.mixer_card);
            strip.gain->setObjectName("audioTrackGain");
            strip.gain->setRange(0, 200);
            mixer_card_layout->addWidget(strip.gain);
            strip.gain_value = new QLabel(strip.mixer_card);
            mixer_card_layout->addWidget(strip.gain_value);
            strip.mute = new QCheckBox(
                QCoreApplication::translate("AudioWorkspace", "Mute"),
                strip.mixer_card);
            strip.mute->setObjectName("audioTrackMute");
            mixer_card_layout->addWidget(strip.mute);
            mixer_layout_->insertWidget(
                mixer_layout_->count() - 1, strip.mixer_card);
            connect(strip.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.gain, &QSlider::valueChanged, this,
                    [this, id = strip.id, gain = strip.gain, mute = strip.mute,
                     value = strip.gain_value](int percent) {
                        value->setText(gainText(percent));
                        emit gainChanged(id, gain->value() / 100.0, mute->isChecked());
                    });
            connect(strip.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.mute, &QCheckBox::toggled, this,
                    [this, id = strip.id, gain = strip.gain](bool muted) {
                        emit muteChanged(id, gain->value() / 100.0, muted);
                    });
            strips_.push_back(strip);
        }
    }
    for (std::size_t index = 0; index < audio_tracks.size(); ++index) {
        const auto* track = audio_tracks[index];
        auto& strip = strips_[index];
        const auto name = QString::fromUtf8(track->name.c_str());
        strip.meter_name->setText(name);
        strip.mixer_name->setText(name);
        const auto gain_percent = static_cast<int>(std::lround(
            std::clamp(track->audio_gain, 0.0, 2.0) * 100.0));
        const QSignalBlocker gain_blocker(strip.gain);
        const QSignalBlocker mute_blocker(strip.mute);
        strip.gain->setValue(gain_percent);
        strip.gain_value->setText(gainText(gain_percent));
        strip.mute->setChecked(track->audio_muted);
    }
}

void AudioWorkspace::setPeaks(
    const std::vector<std::pair<timeline::TrackId, float>>& peaks,
    float master_peak) {
    if (master_meter_ == nullptr) return;
    master_meter_->setValue(static_cast<int>(std::lround(
        std::clamp(master_peak, 0.0F, 1.0F) * 100.0F)));
    if (mixer_master_meter_ != nullptr)
        mixer_master_meter_->setValue(master_meter_->value());
    for (auto& strip : strips_) {
        const auto found = std::find_if(peaks.begin(), peaks.end(),
            [&strip](const auto& peak) { return peak.first == strip.id; });
        strip.meter->setValue(found == peaks.end() ? 0 :
            static_cast<int>(std::lround(
                std::clamp(found->second, 0.0F, 1.0F) * 100.0F)));
    }
}

void AudioWorkspace::clearPeaks() {
    setPeaks({}, 0.0F);
}

}  // namespace ui
