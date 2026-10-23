#pragma once

#include "timeline/timeline_model.h"

#include <QObject>
#include <QWidget>

#include <vector>
#include <utility>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QSlider;
class QCheckBox;
class QVBoxLayout;

namespace ui {

// Presentation for the app-owned audio tracks. All edits go through the
// existing TimelineCommandService; this class owns no project state.
class AudioWorkspace final : public QObject {
    Q_OBJECT
public:
    explicit AudioWorkspace(QObject* parent = nullptr);
    void createPanels(QWidget* parent);
    [[nodiscard]] QWidget* meterPage() const noexcept { return meter_page_; }
    [[nodiscard]] QWidget* mixerPanel() const noexcept { return mixer_panel_; }
    void refreshTracks(const std::vector<timeline::TimelineTrack>& tracks);
    void setPeaks(const std::vector<std::pair<timeline::TrackId, float>>& peaks,
                  float master_peak);
    void clearPeaks();

signals:
    void gainEditStarted();
    void gainChanged(timeline::TrackId track_id, double gain, bool muted);
    void gainEditFinished();
    void muteChanged(timeline::TrackId track_id, double gain, bool muted);

private:
    struct Strip {
        timeline::TrackId id = 0;
        QWidget* meter_card = nullptr;
        QLabel* meter_name = nullptr;
        QProgressBar* meter = nullptr;
        QWidget* mixer_card = nullptr;
        QLabel* mixer_name = nullptr;
        QSlider* gain = nullptr;
        QLabel* gain_value = nullptr;
        QCheckBox* mute = nullptr;
    };
    QWidget* meter_page_ = nullptr;
    QWidget* mixer_panel_ = nullptr;
    QHBoxLayout* meter_layout_ = nullptr;
    QVBoxLayout* mixer_layout_ = nullptr;
    QProgressBar* master_meter_ = nullptr;
    QProgressBar* mixer_master_meter_ = nullptr;
    std::vector<Strip> strips_;
};

}  // namespace ui
