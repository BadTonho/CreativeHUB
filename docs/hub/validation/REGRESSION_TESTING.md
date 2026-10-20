# Hub Regression Testing

Current backup gates save Motion v17 and reject v18. Version-specific sections
below retain the records and evidence from earlier deliveries.

Gaussian Blur backup coverage uses a real CSP v31 producer/reader with
fractional radius, disabled state, shared mask and external Input paths in
`hub-backup-format-test`. `hub-backup-manager-test` rejects CSP32/Motion18.

Composition interchange persistence is covered at the backup boundary by `hub-backup-format-test` (CSP v31/Motion v17 Drop Shadow, Trim Paths and Bezier paths/keys, matte bindings, blend mode, Scale Y/link/affine binding, Anchor, hierarchy and presentation records, shared masks/stage associations and path rebasing) and `hub-backup-manager-test` (future v32/v18 rejection). See [interchange](../../COMPOSITION_INTERCHANGE.md) for producer/consumer and pending manual checks.

## Motion v13 blend-mode backup coverage

`hub-backup-format-test` saves a real native v13 document with Overlay, independent
scale, affine binding, Anchor/Null hierarchy, masks/effects and linked media;
the actual Motion reader reopens vault and relocated restore. Both backup gates
reject v14. Manual Hub UI/platform acceptance remains pending: back up, restore
to another directory and reopen in Motion to compare mode, geometry and media.
See [Blend modes](../../motion-editor/features/BLEND_MODES.md).

## Motion hierarchy backup verification

### Motion scale axes and affine compensation

`hub-backup-format-test` writes native v12 with independent Y/Bezier keys, link
state, affine compensation, Anchor, Null hierarchy, masks/effects and linked
media. It reopens vault and relocated restore with the Motion reader and verifies
media paths. `hub-backup-manager-test` rejects future v13. Manual acceptance:
back up a v12 project in Hub, restore to another directory, reopen in Motion and
compare animation/links/media; visual and other-platform checks remain pending.
Both backup gates passed in the 2026-10-17 Release regression selection. See
[Scale evidence](../../motion-editor/features/SCALE_AXES.md#automated-windows-evidence--2026-10-17)
and [pending checks](../../motion-editor/features/SCALE_AXES.md#pending-manual-checks).

### Motion Anchor Point, 2026-10-17

Windows Release passed both backup gates with native Motion v11, base and Bezier
Anchor curves, an anchored Null, parent compensation, masks/effects and
linked-image references. Actual Motion persistence reopens vault and relocated
restore documents; the manager rejects future v12. The final 16-test Motion/Hub
integration selection passed in 10.55 seconds. Hub Release is at
`build/apps/hub/Release/creative-suite-hub.exe`, modified 17:06:21 UTC−03:00
(825,344 bytes). Native UI/platform acceptance remains pending. See
[Anchor evidence](../../motion-editor/features/ANCHOR_POINT.md#automated-windows-evidence).

### Earlier hierarchy delivery

Motion hierarchy evidence (2026-10-17, Windows Release): both backup gates
passed with `.motion` v10 Null layers, parent IDs, compensation, animated
transforms, shared masks/effects and linked-image media. The actual Motion
reader reopened the vault and relocated restore. Hub Release was rebuilt at
16:34:20 UTC−03:00; native backup UI acceptance remains pending. See
[Motion hierarchy evidence](../../motion-editor/features/HIERARCHY.md#automated-windows-evidence--2026-10-17).

## Background Generator backup verification

`hub-backup-format-test` produces current CSP v31 with a named Background composition,
generators, mask animation, external Input paths and a signed composition offset.
Both vault and relocated restore are reopened through the Video Editor reader
and compared with the producer document. The existing version loop covers older
schemas; `hub-backup-manager-test` rejects future v32. Native backup/restore
acceptance remains separate and pending. Both backup gates passed in the
2026-10-14 Windows Debug run, and `build/apps/hub/Debug/creative-suite-hub.exe`
was rebuilt at 10:14:20 UTC−03:00 (2,305,024 bytes).

## Fusion mask backup verification

`hub-backup-format-test` creates current CSP with an Ellipse Mask, inversion, softness,
Bezier geometry animation, a Mask connection, generators, external Input media
and signed composition time offset. Snapshot and relocated restore are compared
through the Video Editor reader. Legacy schemas remain covered by the existing
version loop. `hub-backup-manager-test` rejects future v32 with diagnostics.
Native backup/restore acceptance on Windows, macOS and Linux remains separate.
Both v28 backup gates passed in the 2026-10-14 Windows Debug run. The canonical
`build/apps/hub/Debug/creative-suite-hub.exe` was rebuilt (00:25:49 UTC−03:00).

## Fusion Composition backup coverage

`hub-backup-format-test` creates current CSP with masks, Background, Text, an external Input
and a signed composition offset. Snapshot and relocated restore are reopened by
the Video Editor reader and compared to the normalized producer document.
`hub-backup-manager-test` rejects future v32. Manual/native restore acceptance
remains separate from the automated format contract.
Both backup tests passed in the 2026-10-14 Windows Debug gate; the canonical
Hub application was rebuilt and its v27 consumer contract passed through the
Video Editor reader.


The Video Editor v27 producer/consumer backup gate includes Fusion Composition and adjustment clips
and the nonzero Motion publication positions introduced in v25.
Run `hub-backup-format-test` for snapshot/restoration
round-trips and `hub-backup-manager-test` for rejection of future v32 documents.

This guide indexes behavior owned by the Hub. Shared update behavior and the
release contract are documented in [`../WINDOWS_UPDATES.md`](../../WINDOWS_UPDATES.md).
Follow the repository-wide requirements in
[`../REGRESSION_POLICY.md`](../../REGRESSION_POLICY.md).

## Coverage index

Motion Anchor backup coverage uses `test_backup_formats.cpp`
(`hub-backup-format-test`) with the current v12 writer/reader round trips of base and
Bezier-animated Anchor, an anchored Null, parent IDs/compensation, masks/effects
and linked-image paths. `hub-backup-manager-test` rejects v13. Vault and relocated
restore comparisons protect retained data and media references; native backup UI
and platform acceptance remain pending in [Backups](../BACKUPS.md) and the
[Anchor checklist](../../motion-editor/features/ANCHOR_POINT.md#pending-manual-checks).

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

**2026-10-15 translation integration verification:** Windows/Release passed
seven selected CTest entries: `creative-suite-translations`,
`creative-suite-translation-updater`, `hub-catalog-test`,
`hub-card-widget-test`, `hub-details-modal-test`, `hub-storage-page-test`, and
`hub-backups-page-test`. The Hub catalog build generated all 195 pt-BR entries
and confirmed that the tracked `.qm` matches its `.ts`; the published
manifest's size and SHA-256 also matched. All four application executables
compiled, linked, and completed Qt runtime deployment under their canonical
`build/apps/<application-id>/Release` directories. Native startup, offline
behavior, manual update, and UI acceptance remain pending.

**2026-10-15 per-app language selection verification:** Windows/Release
compiled and linked all four application executables in their canonical
`build/apps/<application-id>/Release` directories. Seven focused CTest entries
passed: `creative-suite-translations`, `hub-language-settings-page-test`,
`creative-suite-main-editor-settings-dialog`,
`creative-suite-main-editor-translation-catalog`,
`creative-suite-image-editor-ui`,
`creative-suite-image-editor-translation-catalog`, and
`creative-suite-motion-editor-translation-catalog`. All four tracked `.qm`
catalogs matched their `.ts` sources, and the manifest hash/size checks passed.
The translation updater's loopback HTTP test could not be revalidated in this
restricted environment: its local request failed with `Permission denied`.
The CMake post-build Qt deployment command also could not query `qtpaths`, so
packaged runtime deployment and native UI validation remain pending.

| Behavior | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Translation catalog parsing, per-app language preferences, conditional update checks, Qt loading precedence, download, repeat checks, and preservation after invalid data | `libs/translations/tests/translation_catalog_test.cpp` (`creative-suite-translations`), `translation_update_service_test.cpp` (`creative-suite-translation-updater`), and `apps/hub/tests/test_language_settings_page.cpp` (`hub-language-settings-page-test`), registered in their CMake files; Hub source checks the tracked `.qm` against its `.ts` at build time | Automated coverage exercises per-app persistence/defaults, explicit language overrides, pt-BR update decisions, manifest validation, valid and invalid QM data, atomic install, catalog fallback, HTTP success/error/interruption, and repeat checks. Hub offline startup, manual check, and next-launch use after a `main` update remain **pending**; follow [translation acceptance](../../TRANSLATIONS_AND_LANGUAGE_PACKS.md#regression-and-acceptance). |
| Live registered-installation refresh and next-update installer arguments | `apps/hub/tests/test_update_installation_refresh.cpp` (`hub-update-installation-refresh-test`, Windows only), registered in `apps/hub/tests/CMakeLists.txt`; `libs/updater/tests/update_service_test.cpp` (`updater-service-test`) | Automated coverage present for all three editors, first install, a subsequent update without restart, version/path/executable changes, removed registrations, rollback snapshots, stale-package suppression, invalid-version logging, active downloads, and reopening update details. Native installer lifecycle and GUI acceptance remain **pending**; follow [WINDOWS_UPDATES.md](../../WINDOWS_UPDATES.md). |
| Published versions and per-app update status | `apps/hub/tests/test_app_catalog.cpp` (`hub-catalog-test`) | Hub UI refresh and version labels in a packaged Windows build: **pending**. |
| Catalog parsing, semantic version comparison, malformed assets and sizes, and URL resolution | `libs/updater/tests/release_catalog_test.cpp` (`updater-catalog-test`) | Covered by the shared updater test suite; actual GitHub Release availability remains a release operation. |
| Download, resume, cancellation, network failure, incomplete/hash rejection, and one-download lock | `libs/updater/tests/update_service_test.cpp` (`updater-service-test`) | Network interruption and resume through the visible Hub dialog: **pending**. |
| Individual editor updates from the Hub | No packaged installer integration test yet | Update each installed editor separately; verify there is no “Update all” action and that the other editors' versions do not change: **pending**. |
| Multiple pending editor installs and recovery on restart | `libs/updater/tests/update_runtime_test.cpp` (`updater-runtime-test`), registered in `libs/updater/tests/CMakeLists.txt`; real pending records and leases with simulated launches | Automated coverage present for closure order, resume, per-app failure, hash validation, launch contention, and deduplication. Follow the multi-app checklist in [WINDOWS_UPDATES.md](../../WINDOWS_UPDATES.md); real Inno Setup lifecycle and Hub restoration remain **pending**. |
| Hub's own update | Shared updater component and Hub startup integration | Install and update the Hub itself without changing editor installations: **pending**. |
| Changelog discovery, semantic version sorting, and modal markdown display | `apps/hub/tests/test_changelog_reader.cpp` (`hub-changelog-reader-test`), `apps/hub/tests/test_app_details_modal.cpp` (`hub-details-modal-test`) | In Hub UI, open app details modal; verify changelog section renders notes or empty state gracefully: **covered**. |
| Recent projects tracking, file-to-app detection, recency reordering, JSON persistence, and launch delegation | `apps/hub/tests/test_recent_projects.cpp` (`hub-recent-projects-test`) | Open Projects tab in Hub; open project file via dialog or quick cards; verify list updates with recency order, app badges, and removal: **covered**. |
| Suite storage calculation, cache cleanup results, failure logs, feedback, and activities | `apps/hub/tests/test_storage_manager.cpp`, `test_storage_page.cpp` (`hub-storage-manager-test`, `hub-storage-page-test`), registered in `apps/hub/tests/CMakeLists.txt` | Automated coverage present for removal/remaining bytes, multiple/nested/duplicate roots, hidden files, partial/total failure, retry, cancellation, empty cache, invalid roots, new files during cleanup, and error/history consistency. The Windows widget test uses a real file handle denying deletion. Native visual, directory ACL, Debug, and macOS/Linux checks remain **pending**; follow [STORAGE.md](../STORAGE.md). |
| Activity and notification center, event logging, read state tracking, and popup dropdown | `apps/hub/tests/test_activity_manager.cpp` (`hub-activity-manager-test`) | Click notification bell in Hub header bar; verify unread badge, categorized event list, "Marcar lidas", and "Limpar": **covered**. |
| Backup identity, unique creation, persisted source paths, legacy compatibility, and origin labels | `apps/hub/tests/test_backup_manager.cpp`, `test_backups_page.cpp` (`hub-backup-manager-test`, `hub-backups-page-test`), registered in `apps/hub/tests/CMakeLists.txt` | Automated coverage present for same-named projects, repeated creation, restart, malformed records, source labels, and unknown legacy origins. Native folder-opening and new label presentation remain **pending**; follow [BACKUPS.md](../BACKUPS.md). |
| Snapshot relative references, supported document versions, and relocation for restoration | `apps/hub/tests/test_backup_manager.cpp` and `test_backup_formats.cpp` (`hub-backup-manager-test`, `hub-backup-format-test`), registered in `apps/hub/tests/CMakeLists.txt`; real Image/Video/Motion persistence producers and consumers | Automated coverage present. Native UI restoration checks and macOS/Linux execution remain **pending**; see [BACKUPS.md](../BACKUPS.md) for the contract and manual steps. |
| Application discovery, sibling configuration directories, bundle paths, fallbacks, and installation-path priority | `apps/hub/tests/test_app_launcher.cpp` (`hub-launcher-test`, `hub-launcher-build-layout-test`), registered in `apps/hub/tests/CMakeLists.txt`; the integration test uses the enabled editors' actual CMake outputs | Automated coverage present. Native launch and project-open checks for each editor are documented below and remain **pending**, as do Debug and macOS/Linux execution. |

## Cache cleanup verification on 2026-10-11

The new storage widget regression failed before the fix: the success message
appeared while a real Windows file handle prevented cache deletion. The Hub
and affected tests rebuilt in Release, and all 15 Hub CTest entries passed in
12.56 seconds. Tests verify partial/full failure, real unlock-and-retry,
removed/remaining bytes, root preservation, history and feedback consistency,
cancellation, zero-byte files, empty caches, invalid roots, deterministic denied
removal, duplicate/nested roots, error logs, and concurrent cache additions.
Native directory symlink creation lacked the required Windows privilege and
remains pending, along with visual/ACL acceptance, Debug, and macOS/Linux
execution. The canonical Hub executable was rebuilt at
`build/apps/hub/Release/creative-suite-hub.exe` (2026-10-11 12:54:24 UTC).
Follow [STORAGE.md](../STORAGE.md) for the cleanup contract and manual checks.

## Installation refresh verification on 2026-10-11

The new Hub window regression failed before the fix: its service retained
0.1.0 after the simulated installation changed to 0.1.1 and the Hub refreshed.
After the fix, all four Release applications and the affected tests built.
All 21 selected CTest entries passed on Windows/Release in 33.25 seconds:
14 Hub tests, three shared updater tests, Video main-window, Image UI, and
Motion startup/UI. Tests ran outside the tool filesystem restriction with
isolated registry/catalog/process adapters; no installer or release package
was generated or executed. The canonical Hub executable was rebuilt at
`build/apps/hub/Release/creative-suite-hub.exe` (2026-10-11 12:38:33 UTC).
Native installer lifecycle, GUI acceptance, Debug, and macOS/Linux execution
remain pending. Follow the live-refresh checklist in
[WINDOWS_UPDATES.md](../../WINDOWS_UPDATES.md).

## Backup verification on 2026-10-10

The relative-image regression failed against the previous backup implementation
and passed after reference rebasing. Windows/Release built the Hub, all three
editor applications, and the affected test targets. All 14 selected CTest
entries passed: the eleven Hub tests and the Video project, Image core, and
Motion persistence tests. The new format integration test opens snapshots in
the vault and after relocation across all 46 supported schema versions, using
the editors' actual producers and consumers. Native UI, Debug, and macOS/Linux
validation remain pending; follow [BACKUPS.md](../BACKUPS.md).

## Backup identity verification on 2026-10-11

The same-name regression failed before the fix: querying another project's
path returned the first project's snapshot. After the fix, the Hub Release
executable and all affected tests built. All 16 selected CTest entries passed
on Windows/Release in 12.11 seconds: the 13 Hub tests plus Video project,
Image core, and Motion persistence. The tests ran outside the tool filesystem
restriction. The actual three-editor persistence integration still passes all
46 supported schemas with the new per-backup directories.

Coverage includes persisted source/timestamp after reopening, distinct source
directories/extensions, repeated creation across manager instances, names
containing `_backup_`, relative/Windows case aliases, missing originals, complete
record relocation to another vault, legacy visibility without false ownership,
malformed/unsafe records with logs, ignored unfinished records, size accounting,
and source/unknown-origin labels in widgets. Native visual checks, folder
opening, Debug, and macOS/Linux execution remain pending; follow
[BACKUPS.md](../BACKUPS.md).

## Pending-install verification on 2026-10-11

The new `updater-runtime-test` failed before the queue fix. The shared updater,
all four Release applications, and affected consumer tests built on Windows.
The 18 selected CTest entries passed in 26.97 seconds, including multiple
editor waits, managed-ID and file-based schema-1 recovery, removed and failed
items, explicit retry, launch-lock contention, package tampering, and self-update
deferral. Actual installer execution, Debug, and macOS/Linux validation remain
pending. Follow the multi-app steps in [WINDOWS_UPDATES.md](../../WINDOWS_UPDATES.md).

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

## Adjustment document backup gate

`tests/test_backup_formats.cpp` (`hub-backup-format-test`) produces v26 through
the Video Editor writer, relocates the snapshot, and restores it through the
Video Editor reader. It asserts the full normalized document, including
adjustment effects, opacity and scale curves, while retaining linked-image,
Fusion Input and Motion publication references on ordinary clips. The existing
versions 1–25 producer/consumer cases remain in this gate.
`hub-backup-manager-test` rejects future v32 and malformed documents with diagnostics.

On 2026-10-13, the canonical Hub Debug target built successfully and both
backup tests passed as part of the scoped Adjustment Clip gate. The Video
Editor consumer also passed its v26 persistence, autosave/recovery and
MainWindow document-state tests. This is automated contract evidence; it does
not replace the following native UI check.

Manual acceptance: save a project with an adjustment and media below it in the
Debug Video Editor, back it up in the Debug Hub, restore it into another folder,
and reopen it. The same adjustment and lower media must render; no missing-media
prompt may identify the adjustment. Native UI and macOS/Linux validation remain
pending until recorded with the build and platform.

## Motion v14 Track Matte backup coverage

The native producer/reader gate `hub-backup-format-test` saves a Luma Inverted
binding and hidden source alongside independent scale, affine parenting, Anchor,
effects/masks and media. It reopens vault and relocated restore with full data
equality and existing media paths. Both backup gates reject future v15.
Earlier delivery sections retain their version-specific history. Manual UI and
platform checks remain pending; see [mattes](../../motion-editor/features/TRACK_MATTES.md).

## Motion v15 Bezier backup coverage

`hub-backup-format-test` saves animated shapes and shared Bezier masks alongside
previous Motion features, reopens vault/restored documents, checks full equality
and existing media paths. Its original delivery rejected future v16; the current
v16 boundary is recorded below. Manual checks remain pending.

Windows Release evidence (2026-10-19): both Hub backup gates passed in the
53/53 affected-suite regression run for Motion Pen paths. The explicit Release
targets for all four applications built successfully; native Hub UI/platform
acceptance remains pending. See [Pen verification](../../motion-editor/features/BEZIER_PATHS.md#windows-release-evidence--2026-10-19)
for the full boundary and separate GPU evidence.

## Motion v16 Trim Paths backup coverage

`hub-backup-format-test` writes activated Trim Paths with wrapped Start/End and
animated full-turn Offset alongside path geometry/keys, shared masks, hierarchy,
blend/matte records and real media paths. It reopens vault/restored payloads with
the Motion reader, compares full data and verifies rebased media. Current future
rejection is v17 in both backup gates. Older sections retain historical evidence;
native UI and platform acceptance remain pending.

Sources: [producer/reader backup gate](../../../apps/hub/tests/test_backup_formats.cpp),
[manager rejection gate](../../../apps/hub/tests/test_backup_manager.cpp) and
[CTest registration](../../../apps/hub/tests/CMakeLists.txt).

The Windows Release execution on 2026-10-19 passed both backup gates within the
40/40 affected-test gate. Motion and Hub were built at their canonical Release
paths; see the [Trim Paths execution record](../../motion-editor/features/TRIM_PATHS.md#windows-release-execution-2026-10-19)
for output times, separate GPU evidence and pending manual acceptance.

## Motion v17 Drop Shadow backup coverage

[Producer/reader coverage](../../../apps/hub/tests/test_backup_formats.cpp) saves
RGB shadows, full-turn angles, all four curves and shared effect masks alongside
Trim Paths, hierarchy and real media. It reopens vault/restored payloads and
compares complete Motion data plus relocated paths.
[Manager coverage](../../../apps/hub/tests/test_backup_manager.cpp) rejects v18;
[CTest registration](../../../apps/hub/tests/CMakeLists.txt) retains both gates.
Older sections are historical delivery records. Native UI/platform checks remain
pending; see [Drop Shadow](../../motion-editor/features/DROP_SHADOW.md).

On 2026-10-20, both backup gates passed within the **59/59** affected Windows
Release gate. The four applications were built at their canonical Release paths;
see the [Drop Shadow execution record](../../motion-editor/features/DROP_SHADOW.md#windows-release-execution-2026-10-20)
for output times, separate native GPU/fallback evidence and pending acceptance.

## Image Editor v17 outline backup coverage

`hub-backup-format-test` covers Image schemas 1-17 and preserves an outlined
group child with its source/imported paths. The actual reader compares complete
vault/restored documents. `hub-backup-manager-test` rejects future Image v20.
Manual follow-up: back up an outlined image, restore to another directory, open
in Image Editor, and confirm outline settings and source links. Native acceptance
remains pending.

## Image Editor v18 clipping backup coverage

`hub-backup-format-test` exercises actual Image producer/reader round trips for
schemas 1-18 and a v18 masked/outlined chain in a group, including vault and
restored payload equality and media-path rebasing. `hub-backup-manager-test`
rejects future Image v20. Manual Hub backup/restore UI acceptance is pending;
see the [Image checklist](../../image-editor/validation/MANUAL_VALIDATION.md#clipping-masks).

Windows Release evidence (2026-10-20): both Hub backup gates passed in the final
24-test Image/Hub/Video selection. Motion linked references in the shared format
fixture are materialized so canonical-path checks exercise existing paths.
Native backup/restore UI and macOS/Linux acceptance remain pending.

## Image Editor v19 blend backup coverage

`hub-backup-format-test` round trips actual Image schemas 1-19 including group,
base and clipped-child modes, metadata equality and relocated media. The manager
gate rejects future Image v20. Native Hub UI/macOS/Linux acceptance is pending.

Windows Release evidence (2026-10-20): both v19 backup gates passed in the
38-gate blend-mode selection; canonical Hub Debug was rebuilt successfully.
See [Image execution evidence](../../image-editor/features/BLEND_MODES.md#windows-execution-evidence--2026-10-20).
