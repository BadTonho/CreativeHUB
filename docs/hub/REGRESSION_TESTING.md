# Hub Regression Testing

This guide indexes behavior owned by the Hub. Shared update behavior and the
release contract are documented in [`../WINDOWS_UPDATES.md`](../WINDOWS_UPDATES.md).
Follow the repository-wide requirements in
[`../REGRESSION_POLICY.md`](../REGRESSION_POLICY.md).

## Coverage index

| Behavior | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Published versions and per-app update status | `apps/hub/tests/test_app_catalog.cpp` (`hub-catalog-test`) | Hub UI refresh and version labels in a packaged Windows build: **pending**. |
| Catalog parsing, semantic version comparison, malformed assets and sizes, and URL resolution | `libs/updater/tests/release_catalog_test.cpp` (`updater-catalog-test`) | Covered by the shared updater test suite; actual GitHub Release availability remains a release operation. |
| Download, resume, cancellation, network failure, incomplete/hash rejection, and one-download lock | `libs/updater/tests/update_service_test.cpp` (`updater-service-test`) | Network interruption and resume through the visible Hub dialog: **pending**. |
| Individual editor updates from the Hub | No packaged installer integration test yet | Update each installed editor separately; verify there is no “Update all” action and that the other editors' versions do not change: **pending**. |
| Install scheduling and restore after failed startup | Runtime state is covered indirectly; no real Inno Setup integration test | Close an editor to complete an update, then test installer failure and Hub restoration in a disposable Windows profile: **pending**. |
| Hub's own update | Shared updater component and Hub startup integration | Install and update the Hub itself without changing editor installations: **pending**. |
| Changelog discovery, semantic version sorting, and modal markdown display | `apps/hub/tests/test_changelog_reader.cpp` (`hub-changelog-reader-test`), `apps/hub/tests/test_app_details_modal.cpp` (`hub-details-modal-test`) | In Hub UI, open app details modal; verify changelog section renders notes or empty state gracefully: **covered**. |
| Recent projects tracking, file-to-app detection, recency reordering, JSON persistence, and launch delegation | `apps/hub/tests/test_recent_projects.cpp` (`hub-recent-projects-test`) | Open Projects tab in Hub; open project file via dialog or quick cards; verify list updates with recency order, app badges, and removal: **covered**. |
| Suite storage calculation, multi-directory cache discovery, byte formatting, and cache cleanup | `apps/hub/tests/test_storage_manager.cpp` (`hub-storage-manager-test`) | Open Settings tab in Hub; verify total cache size displays formatted bytes; trigger "Limpar Cache da Suíte" and verify feedback and size update: **covered**. |

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
