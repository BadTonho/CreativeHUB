#include "main_window_support.h"

namespace motion::ui::detail {

std::filesystem::path pathFromQString(const QString& value)
{
    const auto utf8 = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first, first + utf8.size()));
}

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

QString pathForDisplay(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(encoded.data()),
                             static_cast<qsizetype>(encoded.size()));
}

} // namespace motion::ui::detail
