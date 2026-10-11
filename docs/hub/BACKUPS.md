# Hub Project Backups

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
| `creative-suite.main-editor` | 1–24 | `media[].path`; clip `source` in v1 flat clips or v2+ tracks; v10+ Image Editor link/variant `document` and `output`; v20+ Fusion Input `source`; v24+ Motion link `document`, `output`, and `source`. |
| `creative-suite.motion-studio` | 1–6 | `media_pool.items[].path`; layer `source_path`; v6 linked-image `document_path`, `published_output_path`, and `source_snapshot_path`. |

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

## Creation and restoration

The snapshot is completely prepared in a temporary file inside the vault
before being renamed to its final timestamped name. An existing snapshot is
never replaced. Failures clear the returned output path, record an actionable
Hub error, remove the temporary file, and emit no successful backup activity
or signal. Vault counts and sizes use the resulting snapshot files.

Use **Abrir Pasta** in the Hub to inspect the vault. A new snapshot can be
opened directly in its editor, or copied to another directory and opened
there: its references still point to the original media and linked documents.
The Hub does not currently provide an automatic restoration command. If those
external files were moved or deleted, use the editor's relink/recovery flow.

Snapshots made by the older verbatim-copy implementation do not contain the
original directory context. Recreate them from the original project while it
is available, or repair their references with the original location known.
This change does not retroactively recover lost context. Timestamp collisions
and identification of same-named projects remain separate outstanding work.

## Verification

`hub-backup-manager-test` reproduces the original relative-image failure and
checks original bytes, unknown fields, failed preparation, unsupported
versions, malformed paths, opaque files, log context, and an unwritable vault.
`hub-backup-format-test` links the enabled editors' actual persistence
readers/writers in the test executable only. With all three editors enabled it
checks 46 supported document versions, opening both the vault snapshot and a
copy relocated for restoration. Coverage includes imported images inside
groups, Fusion inputs, Image Editor sidecars, Motion links, Unicode paths,
spaces, offline sidecars, unchanged source data, and snapshot size accounting.
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

Windows/Release automated results are recorded in
[REGRESSION_TESTING.md](REGRESSION_TESTING.md). Native visual checks and
macOS/Linux execution remain pending.
