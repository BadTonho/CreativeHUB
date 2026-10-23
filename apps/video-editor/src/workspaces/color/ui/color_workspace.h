#pragma once

#include "timeline/timeline_model.h"
#include "rendering/preview_frame_payload.h"
#include "playback/playback_controller.h"

#include <QWidget>
#include <QImage>

#include <functional>
#include <vector>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace ui {

class ColorWorkspace final : public QWidget {
public:
    struct ClipItem {
        timeline::ClipId id = 0;
        QString name;
        QImage thumbnail;
    };

    explicit ColorWorkspace(QWidget* parent = nullptr);
    void setPreviewWidget(QWidget* preview);
    QWidget* takePreviewWidget();
    void setActive(bool active);
    void setClips(std::vector<ClipItem> clips, timeline::ClipId selected,
                  std::vector<creative_suite::effects::EffectInstance> effects);
    void setClipSelectedHandler(std::function<void(timeline::ClipId)> handler);
    void setEffectsChangedHandler(std::function<void(timeline::ClipId,
        std::vector<creative_suite::effects::EffectInstance>)> handler);
    void setPlaybackHandler(std::function<void(playback::PlaybackCommand)> handler);

private:
    void refreshNodes(int requested_row = -1);
    void refreshControls();
    void submit(std::vector<creative_suite::effects::EffectInstance> effects,
                int requested_row);
    void scheduleWaveform();

    QWidget* preview_container_ = nullptr;
    QWidget* preview_ = nullptr;
    QListWidget* clips_ = nullptr;
    QListWidget* nodes_ = nullptr;
    QWidget* controls_ = nullptr;
    QFormLayout* controls_layout_ = nullptr;
    QLabel* waveform_ = nullptr;
    QTimer* waveform_timer_ = nullptr;
    timeline::ClipId selected_clip_ = 0;
    std::vector<creative_suite::effects::EffectInstance> effects_;
    std::function<void(timeline::ClipId)> clip_selected_handler_;
    std::function<void(timeline::ClipId,
        std::vector<creative_suite::effects::EffectInstance>)> effects_changed_handler_;
    std::function<void(playback::PlaybackCommand)> playback_handler_;
    bool active_ = false;
    bool waveform_pending_ = false;
    unsigned int waveform_generation_ = 0;
};

}  // namespace ui
