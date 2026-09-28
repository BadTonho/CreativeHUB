#include "media_import_service.h"

#include <utility>

namespace application {
namespace {

} // namespace

MediaImportService::MediaImportService() = default;

MediaImportService::MediaImportService(Processor processor)
    : importer_(std::move(processor)) {}

MediaImportBatchResult MediaImportService::process(
    std::uint64_t work_id,
    std::uint64_t project_generation,
    std::uint64_t selection_generation,
    const std::vector<std::filesystem::path>& paths,
    const std::atomic_bool& cancel_requested,
    Progress progress) const {
    auto imported = importer_.process(paths, cancel_requested, std::move(progress));
    MediaImportBatchResult batch;
    batch.work_id = work_id;
    batch.project_generation = project_generation;
    batch.selection_generation = selection_generation;
    batch.files = std::move(imported.files);
    batch.cancelled = imported.cancelled;
    return batch;
}

} // namespace application
