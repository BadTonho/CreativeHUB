#pragma once

#include <QString>
#include <QStringList>

namespace creative_suite::app_locator {

// Executable file names a suite application may have on disk, in priority order: the name as given,
// its counterpart with/without a Windows ".exe" suffix, and the macOS bundle layout
// "<name>.app/Contents/MacOS/<name>". The input may carry or omit ".exe"; the result never contains
// duplicates. An empty input yields an empty list.
QStringList executableCandidateNames(const QString& executableName);

// Base executable name of a suite application without a platform suffix, for example
// "creative-suite-image-editor" for the application ID "image-editor". The IDs are `hub`,
// `video-editor`, `image-editor`, and `motion-editor`.
QString applicationBaseName(const QString& applicationId);

// QSettings key under which a user-chosen executable for a suite application is remembered, for
// example "applications/image_editor_executable" for "image-editor".
QString applicationSettingsKey(const QString& applicationId);

// Directories searched for a sibling application next to `applicationDirectory`, in priority order:
// the directory itself (installed or bundled layout), then the canonical development
// output trees "../<id>/<Configuration>" and "../../<id>/<Configuration>" for Release and Debug.
QStringList siblingSearchDirectories(const QString& applicationId, const QString& applicationDirectory);

// Finds another suite application's executable. Order: the QSettings override (must be a regular
// file), the sibling search directories, then PATH. Returns an absolute path or an empty string.
// The caller decides how to ask the user when nothing is found. Reads QSettings with the calling
// application's organization/application identity, as the editors already do.
QString findApplicationExecutable(const QString& applicationId, const QString& applicationDirectory);

} // namespace creative_suite::app_locator
