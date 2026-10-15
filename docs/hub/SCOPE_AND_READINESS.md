# Hub Scope and Readiness

The Hub launches the suite applications and owns suite-level project discovery, backup management, storage cleanup, and per-application update workflows. Detailed behavior belongs to the focused contracts below.

## Canonical references

- [Project backups](BACKUPS.md) documents snapshots, restore behavior, and format compatibility.
- [Storage and cache cleanup](STORAGE.md) documents discovery, cleanup boundaries, and failure handling.
- [Regression testing](validation/REGRESSION_TESTING.md) indexes automated producer/consumer coverage and pending native checks.
- [Windows update contract](../WINDOWS_UPDATES.md) documents the current updater implementation and distribution limitations.

## Readiness

Implementation evidence and release acceptance are separate. The backup and storage contracts describe implemented behavior and automated coverage; platform-native UI, packaged-installation, and distribution checks remain subject to the pending items in the regression guide and the update contract. Do not treat a successful test build as acceptance of an operating system, installer, update channel, or recovery path.

Update this page when Hub responsibilities or acceptance gates change. Keep detailed feature status in the linked contracts and regression guide.