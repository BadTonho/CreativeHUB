# Pan Behind in the Viewer

Status: implemented development interaction. Native `.motion` remains v12 and
application versions are unchanged. Manual visual/platform acceptance, native
GPU qualification and representative benchmarks remain pending.

## Using the pivot

Select a visible layer at a frame inside its interval and drag its existing
Viewer crosshair with the left mouse button. The hit radius is ten logical
pixels; movement begins at the platform drag threshold. Clicking near the
center retains the grab offset, so the pivot does not jump. Nulls and offline
media support the same interaction. Hidden, inactive and offline ancestors
still control the geometry. Export and audio-keyframe jobs prevent starting it.

Pressing pauses playback at the current timeline frame. The pivot and local
Anchor X/Y and Position X/Y values update live; these four fields are read-only
during the gesture. Layer and descendant pixels stay in place. The document,
keys, dirty state and history remain unchanged until release. Release uses the
final pointer position, including outside the composition or Viewer while the
mouse is captured. A marker outside the Viewer cannot be grabbed; use the
Inspector dialog instead. A marker in the visible surrounding gutter can be.

Release records one compensated Anchor/Position edit. Static properties update
their bases; animated properties insert/update keys at the fixed frame, even
between existing keys. Existing keys/easing, scales, scale-link state and parent
bindings are retained. Preservation applies to this frame; animation elsewhere
can change. A click or return to the starting values adds no history.

Escape cancels locally without a global shortcut. Undo during a gesture only
cancels the draft, preserving the preceding history and redo branch. Redo is
unavailable until the gesture ends. Frame/selection/document changes, Viewer
resize/hide, window deactivation and capture loss cancel. Other editing, save
and close actions cancel before continuing. Playback stays paused unless the
user explicitly starts it again. Autosave continues to serialize committed data.
Candidates beyond the existing Anchor ±1e9 px or Position ±1e6 normalized-field
limits cancel atomically. Unexpected numeric failures log operation, layer ID
and frame before a concise status message; ordinary cancellation is not an error.

## Geometry and ownership

The Viewer owns hit-testing, the logical-coordinate aspect-fit mapping, mouse
capture, threshold and lifecycle events. MainWindow owns a lightweight evaluated
gesture snapshot: layer/frame, local transform, scale Y, initial Anchor, canvas,
global matrix and its inverse linear part. No document/curve copy is made per move.
For pointer displacement `d` converted to composition pixels:

`Anchor_new = Anchor_initial + inverse(linear(W_initial)) * d`

Each candidate starts from the press snapshot. Full affine inversion retains
parent/binding shear and unequal scales; source media dimensions are irrelevant.
Motion's pure `compensatedAnchorValues` calculation is shared by the existing
document operation, dialog and live draft:

`delta_Position_px = R * S(X,Y) * (Anchor_new - Anchor_initial)`

Position is normalized separately by canvas width/height. The marker follows
`W_initial * Anchor_new`; complete current-frame geometry stays constant. Values
retain internal precision independently of six-decimal field presentation.
Finite/invertible geometry is checked before preview and the document's complete
staged hierarchy is checked before commit. History is recorded only on success.

Inspector previews block editing signals. Worker-delivered frames do not own
the marker or draft. Moves repaint the overlay and fields using the existing
RGBA frame or GPU texture; they issue no render request, decode, rasterization,
GPU readback or new image intermediate. CPU and GPU presentations share pointer
handling because the OpenGL child is mouse-transparent. No compositor contract
or shared animation type changes are needed.

## Persistence and regression coverage

Only confirmed Anchor/Position bases and keys use the existing v12 fields.
Recovery remains wrapper v1; Hub backup and `.cscomp` compatibility rules are
unchanged. There is no saved gesture state or new tool/shortcut setting. Viewer
zoom/pan, snapping, axis restriction, pick-whip and other transform handles remain
deferred.

`pan_behind_test.cpp`, registered in
[Motion tests](../../../apps/motion-editor/tests/CMakeLists.txt) as
`creative-suite-motion-editor-pan-behind` and `...-pan-behind-hidpi`, covers
independent geometry, pixel/cache parity, gesture mapping/lifecycle, live fields,
document isolation, history, Nulls, offline affine ancestors, animated insertion,
dialog parity, persistence and autosave/recovery at scale factors 1 and 2.
Existing Anchor, Scale, hierarchy, UI, history, persistence, recovery, preview,
export, translation, GPU-composition and texture suites protect adjacent behavior.
Automated GPU execution/fallback evidence is recorded separately from manual
native acceptance and does not constitute a benchmark.

## Windows automated evidence, 2026-10-18

The canonical Release build and explicit affected test targets completed. All
18 selected CTest entries passed in 15.35 seconds: Motion hierarchy, Anchor,
Scale, Pan Behind at scale factors 1/2, document, history, persistence, recovery,
preview, GPU composition, export, startup, translations, UI, audio keyframes,
portable GPU texture delivery, and shared translation catalogs. The changed
diff and documentation links were reviewed; no installers, commits or
publication were produced.

The directly launched executable is
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`,
modified **2026-10-18 13:37:46 UTC-03**, **2,093,568 bytes**.
Native schema v12, recovery v1 and application version metadata are unchanged.

The portable GPU-composition test reported `gpu_composition_frames=0` and
`gpu_composition_fallbacks=1`; that result establishes fallback correctness.
The additional Windows native-platform
`creative-suite-motion-editor-gpu-texture-tests --require-gpu` attempt failed
before its MainWindow/Pan Behind case: a shared GPU texture was delivered
(`direct=1`, `available=1`, `recovered=0`), but the Viewer did not record its
presentation within the test deadline (`expected=1`, `presented=0`). Explicit
window exposure and execution outside the filesystem restriction did not resolve
it. Failure diagnostics are retained in `build/ctest-logs/pan-behind-native-gpu-*.log`.
This is **not a passing native GPU gate**. Native Pan Behind presentation,
manual visual acceptance, other platforms and benchmarks remain pending; the
18 passing portable checks must not be reported as native GPU qualification.

## Pending manual checks

Record the executable date, OS, display scale and graphics backend when these
checks are performed later; no manual acceptance is claimed by this change.

1. Drag image/video, text, shape and Null pivots from their centers and edges,
   including fractional/external Anchors and non-square compositions. Verify no
   jump and unchanged layer/descendant pixels with rotated unequal-scale parents.
2. Repeat with offline/hidden/inactive ancestors, legacy/affine bindings,
   animated Anchor/Position between keys and masks at every stage, including View.
   Inspect the inserted keys, easing and one Undo/Redo.
3. Start during playback, leave the canvas/window and release. Try Escape, Undo,
   returning to origin, resize, window switching, selection/seek, save and close.
   Check no provisional values are saved/recovered and playback remains paused.
4. Repeat with CPU and native OpenGL texture presentation and multiple display
   scale factors on Windows, macOS and Linux. Record fallback separately; confirm
   no overlay flicker or unexpected source work. Benchmark small/medium/heavy
   hierarchies separately before making responsiveness or memory guarantees.
