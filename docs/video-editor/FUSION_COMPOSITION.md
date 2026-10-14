# Fusion Composition

[Editable composition interchange](../COMPOSITION_INTERCHANGE.md) transfers compatible Fusion compositions to Motion layers and back through `.cscomp` files. CSP v30 preserves imported presentation semantics.

The [Background Generator](BACKGROUND_GENERATOR.md) in Edit creates an opaque-white
preset of this clip. Both workspaces edit the same Background node colors; normal
Fusion Composition and the Fusion catalog Background node defaults remain transparent. CSP v29 optionally
preserves composition display names.

Fusion Composition is independent generated content on the Video Editor Timeline.
It owns a Fusion graph and has no primary media file or audio. It is separate
from an Adjustment Clip: its graph produces a normal visual layer.

## Create and edit

Drag **Effects > Video > Fusion Composition** to a free video-track range.
The teal block starts at five seconds, rounded up to project frames. Its initial
graph is a transparent **Background → Output**, so lower tracks remain visible.
Select it in Edit to preview the Timeline and edit the normal Inspector effects,
transform and keyframes. Open Fusion to edit its graph; leaving Fusion restores
Timeline composition. Node VIEW controls remain temporary viewer state.

Add Background, Text, Input, Transform, Color, Merge, Rectangle Mask or Ellipse Mask through the workspace-filtered Effects catalog by dragging or double-clicking. Existing
Effects/Functions filter drops create effect nodes. Background has static RGBA
color; its default alpha is zero. Text reuses the normal text renderer with
content, font, pixel size, alignment and RGBA color. Set these properties and
press **Apply Text**. The default is Text, Sans Serif, 48 px, white and centered.
Use Transform nodes for animation; the existing Brightness animation remains
available. Background/Text properties themselves are static in this version.

Input chooses project video/image media; the empty choice is **Transparent**
for independent compositions. For ordinary media clips it remains **Selected
Timeline clip**. An unconnected Output is transparent. There is exactly one
Output, but media Input nodes are optional. Background/Text can also be used
in ordinary video/image graphs.

Move, split, trim/extend, delete, Copy/Paste Attributes and Undo/Redo use the
existing Timeline commands. Edit > Duplicate Fusion Composition creates an
independent graph copy immediately after the clip when that range is free.
There is no default shortcut. Standard visual transitions are available; audio
tracks and collisions without an explicit overlap transition are refused.
The one-frame minimum, numeric limits and neighboring clips bound duration;
there is no primary source-duration limit. Input video ends become transparent,
while still inputs remain available throughout the composition.

## Rendering and timing

The project canvas is explicit for independent graphs, including graphs with
no media. Inputs fit proportionally into transparent canvas frames. Text keeps
its pixel font size and is centered without enlargement to fill the canvas.
The existing graph evaluator is shared by Preview, node Viewer and export.
The application-owned graph generator preparer rasterizes Background/Text on
the calling playback/export worker and supplies borrowed frame owners for graph
evaluation. Explicit optional evaluation dimensions preserve legacy inference
when absent. CPU RGBA inputs for independent graphs can upload into the existing
GPU graph path; existing device recovery and cancellation remain in force.

Graph output passes through the clip effect stack and Timeline transform.
Clip opacity is normal layer alpha. An Adjustment Clip above it affects the
composited output. Generators retain straight alpha until final Timeline
composition, whose empty background remains black. Technical failures reach
the existing diagnostic boundary before feedback; canceled work is discarded.

`composition_input_offset_frames` starts at zero and is signed project-frame
time. Splits advance the right clip's offset; left trims add their boundary
delta. FPS conversion rescales the offset, and movement does not change it.
Input video samples local time plus that offset. Negative times and samples
past EOF are transparent. Clip/node curves remain clip-local and are sliced
without restarting the sampled animation. Duplication copies the offset and
graph by value, so later edits are independent.

## Creating nodes from Effects and Functions

The Node Editor toolbar contains Grid settings. Use the Effects dock to create
nodes: Video lists the four filters; Video > Generators lists Background; Text
lists Text; Fusion lists Input, Transform, Color and Merge and includes its
Masks child category. All and Favorites follow the active workspace.

Drag an entry onto the graph to choose its position, or double-click it in the
list or Favorites for automatic free placement near the visible center. New
nodes are disconnected and selected in the Inspector. Filter drops on image
cables keep their existing insertion behavior; mask cables reject filters.
With no compatible selected clip, creation is disabled. Output remains required
and is not an addable catalog entry. Each creation is one Undo/Redo operation.
Edit-only timeline tools are hidden in Fusion; Fusion-only nodes are hidden in
Edit, including their favorites, without deleting saved favorites. Background
from this Fusion catalog starts transparent; the Edit generator starts white.
Functions (Shift+Space) searches the same workspace catalog. In Fusion, Add,
Enter and double click create a disconnected node through the same graph edit;
dragging chooses its position. In Edit, Functions applies clip filters and
inserts generators/Text/transitions only by dragging. It closes on success and
stays open after a refused action or canceled/rejected drop.
See [the catalog contract](architecture/EFFECTS.md#workspace-catalog).

## Persistence and boundaries

CSP v27 adds `kind: "fusion_composition"`, its required node graph and the input
offset. Primary source fields remain absent/zero, and clip text/audio properties
are not serialized. Node types `background` and `text` own their generator data.
The reader accepts them in v27 and later. Versions 1–26 retain their previous graph
validation and rendering contracts; older readers reject the new root version.
Autosave/recovery rebuild the generated clip without looking up a primary file.
Hub v28 backups preserve generators and masks and relocate external Input paths through
the Video Editor reader/writer contract.

[Fusion Masks](FUSION_MASKS.md) adds Inspector-edited Rectangle/Ellipse nodes in CSP v28.
This version excludes nested compositions, graph audio, advanced masks, 3D and animated
Background/Text properties. Application version metadata remains unchanged.
See [regression testing](REGRESSION_TESTING.md) for automated coverage and the
separate native/platform acceptance status.
