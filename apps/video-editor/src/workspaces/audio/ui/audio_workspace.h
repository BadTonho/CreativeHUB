#pragma once

#include "timeline/timeline_model.h"

#include <QObject>
#include <map>
#include "voice_isolation_panel.h"
#include <QWidget>

#include <optional>
#include <vector>
#include <utility>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QWidget;
class QSlider;
class QVBoxLayout;

namespace ui {

// Presentation for the app-owned audio mixer. All persistent edits go through
// the existing TimelineCommandService; solo state remains transient.
class AudioWorkspace final : public QObject {
    Q_OBJECT
public:
    explicit AudioWorkspace(QObject* parent = nullptr);
    void createPanels(QWidget* parent);
    [[nodiscard]] QWidget* meterPage() const noexcept { return meter_page_; }
    [[nodiscard]] QWidget* mixerPanel() const noexcept { return mixer_panel_; }
    [[nodiscard]] QWidget* effectsPanel() const noexcept { return fx_browser_panel_; }
    void refreshTracks(
        const std::vector<timeline::TimelineTrack>& tracks,
        const std::vector<timeline::AudioBus>& buses,
        const timeline::MasterAudioMix& master);
    void setPeaks(
        const std::vector<std::pair<timeline::TrackId, float>>& track_peaks,
        const std::vector<std::pair<timeline::AudioBusId, float>>& bus_peaks,
        float master_peak);
    void clearPeaks();
    void setSelectedClip(const timeline::TimelineClip* clip);
    void applyAudioFx(const QString& fx_id,
                      std::optional<timeline::TrackId> track_id = std::nullopt,
                      std::optional<timeline::ClipId> clip_id = std::nullopt);
    void clearSoloState();
    void setIsolationState(bool clip, quint64 id, QString state, int progress);

signals:
    void voiceIsolationChanged(bool clip, quint64 id, timeline::VoiceIsolationSettings settings);
    void voiceIsolationRegenerate(bool clip, quint64 id);
    void voiceIsolationCancel();
    void clipAudioEffectsChanged(timeline::ClipId clip_id, std::vector<timeline::AudioEffectInstance> effects);
    void trackDuckingChanged(timeline::TrackId track_id, timeline::AudioDuckingSettings settings);
    void generateDuckingCurveRequested(timeline::TrackId track_id, timeline::AudioDuckingSettings settings);
    void gainEditStarted();
    void gainEditFinished();
    void trackMixChanged(
        timeline::TrackId track_id, double gain, bool muted, double pan,
        std::optional<timeline::AudioBusId> output_bus_id,
        std::vector<timeline::AudioSend> sends);
    void trackAudioEffectsChanged(
        timeline::TrackId track_id,
        std::vector<timeline::AudioEffectInstance> effects);
    void busMixChanged(
        timeline::AudioBusId bus_id, double gain, bool muted, double pan);
    void masterMixChanged(double gain, bool muted);
    void createBusRequested(QString name);
    void renameBusRequested(timeline::AudioBusId bus_id, QString name);
    void removeBusRequested(timeline::AudioBusId bus_id);
    void soloStateChanged(
        std::vector<timeline::TrackId> track_ids,
        std::vector<timeline::AudioBusId> bus_ids);

private:
    struct SendRow {
        QWidget* widget = nullptr;
        QComboBox* destination = nullptr;
        QSlider* gain = nullptr;
        QLabel* gain_value = nullptr;
        QCheckBox* pre_fader = nullptr;
        QPushButton* remove = nullptr;
        timeline::AudioBusId bus_id = 0;
    };
    struct Strip {
        timeline::TrackId id = 0;
        QWidget* meter_card = nullptr;
        QLabel* meter_name = nullptr;
        QProgressBar* meter = nullptr;
        QWidget* mixer_card = nullptr;
        QLabel* mixer_name = nullptr;
        QProgressBar* mixer_meter = nullptr;
        QSlider* gain = nullptr;
        QLabel* gain_value = nullptr;
        QSlider* pan = nullptr;
        QLabel* pan_value = nullptr;
        QCheckBox* mute = nullptr;
        QCheckBox* solo = nullptr;
        QPushButton* voice_iso_btn = nullptr;
        QPushButton* dial_lev_btn = nullptr;
        QPushButton* effects_in_btn = nullptr;
        QWidget* dyn_widget = nullptr;
        QWidget* eq_widget = nullptr;
        QComboBox* output = nullptr;
        QComboBox* send_destination = nullptr;
        QPushButton* add_send = nullptr;
        QVBoxLayout* sends_layout = nullptr;
        std::vector<SendRow> sends;
    };
    struct BusStrip {
        timeline::AudioBusId id = 0;
        QWidget* meter_card = nullptr;
        QLabel* meter_name = nullptr;
        QProgressBar* meter = nullptr;
        QWidget* mixer_card = nullptr;
        QLabel* mixer_name = nullptr;
        QProgressBar* mixer_meter = nullptr;
        QSlider* gain = nullptr;
        QLabel* gain_value = nullptr;
        QSlider* pan = nullptr;
        QLabel* pan_value = nullptr;
        QCheckBox* mute = nullptr;
        QCheckBox* solo = nullptr;
    };
    struct TrackEffectsRow {
        timeline::TrackId id = 0;
        QLabel* name = nullptr;
        QPushButton* edit = nullptr;
        std::vector<timeline::AudioEffectInstance> effects;
        QPushButton* ducking_button = nullptr;
        timeline::AudioDuckingSettings ducking;
        VoiceIsolationPanel* isolation = nullptr;
    };

    void emitTrackMix(timeline::TrackId track_id);
    void emitBusMix(timeline::AudioBusId bus_id);
    void emitSoloState();
    void rebuildTrackSendRows(Strip& strip, const timeline::TimelineTrack& track,
                              const std::vector<timeline::AudioBus>& buses);

    std::vector<std::pair<std::uint64_t, QString>> ducking_sources_;
    QPushButton* selected_clip_effects_ = nullptr;
    VoiceIsolationPanel* selected_clip_isolation_ = nullptr;
    timeline::VoiceIsolationSettings selected_clip_isolation_settings_;
    std::map<std::pair<bool, quint64>, std::pair<QString, int>> isolation_states_;
    timeline::ClipId selected_clip_id_ = 0;
    timeline::TrackId selected_track_id_ = 0;
    QString selected_clip_name_;
    std::vector<timeline::AudioEffectInstance> selected_clip_chain_;
    QWidget* meter_page_ = nullptr;
    QWidget* mixer_panel_ = nullptr;
    QWidget* fx_browser_panel_ = nullptr;
    QWidget* clip_fx_drawer_ = nullptr;
    QHBoxLayout* meter_layout_ = nullptr;
    QHBoxLayout* meter_filler_layout_ = nullptr;
    QVBoxLayout* mixer_layout_ = nullptr;
    QHBoxLayout* bus_mixer_layout_ = nullptr;
    QHBoxLayout* track_mixer_layout_ = nullptr;
    QVBoxLayout* track_effects_layout_ = nullptr;
    QProgressBar* master_meter_ = nullptr;
    QProgressBar* mixer_master_meter_ = nullptr;
    QProgressBar* control_room_meter_ = nullptr;
    QLabel* tp_value_label_ = nullptr;
    QLabel* loudness_short_value_ = nullptr;
    QLabel* loudness_short_max_value_ = nullptr;
    QLabel* loudness_range_value_ = nullptr;
    QLabel* loudness_integrated_value_ = nullptr;
    QWidget* loudness_meter_widget_ = nullptr;
    bool loudness_paused_ = false;
    float loudness_short_max_lu_ = -100.0F;
    float loudness_min_lu_ = 0.0F;
    float loudness_max_lu_ = -100.0F;
    double loudness_sum_ = 0.0;
    int loudness_samples_ = 0;
    QSlider* master_gain_ = nullptr;
    QLabel* master_gain_value_ = nullptr;
    QCheckBox* master_mute_ = nullptr;
    QPushButton* add_bus_button_ = nullptr;
    std::vector<Strip> strips_;
    std::vector<BusStrip> bus_strips_;
    std::vector<TrackEffectsRow> track_effects_rows_;
    std::vector<timeline::TrackId> solo_track_ids_;
    std::vector<timeline::AudioBusId> solo_bus_ids_;
};

}  // namespace ui
