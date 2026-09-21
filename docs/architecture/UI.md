# UI Boundary

Status: provisional.

Qt 6 Widgets owns the Main Editor window, menus, actions, dialogs, dock
layout, focus, and user input. Qt-specific types stay inside the application
UI and rendering layers. Future shared interfaces use standard C++ types and
do not expose QObject, QString, Qt containers, or Qt signals.

The preview uses provisional Qt OpenGL with a CPU fallback. View > Grayscale
Preview is optional and off by default. GPU failures preserve the current
frame, switch to CPU rendering, show a short status message, and write a
detailed rendering log.

## Media Browser and projects

The Media Browser has a hierarchical bin tree, filtered media list, project
labels, and visible online/offline state. Context menus provide New Bin,
Rename, Move to Bin, Remove from Browser, and Restore Media. Removing media
never deletes the file or its Timeline clips.

File actions provide New Project, Open Project, Save Project, and Save Project
As. Save prompts are transactional and New, Open, and close use Save, Discard,
and Cancel when the project is dirty.

## Timeline interaction

The Timeline draws one vertical row per video track, with the top row having
the highest visual priority. Video 1 is created first; each newly created track
is inserted above the existing tracks. It preserves absolute positions and
gaps, and allows overlap only across different tracks. It displays a shared
`HH:MM:SS.mmm` timecode ruler, dedicated track headers, clip counters,
track-specific colors, and explicit drop/playhead markers. The dock
provides Add Video Track, Rename Track, Track Up, Track Down, and Remove Track.
Only empty tracks can be removed.
Playback state is shown in a compact fixed footer below the timeline content;
it does not expand with the dock. The timeline receives the expandable dock
space, and its track rows grow within that space while additional rows remain
available through vertical scrolling. Each track row has a provisional maximum
height of 180 pixels; extra space in the timeline remains empty until a later
layout milestone gives it another purpose.
The horizontal timeline scale has a one-hour minimum range independent of
the actual clip duration. The one-hour range fills the visible viewport so
short projects keep a stable scale and retain empty space after their last
clip. Projects longer than one hour expand the timeline surface and use the
horizontal scrollbar; the actual content duration still controls playback
limits.

Gesture priority is configurable: by default, normal drag moves clips and
Alt + drag seeks; when the Edit > Require Alt to Move Clips option is enabled,
Alt + drag moves clips and normal drag seeks. Blade Tool click splits and edge
drag trims. Movement, splitting, trimming, and seeking do not decode during
pointer movement. Drops report target track and frame; Add to Timeline
appends to the active track.

Delete removes the active clip. Ctrl + Left and Ctrl + Right nudge it by one
frame when valid. Ctrl + K splits at the playhead. Undo and Redo pause
playback, invalidate worker generations, and restore Timeline metadata,
selection, active track, and playhead without storing decoded frames.

The Timeline audio row exposes independent Clip and Track gain sliders and
mute checkboxes for the active clip. Gains are shown as 0% to 200%; dragging a
slider creates one coalesced Timeline history entry. Audio output follows the
worker playback clock when possible. Missing audio, disabled output, or an
unavailable device keeps the video fallback running and reports a short status
message while the detailed cause goes to the local log.

User-facing keyboard shortcuts are maintained in docs/SHORTCUTS.md and must be
updated in the same change as any shortcut change.

## Transform Inspector

The Inspector exposes Position X/Y, uniform Scale, Rotation, and Opacity for
the selected Timeline clip occurrence. Values use normalized coordinates for
the fixed 1920x1080 canvas. Each property has one diamond keyframe toggle. The
outlined diamond means that the current local frame has no keyframe; a filled,
highlighted diamond means that the playhead is on a keyframe. Clicking the
diamond adds a keyframe at the evaluated value or removes the keyframe at the
current frame. Editing a property while a keyframe exists at the playhead
updates that keyframe; otherwise it changes the static base value. Timeline
keyframe markers are visual and not draggable in this milestone.

The preview composes all visible tracks in worker-owned code, from the bottom
track to the top track, before handing one frame to the GPU/CPU preview. A gap
or an empty canvas is not an error and uses the dark preview background.

## Text clip editing

The Timeline dock also provides Add Text. It creates a five-second manual text
clip at the current playhead on the active track, using the selected media FPS
or a 30 FPS fallback. Text clips have a distinct visual style and may sit
above a video clip in the same track; same-kind overlap is rejected.

When a text occurrence is selected, the Inspector shows a multiline content
editor, font family, pixel size, RGBA color, horizontal alignment, and an
Apply action, followed by the shared transform and keyframe controls. A text
selection keeps the Media Browser selection, current video session, and
playback clock unchanged. Text-only projects can show a static composition,
but playback remains disabled without video media. Confirmed text/style edits
are Timeline Undo/Redo entries and are persisted by the current `.csp` version
5 format.

Text rasterization is performed with `QImage/QPainter` by the playback worker;
the UI only edits the values and receives the composed RGBA frame. No new
keyboard shortcut is introduced for text creation or editing.

## Transition editing

The Timeline marks valid clip junctions with a transition region. A junction
context menu provides Add Cross Dissolve, Add Fade to Black, and Remove
Transition. Selecting a junction switches the Inspector to transition controls
for the type and duration; applying a change creates one Timeline history
entry. The default duration is 15 frames and it is limited by the endpoint
clips.

Transitions do not create overlap or change clip placement. They are evaluated
by the playback worker while the UI continues to present the resulting
composed frame. Playback pauses while a transition is created, edited, or
removed, then the current composition is requested again. Undo/Redo restores
transition data, selection, and playhead while remaining paused. Invalid
junctions and gaps are intentional no-op outcomes and are not logged.

The `MainWindow` coordinator is implemented in responsibility-focused
translation units under `apps/main-editor/src/main_window/`. Workspace,
project, Media Browser, Timeline, playback, and Inspector construction and
coordination remain part of the same window class; this organization does not
introduce additional controllers or change ownership.
