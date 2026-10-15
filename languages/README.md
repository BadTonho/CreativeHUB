# Translation catalogs

Qt translation sources use `.ts` files; compiled `.qm` catalogs are committed
alongside them because the Hub downloads those individual files directly from
the repository's `main` branch. The `manifest.json` file lists only catalogs
that are currently published for download, with their exact byte size and
SHA-256 digest.

Catalogs are organized by application ID and locale, for example
`hub/pt_BR.ts` and `hub/pt_BR.qm`. English is the source language. The first
published translation is Brazilian Portuguese for the Hub; editor catalogs
will be added as their user interface strings are migrated.

Use the `update-hub-translations` CMake target to refresh the Hub `.ts` source.
The Hub build compiles the `.ts` and fails if its generated `.qm` differs from
the tracked file. After intentionally updating the `.ts`, generate the `.qm`,
review both files, then update the manifest size and SHA-256 values together.
Do not publish the manifest entry until the matching `.qm` is present.
