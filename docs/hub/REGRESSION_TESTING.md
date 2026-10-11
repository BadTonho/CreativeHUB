# Hub Regression Testing

This guide indexes behavior owned by the Hub. Shared update behavior and the
release contract are documented in [`../WINDOWS_UPDATES.md`](../WINDOWS_UPDATES.md).
Follow the repository-wide requirements in
[`../REGRESSION_POLICY.md`](../REGRESSION_POLICY.md).

## Coverage index

All plain C++ Hub tests and the shared updater catalog test use
`CS_TEST_CHECK` from `cmake/test_support/test_check.h`. Their conditions remain
active in Debug and Release, including file setup checks. The repository's
`creative-suite-test-checks` CTest probe verifies evaluation and a deliberate
failure with `NDEBUG` defined. A failed check reports its expression and source
location and returns a nonzero process exit code.

**2026-10-10 local verification:** Windows/Release rebuilt all ten Hub test
executables, the updater catalog test, three Video Editor test executables, and
the new check probe. The 16 selected CTest entries passed; the complete
registered suite passed 103/103 in 25.54 seconds. Direct execution of the
deliberate failed check exited with code 1 and reported its source location
and condition. Debug and macOS/Linux execution of this change remain pending.

| Behavior | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Published versions and per-app update status | `apps/hub/tests/test_app_catalog.cpp` (`hub-catalog-test`) | Hub UI refresh and version labels in a packaged Windows build: **pending**. |
| Catalog parsing, semantic version comparison, malformed assets and sizes, and URL resolution | `libs/updater/tests/release_catalog_test.cpp` (`updater-catalog-test`) | Covered by the shared updater test suite; actual GitHub Release availability remains a release operation. |
| Download, resume, cancellation, network failure, incomplete/hash rejection, and one-download lock | `libs/updater/tests/update_service_test.cpp` (`updater-service-test`) | Network interruption and resume through the visible Hub dialog: **pending**. |
| Individual editor updates from the Hub | No packaged installer integration test yet | Update each installed editor separately; verify there is no “Update all” action and that the other editors' versions do not change: **pending**. |
| Multiple pending editor installs and recovery on restart | `libs/updater/tests/update_runtime_test.cpp` (`updater-runtime-test`), registered in `libs/updater/tests/CMakeLists.txt`; real pending records and leases with simulated launches | Automated coverage present for closure order, resume, per-app failure, hash validation, launch contention, and deduplication. Follow the multi-app checklist in [WINDOWS_UPDATES.md](../WINDOWS_UPDATES.md); real Inno Setup lifecycle and Hub restoration remain **pending**. |
| Hub's own update | Shared updater component and Hub startup integration | Install and update the Hub itself without changing editor installations: **pending**. |
| Changelog discovery, semantic version sorting, and modal markdown display | `apps/hub/tests/test_changelog_reader.cpp` (`hub-changelog-reader-test`), `apps/hub/tests/test_app_details_modal.cpp` (`hub-details-modal-test`) | In Hub UI, open app details modal; verify changelog section renders notes or empty state gracefully: **covered**. |
| Recent projects tracking, file-to-app detection, recency reordering, JSON persistence, and launch delegation | `apps/hub/tests/test_recent_projects.cpp` (`hub-recent-projects-test`) | Open Projects tab in Hub; open project file via dialog or quick cards; verify list updates with recency order, app badges, and removal: **covered**. |
| Suite storage calculation, multi-directory cache discovery, byte formatting, and cache cleanup | `apps/hub/tests/test_storage_manager.cpp` (`hub-storage-manager-test`) | Select dedicated "Armazenamento" tab in sidebar; verify total cache size displays formatted bytes; inspect per-directory breakdown; trigger "Limpar Cache Seguro" and verify feedback and size update: **covered**. |
| Activity and notification center, event logging, read state tracking, and popup dropdown | `apps/hub/tests/test_activity_manager.cpp` (`hub-activity-manager-test`) | Click notification bell in Hub header bar; verify unread badge, categorized event list, "Marcar lidas", and "Limpar": **covered**. |
| Project backup vault, timestamped snapshot creation, storage configuration, and folder inspection | `apps/hub/tests/test_backup_manager.cpp` (`hub-backup-manager-test`) | Select dedicated "Backups" tab in sidebar; verify list of saved snapshots, folder changer, and "Abrir Pasta"; click "Backup" on project card in Projects tab: **covered**. |
| Snapshot relative references, supported document versions, and relocation for restoration | `apps/hub/tests/test_backup_manager.cpp` and `test_backup_formats.cpp` (`hub-backup-manager-test`, `hub-backup-format-test`), registered in `apps/hub/tests/CMakeLists.txt`; real Image/Video/Motion persistence producers and consumers | Automated coverage present. Native UI restoration checks and macOS/Linux execution remain **pending**; see [BACKUPS.md](BACKUPS.md) for the contract and manual steps. |
| Application discovery, sibling configuration directories, bundle paths, fallbacks, and installation-path priority | `apps/hub/tests/test_app_launcher.cpp` (`hub-launcher-test`, `hub-launcher-build-layout-test`), registered in `apps/hub/tests/CMakeLists.txt`; the integration test uses the enabled editors' actual CMake outputs | Automated coverage present. Native launch and project-open checks for each editor are documented below and remain **pending**, as do Debug and macOS/Linux execution. |

## Backup verification on 2026-10-10

The relative-image regression failed against the previous backup implementation
and passed after reference rebasing. Windows/Release built the Hub, all three
editor applications, and the affected test targets. All 14 selected CTest
entries passed: the eleven Hub tests and the Video project, Image core, and
Motion persistence tests. The new format integration test opens snapshots in
the vault and after relocation across all 46 supported schema versions, using
the editors' actual producers and consumers. Native UI, Debug, and macOS/Linux
validation remain pending; follow [BACKUPS.md](BACKUPS.md).

## Pending-install verification on 2026-10-11

The new `updater-runtime-test` failed before the queue fix. The shared updater,
all four Release applications, and affected consumer tests built on Windows.
The 18 selected CTest entries passed in 26.97 seconds, including multiple
editor waits, managed-ID and file-based schema-1 recovery, removed and failed
items, explicit retry, launch-lock contention, package tampering, and self-update
deferral. Actual installer execution, Debug, and macOS/Linux validation remain
pending. Follow the multi-app steps in [WINDOWS_UPDATES.md](../WINDOWS_UPDATES.md).

## Discovery verification on 2026-10-11

The canonical Release fixture failed before the discovery fix. After the fix,
the Hub and all three editor producer targets built in their canonical Release
directories, and all 12 Hub CTest entries passed on Windows in 5.08 seconds.
The integration check found the exact CMake target outputs for Video Editor,
Image Editor, and Motion Studio from the Hub's actual Release directory.
The tests ran outside the tool environment's filesystem restriction; no
installers or release packages were generated. Native GUI, Debug, and
macOS/Linux validation remain pending.

## Development executable discovery

The default `AppLauncher` resolves paths relative to the running Hub executable.
The explicit-directory constructor provides the same resolution for tests.
Search priority is:

1. Paths prepended by `addSearchPath`, including registered installation paths
   supplied by the Hub; the most recently added path takes precedence.
2. The Hub executable's own directory.
3. Sibling editor directories under `build/apps`, using the Hub's configuration
   first. For a single-configuration Hub under `build/apps/hub`, flat editor
   directories come first. Remaining configurations are checked in the order
   Release, Debug, RelWithDebInfo, MinSizeRel; duplicate configurations are
   removed. A custom configuration directly beneath `hub` is also recognized.
4. Flat sibling editor directories when the Hub has a configuration directory.
5. The standard per-user application-data `apps` directory.

Executable lookup accepts catalog names with or without `.exe`, and checks
`<name>.app/Contents/MacOS/<name>` in each search directory. A Hub inside a
macOS bundle resolves its sibling app root from the directory containing the
bundle. Candidates must be existing executable files. `setSearchPaths` replaces
the list entirely. Launching retains project arguments and uses the executable
directory as the process working directory.

The producer/consumer test builds the Hub and each enabled editor, then compares
the discovered path against CMake's actual target output. It runs without GUI
process launches or local installation registration. The fixture test covers
all three catalog apps, standard and custom configurations, flat outputs,
bundle layouts, fallback selection, wrong-depth binaries, explicit-path
priority, missing files, and directories masquerading as executables.
Windows fixtures use `.exe` files and exercise Hub bundle-root resolution with
native executable fixtures. Extensionless files and editor bundle executable
lookup are exercised by the fixture test on Unix platforms; their native
macOS/Linux execution remains pending.

### Manual development launch checklist

Use a disposable profile without registered editor installations, so discovery
uses the development outputs. Record configuration, OS, full executable paths,
and results. Do not remove registrations from the regular user profile.

1. Run `build/apps/hub/Release/creative-suite-hub.exe` directly. Confirm all three
   editors built under their sibling Release directories appear available.
2. Launch Video Editor, Image Editor, and Motion Studio separately from the Hub.
   Confirm each starts from its corresponding Release directory. Close them.
3. Open a saved `.csp`, `.cimg`, and `.motion` project through the Hub. Confirm
   the intended editor receives the project path and opens the expected data,
   including when the path contains spaces.
4. With both Debug and Release outputs present, repeat from the Debug Hub and
   confirm that each Debug editor takes precedence. If that editor's matching
   binary is absent, verify the documented fallback is selected.
5. Repeat native launch/project-open checks for Linux single-configuration
   outputs and macOS app bundles, recording their actual process paths.

**Current result:** Native UI launch/project-open checks, Debug execution, and
macOS/Linux execution remain pending. Synthetic bundle layouts do not establish
native macOS launch acceptance.

## Manual Windows checklist

Run against generated installers in a disposable per-user profile. Record the
Windows build, installer versions, selected install paths, steps, and outcome.

1. Install each application into a different user-chosen directory. Confirm the
   Hub discovers the registered installation and displays the installed
   version.
2. Publish a higher version for one editor and start its update from that
   editor. Confirm its dialog shows release notes, download progress, cancel,
   and retry controls. Confirm only that editor is updated.
3. Repeat from the Hub. Confirm each editor has its own action, starting one
   download prevents a second simultaneous suite download, and no bulk update
   action is present.
4. Confirm the full installer runs after the target app closes, reuses the
   selected install path, and preserves projects, preferences, and recovery
   snapshots. Confirm the currently open app remains usable until it exits.
5. Interrupt a download and resume it. Cancel another download and verify that
   the partial file is not installed. Test an invalid catalog and a mismatched
   package hash; both must leave the installed app unchanged.
6. Cause setup to fail and verify automatic restoration. Simulate a new app
   failing its first startup and confirm the Hub offers restoration of the
   previous version. On a successful first launch, confirm **Está funcionando**
   clears the pending startup check so the prompt does not repeat.
7. Update the Hub itself and verify that editor versions and installation
   directories remain unchanged.

**Current result:** Inno Setup 6.7.3 compiled all four installers. The manual
install, update, cancellation, rollback, and Hub restoration checklist remains
pending until it is run in a disposable Windows user profile.
