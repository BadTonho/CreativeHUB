# Hub Project Backups

Motion layer parenting and Null controls extend native backup support to Motion v10; CSP v31 remains supported. Parent IDs and pose-preserving compensation, Null control layers, optional presentation metadata, shared mask definitions, stage associations, and effect parameter keyframes are retained unchanged and existing media/Input paths are rebased. Future CSP v32 and Motion v11 remain unsupported. The referenced `.cscomp` file is not required to reopen an imported native project. See [contract](../COMPOSITION_INTERCHANGE.md).

## Video Editor v29 Background presets

Named Fusion Compositions retain optional `display_name`, including Background
presets, through snapshot and restored projects. The existing graph/media path
adapter preserves node colors, masks and input offsets while relocating external
Input sources. Formats 1–31 are supported; future v32 is refused. The actual
Video Editor reader verifies the Hub restore in `hub-backup-format-test`.

## Video Editor v28 masks

The backup adapter preserves Rectangle/Ellipse node types, geometry, curves and
Mask input 2 alongside generators and composition input offsets. Masks carry no
media references; external Input paths continue to be rewritten on snapshot and
restore. Supported Video Editor schemas are 1–31; future v32 is rejected.
The Video Editor reader reopens restored fixtures in `hub-backup-format-test`.

## Video Editor v27 compositions

Fusion Composition clips have no primary source. Backup preserves Background/
Text data, graph curves and the signed input-time offset, while external Input
paths are relocated through the existing node path adapter. Video formats 1–31
are accepted; future versions are rejected. Producer/consumer round trips use
the actual Video Editor writer and reader.


Status: **provisional implementation contract**. The Hub creates document
snapshots; media files remain external references and are not bundled.

## Reference preservation

Copying a project unchanged into the vault changes the directory from which
an editor resolves relative media paths. Before publishing a snapshot, the
Hub resolves documented relative references against the original document's
directory and writes absolute paths into the snapshot only. The original
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
| `creative-suite-image-document` | 1–16 | v1 `source.path`; v2+ source-image `base.path`; v11+ raster-image operation `path` in legacy operations and layers, including group children. |
| `creative-suite.main-editor` | 1–31 | `media[].path`; clip `source` in v1 flat clips or v2+ tracks; v10+ Image Editor link/variant `document` and `output`; v20+ Fusion Input `source`; v24+ Motion link `document`, `output`, and `source`. |
| `creative-suite.motion-studio` | 1–10 | `media_pool.items[].path`; layer `source_path`; v6 linked-image `document_path`, `published_output_path`, and `source_snapshot_path`. |

Video v26 adjustment clips have no source path. Their duration, effects and
transform curves are preserved by backup and restoration. They do not cause
media lookup or path rewriting. Versions 1–25 remain accepted, and versions
newer than 30 are rejected until their reference contracts are supported.

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
