#pragma once

#include <QFileInfo>
#include <QMimeData>
#include <QStringList>
#include <QUrl>

namespace media_browser_ui {

// Returns existing local files in the order supplied by the operating system.
// Directories and non-local URLs are deliberately excluded.
[[nodiscard]] inline QStringList localFilesFromUrls(const QMimeData* mime_data) {
    QStringList files;
    if (mime_data == nullptr || !mime_data->hasUrls()) return files;
    const auto urls = mime_data->urls();
    files.reserve(urls.size());
    for (const auto& url : urls) {
        if (!url.isLocalFile()) continue;
        const auto path = url.toLocalFile();
        if (path.isEmpty()) continue;
        const QFileInfo info(path);
        if (info.isFile()) files.push_back(info.absoluteFilePath());
    }
    return files;
}

} // namespace media_browser_ui
