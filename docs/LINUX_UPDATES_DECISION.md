# Linux Distribution and Update Decision

Status: **provisional proposal, not implemented**. Nothing in this document is
final until the maintainer accepts it and the validation below passes.

## Problem

Windows users update every application from the Hub through `updates.json` and
a per-user installer (see [Windows Update System](WINDOWS_UPDATES.md)). Linux
release packages are currently manual-download `.tar.gz` archives
([`../packaging/linux/README.md`](../packaging/linux/README.md)) that the
updater cannot see. `AGENTS.md` requires every supported OS to keep a path to
the latest release without stranding the user, so Linux users should be able to
update without downloading from GitHub.

## Constraints from the current implementation

These were read from `libs/updater` and `docs/WINDOWS_UPDATES.md`; verify them
again before implementing.

- Installed clients fetch
  `https://github.com/BadTonho/CreativeHUB/releases/latest/download/updates.json`.
  That URL is baked into shipped builds and cannot be changed for them. `latest`
  resolves to the newest published, non-draft, non-prerelease release, so the
  draft-first release workflow does not affect it until publication.
- `ReleaseCatalog::fromJson` accepts only `schema_version == 1`, requires
  `installer_asset`, `size_bytes`, and `sha256` per app, and ignores unknown
  keys. A new schema version would make every installed Windows client reject
  the catalog.
- Installation state (path, version, rollback path, launch check) lives in the
  Windows registry. On non-Windows builds `readInstalledApplication` returns
  nothing, and `isApplicationRunning` relies only on a lock file.
- The updater downloads to a staging directory, verifies size and SHA-256, then
  launches the installer. There is no Linux install step.
- Trust is HTTPS plus the hash in the same release. Neither catalog nor
  artifacts are signed. The same limit applies to Linux unless signing is added.
- Each release must carry every asset, because `latest/download/<asset>` is
  resolved per release. Windows reuses unchanged installers byte-for-byte; the
  Linux packaging must do the same.

## Options

| Option | Benefits | Costs and risks |
| --- | --- | --- |
| **A. Own tarball updater** (per-user managed install, Hub downloads, verifies, swaps) | Mirrors the Windows model; no new infrastructure; works on every distro; reuses catalog, download, hash, and rollback logic; full control over rollback | We maintain installation logic; no desktop integration; unsigned; every app must bundle or tolerate differing system libraries |
| **B. AppImage + AppImageUpdate (zsync)** | One self-contained file per app; delta updates; common for Linux desktop tools | Large bundles (Qt + FFmpeg); needs FUSE 2 on many current distros; update tooling is external; the Hub still needs to know about the files |
| **C. Flatpak (Flathub or own remote)** | Updates handled by the system; sandboxed; wide reach | Sandbox limits on GPU, codecs, and file access need work; Flathub review and policy; build recipe to maintain; the in-app updater must be disabled inside Flatpak |
| **D. Native `.deb`/`.rpm` with a signed repository** | Fits distro tooling and system updates | Per-distro maintenance and hosting; signing key management; most work of all options |

Alternatives are not exclusive: Flatpak or native packages could later be added
as extra channels. Inside such a channel the in-app updater must stay disabled
so two mechanisms never update one installation.

## Recommendation (provisional)

Choose **A** for the baseline, and keep the tarballs as manual downloads too:

- It reuses the most existing, tested updater code and needs no hosting beyond
  GitHub Releases.
- It keeps the project local-first and distro-neutral, matching `AGENTS.md`.
- It does not block adding C or D later as additional channels.

Revisit if bundling libraries per app proves too heavy (then consider B) or if
distro-integrated installs become the main need (then C or D).

## Proposed design for option A

### Catalog compatibility

Publish a **separate `updates-linux.json`** with the same schema version 1 and
field layout as `updates.json`, plus an `architecture` key (ignored by the
current parser, so it needs no change to validation). Linux clients request
`updates-linux.json`; Windows clients keep requesting `updates.json` unchanged.

Rejected alternative: adding per-platform entries inside `updates.json`. Old
Windows clients ignore unknown keys, so it would work, but it couples both
platforms to one file and risks confusing the required top-level fields.

An app's Linux asset follows the same rule as Windows: unchanged versions reuse
the exact previously published archive, and a version must advance when the
archive contents change.

### Managed install layout and state

The Hub installs and updates apps under a per-user root such as
`~/.local/share/Tonho Studios/Creative Suite/Apps/<app-id>/<version>/`, with a
`current` symlink switched by an atomic rename. Rollback repoints the symlink to
the retained previous version. A JSON state file next to it records install
path, version, executable, rollback path, and a "needs launch check" flag, the
Linux equivalent of the registry values.

### Install, verify, and recover

1. Download the archive to the existing staging directory, verify size and
   SHA-256 (existing code).
2. Extract into a new version directory, rejecting absolute paths, `..`, and
   links that escape the target.
3. Confirm the expected executable exists and is runnable.
4. Switch `current`, mark "needs launch check", and relaunch.
5. On first successful start the app clears the flag; otherwise the Hub offers
   rollback, as it does on Windows.

A failure at any step leaves `current` untouched. Detect a running instance
through the existing lock file; replacing files is safe on Linux but the user is
still asked to restart.

### Open questions

- **First installation:** how does a manually extracted Hub become a managed
  one? Candidates: the Hub offers to install a managed copy of itself, or a tiny
  documented bootstrap script does so.
- **Hub self-update:** the Hub can switch its own `current` link and restart;
  confirm that works for a Hub launched from outside the managed root.
- **Library bundling:** the current packages only bundle libraries from outside
  the system library directories. Confirm what the CI vcpkg build actually
  produces before relying on it.
- **Architectures:** x86_64 only for now; aarch64 needs its own runner and
  catalog entries.

## Implementation plan

Small, independently testable steps, each with regression coverage:

1. Linux release flow: reuse unchanged archives and generate
   `updates-linux.json` in the release workflow.
2. Platform-neutral abstraction in `libs/updater` for installation state and
   the install step; keep the Windows behavior unchanged and covered.
3. Linux state store, extraction with path-safety checks, symlink switch, and
   rollback, tested with synthetic archives and installations.
4. Hub integration and the first-install path.
5. Documentation and `AGENTS.md` status update (Linux no longer pending).

## Validation

- Unit tests: catalog parsing for both files, strict version comparison,
  path-traversal and symlink rejection in extraction, interrupted extraction,
  failed launch followed by rollback, and unchanged Windows behavior.
- Compatibility: confirm an installed Windows build still reads `updates.json`
  after the Linux files are added; test a published release that contains both.
- Manual, on at least two distributions (for example Debian/Ubuntu and
  Fedora): clean install, update, update with a running app, failed update, and
  rollback. Record the result in this document as pending until performed.
- Check `ldd` on each packaged executable on a clean machine.

## Decision record

| Date | Decision | Status |
| --- | --- | --- |
| 2026-11-12 | Plain `.tar.gz` packages published as manual downloads | Provisional, implemented |
| 2026-11-12 | In-app Linux updates via option A | Proposed, awaiting maintainer decision |
