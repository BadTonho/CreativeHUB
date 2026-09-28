#pragma once

#include "media/media_library.h"

#include <creative_suite/media/media_importer.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace application {

using MediaImportFileStatus = creative_suite::media::MediaImportFileStatus;
using MediaImportFileResult = creative_suite::media::MediaImportFileResult;

struct MediaImportBatchResult {
    std::uint64_t work_id = 0;
    std::uint64_t project_generation = 0;
    std::uint64_t selection_generation = 0;
    std::vector<MediaImportFileResult> files;
    bool cancelled = false;
};

class MediaImportService final {
public:
    using Processor = creative_suite::media::MediaImporter::Processor;
    using Progress = creative_suite::media::MediaImporter::Progress;

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
    creative_suite::media::MediaImporter importer_;
};

} // namespace application
