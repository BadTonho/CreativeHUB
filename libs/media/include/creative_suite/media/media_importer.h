#pragma once

#include "media_library.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace creative_suite::media {

enum class MediaImportFileStatus { Imported, Failed, Discarded };

struct MediaImportFileResult {
    std::filesystem::path path;
    MediaImportFileStatus status = MediaImportFileStatus::Failed;
    std::optional<MediaItem> item;
    std::optional<int> error_code;
    std::string cause;
};

struct MediaImportBatchResult {
    std::vector<MediaImportFileResult> files;
    bool cancelled = false;
};

class MediaImporter final {
public:
    using Processor = std::function<MediaItem(const std::filesystem::path&)>;
    using Progress = std::function<void(
        std::size_t,
        std::size_t,
        const std::filesystem::path&)>;

    MediaImporter();
    explicit MediaImporter(Processor processor);

    [[nodiscard]] MediaImportBatchResult process(
        const std::vector<std::filesystem::path>& paths,
        const std::atomic_bool& cancel_requested,
        Progress progress = {}) const;

private:
    Processor processor_;
};

} // namespace creative_suite::media
