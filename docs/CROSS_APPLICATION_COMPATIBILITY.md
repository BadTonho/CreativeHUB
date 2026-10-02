# Cross-Application Compatibility Proposal

**Status:** Provisional. This document records a direction to validate, not a
final format, API, implementation commitment, or change in application
priorities.

## Goal

Allow the Video Editor, Motion Studio, and Image Editor to share stable
domain capabilities and continue supported work across applications. Keep
shared code reuse and document handoff as separate concerns: applications can
reuse a library without sharing project files, and they can exchange a linked
document without sharing their entire editing workflow.

The Image Editor standalone minimum was developed first. Its manual and
packaging checks are the current gate for accepting the linked-image workflow.
The Video Editor linked-image implementation already exists as a bounded
prototype and should remain stable until that gate passes. This is an
acceptance dependency within the Image Editor track, not a required development
sequence between applications. The Video Editor, Image Editor, and Motion
Studio may be developed in parallel; technical contracts in this proposal
remain provisional.

The initial Video Editor to Image Editor handoff is now implemented as a
bounded prototype. A user confirmed the basic linked edit/save workflow and
that the Video Editor refreshed after the Image Editor saved. The broader
acceptance checks follow the standalone Image Editor gate and remain pending.
This document remains provisional until the file, conflict, and cross-platform
workflows have been validated manually.

## Recommended Boundaries

Applications should depend on reusable libraries through documented APIs.
Shared libraries must not depend on application UI code. Each application can
adapt its own controls and workflows to shared operations.

| Capability | Suggested ownership |
| --- | --- |
| Layers, transforms, masks, and composition | Shared composition library when a second application has a real use case |
| Keyframe evaluation and curves | Shared by the Video Editor and Motion Studio; include the Image Editor only if animation becomes an approved use case |
| Timeline editing and audio workflows | Application-specific, with reusable lower-level services when a real second consumer exists |
| Painting, selection, and retouching tools | Image Editor |
| Advanced motion-design workflows | Motion Studio |
| Panels, controls, shortcuts, and application workflows | Each application |
| Media metadata and resource references | Shared where their semantics match; import and export workflows may remain application-specific |
| Cross-application document links and revision notifications | A small shared compatibility contract, with application-specific document adapters |

Color management and common effect definitions are possible shared
capabilities. Define their contracts after the project establishes the
relevant color pipeline and confirms that multiple applications need the same
behavior.

## Existing Shared Libraries and Boundaries

The repository already has focused CMake targets under `libs/`: `media-frame`,
`animation`, `composition`, `diagnostics`, `system-monitor`, `shortcuts`, and
the media targets `video-media`, `video-encoding`, and `media-assets`. The
Video Editor and Motion Studio consume shared media, animation, and composition
capabilities; both also use shared diagnostics and system monitoring. Shortcut
registration is shared by all three editors. Each application keeps its own
timeline, project/document model, task orchestration, panels, and editing
workflow. The shared libraries do not own application documents or widgets.

The optional `creative-suite::composition-opengl` adapter now provides worker
GPU composition/readback for the existing ordered RGBA frame contract. Video
Editor is its first consumer, behind a global experimental preference; Motion
Studio adoption remains planned and Image Editor requires its own transparent
output contract. The CPU compositor remains Qt independent. This adds no native
format or handoff change. Native tests compare masked PNG producer output and
consumer refresh through the Video worker. See [delivery evidence](video-editor/GPU_COMPOSITION_RESULTS.md).

These APIs remain provisional and should continue to gain focused tests for
their shared behavior and each consumer boundary. In particular, new
cross-application handoffs still require producer/consumer regression coverage
and documented compatibility behavior.

The API contract should document:

- coordinate systems, units, and transform conventions;
- pixel formats, color assumptions, and alpha behavior;
- resource ownership, lifetime, and thread requirements;
- errors and the context that must be logged;
- serialization and compatibility expectations, if the data is persisted.

Keyframe and curve evaluation is already a separate shared capability in
`creative-suite::animation`; applications can use that evaluator without
sharing their document or timeline models.

## Extraction Criteria and Build Shape

Keep a capability application-local while the Video Editor is its only
consumer. Motion Studio now reuses the media asset catalog and decoders while
keeping its import dialog, worker orchestration, pool presentation, and
composition lifecycle application-owned. The library APIs remain provisional
until both consumers have regression coverage at their application boundaries.

Use CMake library targets and link each consuming application to the required
targets. Prefer focused modules such as `composition` or `animation` over a
catch-all core target. The current targets are static libraries linked into
each consuming application, so each application is packaged independently;
runtime-loaded libraries are not required to share code in one repository.

Before treating an extracted capability as a stable shared contract, confirm
that:

1. one implementation owns the behavior;
2. both consumers use the same documented contract;
3. regression coverage exercises the shared behavior and both application
   boundaries;
4. application UI and workflow decisions remain outside the library.

## Linked Editing Between Applications

The shared media catalog contains neutral metadata and original file paths.
Image Editor link identities, companion document paths, published outputs,
and revision handling remain in a Video Editor-owned sidecar and `.csp`
adapter. They are not part of the Media Pool API consumed by Motion Studio.

The proposed workflow uses a separately saved, editable document linked from
the Video Editor. The Video Editor keeps a stable reference to that document and
uses its current supported output in the timeline. The linked document keeps
its own native editing model and history. The source media remains available
and is not silently overwritten by an editor handoff.

For Motion Studio, the agreed first usable workflow is standalone composition
creation and rendered video export. The linked workflow below is deferred until
after that scope and the required contracts are validated. When implemented,
the initial host refresh follows a successful save. Unsaved live previews are
deferred and require a separate demonstrated use case because they add runtime
coordination, resource-lifetime rules, and stale-frame handling.

### Image document workflow

- Opening an image from the Media Pool creates or reopens a shared `.cimg`
  companion under `<source>.image-editor/asset.cimg`; its flattened PNG is
  `<source>.image-editor/asset.png`. The source image stays unchanged. All
  timeline occurrences of that media item use a saved shared revision.
- Opening an image clip from the timeline creates or reopens an isolated
  variant under `<source>.image-editor/clips/<uuid>/`. A `source.png` copy
  captures the image shown when the variant is first created; its editable
  document and published output remain separate from the shared Media Pool
  link and other clips.
- Video Editor `.csp` version 10 stores optional shared and clip-specific
  references. Versions 1 through 9 load without those references. Resolution
  order is clip variant, shared Media Pool output, then original source.
- The Image Editor receives `--linked-source`, `--linked-document`, and
  `--publish-output`. It opens an existing `.cimg` or creates one from the
  supplied source. Save writes the native document and then atomically replaces
  the published PNG. A per-document lock and SHA-256 baseline prevent a second
  Image Editor instance from replacing a newer saved revision.
- The Video Editor polls linked output files and decodes changes asynchronously.
  It refreshes the Media Pool thumbnail and every shared-media use, or only the
  matching clip variant. It rejects callbacks from a replaced project and
  invalidates the current composition/preview after a successful decode.
  Unsaved Image Editor changes are not streamed.
- Sidecar documents and outputs currently live beside the source. Moving the
  source after creating the link does not automatically relocate those
  sidecars; path repair remains a future workflow to validate.

### Motion composition workflow

- **Deferred until the standalone Motion Studio MVP passes its documented
  acceptance criteria and a stable handoff contract is validated.** The
  standalone editor is implemented, but opening or creating a linked
  composition from a Video Editor clip or Media Pool item is not available yet.
- Before insertion, the composition's duration, frame rate, canvas, and media
  dependency behavior must be explicit.
- Saving a supported composition publishes a new saved revision to the Video
  Editor, which refreshes the corresponding output and invalidates dependent
  render-cache entries. The native Motion Studio document remains separate
  from the `.csp` project and source media is not overwritten.

These workflows share a handoff contract, but image documents and motion
compositions have different semantics and should keep distinct native formats
and adapters.

### Update and conflict behavior

For the initial linked integration, the Video Editor refreshes after a linked
document is saved; implementation details such as polling output size and
modification time remain subject to validation. The Image Editor compares
the linked document's saved SHA-256 fingerprint and serializes linked writers
with a lock file. Streaming unsaved preview frames between running applications
is a later capability because it requires
continuous inter-process communication, resource-lifetime rules, and additional
stale-frame and performance handling.

The initial contract should define how to handle:

- document identity, type, schema version, and saved revision;
- project-relative or otherwise portable resource locations, plus explicit
  external references;
- linked media dependencies and missing resources;
- output properties needed by the host, including image dimensions and color
  behavior, and motion canvas, frame rate, and duration;
- cache invalidation when a saved revision changes;
- unsupported document versions and edits;
- simultaneous edits, stale revisions, and safe conflict recovery.

The applications should report missing or unsupported links clearly and retain
enough information to recover the link when the resource becomes available.
Saving should not leave a partially written document that the host can mistake
for a complete revision. Whether this requires atomic file replacement,
revision manifests, or another mechanism remains to be validated.

## Costs, Risks, and Alternatives

Linked editing can preserve editable structure and reduce repeated manual
exports. It also adds compatibility, path portability, cache invalidation, and
conflict-handling work. Live unsaved updates add substantially more runtime
coordination and should be justified by a concrete workflow before being
implemented.

Flattened export/import is a simpler fallback for initial interoperability,
but it loses editable structure and does not provide automatic updates to the
Video Editor. A linked native document offers a richer workflow at the cost of
requiring explicit version and dependency behavior. Validate both against
representative projects before choosing the first supported interchange path.

## Validation Criteria

Before making this direction final, prototype and document:

1. opening a linked document from the Video Editor and returning to the host;
2. saving a new revision and refreshing the affected preview and render cache;
3. preserving the source asset and detecting missing or moved dependencies;
4. rejecting or recovering from unsupported versions and stale concurrent
   edits;
5. measuring frame refresh time, memory use, and behavior with large images
   and compositions;
6. building and running the handoff workflow on Windows, macOS, and Linux.

## Coordination Guidance

1. Maintain a capability ownership matrix for the applications.
2. Develop the three application tracks in parallel while keeping Video Editor
   stability a priority.
3. Within the Image Editor track, require standalone acceptance before
   accepting linked-image compatibility; the existing prototype may remain
   bounded while that work proceeds.
4. Define and validate Video Editor and Motion Studio composition contracts
   before relying on them for cross-application handoff.
5. Extract only proven capabilities used by multiple applications into
   focused libraries with stable APIs.
6. Validate each handoff with regression coverage in both producer and
   consumer applications.
7. Review advanced Image Editor expansion separately against project capacity.

This coordination keeps shared libraries aligned with real consumers, gives each
document type a suitable contract, and avoids duplicating media, rendering,
or animation engines without a documented technical reason. Parallel
development does not remove acceptance gates for dependencies within an
application or between producer and consumer applications.

OpenFX may be evaluated later for third-party effect-plugin interoperability.
A plugin host API is distinct from the internal APIs and linked-document
contract used by the project's own applications.

## Image Editor Layer Mask Publication

The Image Editor writes `.cimg` v10 with optional raster layer masks and reads
v1–v9. Linked saves still publish an ordinary flattened PNG with straight alpha;
the Video Editor `.csp` format and handoff arguments are unchanged. Producer UI
coverage edits and saves a mask through linked mode. When `BUILD_IMAGE_EDITOR`
is enabled, the Video Editor main-window regression links the Image Editor core
for a test fixture, publishes a masked PNG, and checks refresh, retained alpha,
and clip-variant isolation. This test dependency does not affect application
runtime dependencies or a Video Editor-only build.
