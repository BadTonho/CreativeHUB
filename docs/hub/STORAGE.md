# Hub Cache Cleanup

The Storage page discovers the suite/editor caches, the suite temporary cache,
and the local-data cache through `StorageManager::suiteCachePaths`. It displays
their logical file sizes and requests confirmation before cleanup. Cancelling
does not remove files, add activities, or produce error entries.

## Cleanup contract

`StorageManager::clearCachePaths` operates synchronously on explicit,
caller-owned cache directories and retains those root directories. It returns
`CacheCleanupResult` with successfully removed regular-file bytes and entry
count, a subsequent
remaining-size snapshot, whether that snapshot is complete, and failures with
path, operation, cause, and error code. The existing boolean `clearDirectory`
and `clearSuiteCache` methods delegate to this result and return its success.

Cleanup continues across independent files and roots after a failure. Nested
directories are removed only after their entries have been removed. Missing
roots are harmless; invalid roots are preserved and reported. Hidden entries
are included. Duplicate and nested roots do not double-count file sizes.
Symbolic links encountered inside a cache are removed as entries without
traversing their targets or counting target bytes; a symbolic-link root is
rejected. Cleanup must only be called for disposable cache roots, never project,
media, preference, or recovery directories.

Removed bytes are the logical sizes of files successfully removed, not the
entire pre-cleanup total or an assertion about physically reclaimed blocks.
Compression, hard links, and open handles can affect physical disk reclamation.
The final snapshot detects entries created during cleanup, including zero-byte
files. If a directory cannot be fully measured, the known remainder is reported
as a lower bound, rather than presenting an unknown amount as zero.
Removed-entry counts distinguish cleaning zero-byte files/empty directories
from an already-empty cache even when both report zero bytes removed.

Each technical failure is logged before the result reaches the UI, with UTC
timestamp, error severity, StorageManager operation, code/category, cause, and
path. A summary records removed bytes, remaining bytes, completeness, and
failure count. The UI and activity history use the same result: complete
success, partial cleanup, failure, or already-empty cache. Failures suggest
closing editors and retrying and refer to the log for detailed causes.

The optional `RemoveEntry` callback isolates deletion failures in tests; all
enumeration, measurement, and other removals remain real filesystem operations.
`StoragePage` accepts explicit roots for widget regressions. Production uses
the platform-derived suite roots and native filesystem deletion. No dependency
or persisted format is added by this change.

## Verification

- `hub-storage-manager-test` covers nested/hidden files, multiple roots,
  duplicates, partial failures, cause/path/error logging, retained roots,
  retry, zero-byte failures, invalid/missing roots, concurrent additions,
  preservation of unrelated files, and symbolic links when permitted by the OS.
  Permission-denied deletion is simulated deterministically on every platform.
- `hub-storage-page-test` drives the real confirmation dialog and checks
  cancellation, feedback, history, refreshed totals, success, and empty cache.
  On Windows it holds a real file handle denying deletion and verifies partial
  cleanup, total failure, and successful retry after releasing the handle.
  Other platforms exercise failure/unknown measurement with an invalid root.

Both targets are registered in `apps/hub/tests/CMakeLists.txt`. Native visual
acceptance, unreadable-directory/ACL behavior, Debug, and macOS/Linux execution
remain pending. Symbolic-link checks require permission to create native links;
an unavailable privilege is reported by the test rather than claimed as passing.

**2026-10-11 Windows/Release:** the widget regression failed before the fix,
announcing success with a real deletion-blocking handle still open. After the
fix, the Hub and affected tests built and all 15 Hub CTest entries passed in
12.56 seconds. The widget test confirmed partial/full failure, logged causes,
retry after releasing the handle, accurate totals/history, cancellation,
zero-byte-file cleanup, and already-empty feedback. Permission-denied removal,
overlapping roots and concurrent additions passed in the model test.
Native directory symlink creation was unavailable because the Windows process
lacked the required privilege; that branch remains pending. The executable
was rebuilt at `build/apps/hub/Release/creative-suite-hub.exe`, modified
2026-10-11 12:54:24 UTC. Tests ran outside the tool filesystem restriction and
only deleted generated files in temporary fixtures. Visual/ACL checks, Debug,
and macOS/Linux execution remain pending.

### Manual checklist

Use a disposable profile/cache containing only generated files. Record the
Hub executable, configuration, OS, fixture sizes, logs, and results.

1. Cancel cleanup and confirm files, history, and error logs are unchanged.
2. Clean nested/hidden files normally. Check that roots remain, the total
   updates, and the activity reports only the removed size. Repeat on empty
   cache and confirm the already-empty message.
3. Hold one file open without delete sharing on Windows, or deny deletion with
   appropriate permissions on Unix. Confirm other files are removed, the
   blocked file remains, and partial feedback/history show the removed and
   remaining sizes. Verify its path, operation, cause and code in the log.
4. Retry with only the blocked file left: the outcome must be failure with
   zero bytes removed. Release the handle/permissions and retry successfully.
5. Deny directory enumeration in a disposable cache. Confirm cleanup reports
   failure, the log identifies the directory and cause, and any incomplete
   remaining size is described as a lower bound. Restore access afterwards.
6. Check readable, wrapped feedback and the per-directory breakdown in the
   native Hub window. Keep projects/media outside the cache and verify their
   contents are unchanged throughout.
