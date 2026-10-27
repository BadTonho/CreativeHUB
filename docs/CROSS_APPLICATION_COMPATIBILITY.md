# Cross-Application Compatibility Proposal

Static Gaussian Blur transfers as an ordered editable effect in `.cscomp` v3.
Its CPU implementation and existing GPU operation are shared; native Motion
stays v8 and CSP advances to v31. See [contract](COMPOSITION_INTERCHANGE.md).

[Shape-mask interchange](COMPOSITION_INTERCHANGE.md) adds editable shared
Rectangle/Ellipse masks on sources, effects and Transform. Coverage and scalar
curve slicing are shared; the mask delivery introduced Motion v8; Gaussian Blur now advances CSP to v31.
Automated coverage: `creative-suite-mask-interchange`, shared composition/
OpenGL, native Motion preview/export/history/persistence, and Hub backup gates.
Native UI and platform acceptance remain separately pending.

Editable Fusion/Motion interchange is documented in [Composition interchange](COMPOSITION_INTERCHANGE.md). CSP v31 and Motion v8 carry portable presentation metadata and editable shape masks; linked Motion publication remains a separate workflow.

## Named Background compositions

CSP v29 adds optional Fusion Composition display names for the Edit Background
preset. Video Editor owns insertion, naming and graph editing. Hub supports
schemas 1–29, preserves names and graph/input-offset data, relocates external
Input sources, and rejects future v30. Producer/consumer coverage is in
`hub-backup-format-test`; no shared rendering API or other app format changes.

## Fusion mask contracts

Video Editor owns CSP v28 mask geometry/animation and typed graph connections.
Hub preserves these fields and composition offsets while relocating external
Input paths; its supported upper bound is 28, rejecting future 29. Shared
composition ApplyMask/MaskedBlend uses straight RGBA with premultiplied blending.
Video Preview/export plus shared OpenGL, Image raster and Motion GPU/export
consumer regressions cover the affected boundary without changing their formats.
Shared CPU and OpenGL mask coverage uses Rec.709 luminance with the same
integer rounding rule for colored mask pixels. Grayscale masks retain their
existing coverage. Track mattes and effect masks follow the same RGB weights;
mask alpha continues to follow each operation's existing contract.

## Fusion Composition backups

CSP v27 is owned by the Video Editor. The Hub backup adapter preserves independent
Fusion graphs and Background/Text generators without inventing a primary media
path; external Input paths use the existing relocation contract. The shared
Motion/Image document formats and application versions are unchanged.


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

The initial Video Editor to Motion Studio linked-composition workflow is also
implemented behind its own versioned handoff contract. It supports Timeline
video clips and Media Pool images or videos. This integration proceeds in
parallel with remaining standalone Motion Studio acceptance work; it is not
accepted until producer/consumer regression coverage and the manual checks
below pass in both applications.

## Hub document snapshots

The Hub rewrites documented relative references in Image, Video, and Motion
backups to the original absolute locations while preserving the document
version and editing data. Media remain external. This adapter is owned by the
Hub; editor schemas and readers remain application-owned. The test-only
`hub-backup-format-test` uses all enabled editors' persistence producers and
consumers to verify supported older versions and relocated snapshots. See
[Hub backups](hub/BACKUPS.md) for supported fields, failure handling, limits,
and the pending native cross-application checklist.
New snapshots have a unique per-backup directory and a Hub-owned version-1
`record.json` that persists the original path. Editors read only the project
file; no editor schema or reader change is required. Older flat snapshots
remain available with unknown ownership, and are not attributed by basename.

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
consumer refresh through the Video worker. See [delivery evidence](video-editor/gpu/GPU_COMPOSITION_RESULTS.md).

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

Standalone Motion Studio composition creation and rendered video export remain
independently usable. Saving a linked composition publishes a new silent video
render, and the Video Editor refreshes after that saved revision. Unsaved live
previews are outside this first integration step.

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
  Offline export resolves this same order from its immutable project snapshot,
  including shared publications used by explicit still-image Fusion inputs.
  Missing publication files fall back to the next source; an existing but
  undecodable publication fails export with a logged media error.
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

- Right-click a video Timeline clip to create or reopen its linked composition.
  The selected source range becomes a video layer at the start of the Motion
  timeline; its source-in is stored in `.motion` v5. The Video Editor keeps the
  clip's Timeline position, duration, and original audio. A longer Motion
  render remains trimmed to that clip duration until the editor user extends
  the clip.
- Video Editor offline export opens the published Motion video at clip-local
  time using the publication's frame rate. Original source-in and frame rate
  remain the basis for original embedded audio; publication audio is ignored.
  A missing publication falls back to the original video. An online publication
  can export without audio while the original video is offline.
- Right-click a video or image Media Pool item to create or reopen a separate
  linked composition. The source stays unchanged and the linked render is
  added as a new Video item. Image layers begin with Motion Studio's existing
  five-second duration; video layers begin at source frame zero.
- Per-origin sidecars live under `<source>.motion-studio/clips/<uuid>/` for a
  Timeline clip and `<source>.motion-studio/assets/<uuid>/` for a Media Pool
  item. Each contains `composition.motion`, `handoff.json`, and the published
  render. Video Editor `.csp` v24 stores the link, source identity, and output
  profile; versions 1 through 23 load without Motion references.
  Version 25 stores nonnegative `render_start_time_us` on links; v24 links start
  at zero. The Video Editor advances publication time on splits and left/rolling
  trims, preserving it across history, saves, and project frame-rate changes.
  Preview, Fusion Viewer, and export convert this time to the publication's
  actual frame rate; original audio source-in remains independent.
- The Video Editor offers a container, compatible video codec, and quality on
  first link creation. Motion Studio uses the composition canvas and frame rate
  for publication and exports without audio. It saves `.motion` before
  exporting to a temporary file, validates the completed output, then replaces
  the last valid render atomically. Cancellation or failure leaves the prior
  render intact and marks publication pending.
- The Video Editor watches linked renders, decodes updates in the background,
  refreshes only linked items or clips, and invalidates affected preview/cache
  state. It rejects responses for a replaced project. A missing render falls
  back to the original source when available; if both are missing, the Video
  Editor reports that the link needs repair. Motion Studio locks `.motion`
  saves and checks the saved revision to reject stale concurrent writers.
- Motion Studio **Save As** creates an independent composition copy and removes
  its link to the original Video Editor item. Neither source media nor the
  original `.csp` link is rewritten by Motion Studio.
- Unsaved changes are not transmitted. Only a successful Save followed by a
  successful export updates the linked render.

### Motion Studio image-layer workflow

- Select or right-click an Image layer in Motion Studio and choose **Edit Image
  in Image Editor**. Motion Studio opens or reopens the linked `.cimg` with the
  layer's saved source snapshot. The Image Editor's existing linked launch
  arguments and per-document lock protect the editable document from stale
  concurrent saves.
- Each Motion composition and layer instance owns a sidecar under
  `<composition>.motion-studio/image-editor/<layer-id>-<uuid>/` containing
  `composition.cimg`, `published.png`, and `source.png`. The original media and
  other Motion layers that use the same source remain unchanged.
- Motion Studio `.motion` v6 stores the three paths on that Image layer. Versions
  1 through 5 load without Image Editor links. Preview and export resolve the
  published PNG by layer ID, preserving the layer's Motion transforms,
  keyframes, effects, timing, and ordering.
- Motion Studio polls the published PNG metadata and decodes changes
  asynchronously. A successful saved publication refreshes only its layer and
  invalidates the preview. Unsaved Image Editor changes are not streamed.
  Decode failures retain the last valid frame; if no valid published frame is
  available, Motion Studio falls back to `source.png`, then the original source
  when available, and reports a missing link when neither can be read.
- Motion Studio **Save As** copies the sidecar files to new per-layer locations
  and updates only the copied composition's references. Image Editor changes
  made through either composition therefore publish to separate PNG files.
- Manual producer/consumer validation remains pending. The Image Editor
  standalone acceptance gate remains a prerequisite for accepting this linked
  compatibility workflow.

These workflows share a handoff contract, but image documents and motion
compositions have different semantics and should keep distinct native formats
and adapters.

### Update and conflict behavior

For Image Editor links, the Video Editor compares the linked document's saved
SHA-256 fingerprint and serializes linked writers with a lock file. For Motion
Studio, the Video Editor refreshes after a successful atomic render publication
by watching output size and modification time. Streaming unsaved preview frames
between running applications is outside this first integration step.

The Motion handoff and `.motion`/`.csp` references define the first contract for
document identity, source range, composition timing, output profile, cache
refresh, and stale-writer rejection. The `.motion` v6 Image Editor reference
adds per-layer image-document and publication paths. The contract remains
provisional while manual checks validate portability, missing-media recovery,
and conflict handling. Future fields must be versioned and must preserve the
Image Editor's separate document and publication semantics.

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

## Video adjustment persistence and Hub backups

The Video Editor owns source-free Adjustment Clips in `.csp` v26. Their ordered
lower-track composition is an editor-specific timeline operation, reusing shared
animation, effects and composition primitives. It does not introduce a shared
Motion/Image document or handoff protocol. The Hub path adapter accepts v26
and preserves adjustment duration, effects and transform curves without adding
media references. `hub-backup-format-test` produces with the Video Editor writer
and restores through its reader, alongside versions 1–25. Application version
metadata remains unchanged. Native restore/reopen visual acceptance is separate
from this automated producer/consumer contract; see [Hub regression testing](hub/validation/REGRESSION_TESTING.md).

## Shared static blend-mode composition

The shared CPU/OpenGL layer contract has a Qt-free `BlendMode` defaulting to
Normal. Existing callers and image operations retain their defaults. Motion alone
exposes the six modes; it owns native v13, history and scene snapshots. Other
applications do not adopt Motion's schema or workflow. `.cscomp` remains v3 and
refuses non-Normal layers; Hub preserves v13 backups and rejects v14.

Non-Normal scenes accumulate transparent premultiplied RGBA8 internally and flatten
over black before public output. Normal-only scenes keep their exact old path.
OpenGL uses one worker-owned, budgeted backdrop without draw/sampler aliasing;
Vulkan returns Unsupported so callers can recover. Shared CPU/OpenGL/Vulkan and
Motion tests cover new semantics; Video/Image rendering/preview/export gates
protect the existing contracts. Native GPU acceptance and fallback are recorded
separately. See [pixel/ownership contract](motion-editor/features/BLEND_MODES.md).

## Track Matte processing boundary

Motion native v14 owns matte references, graph orchestration and UI. The shared
Qt-free TrackMatteMode/CPU operation and additive OpenGL image operation receive
same-size processed pixels, not application document references. Vulkan refuses
the new operation. Video/Image continue their current Normal and image contracts
without matte UI or persisted data. Hub preserves native bindings and media paths;
`.cscomp` v3 refuses them. Shared, Motion, Hub and affected consumer regressions
cover the boundary; native/platform acceptance remains separate.

Motion native v15 adds Bezier shapes/masks and complete-path keys. Its original Hub delivery preserved them in vault/restored backups and rejected future v16; the current boundary follows below. Editable `.cscomp` v3 transfer refuses these features explicitly. Video/Image retain their current UI and persisted formats; their shared mask-coverage calls keep the default coordinate path.

Motion native v16/Hub backup preserves animated Trim Paths without changing
media rebasing or shared renderer contracts. Hub rejects v17. `.cscomp` v3
continues to refuse these Bezier shapes with layer identifiers and a Trim Paths
annotation. Producer/reader backup and exchange regressions protect this boundary.

## Drop Shadow contract maintenance

Motion native v17/Hub backup preserves Drop Shadow colors, four bases/curves and effect masks while rebasing existing media paths. Motion v18 adds Glow, and Hub now accepts through v18 and rejects v19. `.cscomp` v3 explicitly refuses both effects. Video/Image maintain the shared CPU/GPU operation contracts without these effect controls or persisted records.

## Image Editor v17 layer outlines

Image Editor adds an optional native layer outline without changing linked PNG
publication or Video Editor host-link schemas. The published raster includes
masked outlines. Hub accepts Image v17, rebases existing source/import paths,
and preserves style data through backup/restoration. The current Hub accepts
Image v1-19 and rejects v20. Image reads v1-16 with no outline; older Image binaries reject
v17. Producer UI publication, Video Editor refresh regressions and Hub actual
reader round trips protect this boundary. Native cross-application acceptance
remains pending. See [outline contract](image-editor/features/LAYER_OUTLINE.md).

## Shared transparent raster blend composition

Image v19 introduces layer/group modes; Motion and Video native formats and
published PNG handoff remain unchanged. Qt-free channel/alpha helpers in
`composition/blend_mode.h` serve transparent source-over and SourceAtop without
opaque finalization. Video's existing CPU accumulator reuses them; Normal and
opaque output behavior is preserved. OpenGL raster layer/node records add
trailing default-Normal fields, validated before rendering. A separately
budgeted GPU backdrop serves transparent raster blending; general Video/Motion
composition retains its established finalization. Shared composition/OpenGL,
Image, Video and Motion consumer gates protect the additive boundary. See
[Image contract](image-editor/features/BLEND_MODES.md).


## Shared Gradient Map adjustment processing

Image native v20 introduces app-owned adjustment scope and selection-seeded masks. Hub preserves these in backups and accepts Image through v30, rejecting v31. Shared effects provide validated RGB stop interpolation; optional trailing OpenGL raster fields preserve Image alpha without changing general Video/Motion opaque output, registries or formats. PNG publication remains unchanged. See [Image contract](image-editor/features/GRADIENT_MAP.md).

## Brightness/Contrast shared boundary

Image v21 adds a typed adjustment without changing PNG publication. Hub preserves
all current adjustment types and masks and accepts v1-30, rejecting v31. The shared RGB
evaluator preserves existing Video/Motion effects; no new registry entries are
added. [Contract](image-editor/features/BRIGHTNESS_CONTRAST.md).

## Exposure / Offset / Gamma shared boundary

Image native v22 adds the third typed adjustment. Shared effects provide the
validated linear-sRGB evaluator and the OpenGL raster compositor evaluates it
on its own backdrop without readback; Image owns workflow and stack order.
Video/Motion effects, native formats and PNG handoff remain unchanged. Hub
accepts Image v1-30, rejects v31 and preserves all six adjustment payloads and
masks through real-reader restoration. Existing consumers and shared regressions
protect the additive API; native workflow acceptance remains separate. See
[Image contract](image-editor/features/EXPOSURE_OFFSET_GAMMA.md).

## Global Hue/Saturation shared boundary

Image v23 introduces the fourth adjustment kind. Shared effects provides an
encoded-sRGB HSL evaluator and OpenGL raster evaluates it on the retained
backdrop. Image owns scope/history; Video's Rec.709 saturation, Video/Motion
registries/formats and PNG publication retain their contracts. Hub accepts Image
v1-30, rejects v31 and preserves all six kinds/masks through real-reader restore.
The Video focused linked-image gate uses a real exposure+HSL producer and verifies
rotated RGB, alpha and variant isolation. See [contract](image-editor/features/HUE_SATURATION.md).

## Global Levels shared boundary

Image v24 introduces the fifth adjustment kind. Version 27 adds Red, Green and
Blue parameter sets alongside the existing RGB master. Shared effects applies
the encoded-sRGB master mapping first, rounds to a byte, and then evaluates the
matching channel set; OpenGL follows the same retained-backdrop stages. Image
owns layer scope, histogram, masks, history and persistence. Video/Motion
formats and linked PNG protocol remain unchanged. Hub accepts Image v1–30 and
rejects v31, retaining all four parameter sets and masks through backup and
relocation. The Video linked-image test consumes flattened output from a v27
Image producer; Motion keeps using the same PNG contract. See
[contract](image-editor/features/LEVELS.md).

## Global Curves shared boundary

Image v25 introduces the sixth adjustment kind. Shared effects builds bounded
shape-preserving encoded-sRGB lookup tables; the OpenGL raster path consumes the
same tables as uniforms. Image owns point editing, histogram, masks, history and
persistence. Video/Motion effect registries and native formats remain unchanged;
linked PNG publication retains its protocol. Hub accepts Image v1–30 and
rejects v31, preserving Curves and masks through backup and relocation. The
Video linked-image gate consumes flattened output from a v27 Image producer. See the
[contract](image-editor/features/CURVES.md).

## Per-channel Curves document and rendering compatibility

Image v26 adds an RGB master curve followed by individual Red, Green and Blue
curves in its application-owned adjustment payload. The shared evaluator
composes effective channel lookup tables; CPU and OpenGL preserve alpha and
match byte results. Version 25 documents migrate their existing curve to the
master and initialize individual channels to identity. Hub accepts Image v1–30
and rejects v31 while preserving all four point sets and relocated media. Video
and Motion continue consuming flattened PNGs under the existing publication
protocol. See the [Curves contract](image-editor/features/CURVES.md).

## Per-channel Levels document and PNG compatibility

Image v27 stores the existing RGB Levels values as the master and adds
independent Red, Green and Blue sets. Versions 24–26 migrate their old values
to the master and initialize individual channels to identity; v26 Curves
payloads remain unchanged. Hub backups retain v27 documents, accept through
v30 and reject v31.
The Video Editor linked-image regression produces the v27 Image document,
publishes its adjusted PNG, and confirms the consumer refreshes the media and
preview. Motion continues consuming the same flattened PNG contract without
reading the Image document schema. See the
[Levels contract](image-editor/features/LEVELS.md).

## Hue/Saturation color-range compatibility

Image v29 stores six fixed color-range parameter sets. Earlier Image documents
migrate them to neutral values. Hub accepts v1-30 and preserves the values
through backup and restore; Video Editor and Motion Studio consume the resulting
flattened PNG under the existing publication contract. CPU and OpenGL share the
original-hue interpolation rule. See [Image contract](image-editor/features/HUE_SATURATION.md).

## Color Balance document compatibility

Image v30 adds Color Balance adjustment parameters. Hub backup and restore
preserve those parameters; Video Editor and Motion Studio continue to consume
the flattened PNG under the existing publication contract.
