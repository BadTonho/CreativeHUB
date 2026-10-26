# Hub Project Backups

Image `.cimg` v28 is accepted, including independent Hue/Saturation Colorize values; future v29 is rejected. Glow extends native backup support to Motion v18; CSP v32 adds the Video Editor Primary Grade effect, v33 adds the Color Gallery, v34 stores the Color correction graph, v35 expands primary controls, v36 adds independent Log adjustments, v37 stores named per-clip grade graph snapshots, v38 adds Color Curves and tonal qualifiers, v39 adds qualifier matte refinement, v40 adds animated Color Power Windows, and v41 adds Audio mixer buses, routes, sends, pan, and Master settings; v42 adds per-track EQ/compressor chains and v43 adds per-clip EQ/compressor chains. Gallery PNG sidecars are copied beside each backup snapshot so Gallery items remain available after restore. Color graphs, windows and geometry, matte bindings and visibility, blend mode, Scale Y, link state, affine compensation, Anchor bases and curves, parent IDs and pose-preserving compensation, Null control layers, optional presentation metadata, shared mask definitions, stage associations, effect parameter keyframes, and v42-v43 audio mix/effect data are retained; external media/Input paths are rebased. Future CSP v44 and Motion v19 remain unsupported. The referenced `.cscomp` file is not required to reopen an imported native project. See [contract](../COMPOSITION_INTERCHANGE.md).

## Video Editor v29 Background presets

Named Fusion Compositions retain optional `display_name`, including Background
presets, through snapshot and restored projects. The existing graph/media path
adapter preserves node colors, masks and input offsets while relocating external
Input sources. The current range is CSP v1-v43; future v44 is refused. The actual
Video Editor reader verifies the Hub restore in `hub-backup-format-test`.

## Video Editor v28 masks

The backup adapter preserves Rectangle/Ellipse node types, geometry, curves and
Mask input 2 alongside generators and composition input offsets. Masks carry no
media references; external Input paths continue to be rewritten on snapshot and
restore. Current Video Editor schemas v1-v43 are supported; future v44 is rejected.
The Video Editor reader reopens restored fixtures in `hub-backup-format-test`.

## Video Editor v27 compositions

Fusion Composition clips have no primary source. Backup preserves Background/
Text data, graph curves and the signed input-time offset, while external Input
paths are relocated through the existing node path adapter. Video formats 1–31
are accepted; future versions are rejected. Producer/consumer round trips use
the actual Video Editor writer and reader.


Status: **provisional implementation contract**. The Hub creates document
snapshots; external media remains referenced, while Video Editor Gallery PNG
sidecars are copied beside their CSP snapshots.

## Reference preservation

Copying a project unchanged into the vault changes the directory from which
an editor resolves relative media paths. Before publishing a snapshot, the
Hub resolves documented relative references against the original document's
directory and writes absolute paths into the snapshot only. Video Editor
Gallery stills use leaf filenames within `<project>.csp.gallery/`; the Hub
copies that sidecar directory beside the backup CSP file and does not rewrite
those filenames. Other media files remain external references. The original
document and media files remain unchanged. The format version, revision,
editing data, and unrelated JSON fields are preserved; JSON formatting may
change when references are rewritten. Documents with no rewritten paths retain
their original bytes.

This bounded adapter keeps editor document ownership separate and avoids
linking editor cores into the Hub application. A sidecar storing the original
directory would require changes in every reader; bundling media would require
a separate archive and restoration contract. Absolute references resolve the
current relocation failure without either change, but still depend on the
original media locations being available. The adapter must be updated with
producer/consumer regression coverage when supported schemas gain path fields.

| Format identifier | Supported versions | References rewritten |
| --- | --- | --- |
| `creative-suite-image-document` | 1–23 | v1 `source.path`; v2+ source-image `base.path`; v11+ raster-image operation `path` in legacy operations and layers, including group children. |
| `creative-suite.main-editor` | 1–43 | `media[].path`; clip `source` in v1 flat clips or v2+ tracks; v10+ Image Editor link/variant `document` and `output`; v20+ Fusion Input `source`; v24+ Motion link `document`, `output`, and `source`. CSP v33 Gallery sidecar files are copied beside each backup snapshot; v34 Color graphs, v35 expanded Primary Grade controls, v36 Log values, v37 named per-clip graph snapshots, v38 Curves, v39 matte refinement, v40 Power Windows and geometry tracks, v41 audio mixer state, v42 track effects, and v43 clip effects remain in project JSON. |
| `creative-suite.motion-studio` | 1–18 | `media_pool.items[].path`; layer `source_path`; v6 linked-image `document_path`, `published_output_path`, and `source_snapshot_path`. |

Video v26 adjustment clips have no source path. Their duration, effects and
transform curves are preserved by backup and restoration. They do not cause
media lookup or path rewriting. Versions 1–42 are accepted, and versions
newer than 42 are rejected until their reference contracts are supported.

Image paths use the original document directory. Video and Motion paths use
the weakly canonical original document location, matching their readers.
Motion relative references that escape the document directory, including
symlink escapes, are rejected instead of being converted into unrestricted
absolute paths. Existing absolute paths and empty optional paths are kept.
Missing media do not prevent snapshot creation; their intended references are
retained for the editor's existing offline/relink workflow.

The format identifier controls recognized JSON documents regardless of the
extension. Malformed or unrecognized documents with `.cimg`, `.csie`, `.csp`,
`.csve`, `.motion`, or `.csme` extensions are rejected, as are unsupported
versions and invalid path-field types. Other unrecognized file types are
copied verbatim. This adapter checks the reference structure; each editor
continues to validate its complete document when opening it.

## Identity and vault format

New backups use an exclusively created `backup-XXXXXX` directory for each
snapshot. Qt's temporary-directory allocator handles existing names; creation
does not depend on timestamp precision or shared in-memory state. Inside that
directory are the timestamped project file and `record.json`. Repeated backups,
including same-named projects and two managers sharing a vault, have distinct
full paths and never replace an existing snapshot.

The Hub-owned record format is `creative-suite.hub-backup`, version **1**:

| Field | Meaning |
| --- | --- |
| `format`, `version` | Record format identifier and integer schema version. |
| `original_path` | Absolute source path, canonicalized while the source exists. |
| `project_name` | Complete source basename, stored independently of `_backup_` delimiters; filenames with an empty basename use their full name. |
| `created_at` | UTC ISO 8601 creation time with milliseconds; displayed in local time. |
| `snapshot_file` | Project filename directly within this backup's directory. |

`backupsForProject` compares the persisted source path with the normalized,
absolute query path, resolving canonical aliases when the queried file exists.
Windows path comparison ignores case; other platforms compare canonical paths
exactly. Extensions and source directories are part of the identity. A moved
or renamed original has a different identity; hard links at distinct paths are
also distinct. No filename-only fallback guesses ownership. Ordinary missing
source files can still be queried by their original absolute paths.

The record does not change the editor document schema or add fields to it.
Editors open the project file without reading the Hub record. Moving a complete
backup directory to another vault preserves its identity; move both files to
retain Hub listing. Copying the project file alone is enough for manual editor
restoration, but loses the Hub's association record. No cross-OS source-path
mapping or automatic relinking is provided.

## Creation and restoration

The project file is completely written and closed in its exclusive directory
before `QSaveFile` atomically commits `record.json`. That commit publishes the
backup to Hub readers. Direct-write fallback is disabled. Incomplete directories
without a committed record are ignored; an interrupted process can leave one
for manual inspection. Ordinary failures clear the returned output path, log
an actionable cause, remove the new directory, and emit no successful activity
or signal. Vault counts and sizes include published project files and legacy
snapshots, excluding record bytes and unfinished directories.

Malformed, unsupported, unreadable, oversized (over 256 KiB), or unsafe records
are logged and excluded from listing. Snapshot references must be local
filenames pointing to regular files; symlink records, snapshots, and backup
directories are not followed. Invalid or incomplete data are never deleted by
listing, and invalid records never fall back to a guessed source identity.

The Backups page shows the original path to distinguish same-named projects,
with the full source and backup paths in its tooltip. Legacy entries show
**origem desconhecida**. **Ver Arquivo** opens that snapshot's directory.

Use **Abrir Pasta** in the Hub to inspect the vault. A new snapshot can be
opened directly in its editor, or copied to another directory and opened
there: its references still point to the original media and linked documents.
The Hub does not currently provide an automatic restoration command. If those
external files were moved or deleted, use the editor's relink/recovery flow.

Snapshots made by the older verbatim-copy implementation do not contain the
original directory context. Recreate them from the original project while it
is available, or repair their references with the original location known.
This change does not retroactively recover lost context. Older flat snapshots
remain visible in the vault with an empty `originalFilePath`, including names
containing `_backup_`. They are excluded from per-project history because their
source identity was never persisted. No implicit migration changes their bytes.
Hub 0.1.0 does not index the new per-backup directories; their project files
remain available for manual opening through the vault.

## Verification

Project-card backup creation runs in a worker task. The card remains disabled
until the snapshot operation completes; success or logged failure feedback is
delivered on the Hub UI thread. Closing or rebuilding the card does not leave a
callback that references a destroyed widget.

`hub-backup-manager-test` reproduces the original relative-image failure and
checks original bytes, unknown fields, failed preparation, unsupported
versions, malformed paths, opaque files, log context, and an unwritable vault.
It also covers same-named projects, extensions, relative and Windows case
aliases, repeated creation across managers, persisted origin and timestamp,
unavailable originals, legacy names, corrupt/unsafe records, unpublished
directories, and payload-only size accounting. `hub-backups-page-test` checks
that same-named rows expose distinct source paths and that legacy origins are
identified as unknown, using temporary data without opening folders.
`hub-backup-format-test` links the enabled editors' actual persistence
readers/writers in the test executable only. With all three editors enabled it
checks 47 supported document versions, opening both the vault snapshot and a
copy relocated for restoration. Coverage includes imported images inside
groups, Fusion inputs, Image Editor sidecars, Motion links, Unicode paths,
spaces, offline sidecars, unchanged source data, and snapshot size accounting.
Video v25 coverage preserves nonzero Motion publication positions as well as
rebased link paths, so restored split clips retain their animation time.
The consumer checks are conditional on the corresponding CMake app options;
the manager regression also runs in Hub-only configurations.

Manual cross-application check (native UI acceptance **pending**):

1. Save an Image Editor project with an original image and an imported image
   in a group under the document directory. Save a Video Editor project with
   local footage, a second Fusion input, and Image/Motion links. Save a Motion
   project with local media and an Image Editor linked layer.
2. Create each backup through its Hub project card. Open the vault snapshots
   in the corresponding editors, then copy them to a separate restoration
   directory and open those copies. Verify the same media, editing state,
   linked output, and source paths without a relink prompt for available files.
3. Confirm original project/media bytes are unchanged. Temporarily move a
   referenced source aside and verify the existing offline/relink behavior;
   restore it afterwards. Record the application versions, OS, and result.
4. Create two same-named projects in different directories. Back up both, and
   create several more backups immediately. Restart the Hub and confirm the
   distinct original paths and all copies remain visible. **Ver Arquivo** must
   open the corresponding unique backup directory.
5. Inspect an older flat snapshot. Confirm it is marked **origem desconhecida**
   and remains available without being attributed by name. Move a complete new
   backup directory to another vault, select that vault in the Hub, and verify
   the same original path. Retain both its project file and `record.json`.

Windows/Release automated results are recorded in
[REGRESSION_TESTING.md](validation/REGRESSION_TESTING.md). Native visual checks and
macOS/Linux execution remain pending.

Motion v15 backups preserve Bezier shape/mask bases, complete-path keys and
relative tangents along with earlier features. Existing path rebasing changes
only media/link paths. `hub-backup-format-test` reopens vault/restored documents
with the Motion reader. The original v15 gate rejected v16; the current gate below accepts it. Native Hub UI/platform checks remain pending.

Motion v16 backups preserve Trim Paths activation, Start/End/Offset bases and
curves (including complete turns) alongside Bezier geometry, hierarchy and media.
The original v16 producer/reader gate reopened vault/restored documents and rejected v17. Native
UI/platform checks remain pending; see [Trim Paths](../motion-editor/features/TRIM_PATHS.md).

Motion v17 backups preserve ordered Drop Shadow activation, RGB, four bases/curves
and masks alongside earlier geometry/effects. The actual Motion producer/reader
reopens vault/restored files after media rebasing, and the gates reject v18.
See [Drop Shadow](../motion-editor/features/DROP_SHADOW.md); native restore/UI and
platform acceptance remain pending.

## Motion v18 Glow backup coverage

The Motion producer/reader round trips Outer, Inner and Both modes, static RGB,
animated opacity/radius and repeated ordered effects through vault and relocated
restore. Glow masks and rebased media remain intact; `hub-backup-manager-test`
accepts v18 and rejects v19. See [Glow](../motion-editor/features/GLOW.md).
Native Hub restore UI and platform acceptance remain pending.

## Image Editor v17 outlines

Backup/restore accepts Image documents through v28 and preserves optional layer
outline parameters, including disabled styles, while rebasing existing source
and imported-image paths. `hub-backup-format-test` reopens vault and relocated
restores with the actual Image reader, including an outlined grouped image.
The same test rejects future Image v29. Native Hub UI/platform
acceptance remains pending.

## Image Editor v18 clipping masks

Clipping was introduced in Image v18. The current reader accepts v1-28 and
preserves `clip_to_base`, masks and outlines while
rebasing source/import paths in root layers and group children. The actual
Image reader verifies vault and restored v18 chains. Future Image v29 is
rejected. The recovery envelope and published PNG protocol are unchanged.
Native Hub UI/platform acceptance remains pending.

## Image Editor v19 blend modes

Accept Image schemas 1-27 and preserve layer/group `blend_mode` strings with
clipping, masks and outlines while rebasing existing paths. Actual Image reader
round trips cover group/base/clipped-child modes in vault and relocated files.
Reject future Image v29. Native backup UI/platform acceptance remains pending.


## Image Editor v20 adjustments

The reader accepts v1-28 and rejects v29. Preserve Gradient Map adjustment
payloads and mask `initial_selection` paths during media rebasing, backup and
restoration. Actual Image producer/reader tests verify points, masks and media;
recovery envelopes and published PNG protocols remain unchanged. Native backup
UI and macOS/Linux acceptance remain pending.

## Image Editor v21 adjustments

Preserve Brightness/Contrast integer parameters and Gradient Map points, including
selection-seeded masks and media rebasing. Accept v1-28, reject v29, and verify
vault/restored documents with the real Image reader. Native UI acceptance is pending.

## Image Editor v22 adjustments

Accept Image v1-28 and reject v29. Preserve Exposure / Offset / Gamma floating
parameters and selection-seeded masks alongside Gradient Map and Brightness/
Contrast during media rebasing. `hub-backup-format-test` opens vault and relocated
restores with the actual Image reader. Application versions and snapshot envelope
remain unchanged. Native backup UI/platform acceptance remains pending.

## Image Editor v23 HSL adjustments

Accept Image v1-28 and reject v29. Preserve integer Hue/Saturation/Lightness
controls, all older adjustment payloads and selection-seeded masks during media
rebasing, backup and relocated restoration. Actual Image readers verify vault
and restored data; snapshot envelope and application versions remain unchanged.
Native backup UI/platform acceptance is separate and pending.

## Image Editor v24 Levels adjustments

Accept Image v1–28 and reject v29. Preserve all five Levels controls, the other
adjustment kinds and selection-seeded masks during media rebasing, backup and
relocated restoration. `hub-backup-format-test` opens the resulting documents
with the actual Image reader. Native backup UI/platform acceptance is pending.

## Image Editor v25 Curves compatibility

Preserve v25 Curves documents unchanged and accept them for backup as part of
the supported Image v1–28 range. Keep ordered Curves points, all older
adjustment kinds, masks and rebased media through vault and relocated restore.
The historical v25 gate reopens both outputs with the actual Image reader.
Native Hub UI/platform acceptance remains pending.

## Image Editor v26 per-channel Curves

Accept Image v1–28 and reject v29. Preserve the RGB master and all three
individual channel point sets, older adjustment kinds, masks and rebased media
through vault and relocated restore. `hub-backup-format-test` reopens both
outputs with the actual Image reader. Native Hub UI/platform acceptance remains
pending.

## Image Editor v27 per-channel Levels

Accept Image v1–28 and reject v29. Preserve the RGB master and independent Red,
Green and Blue Levels sets, existing Curves v26 data, older adjustment kinds,
masks and rebased media through vault and relocated restore.
`hub-backup-format-test` reopens the v27 producer fixture with the actual Image
reader; the same test rejects future Image v29. Native Hub UI and
platform acceptance remain pending.

## Image Editor v28 Hue/Saturation Colorize

Accept Image v1–28 and reject v29. Preserve the independent normal and
Colorize values while rebasing media paths through vault and relocated restore.
`hub-backup-format-test` opens both outputs with the actual Image reader and
compares the decoded document, including enabled Colorize controls. The recovery
envelope and published PNG protocol remain unchanged. Native Hub UI/platform
acceptance remains pending.
