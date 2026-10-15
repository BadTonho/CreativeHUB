# Translation catalogs

Qt translation sources use editable `.ts` files; compiled `.qm` catalogs are
committed alongside them because the Hub downloads those individual files
directly from the repository's `main` branch. The `manifest.json` file lists
only catalogs that are currently published for download, with their exact byte
size and SHA-256 digest.

Catalogs are organized by application ID and locale, for example
`hub/pt_BR.ts` and `video-editor/pt_BR.ts`. English is the source language.
Brazilian Portuguese catalogs are available for the Hub and Video Editor;
other editor catalogs will be added as their user interface strings are
migrated.

Use `update-hub-translations` or `update-video-editor-translations` in the
corresponding CMake build to refresh that application's `.ts` source. Each app
build compiles its `.ts` and fails if the generated `.qm` differs from the
tracked file. After intentionally updating a `.ts`, finish the translations,
generate the matching `.qm`, review both files, then update the manifest size
and SHA-256 values together. Do not publish a manifest entry until its matching
`.qm` is present.
