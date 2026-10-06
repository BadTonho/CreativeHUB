#pragma once

#include "../audio/audio_envelope_analyzer.h"

#include <QPointer>
#include <QThread>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace motion::ui {

struct AudioKeyframeGenerationRequest final {
    std::filesystem::path audio_path;
    model::LayerId layer_id = 0;
    model::FrameRate frame_rate;
    std::int64_t layer_duration_frames = 0;
    std::int64_t analysis_frame_limit = 0;
    creative_suite::animation::TransformProperty property =
        creative_suite::animation::TransformProperty::Scale;
    double minimum_value = 1.0;
    double maximum_value = 2.0;
    std::size_t maximum_keyframe_count = 0;
};

struct AudioKeyframeGenerationResult final {
    bool succeeded = false;
    bool cancelled = false;
    bool silent = false;
    std::string error_message;
    int error_code = -1;
    std::filesystem::path audio_path;
    model::LayerId layer_id = 0;
    creative_suite::animation::TransformProperty property =
        creative_suite::animation::TransformProperty::Scale;
    std::vector<creative_suite::animation::Keyframe> keyframes;
};

class AudioKeyframeGenerationWorker final : public QThread {
public:
    using ProgressHandler = std::function<void(int)>;
    using FinishedHandler = std::function<void(AudioKeyframeGenerationResult)>;

    AudioKeyframeGenerationWorker(
        QObject* receiver,
        AudioKeyframeGenerationRequest request,
        ProgressHandler progress_handler,
        FinishedHandler finished_handler);
    ~AudioKeyframeGenerationWorker() override;

    void cancel() noexcept;
    void cancelAndWait();

protected:
    void run() override;

private:
    QPointer<QObject> receiver_;
    AudioKeyframeGenerationRequest request_;
    std::shared_ptr<std::atomic_bool> cancel_requested_;
    ProgressHandler progress_handler_;
    FinishedHandler finished_handler_;
};

} // namespace motion::ui
