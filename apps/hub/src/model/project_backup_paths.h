#pragma once

#include <QByteArray>
#include <QString>

namespace creative_suite::hub {

// Newest persisted document versions the Hub backup adapter can rebase. Each value mirrors the
// owning editor's current writer version; `hub-backup-format-test` asserts equality with the
// editors' own constants whenever those editors are built, so a format bump without a Hub update
// fails regression coverage instead of silently breaking backups.
inline constexpr int kMaxBackupImageDocumentVersion = 31;  // .cimg
inline constexpr int kMaxBackupVideoProjectVersion = 45;    // .csp
inline constexpr int kMaxBackupMotionDocumentVersion = 18;  // .motion

// Prepare a relocatable document snapshot. The prepared output must be non-null.
// Only documented path fields in supported editor formats are rebased; versions and other JSON
// fields are preserved. No media is copied or required to exist. Unknown file
// types are copied verbatim; unsafe/unsupported editor documents return a cause.
bool prepareProjectBackup(const QByteArray& original, const QString& originalPath,
                          QByteArray* prepared, QString* error);

} // namespace creative_suite::hub
