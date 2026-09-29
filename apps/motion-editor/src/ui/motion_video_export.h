#pragma once

#include "model/composition_document.h"
#include "preview_renderer.h"

#include <creative_suite/media/video_encoder.h>

#include <QPointer>
#include <QThread>

#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace motion::ui {

struct MotionExportSettings {
    std::filesystem::path output_path;
    std::string container_name;
    std::string video_encoder_name;
    int width = 0;
    int height = 0;
    model::FrameRate frame_rate;
    double video_bitrate_mbps = 10.0;
};

struct MotionExportSnapshot {
    model::CanvasSize canvas_size{};
    model::FrameRate frame_rate{};
    std::vector<model::CompositionLayer> layers;
    std::map<std::filesystem::path, creative_suite::media::RgbaFramePtr> still_frames;
};

class MotionExportCancelled final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "Motion Studio video export was canceled.";
    }
};

class MotionVideoExporter final {
public:
    using ProgressCallback = std::function<void(int)>;

    // Writes the full layer extent from frame zero. The snapshot and its media
    // references must remain valid for the duration of this synchronous call.
    static void exportVideo(
        const MotionExportSnapshot& snapshot,
        const MotionExportSettings& settings,
        const std::atomic_bool& cancel_requested,
        ProgressCallback report_progress = {});
};

struct MotionExportResult {
    bool succeeded = false;
    bool cancelled = false;
    std::string error_message;
    int error_code = -1;
    std::filesystem::path output_path;
};

class MotionVideoExportWorker final : public QThread {
public:
    using ProgressHandler = std::function<void(int)>;
    using FinishedHandler = std::function<void(MotionExportResult)>;

    MotionVideoExportWorker(
        QObject* receiver,
        MotionExportSnapshot snapshot,
        MotionExportSettings settings,
        ProgressHandler progress_handler,
        FinishedHandler finished_handler);
    ~MotionVideoExportWorker() override;

    void cancel() noexcept;
    void cancelAndWait();

protected:
    void run() override;

private:
    QPointer<QObject> receiver_;
    MotionExportSnapshot snapshot_;
    MotionExportSettings settings_;
    std::shared_ptr<std::atomic_bool> cancel_requested_;
    ProgressHandler progress_handler_;
    FinishedHandler finished_handler_;
};

} // namespace motion::ui
