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

The current trust model uses HTTPS transport and the SHA-256 published in the
same release catalog as the installer. The catalog is not digitally signed, so
the hash detects transfer or artifact mismatch but does not authenticate the
publisher if the release metadata and installer are both compromised.

Updater downloads, pending-install records, and rollback snapshots live in the
per-user persistent data directory. On first startup after this storage change,
the updater copies the former cache-based tree before resuming pending work. If
migration cannot complete, it continues using the old tree so a pending update
is not stranded. The Qt Widgets adapter is built inside each application; the
shared updater contains only service and runtime behavior.

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

The Hub refreshes each editor service's installed version, destination,
executable name, and registered-installation flag from the current registry
snapshot whenever it scans installations, including refreshes after setup. It
reads the snapshot again before launching setup and when opening update details.
The details action rechecks the catalog unless a check/download is already
active. Removing a registration clears the service's installed version and
destination and restores its catalog executable name; finding a development
binary still permits launching it, without treating it as a registered install.
First installation remains interactive; later updates use the newly registered
destination. A verified package already installed at that version or newer is
not launched again. An invalid registered version blocks setup with a local
error log. Successful Hub rollback also refreshes installation and catalog data.

`UpdateService::setInstallationState` is a synchronous QObject-thread operation
that changes only installation configuration. It retains active requests,
partial downloads, package identity, download locks, and release signal
references. Callers recheck the catalog to refresh the offer and validate live
installation state before scheduling setup. `MainWindow::UpdateDependencies`
accepts optional registry-reader, service, and runtime factories for isolated
application regressions; factories parent their objects to the supplied owner
and run on the GUI thread. Production uses the Windows registry, Qt network,
shared update directory, and native process launcher.

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
Hub restoration copies the snapshot into a temporary directory beside the
installation before replacing the installed tree. A failed staging copy leaves
the current installation untouched, and a successful restore removes files
that existed only in the newer version.

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

**2026-10-11 installation-refresh fix:** the Hub regression failed before the
fix because a refreshed service still held the previous installed version.
All four Release applications and affected tests rebuilt on Windows, and all
21 selected CTest entries passed in 33.25 seconds: 14 Hub tests, three updater
tests, Video main-window, Image UI, and Motion startup/UI. The new Hub test
uses synthetic installations/catalogs/packages and simulated setup launches;
the shared service tests retain controlled in-flight requests/downloads.
Native installer lifecycle and GUI acceptance, Debug, and macOS/Linux
execution remain pending.

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
  Installation refresh is covered during catalog requests and active downloads,
  including unchanged package identity/configuration and clearing old metadata.
- `updater-runtime-test` covers multiple open editors closing in either order,
  resuming all schema-1 pending records with managed IDs or file enumeration,
  mixed open/closed targets, deduplication, self-update deferral, per-app launch
  lock contention, removed records, failed launches and explicit retry,
  tampered packages, and normal first-install arguments. It uses isolated
  storage, real application leases, and simulated launches; it does not run
  installers or modify registered installations.
- `hub-catalog-test` covers the Hub's per-app published-version state.
- The Windows-only `hub-update-installation-refresh-test`, registered in
  `apps/hub/tests/CMakeLists.txt`, exercises the real Hub window, service and
  installation runtime with simulated registry/catalog/process boundaries.
  It covers all three editors, first install followed by another update without
  restart, relocated destinations/executables, removed registrations, rollback
  version snapshots, stale-package suppression, invalid-version logging,
  refresh during a download, and reopening the actual update dialog. Synthetic
  packages pass real pending-record and hash validation; native registrations
  are never written and no installers or editor processes run.

Required manual Windows checks are indexed in the [Hub](hub/validation/REGRESSION_TESTING.md),
[Video Editor](video-editor/validation/REGRESSION_TESTING.md),
[Image Editor](image-editor/validation/MANUAL_VALIDATION.md), and
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

For live installation refresh, use a disposable Windows profile and keep the
same Hub session open. Install each editor into a chosen directory, finish
setup, and choose Check updates: its current version must match the registration
and the installed release must not be offered again. Publish a newer compatible
test catalog and confirm the next update reuses that editor's registered
destination silently. Repeat after relocation and after restoring the previous
version. Remove an editor through its uninstaller and confirm setup returns to
interactive first-install behavior. If registration changes during a download,
setup must use the latest destination or skip an already installed package.
Reopen update details and verify the current-version label and offer agree.
These native installer/UI checks remain **pending**; the automated regression
uses controlled I/O rather than establishing packaged acceptance.

The current Release build passed **79/79 CTest tests**. Inno Setup 6.7.3
compiled the four installers and the script generated `updates.json` with the
measured package sizes and SHA-256 digests. The artifacts are in the ignored
local directory `build/windows-release-assets`; they have not been published.
Those local artifacts predate aligning the three editor versions to `0.1.0`,
so regenerate them during release preparation before uploading anything.

To build release assets, use the command and previous-release verification
requirements in [`../packaging/windows/README.md`](../packaging/windows/README.md).
