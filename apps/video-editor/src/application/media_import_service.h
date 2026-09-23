#pragma once

#include "media/media_library.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace application {

enum class MediaImportFileStatus { Imported, Failed, Discarded };

struct MediaImportFileResult {
    std::filesystem::path path;
    MediaImportFileStatus status = MediaImportFileStatus::Failed;
    std::optional<media::MediaItem> item;
    std::optional<int> error_code;
    std::string cause;
};

struct MediaImportBatchResult {
    std::uint64_t work_id = 0;
    std::uint64_t project_generation = 0;
    std::uint64_t selection_generation = 0;
    std::vector<MediaImportFileResult> files;
    bool cancelled = false;
};

class MediaImportService final {
public:
    using Processor = std::function<media::MediaItem(const std::filesystem::path&)>;
    using Progress = std::function<void(
        std::size_t,
        std::size_t,
        const std::filesystem::path&)>;

    MediaImportService();
    explicit MediaImportService(Processor processor);

    [[nodiscard]] MediaImportBatchResult process(
        std::uint64_t work_id,
        std::uint64_t project_generation,
        std::uint64_t selection_generation,
        const std::vector<std::filesystem::path>& paths,
        const std::atomic_bool& cancel_requested,
        Progress progress = {}) const;

private:
    Processor processor_;
};

} // namespace application
