# Cross-Application Compatibility Proposal

**Status:** Provisional. This document records a direction to validate, not a
final format, API, implementation commitment, or change in application
priorities.

## Goal

Allow the Main Editor, Motion Studio, and a future Image Editor to share stable
domain capabilities and continue supported work across applications. Keep
shared code reuse and document handoff as separate concerns: applications can
reuse a library without sharing project files, and they can exchange a linked
document without sharing their entire editing workflow.

The Image Editor remains a future module. This proposal does not move it ahead
of the Main Editor or Motion Studio foundations.

## Recommended Boundaries

Applications should depend on reusable libraries through documented APIs.
Shared libraries must not depend on application UI code. Each application can
adapt its own controls and workflows to shared operations.

| Capability | Suggested ownership |
| --- | --- |
| Layers, transforms, masks, and composition | Shared composition library when a second application has a real use case |
| Keyframe evaluation and curves | Shared by the Main Editor and Motion Studio; include the Image Editor only if animation becomes an approved use case |
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

## First Shared Capability to Validate

Start with a small composition operation: a layer with a source, transform,
opacity, optional mask, and a defined compositing result. The Main Editor
already has composition-related code, including a backend-neutral composition
stage and a CPU frame compositor. Use those existing boundaries to prototype a
stable API before moving code into a shared library.

The API contract should document:

- coordinate systems, units, and transform conventions;
- pixel formats, color assumptions, and alpha behavior;
- resource ownership, lifetime, and thread requirements;
- errors and the context that must be logged;
- serialization and compatibility expectations, if the data is persisted.

Keep keyframe and curve evaluation as a separate capability so applications
can adopt it according to their needs.

## Extraction Criteria and Build Shape

Keep a capability application-local while the Main Editor is its only
consumer. Extract it into a focused library under `libs/` when a second
application has a real use case and the API can serve both without
application-specific conditions.

Use CMake library targets and link each consuming application to the required
targets. Prefer focused modules such as `composition` or `animation` over a
catch-all core target. Choose static or shared linkage based on deployment and
plugin requirements; runtime-loaded libraries are not required merely to
share code among applications in one build.

Before extraction, confirm that:

1. one implementation owns the behavior;
2. both consumers use the same documented contract;
3. regression coverage exercises the shared behavior and both application
   boundaries;
4. application UI and workflow decisions remain outside the library.

## Linked Editing Between Applications

The proposed workflow uses a separately saved, editable document linked from
the Main Editor. The Main Editor keeps a stable reference to that document and
uses its current supported output in the timeline. The linked document keeps
its own native editing model and history. The source media remains available
and is not silently overwritten by an editor handoff.

The intended experience is for the Main Editor to reflect changes promptly
while the linked document is being edited. Deliver this in stages: first
refresh after each successful save, then evaluate unsaved live previews if
that workflow justifies the added runtime coordination.

### Image document workflow

- Opening an image from the Media Pool can create a companion document in the
  Image Editor's native format, initialized from that image. Later opens should
  reuse the existing companion document instead of creating another copy.
- The original image remains unchanged. The companion document may reference
  the original as its source layer or embed a copy when portability requires
  it; the storage policy must be validated, especially for large images.
- A Media Pool edit applies to that media item, so every timeline clip that
  uses the item receives the newly saved revision. The document location,
  native extension, and behavior when the source moves remain open decisions.
- From an image clip in the timeline, the user can open or create a linked
  image document for editing. Whether this link edits every use of the Media
  Pool item or creates a clip-specific variant is an open product decision;
  define it before implementation.
- Saving a supported image document makes the new saved revision available to
  the Main Editor, which refreshes affected previews and invalidates dependent
  render-cache entries.

### Motion composition workflow

- From a supported timeline clip, the user can open or create a linked
  composition in Motion Studio.
- From a Media Pool item, the user can create a composition based on that
  resource. The composition's duration, frame rate, canvas, and dependency
  behavior must be explicit before it is inserted into the timeline.
- Saving a supported composition makes its new saved revision available to
  the Main Editor, which refreshes the clip output and invalidates dependent
  render-cache entries.

These workflows share a handoff contract, but image documents and motion
compositions have different semantics and should keep distinct native formats
and adapters.

### Update and conflict behavior

For an initial integration, update the Main Editor when a linked document is
saved. Detecting a new saved revision may use a portable file/revision check;
the transport mechanism is not decided. Streaming unsaved preview frames
between running applications is a later capability because it requires
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
Main Editor. A linked native document offers a richer workflow at the cost of
requiring explicit version and dependency behavior. Validate both against
representative projects before choosing the first supported interchange path.

## Validation Criteria

Before making this direction final, prototype and document:

1. opening a linked document from the Main Editor and returning to the host;
2. saving a new revision and refreshing the affected preview and render cache;
3. preserving the source asset and detecting missing or moved dependencies;
4. rejecting or recovering from unsupported versions and stale concurrent
   edits;
5. measuring frame refresh time, memory use, and behavior with large images
   and compositions;
6. building and running the handoff workflow on Windows, macOS, and Linux.

## Suggested Sequence

1. Maintain a capability ownership matrix for the applications.
2. Specify and validate the composition contract inside the Main Editor.
3. Build a Motion Studio prototype that consumes the same operation.
4. Extract proven capabilities into focused libraries once a second real
   consumer exists.
5. Define and validate saved-revision handoff for a concrete Motion Studio
   composition workflow.
6. Evaluate Image Editor adoption and its linked-image workflow after the Main
   Editor and Motion Studio foundations are stable and the project has
   capacity.

This sequence keeps shared libraries aligned with real consumers, gives each
document type a suitable contract, and avoids duplicating media, rendering,
or animation engines without a documented technical reason.

OpenFX may be evaluated later for third-party effect-plugin interoperability.
A plugin host API is distinct from the internal APIs and linked-document
contract used by the project's own applications.
