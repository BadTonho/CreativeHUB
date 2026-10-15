# Shared Visual Effects Contract

## Workspace catalog

The Effects dock is the creation catalog for Edit and Fusion. App-owned
`Definition` metadata describes an action for each workspace; `actionFor`,
`isAvailable`, workspace-aware `isDraggable`, and `categoryAvailable` are shared
by Toolbox, Effects and Favorites. This UI catalog does not own render engines
or persisted graph types. Render offers no catalog creation actions.

| Category | Edit | Fusion |
| --- | --- | --- |
| Video | Visual effects, Fusion Composition, Adjustment Clip | Visual effect nodes |
| Video > Generators | Opaque-white Background clip | Transparent Background node |
| Text | Text clip | Text node |
| Audio / Transitions | Existing Edit tools | Hidden |
| Fusion | Hidden | Input, Transform, Color, Merge |
| Fusion > Masks | Hidden | Rectangle Mask, Ellipse Mask |

All includes only available items; parents include descendants and empty
categories are hidden. A category unavailable after a workspace change resets
to All. Favorites keep their existing settings key and stable IDs; filtering
never removes a hidden workspace-specific favorite from storage.

In Fusion, drag from Effects or Favorites to create a node at the drop position,
or double-click to create and select a disconnected node at a free location
near the visible graph center. Automatic placement avoids existing nodes and
scrolls the result into view. Creation is disabled without a compatible selected
clip. The Node Editor retains Grid controls; the Add Node selector/button have
been removed. Existing visual effects still split image cables when dropped
on them and refuse mask cables. Other catalog nodes remain disconnected even
when dropped over a cable. Typed wiring is unchanged.

Fusion IDs `fusion.input`, `fusion.transform`, `fusion.color`, `fusion.merge`,
`fusion.rectangle_mask`, and `fusion.ellipse_mask` use the existing effect-ID
MIME with the same copy semantics. Background and Text retain `video.background`
and `text.text`, resolving to the workspace's action. Both drop destinations
validate compatibility; unknown IDs and Fusion-only timeline drops are refused.
Creation reuses graph commands, dirty tracking, playback invalidation and
Undo/Redo. Functions shares this catalog and its workspace actions as described
below. No shortcut, rendering or CSP v29 format change is introduced.


## Functions

`Shift+Space` opens the searchable, non-modal Functions palette. The list reads
`effects::definitions()` and uses the same workspace availability/actions and
copy-only effect-ID MIME as Effects; it has no separate list of supported IDs.
Entries use spaced, rounded cards with scalable leading icons and distinct
hover/selection states. Full names remain in the model and tooltips even when
elided visually. The resizable palette starts at 420x480 logical pixels and
keeps the existing activation, keyboard and drag contracts.
Search remains available without a compatible selection and retains its query
when context changes while the window is open. Render closes/disables Functions.

| Workspace / item | Add, Enter or double click | Drag |
| --- | --- | --- |
| Edit visual filter | Apply to the compatible selected clip | Existing compatible Timeline target |
| Edit Background, Text, Adjustment Clip, Fusion Composition | Disabled; show `Drag this item to the timeline.` | Create at the chosen Timeline position |
| Edit transition | Disabled; same drag hint | Existing compatible cut |
| Edit Gain | Unavailable | Unavailable |
| Fusion available catalog entry | Create a disconnected node at a free visible position and select it | Create at the chosen graph position; visual filters retain image-cable insertion |

Fusion requires a compatible selected clip for both activation and drag. Edit
filters require a compatible selected clip for activation, while dragging can
choose another existing target; generators remain draggable without selection.
Hidden, unavailable and query-filtered entries cannot produce drag MIME.
Background retains opaque-white clip defaults in Edit and transparent node
defaults in Fusion. Input/Text/mask defaults and typed-port rules are unchanged.

`FunctionPalette::setContext` synchronizes workspace/target availability;
`setAddHandler` installs a synchronous bool-returning edit callback. MainWindow
routes Fusion to `addCatalogNode` and Edit visual filters to
`addEffectToSelectedClip`, whose bool reports the existing command result.
The palette closes only on successful activation or accepted drop. Refused
activation and canceled/rejected drops keep it open; nested activation is
blocked. Cancel, Escape, outside clicks, title close, shortcut toggling and drag
deactivation protection retain their existing lifecycle. Creation uses normal
history/dirty-state updates; technical errors retain existing diagnostic
boundaries and normal refusals do not log errors.

## Background Generator

Video has a Generators child category containing Background; parent filtering includes descendants. Background uses `video.background` drag MIME and creates a clip. All and local Favorites expose it too. Favorites persist known draggable catalog IDs and can be removed through context menus. See [Background Generator](../features/BACKGROUND_GENERATOR.md).

## Fusion masks v1

Optional typed Mask input 2 limits Effect/Color/Transform processing through premultiplied blending with the original. Merge attenuates foreground contribution; generators/Input/Output multiply alpha. No mask preserves existing behavior. See [Fusion Masks](../features/FUSION_MASKS.md).

## Fusion generators

Effects > Video includes the `video.fusion_composition` generator. Its clip
supports the existing effect stack and Functions. Fusion also offers Background
and Text generator nodes, distinct from the Color correction node. See
[Fusion Composition](../features/FUSION_COMPOSITION.md) for static generator controls
and the existing node animation boundary.


Status: provisional.

`libs/effects` provides a shared CPU visual-processing API used by the
Video Editor and Motion Studio. The `creative-suite::effects` target depends
on the shared RGBA frame type and standard C++ only; it does not depend on Qt
Widgets or own application UI. Each application keeps its own effect model,
document format, and user interface.

## Video Editor adjustment layer

The Effects > Video catalog also contains `video.adjustment_clip`, an
application-owned generator rather than a new shared filter. Existing
Grayscale, Brightness, Contrast and Saturation stacks can target an adjustment
through drag/drop, Functions or the Inspector, with enabled state and order
retained. The Inspector's position, scale, rotation and opacity curves work on
the lower-track composition. Opacity controls the strength of the complete
adjustment: 0% preserves the original; 100% uses the fully adjusted canvas.
Copy/Paste Attributes exchanges effects and transforms with ordinary visual
clips; audio, text and Fusion attributes remain incompatible with adjustments.
See [TIMELINE.md](TIMELINE.md) and [RENDERING.md](RENDERING.md).


## Built-in filters

| Stable ID | Parameter | Range | Default |
| --- | --- | --- | --- |
| `video.grayscale` | Amount | 0–100% | 100% |
| `video.brightness` | Brightness | -100–100 | 0 |
| `video.contrast` | Contrast | 0–200% | 100% |
| `video.saturation` | Saturation | 0–200% | 100% |

Definitions are discoverable through `builtInEffects()` and `findDefinition()`.
Instances hold an ID, explicit parameter IDs/values, and an `enabled` flag that
defaults to true. A stack may contain repeated instances; evaluation follows
vector order while skipping disabled instances without changing their stored
position or parameters. Validation rejects unknown IDs, missing or duplicate
parameters, non-finite values, and values outside the definition's range.

## Processing

`applyStack()` mutates a valid RGBA frame in place. It processes only the
visible RGB bytes for each pixel, respects row stride, preserves alpha and row
padding, and returns false for malformed frames or invalid stacks. An empty
stack succeeds without changing pixels. Brightness offsets encoded channel
values; contrast scales them around 127.5; grayscale and saturation use
Rec. 709 luma coefficients. Each stage clamps to the 8-bit range before the
next filter runs.

The Video Editor submits each clip's original stack to the shared compositor.
Preview and offline export use `colorAdjustmentPasses()` to prepare the same
ordered GPU operations when experimental composition is selected. That helper
validates every instance, including disabled instances, and preserves rounding
after each enabled pass. CPU composition applies the original stack exactly once,
including GPU recovery. Disabled instances do not modify the frame;
active instances retain their relative order. Filter parameters and enabled
states are Timeline project data; caches and temporary preview state are not
added by this library. The Video Editor persists stacks under the `.csp`
version 18 clip schema described in [PROJECT.md](PROJECT.md). Version 17
stacks migrate with every instance enabled.

`applyColorAdjustment()` is a fused processing entry point for applications
whose Color Adjustment combines brightness (-100 to 100), contrast (0–200%),
and saturation (0–200%). It applies brightness, contrast around 0.5, and
Rec. 709 saturation in one pixel pass and rounds RGB only at the end. This
preserves Motion Studio's existing pixel result; it is separate from the
Video Editor's independently ordered brightness, contrast, and saturation
filters in `applyStack()`, which quantize after each filter. The fused call
preserves alpha and row padding, validates its frame and parameters before
processing, and checks cancellation before each row. It returns Completed,
Cancelled, or InvalidInput. Cancellation may leave earlier rows changed, so
callers must discard a cancelled frame. Exceptions raised by the cancellation
callback propagate to the caller.

## Verification

`libs/tests/effects_test.cpp` covers neutral values, parameter bounds,
grayscale, alpha and stride preservation, stack order, enabled-state defaults,
disabled-instance skipping, repeated instances, invalid IDs, malformed RGBA
frames, fused Color Adjustment compatibility, and cancellation. Motion Studio consumer coverage lives in
`apps/motion-editor/tests/preview_renderer_test.cpp` and exercises delegation,
pixel parity, cancellation, and timing. Video Editor boundary coverage lives
in the project, Timeline command, Preview, offline export, Effects dock,
Functions, and Timeline widget tests. The manual acceptance checklist is in
[`../REGRESSION_TESTING.md`](../validation/REGRESSION_TESTING.md#manual-ui-validation).
