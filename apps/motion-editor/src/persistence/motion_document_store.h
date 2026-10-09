#pragma once

#include "../model/motion_project_data.h"

#include <filesystem>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace motion::persistence {

struct MotionRecoveryData {
    model::MotionProjectData document;
    std::filesystem::path target_document_path;
    std::string session_id;
};

enum class MotionDocumentErrorCode {
    Io,
    InvalidFormat,
    UnsupportedVersion,
    MissingField,
    InvalidValue,
};

class MotionDocumentError final : public std::runtime_error {
public:
    MotionDocumentError(MotionDocumentErrorCode code,
                        std::string message,
                        std::filesystem::path path,
                        std::optional<int> system_error = std::nullopt);

    [[nodiscard]] MotionDocumentErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] const std::optional<int>& systemError() const noexcept { return system_error_; }

private:
    MotionDocumentErrorCode code_;
    std::filesystem::path path_;
    std::optional<int> system_error_;
};

class MotionDocumentStore final {
public:
    static constexpr int current_format_version = 6;
    static constexpr std::size_t maximum_keyframe_count = 2'000'000;
    static constexpr const char* format_identifier = "creative-suite.motion-studio";

    [[nodiscard]] static model::MotionProjectData load(
        const std::filesystem::path& document_path);
    static void save(const std::filesystem::path& document_path,
                     const model::MotionProjectData& document);

    [[nodiscard]] static MotionRecoveryData loadRecovery(
        const std::filesystem::path& recovery_path);
    static void saveRecovery(
        const std::filesystem::path& recovery_path,
        const std::filesystem::path& target_document_path,
        const std::string& session_id,
        const model::MotionProjectData& document);
};

} // namespace motion::persistence
