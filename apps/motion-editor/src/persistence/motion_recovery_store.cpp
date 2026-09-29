#include "motion_recovery_store.h"

#include "motion_document_store.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_library.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace motion::persistence {
namespace {

std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(first, first + bytes.size()));
}

QString pathToQString(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()),
                              static_cast<qsizetype>(value.size()));
}

std::filesystem::path normalizedPath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

std::filesystem::path defaultRecoveryRoot()
{
    const auto location = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (location.isEmpty()) {
        throw std::runtime_error(
            "Could not determine the Motion Studio application data directory.");
    }
    return pathFromQString(location) / "autosave";
}

bool isSnapshotName(const std::filesystem::path& path)
{
    const auto name = path.filename().u8string();
    constexpr std::u8string_view prefix = u8"snapshot-";
    return name.size() > prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), name.begin()) &&
        path.extension() == ".motion-recovery";
}

bool isRegularFile(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

void logInvalidSnapshot(const std::filesystem::path& path,
                        const std::string& cause,
                        int error_code = -1) noexcept
{
    creative_suite::diagnostics::Context context{{"snapshot_path", pathToUtf8(path)}};
    if (error_code >= 0) context.emplace_back("error_code", std::to_string(error_code));
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Warning,
        "motion_recovery",
        "validate_snapshot",
        cause,
        context);
}

void throwIo(const std::string& message,
             const std::filesystem::path& path,
             const std::error_code& error)
{
    throw MotionDocumentError(
        MotionDocumentErrorCode::Io,
        message,
        path,
        error ? std::optional<int>(error.value()) : std::nullopt);
}

} // namespace

MotionRecoveryStore::MotionRecoveryStore(
    std::filesystem::path recovery_root,
    std::string session_id)
    : recovery_root_(recovery_root.empty()
                         ? defaultRecoveryRoot()
                         : std::move(recovery_root)),
      session_id_(session_id.empty()
                      ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            .toUtf8().toStdString()
                      : std::move(session_id))
{
    recovery_root_ = normalizedPath(recovery_root_);
}

const std::filesystem::path& MotionRecoveryStore::recoveryRoot() const noexcept
{
    return recovery_root_;
}

const std::string& MotionRecoveryStore::sessionId() const noexcept
{
    return session_id_;
}

std::filesystem::path MotionRecoveryStore::savedProjectDirectory(
    const std::filesystem::path& project_path)
{
    if (project_path.empty()) return {};
    auto directory = normalizedPath(project_path);
    directory += ".autosave";
    return directory;
}

std::filesystem::path MotionRecoveryStore::currentUnsavedDirectory() const
{
    const std::filesystem::path session(session_id_);
    return recovery_root_ / "unsaved" / session;
}

void MotionRecoveryStore::saveSnapshot(
    const model::MotionProjectData& document,
    const std::filesystem::path& target_document_path,
    int retention)
{
    if (target_document_path.empty()) {
        throw MotionDocumentError(
            MotionDocumentErrorCode::InvalidValue,
            "A saved-project recovery snapshot requires its document path.",
            target_document_path);
    }
    saveSnapshotInDirectory(
        savedProjectDirectory(target_document_path),
        normalizedPath(target_document_path), session_id_, document, retention);
}

void MotionRecoveryStore::saveSnapshot(
    const model::MotionProjectData& document,
    int retention)
{
    saveSnapshotInDirectory(
        currentUnsavedDirectory(), {}, session_id_, document, retention);
}

void MotionRecoveryStore::saveSnapshotInDirectory(
    const std::filesystem::path& directory,
    const std::filesystem::path& target_document_path,
    const std::string& session_id,
    const model::MotionProjectData& document,
    int retention)
{
    if (retention < minimum_retention || retention > maximum_retention) {
        throw MotionDocumentError(
            MotionDocumentErrorCode::InvalidValue,
            "Recovery retention must be between 5 and 20 snapshots.", directory);
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) throwIo("Could not create the Motion Studio recovery directory.", directory, error);

    const auto timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz")).toUtf8().toStdString();
    const auto make_snapshot_path = [&](std::size_t sequence) {
        std::ostringstream name;
        name << "snapshot-" << timestamp << "-" << std::setw(10)
             << std::setfill('0') << sequence << ".motion-recovery";
        return directory / std::filesystem::path(name.str());
    };
    auto snapshot_path = make_snapshot_path(sequence_++);
    while (std::filesystem::exists(snapshot_path, error) && !error) {
        snapshot_path = make_snapshot_path(sequence_++);
    }
    if (error) throwIo("Could not choose a recovery snapshot path.", directory, error);

    MotionDocumentStore::saveRecovery(
        snapshot_path, target_document_path, session_id, document);
    pruneDirectory(directory, retention);
}

std::vector<MotionRecoverySnapshot> MotionRecoveryStore::snapshotsInDirectory(
    const std::filesystem::path& directory,
    const std::filesystem::path& target_document_path) const
{
    std::vector<MotionRecoverySnapshot> snapshots;
    if (directory.empty()) return snapshots;

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) return snapshots;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (!entry.is_regular_file(error) || error || !isSnapshotName(entry.path())) {
            error.clear();
            continue;
        }
        const auto modified = std::filesystem::last_write_time(entry.path(), error);
        if (error) {
            error.clear();
            continue;
        }
        snapshots.push_back({entry.path(), modified, target_document_path});
    }

    std::sort(snapshots.begin(), snapshots.end(),
        [](const auto& left, const auto& right) {
            if (left.modified_time != right.modified_time)
                return left.modified_time > right.modified_time;
            return left.path > right.path;
        });
    return snapshots;
}

std::vector<MotionRecoverySnapshot> MotionRecoveryStore::snapshotsForProject(
    const std::filesystem::path& project_path) const
{
    if (project_path.empty()) return {};
    const auto normalized = normalizedPath(project_path);
    return snapshotsInDirectory(savedProjectDirectory(normalized), normalized);
}

std::vector<MotionRecoverySnapshot> MotionRecoveryStore::validSnapshots(
    std::vector<MotionRecoverySnapshot> snapshots) const
{
    snapshots.erase(std::remove_if(snapshots.begin(), snapshots.end(),
        [](const MotionRecoverySnapshot& snapshot) {
            if (!isRegularFile(snapshot.path)) return true;
            try {
                const auto recovery = MotionDocumentStore::loadRecovery(snapshot.path);
                const auto expected_target = snapshot.target_document_path.empty()
                    ? std::filesystem::path{}
                    : creative_suite::media::MediaLibrary::canonicalPath(
                          snapshot.target_document_path);
                const auto actual_target = recovery.target_document_path.empty()
                    ? std::filesystem::path{}
                    : creative_suite::media::MediaLibrary::canonicalPath(
                          recovery.target_document_path);
                if (actual_target != expected_target) {
                    logInvalidSnapshot(snapshot.path,
                        "The recovery snapshot document path does not match its recovery location.");
                    return true;
                }
                return false;
            } catch (const MotionDocumentError& error) {
                logInvalidSnapshot(snapshot.path, error.what(),
                                   static_cast<int>(error.code()));
                return true;
            } catch (const std::exception& error) {
                logInvalidSnapshot(snapshot.path, error.what());
                return true;
            } catch (...) {
                logInvalidSnapshot(snapshot.path,
                    "A recovery snapshot could not be validated due to an unknown error.");
                return true;
            }
        }), snapshots.end());
    return snapshots;
}

std::vector<MotionRecoverySnapshot> MotionRecoveryStore::validSnapshotsForProject(
    const std::filesystem::path& project_path) const
{
    return validSnapshots(snapshotsForProject(project_path));
}

std::vector<MotionRecoverySnapshot> MotionRecoveryStore::recoverableSnapshotsForProject(
    const std::filesystem::path& project_path) const
{
    auto snapshots = validSnapshots(snapshotsForProject(project_path));
    std::error_code error;
    const auto project_time = std::filesystem::last_write_time(project_path, error);
    if (error) return {};

    std::optional<model::MotionProjectData> current_document;
    try {
        current_document = MotionDocumentStore::load(project_path);
    } catch (...) {
        // A valid snapshot remains available when the main project is damaged.
    }

    snapshots.erase(std::remove_if(snapshots.begin(), snapshots.end(),
        [&project_time, &current_document](const MotionRecoverySnapshot& snapshot) {
            if (snapshot.modified_time <= project_time) return true;
            if (!current_document.has_value()) return false;
            try {
                return MotionDocumentStore::loadRecovery(snapshot.path).document ==
                    *current_document;
            } catch (...) {
                return false;
            }
        }), snapshots.end());
    return snapshots;
}

std::vector<MotionRecoverySnapshot> MotionRecoveryStore::unsavedSnapshots() const
{
    std::vector<MotionRecoverySnapshot> snapshots;
    const auto root = recovery_root_ / "unsaved";
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return snapshots;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) break;
        if (!entry.is_directory(error) || error) {
            error.clear();
            continue;
        }
        auto session_snapshots = snapshotsInDirectory(entry.path(), {});
        snapshots.insert(snapshots.end(),
                         std::make_move_iterator(session_snapshots.begin()),
                         std::make_move_iterator(session_snapshots.end()));
    }
    snapshots = validSnapshots(std::move(snapshots));
    std::sort(snapshots.begin(), snapshots.end(),
        [](const auto& left, const auto& right) {
            if (left.modified_time != right.modified_time)
                return left.modified_time > right.modified_time;
            return left.path > right.path;
        });
    return snapshots;
}

bool MotionRecoveryStore::containsSnapshotData(
    const model::MotionProjectData& document,
    const std::filesystem::path& target_document_path) const
{
    const auto snapshots = target_document_path.empty()
        ? unsavedSnapshots()
        : validSnapshotsForProject(target_document_path);
    for (const auto& snapshot : snapshots) {
        try {
            if (MotionDocumentStore::loadRecovery(snapshot.path).document == document)
                return true;
        } catch (const MotionDocumentError& error) {
            logInvalidSnapshot(snapshot.path, error.what(),
                               static_cast<int>(error.code()));
        } catch (const std::exception& error) {
            logInvalidSnapshot(snapshot.path, error.what());
        }
    }
    return false;
}

void MotionRecoveryStore::pruneDirectory(
    const std::filesystem::path& directory,
    int retention) const
{
    const auto snapshots = snapshotsInDirectory(directory, {});
    for (std::size_t index = static_cast<std::size_t>(retention);
         index < snapshots.size(); ++index) {
        std::error_code error;
        std::filesystem::remove(snapshots[index].path, error);
    }
}

void MotionRecoveryStore::removeSnapshot(
    const std::filesystem::path& snapshot_path) const
{
    std::error_code error;
    std::filesystem::remove(snapshot_path, error);
    if (error) throwIo("Could not remove the recovery snapshot.", snapshot_path, error);
}

void MotionRecoveryStore::removeSnapshotsForProject(
    const std::filesystem::path& project_path) const
{
    const auto directory = savedProjectDirectory(project_path);
    if (directory.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) throwIo("Could not remove the project's recovery snapshots.", directory, error);
}

void MotionRecoveryStore::removeUnsavedSnapshotsForSession(
    const std::filesystem::path& snapshot_path) const
{
    if (snapshot_path.empty() || !isSnapshotName(snapshot_path)) return;
    const auto directory = normalizedPath(snapshot_path).parent_path();
    const auto unsaved_root = normalizedPath(recovery_root_ / "unsaved");
    if (directory.parent_path() != unsaved_root) return;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) throwIo("Could not remove the untitled recovery session.", directory, error);
}

void MotionRecoveryStore::removeCurrentUnsavedSnapshots() const
{
    const auto directory = currentUnsavedDirectory();
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) throwIo("Could not remove the current untitled recovery session.", directory, error);
}

} // namespace motion::persistence
