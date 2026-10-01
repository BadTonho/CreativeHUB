# Regression Prevention Policy

Status: **mandatory for every application and shared module in this repository,
including applications added in the future**.

This policy makes regression checks part of feature work so that changes can be
made without relying on one person to manually retest every existing behavior.
Automated tests reduce regressions; they do not prove that every possible input
and environment is defect-free. Manual validation remains required for behavior
that cannot be tested reliably without a real UI, device, graphics driver, or
codec installation.

## Required Coverage for Every Change

- Add regression coverage in the same change as every new or modified function
  and user-facing behavior. Tests should assert behavior and contracts rather
  than implementation details.
- Use automated tests whenever behavior is deterministic. Cover normal use,
  relevant boundary cases, failure handling, and state changes such as undo,
  redo, save, reopen, and recovery when applicable.
- For a bug fix, add a test that reproduces the reported failure. The test
  should fail before the fix when practical and pass after it.
- For visual or full-application behavior that cannot be automated reliably,
  add a reproducible manual validation step with expected results. Keep
  automated tests for the underlying model and module boundaries.
- Keep an app-level feature-to-verification index in that application's
  regression guide or roadmap. For each important behavior, identify its
  automated test and any required manual checklist. Update the index when the
  behavior or test location changes.

Coverage is measured against documented behavior and interfaces, not a target
percentage of source lines. Tests may cover several closely related behaviors
when the assertions remain clear and failures are diagnosable.

## Coverage Status and Priorities

Use the following statuses in each application's feature-to-verification
index:

- **Automated coverage present:** a registered test directly exercises the
  behavior; name its source file and link to the CMake registration, including
  the CTest target name when that adds useful traceability.
- **Manual check documented:** a repeatable checklist exists for behavior that
  requires a running application, hardware, a driver, or a packaged runtime.
- **Pending validation:** required manual or platform validation has no recorded
  result. This is distinct from missing automated coverage.
- **Coverage gap (P0/P1/P2):** an implemented behavior has no direct automated
  test or documented manual check.
- **Planned, not implemented:** the behavior is not part of the current
  implementation and is not counted as a regression-coverage gap.

Assign P0/P1/P2 to uncovered behaviors and pending validation actions by
impact, while keeping their status distinct:

- **P0:** data integrity, startup, project open/save/recovery, and supported
  format compatibility.
- **P1:** core editing, import/export, playback/rendering, and cross-application
  workflows.
- **P2:** secondary interface behavior and quality, performance, or platform
  validation.

Do not mark a test as passing based only on its presence in the test tree. The
index records evidence and validation state; test execution results belong in
the CI run or manual validation record.

## Test Layers

Use the narrowest reliable test that protects the behavior, and add broader
coverage when the change crosses a boundary:

1. **Unit tests** cover deterministic logic such as document models, timeline
   rules, transforms, calculations, validation, and serialization.
2. **Module integration tests** cover interactions such as import and decode,
   composition and preview, editing and history, or rendering and export.
3. **Application tests** cover UI commands and user workflows at stable
   boundaries, including disabled, rejected, cancelled, and failure paths.
4. **Compatibility tests** load supported older project and document versions,
   verify migration, and confirm that saving does not lose required behavior.
5. **Cross-application tests** cover both producer and consumer whenever a
   change affects shared formats, media identity or paths, published assets,
   shared APIs, launch arguments, handoff, or refresh behavior.

Cross-application changes must preserve older supported formats or protocols
where applicable. If an end-to-end handoff cannot run in automation, document a
manual validation that names both applications and checks the persisted and
visible result in each.

## Test Quality and Fixtures

- Keep tests deterministic, independent, and safe to run in parallel where the
  framework permits. Use temporary directories and generated or approved test
  assets; do not depend on personal media, private paths, network services, or
  a developer's local configuration.
- Prefer controllable clocks, fake devices, synthetic media, and injected
  failures over timing assumptions or machine-specific behavior.
- Keep representative project fixtures small and versioned. Include older
  format fixtures needed to protect migration and compatibility.
- Assert that unexpected technical errors are logged with actionable context;
  intentional user actions such as cancellation or duplicate import are not
  error cases.
- Keep manual steps repeatable. Record the application build, operating system,
  relevant hardware or plugin details, actions, expected result, and outcome.

## Local and Continuous-Integration Gates

Before considering an application change complete:

1. Build the affected application and run its focused tests.
2. Run the repository's complete CTest suite to catch regressions in shared
   libraries and other applications.
3. Complete the linked manual checks for visual, hardware-dependent, or
   otherwise non-automatable behavior.
4. Run `git diff --check` and review the changed behavior and its test coverage.

The current local CMake gate is:

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

The GitHub Actions workflow must build and test the repository on Windows,
macOS, and Linux. Changes that affect interoperability must build and test all
affected producer and consumer applications. A change is not ready for release
while a relevant automated gate fails or a required manual check is unrecorded.
Resolve failures or update expected behavior and its tests deliberately; do not
silently bypass a failing regression check.

Every new application must register its automated suite with CTest or the
repository's equivalent test runner, add its feature-to-verification index,
and join the CI build-and-test matrix before the application is treated as
integrated into the repository.

## Performance and Hardware Checks

Performance tests complement behavior tests. Run representative workloads
documented in each application's scope on the reference PC and capture the
build, workload, operating system, hardware, measurements, and outcome. Keep
hosted CI assertions independent of variable runner speed unless the check is
deterministic. Set general hardware requirements and performance guarantees
only from repeatable measurements on the reference PC and additional systems.

## Application Test Guides

- [Video Editor regression testing](video-editor/REGRESSION_TESTING.md)
- [Image Editor roadmap and coverage index](image-editor/ROADMAP.md), with the
  detailed [manual validation checklist](image-editor/MANUAL_VALIDATION.md)
- [Motion Studio roadmap and coverage index](motion-editor/ROADMAP.md)

These app-level guides provide detailed test cases. This policy defines the
shared minimum that applies across all applications and future modules.
