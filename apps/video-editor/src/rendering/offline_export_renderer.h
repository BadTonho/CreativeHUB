#pragma once

#include "ui/workspace/pages/render/render_job.h"

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace rendering {

class ExportCanceled final : public std::runtime_error {
public:
    ExportCanceled() : std::runtime_error("Export canceled.") {}
};

class ExportError final : public std::runtime_error {
public:
    ExportError(std::string message, int error_code)
        : std::runtime_error(std::move(message)), error_code_(error_code) {}

    [[nodiscard]] int errorCode() const noexcept { return error_code_; }

private:
    int error_code_;
};

class OfflineExportRenderer final {
public:
    using ProgressCallback = std::function<void(int)>;

    // Renders every output frame synchronously. Call this from a worker thread.
    static void render(
        const ui::RenderJob& job,
        const std::atomic_bool& cancel_requested,
        ProgressCallback report_progress = {});
};

}  // namespace rendering
