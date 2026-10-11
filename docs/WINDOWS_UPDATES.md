# Windows Update System

Status: **first Windows implementation built**. The catalog, shared Qt updater,
application integrations, release packaging script, and automated updater tests
are implemented. Inno Setup 6.7.3 successfully compiled all four full Windows
installers. A manual installer lifecycle run in a disposable Windows user
profile is still pending.

This is the current contract for the Hub, Video Editor, Image Editor, and Motion
Studio. Linux and macOS packaging and update behavior are future work.

## Release contract

The internal application IDs are stable: `hub`, `video-editor`,
`image-editor`, and `motion-editor`. Application names and versions are defined
once in [`../packaging/app-versions.json`](../packaging/app-versions.json).
CMake embeds those versions in each executable, and the Windows release script
uses the same file for installer metadata and `updates.json`.

Each manual GitHub Release contains `updates.json` and one complete installer
for each of the four applications. The suite release version is independent of
the application versions. If an application's version did not change, the
release script verifies and republishes the exact prior installer bytes; it
stops if that previous artifact is missing or disagrees with the previous
catalog.

The schema version 1 catalog has this shape:

```json
{
  "schema_version": 1,
  "suite_version": "2026.10.0",
  "applications": {
    "video-editor": {
      "version": "0.1.0",
      "installer_asset": "creative-suite-video-editor-setup.exe",
      "size_bytes": 123456789,
      "sha256": "<64 lowercase hexadecimal characters>",
      "release_notes": "Release notes for this application."
    }
  }
}
```

The real catalog includes all four IDs. The updater rejects unsupported schema
versions, missing or malformed entries, unsafe asset names, malformed semantic
versions, invalid sizes, and invalid SHA-256 values. It compares the installed
application's version with that application's catalog entry; a suite release
alone does not make an unchanged app appear out of date.

## Application behavior

Each editor checks and updates only its own installation. It offers a discreet
update action and a details dialog with release notes. The person chooses when
to download; declining leaves the offer available without reopening a modal on
every launch. The dialog reports download progress and offers cancellation and
retry. Downloads use a `.part` file, resume through HTTP range requests when
possible, and are accepted only after the expected byte count and SHA-256 match.
An incomplete or invalid file is never passed to the installer.

The Hub checks the three editor IDs and shows a separate action for each
installed app. It has no “Update all” action. A shared per-user lock allows at
most one suite download at a time. The Hub also has its own independent update
check and installer.

After the person confirms the download, the verified full installer is staged
and installation is scheduled for when the target application closes. If the
Hub starts an update for a closed editor, the installer can run immediately.
Every application continues to use the same full installer for first
installation and update; there is no second updater installer.

The shared runtime monitors pending editor IDs independently with one timer.
Scheduling another editor does not replace an existing wait. Startup recovery
enumerates every managed pending installation, so an open editor does not block
another editor whose installer is ready. Repeated scheduling/resume calls do
not duplicate monitored IDs. Success, disappearance of a pending record, or
failure removes only that ID; monitoring stops when none remain. A failed
launch is logged before its failure signal and its pending record remains for
an explicit retry. An occupied per-app installer launch lock defers that app
without reporting a launch or stopping the others. Each eligible app starts
independently; this does not add a suite-wide installation lock or bulk action.

Polling reads pending metadata while an editor is open, rather than hashing
every waiting full installer every 750 ms. The package is validated when staged
and revalidated under its launch lock before it is started. Persisted pending
records retain schema version 1, including the previous updater's records;
self-updates still wait for the owner's shutdown. `UpdateRuntime` and its
timer belong to their QObject thread. Its optional constructor accepts an
isolated update directory and a synchronous process launcher for integration
tests; the normal application constructor retains shared per-user storage and
native process launching. Tests replace only this process boundary, while
exercising real pending persistence, hashes, leases, and signals.

## Windows installation and recovery

The per-user Inno Setup installer asks for a destination on a first install.
For an existing registered installation it reuses that destination and the
updater starts setup silently after the application exits. Administrative
privileges are not required. Projects, preferences, and recovery snapshots are
outside the program files staged by the installer and must remain untouched.

Before replacing an existing app, the installer copies the installed files to
the per-user updater rollback directory and writes operation state to the
current user's registry. An installer failure attempts to restore the previous
files. After a successful setup, the updated app marks its first startup
healthy. If that confirmation is still pending when the Hub opens, the Hub can
offer restoration of the retained previous version. The complete GitHub
installer remains the recovery route if the installed updater cannot continue.

The Windows package includes the official Visual C++ runtime DLLs beside each
application executable so a per-user setup does not need to install a
machine-wide prerequisite. Microsoft supports this local deployment method,
but does not recommend it for most deployments because the application owner
must service those copies. A runtime refresh therefore requires rebuilding and
advancing the version of each affected app installer. See Microsoft's
[Visual C++ deployment methods](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method?view=msvc-170).

Update continuity is a release requirement: every still-supported Windows
version must be able to reach the latest compatible version directly or
through updater-managed intermediate releases. A catalog or updater compatibility
change must include a migration route and regression coverage from every
still-supported version. Release packaging refuses an unknown previous catalog
schema until that migration is implemented.

Catalog and package failures, network errors, cancellations, and install
failures are logged with diagnostic context. Logs must not include credentials,
private keys, or project content.

## Release packaging

[`../packaging/windows/Build-Installers.ps1`](../packaging/windows/Build-Installers.ps1)
stages the CMake install component for each app, compiles changed versions with
`creative-suite-app.iss`, reuses unchanged installer files, and writes the
catalog with actual file sizes and hashes. The release operator uploads the
four `.exe` files and `updates.json` to the same GitHub Release.

The script accepts the previous release's assets directory so that unchanged
installers can be verified and reused byte-for-byte. An app version must be
advanced when its installer contents change.

## Verification record

**2026-10-11 pending-install fix:** the new runtime regression failed before
the queue correction. Windows/Release built all four applications and the
affected consumer test executables. All 18 selected CTest entries passed in
26.97 seconds: three updater tests, eleven Hub tests, Video main-window
integration, Image UI, and Motion startup/UI. The runtime regression uses
synthetic packages and simulated launches. Packaged Inno Setup lifecycle,
Debug, and macOS/Linux execution of this change remain pending.

Automated coverage is registered in `libs/updater/tests/CMakeLists.txt`:

- `updater-catalog-test` covers strict version comparison, catalog validation,
  unsafe paths, integer package sizes, hashes, and asset URL resolution.
- `updater-service-test` covers network failure, resumable downloads,
  cancellation and resume, rejection of incomplete or incorrectly hashed
  packages, unchanged app versions, and the one-download-at-a-time lock.
- `updater-runtime-test` covers multiple open editors closing in either order,
  resuming all schema-1 pending records with managed IDs or file enumeration,
  mixed open/closed targets, deduplication, self-update deferral, per-app launch
  lock contention, removed records, failed launches and explicit retry,
  tampered packages, and normal first-install arguments. It uses isolated
  storage, real application leases, and simulated launches; it does not run
  installers or modify registered installations.
- `hub-catalog-test` covers the Hub's per-app published-version state.

Required manual Windows checks are indexed in the [Hub](hub/REGRESSION_TESTING.md),
[Video Editor](video-editor/REGRESSION_TESTING.md),
[Image Editor](image-editor/MANUAL_VALIDATION.md), and
[Motion Studio](motion-editor/ROADMAP.md) guides. They cover a clean install to
a chosen directory, updating into that same directory, preserving user data,
an interrupted or failed installer, rollback, Hub restoration, and verifying
that each editor updates only itself. These checks remain **pending** until
they are run against generated installers in a disposable Windows user
profile.

For the multi-app pending-install regression, keep two installed editors open
and download their updates sequentially from the Hub. Close the first
scheduled editor and confirm its setup starts while the other editor remains
usable. Close the second and confirm its setup also starts without restarting
the Hub. Repeat in reverse closure order; then restart the Hub with both
schema-1 pending records present and repeat. In a disposable profile, make one
setup launch fail and verify that the other app proceeds, the failed pending
record remains available for retry, and its log identifies the affected app.
These packaged lifecycle checks remain **pending**.

The current Release build passed **79/79 CTest tests**. Inno Setup 6.7.3
compiled the four installers and the script generated `updates.json` with the
measured package sizes and SHA-256 digests. The artifacts are in the ignored
local directory `build/windows-release-assets`; they have not been published.
Those local artifacts predate aligning the three editor versions to `0.1.0`,
so regenerate them during release preparation before uploading anything.

To build release assets, use the command and previous-release verification
requirements in [`../packaging/windows/README.md`](../packaging/windows/README.md).
