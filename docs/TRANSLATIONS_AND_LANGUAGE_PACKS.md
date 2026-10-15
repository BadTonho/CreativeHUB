# Translations and language packs

## Catalog format and layout

English is the source language. Each application has one Qt Linguist source
catalog (`.ts`) per locale and a compiled Qt catalog (`.qm`) beside it under
`languages/<application-id>/`. For example, the Image Editor's Brazilian
Portuguese files are `languages/image-editor/pt_BR.ts` and
`languages/image-editor/pt_BR.qm`.

The `.ts` file is the editable source for translators. The `.qm` file is the
runtime catalog loaded by Qt. Both are committed. The Video Editor, Image
Editor, and Motion Studio bundle their catalogs for offline use. The Hub can
obtain the same `.qm` independently from the repository's `main` branch when a
catalog is listed in `languages/manifest.json`.

The manifest uses schema version 1. Each published entry contains an
application ID, locale, repository-relative catalog path, exact byte size, and
SHA-256 digest. The Hub validates these fields and the compiled Qt catalog
before atomically installing a downloaded file in the shared per-user
translation directory. Do not add archives or translation-only releases.

## Runtime behavior

Applications select a catalog from the system locale. Any Portuguese locale
uses `pt_BR`; unsupported locales use English source text. At startup, the
downloaded catalog takes precedence over the bundled catalog. If a downloaded
catalog cannot be loaded, the bundled catalog remains available. A source
string missing from either catalog remains in English.

The Hub checks for available translation updates at startup and from its
existing Updates screen. Editors load the local catalog when they start and do
not access the network themselves. A newly downloaded catalog takes effect the
next time that application opens. It can change translations for existing
source strings; source strings added by later application code still require
an application update.

## Updating a catalog

1. Add or change user-visible source strings in the application, using Qt
   translation calls and stable contexts. Keep logs, identifiers, protocols,
   and project data out of the UI catalog.
2. Run the application's CMake update target. The current targets are
   `update-hub-translations`, `update-video-editor-translations`,
   `update-image-editor-translations`, and
   `update-motion-editor-translations`.
3. Translate every active entry in the `.ts` file. Preserve placeholders such
   as `%1` and `%n`, line breaks, accelerator markers, formatting, and plural
   forms. Keep product names and common technical terms when they are clearer
   than a literal translation.
4. Compile the `.qm` with Qt Linguist's `lrelease`. The application build also
   compiles the source catalog and fails if the generated file differs from
   the tracked `.qm`.
5. Review both files and update the matching entry in `manifest.json` with the
   `.qm` file's exact size and SHA-256 digest. Keep the old valid catalog
   available until the new files and manifest entry are ready together.
6. Update the affected application's regression index and version-specific
   changelog. Do not change application version metadata during ordinary
   development.

For the Video Editor, the catalog is embedded as
`:/translations/video-editor/pt_BR.qm`. Its application startup installs the
local translation before constructing the main window. The registered
`creative-suite-main-editor-translation-catalog` test checks bundled loading,
regional Portuguese selection, dynamic Effects labels, placeholder and plural
handling, and English fallback.

For the Image Editor, the catalog is embedded as
`:/translations/image-editor/pt_BR.qm`. Its startup installs the local
translation before constructing the main window. The
`creative-suite-image-editor-translation-catalog` test checks bundled loading,
regional Portuguese selection, translated error messages, placeholder
handling, and English fallback.

For Motion Studio, the catalog is embedded as
`:/translations/motion-editor/pt_BR.qm`. Its startup installs the local
translation before constructing the main window. The
`creative-suite-motion-editor-translation-catalog` test checks bundled loading,
regional Portuguese selection, visible menu labels, placeholders, missing-text
fallback, and English fallback.

## Regression and acceptance

Automated catalog tests cover manifest validation, locale selection, compiled
catalog loading, download failures, integrity checks, and atomic installation.
The Video Editor, Image Editor, and Motion Studio tests cover their bundled
pt-BR catalogs and English fallback. These automated checks do not replace
native visual review.

For each application with a published catalog, verify the following on the
target operating system:

- Start with a Portuguese system locale and confirm the translation is active
  before the first window appears.
- Review menus, primary workspaces, dialogs, tooltips, status messages, and
  accessible labels. Check that placeholders, accelerators, and plural forms
  display correctly without clipping.
- Start with an unsupported locale and confirm source text remains English.
- Start offline and confirm the bundled or previously downloaded catalog
  still loads.
- In the Hub, check for translations while offline and confirm that the
  current valid catalog remains intact after a network failure.
- Publish a changed `.qm` and matching manifest entry on `main`, let the Hub
  install it, then reopen the editor. Confirm that the new text appears
  without replacing the editor executable.

Native visual review of the Video Editor's Edit, Fusion, Render, dialogs, and
tooltips, and of the Image Editor's menus, tools, panels, dialogs, and
tooltips, and of Motion Studio's menus, panels, timeline, dialogs, tooltips,
and accessibility labels, plus the end-to-end Hub download and next-launch
check, remain pending until performed on a native desktop environment.
