#pragma once

#include <QByteArray>
#include <QString>

namespace creative_suite::hub {

// Prepare a relocatable document snapshot. The prepared output must be non-null.
// Only documented path fields in supported editor formats are rebased; versions and other JSON
// fields are preserved. No media is copied or required to exist. Unknown file
// types are copied verbatim; unsafe/unsupported editor documents return a cause.
bool prepareProjectBackup(const QByteArray& original, const QString& originalPath,
                          QByteArray* prepared, QString* error);

} // namespace creative_suite::hub
