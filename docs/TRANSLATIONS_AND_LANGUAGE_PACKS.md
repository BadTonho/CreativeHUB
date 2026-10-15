# Translations and downloadable catalogs

Status: **implemented, first locale in progress**. The shared translation
library is used by all four applications. The Hub publishes the first complete
catalog, Brazilian Portuguese (`pt_BR`). Editor interfaces currently load
catalogs through the same library but still need their visible strings migrated
and translated.

## Source files and publishing

- Keep editable Qt Linguist `.ts` files and compiled `.qm` files in
  `languages/<application-id>/<locale>.*`. English is the source language.
- Commit each `.qm` beside its `.ts`; the Hub downloads the individual `.qm`
  directly from this repository's `main` branch. No archive, separate release,
  or translation service is used.
- `languages/manifest.json` is the publication index. Schema version 1 uses a
  `translations` array; each entry contains `application`, `locale`, `path`,
  `size_bytes`, and lowercase hexadecimal `sha256` fields. Paths must match
  `<application>/<locale>.qm` exactly. The client rejects unknown schemas,
  unknown application IDs, unsafe paths, duplicates, invalid sizes and hashes,
  and catalogs larger than the configured limit.
- Only add a manifest entry after its `.qm` is committed and its measured byte
  size and SHA-256 match. A pull request must review the `.ts`, binary `.qm`,
  and manifest changes together.
- The `update-hub-translations` CMake target updates the Hub `.ts` from source
  annotations. Building the Hub recompiles the source and compares its result
  with the tracked `.qm`, so stale binaries fail the build.

## Runtime behavior

- All applications install their translator before creating their main window.
  Portuguese system locales, including regional variants, select `pt_BR`;
  other locales use English source text.
- Catalogs are saved under the current user's shared data directory at
  `CreativeSuite/translations/<application-id>/<locale>.qm`. Downloaded
  catalogs override bundled catalogs; missing entries fall through to the
  bundled translation, then the English source string.
- The Hub checks the repository manifest asynchronously at startup and has a
  manual check on its Updates page. Only the Hub contacts the network. Editors
  load an already downloaded catalog on their next launch and do not fetch it.
- A downloaded update takes effect the next time the matching application is
  opened. Live language switching and retranslation of open windows are not
  implemented.
- Missing files, network access, or a rejected catalog do not prevent offline
  startup. The previous good catalog remains in place unless the replacement
  passes size, SHA-256, and Qt QM-format validation and is installed atomically.

## Shared library and trust boundary

`libs/translations` owns catalog parsing and validation, locale selection,
shared per-user storage, translation loading, and the Hub's asynchronous
download/install service. It adds no third-party dependency. Network requests
use Qt Network and are restricted to HTTPS; redirects and catalog assets from
other origins are rejected. HTTP is allowed only for loopback test servers.

The initial trust root is the project's GitHub repository over HTTPS. The
manifest hash detects corruption and mismatched content; it does not defend
against a malicious repository change. Qt catalogs contain display text, not
executable code. Before the channel serves a wider public audience, evaluate
signing the manifest or catalogs and offline private-key custody.

Requests send no user, project, or installation data. A failed automatic check
is logged locally without presenting a startup error. User-visible status is
available on the Hub Updates page.

## Regression and acceptance

The shared library tests cover manifest parsing, unsupported schemas, unsafe
paths, duplicates, invalid sizes and hashes, corrupted QM data, atomic
replacement, locale mapping, downloaded-over-bundled precedence, missing-key
fallback, asynchronous downloads, failed network responses, repeat checks,
and retention of the previous catalog after rejected data. The Hub regression
guide links its UI and manual checks; each editor's regression index records
startup loader coverage.

Manual checks remain pending on native Windows, macOS, and Linux environments:

1. Start the Hub offline with a valid bundled catalog. The UI should open in
   Portuguese and the failed background network check should only be logged.
2. Publish a reviewed `.qm` and matching manifest update to `main`, then use
   the Hub's manual check. It should report that the catalog was downloaded;
   opening the Hub again should load it without changing the application
   executable. Repeat with an editor catalog when one is published.
3. Confirm that an editor without a downloaded or bundled catalog opens in
   English, and that a downloaded Hub catalog is used by the Hub on its next
   launch. Exercise a Portuguese regional system locale as well as a
   non-Portuguese locale.

Automated build and CTest results are recorded in the affected regression
guides. Platform builds and native UI acceptance are tracked separately.
