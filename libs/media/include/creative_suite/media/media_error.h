#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace creative_suite::media {

class MediaError final : public std::runtime_error {
public:
    explicit MediaError(
        std::string message,
        std::optional<int> error_code = std::nullopt)
        : std::runtime_error(std::move(message)),
          error_code_(error_code) {}

    [[nodiscard]] std::optional<int> error_code() const noexcept {
        return error_code_;
    }

private:
    std::optional<int> error_code_;
};

} // namespace creative_suite::media
