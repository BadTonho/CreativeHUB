# Background Generator

Drag **Effects > Video > Generators > Background** onto a free video-track range.
Alternatively, search Background in Functions (Shift+Space) while in Edit and
drag it onto the Timeline; Add/Enter/double click are disabled for this preset.
In Fusion, Functions creates the transparent Background node through Add, Enter,
double click or drag. This Edit preset starts as opaque white for five seconds, rounded up to project
frames. It creates a source-free Fusion Composition named **Background**, with
the existing **Background → Output** graph. Video includes its Generators
subcategory; All also lists the generator. Right-click to Add to Favorites or
Remove from Favorites. Favorites persist in local application settings and
remain draggable using the same effect MIME identity, `video.background`.

The teal generated block has no thumbnail or waveform. Existing Fusion
Composition movement, duration, trimming, splitting, duplication, attributes,
visual transitions, effects, transforms and keyframes apply. Audio-track drops
and occupied ranges are refused normally. Insertion is one Undo/Redo operation.
Edit > Duplicate Fusion Composition copies its graph and name independently.

The Edit Inspector has a **Background** section whenever the selected video,
image or Fusion Composition graph contains Background nodes. One node exposes
its controls directly. Multiple nodes expose a **Background #ID** selector;
selection is temporary interface state and does not dirty the document.
Disconnected Background nodes are also editable. Removing the selected node
selects the first remaining Background; removing all hides the section.

**Choose RGBA Color** opens the Qt color selector with alpha enabled. The sample
shows RGBA values and color. Accepting updates the selected node through the
existing graph command and history; Cancel leaves the graph and dirty state
unchanged. The graph is re-read after the dialog to avoid restoring stale edits.
Edit and Fusion operate on the same node data. Graph changes and Undo/Redo
refresh the controls; no separate color property or rendering path is introduced.

Color and node alpha are static in this version. Node alpha combines normally
with the clip's keyframed opacity. Open Fusion to add nodes, masks or media and
continue editing. A Background added through Effects while in Fusion and a
normal Fusion Composition still start transparent; only this preset is white.
Existing Preview, node Viewer, GPU/CPU processing and export render the graph.

CSP v29 adds optional UTF-8 `display_name` on `kind: "fusion_composition"` clips.
It preserves the Background label through save, recovery, splitting and
duplication. Missing/empty names retain the historical Fusion Composition label.
Names are independent of graph content and are not inferred from its topology.
Versions 1–28 remain readable. Mask/generator data and composition input offsets
retain their existing representation. Hub accepts schemas 1–42, preserves names
and graph data, copies Color Gallery sidecars, relocates external Input paths,
and rejects future v43.

The Video Editor owns the preset, commands, Inspector and document metadata.
Effects categories optionally declare a parent so filtering Video includes
Generators. Favorites use bounded catalog IDs in settings, not project data;
unavailable IDs are ignored and persistence failures are logged before feedback.
No new engine, clip kind, dependency, application version or keyboard shortcut
is added. Gradients, animated colors and additional generators are out of scope.
See [regression testing](../validation/REGRESSION_TESTING.md) for coverage and pending native
platform acceptance.
