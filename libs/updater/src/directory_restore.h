#pragma once

#include <QString>

namespace creative_suite::updater::detail {

// Replaces an installed directory with a staged copy of a previous snapshot.
// The current installation remains in place if staging fails.
[[nodiscard]] bool restoreDirectorySnapshot(
    const QString& snapshot_path,
    const QString& install_path);

} // namespace creative_suite::updater::detail
