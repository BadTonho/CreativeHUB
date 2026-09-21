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

The Media Browser belongs to the logical `Media Pool` group. `Bins` and
`Media` are independent dock widgets: `Bins` contains the hierarchical
tree, while `Media` contains the filtered view with compact list and
fixed-size block modes. The global `QSettings` preference
`media_browser/view_mode` restores the last mode, defaulting to list mode on
first use. Block mode uses each online item's cached first frame as a
thumbnail; changing modes does not decode frames or mark the project dirty.
Items show a compact name and summary instead of a large technical-details
panel. An information icon in each item's upper-right corner displays the
complete technical metadata on hover, including offline status, bin, and path
when applicable. Context menus provide New Bin, Move to Bin, Remove from
Browser, and Restore Media. Removing media never deletes the file or its
Timeline clips, and the existing selection and internal drag-and-drop MIME
flow remain unchanged. The Media Browser does not display separate New Bin or
Add to Timeline buttons; imported media can still be added through the existing
Timeline drag-and-drop path and internal add operation. An empty library does
not add a redundant status row below the media view. The workspace stores the
native dock arrangement globally in `workspace/dock_layout_state`; the default
arrangement places `Bins` above `Media` on the left. Both docks can be
moved, resized, floated, closed, re-docked, and tabified independently. The
`Media Pool` text button in the toolbar below the menu bar activates this pair
and hides the Effects pair. The adjacent `Effects` button activates the
Effects pair in the same left workspace area. The docks can also be controlled
individually through the
`View > Media Pool` submenu. The global action is checked only when both docks
are visible while the Effects pair is hidden, and it does not affect project
state.

The Effects workspace contains three additional native docks: `Toolbox`,
`Favorites`, and `Effects`. `Toolbox` and `Favorites` form a vertical column,
with `Favorites` below `Toolbox`, while `Effects` is placed to their right.
All three remain independently movable, resizable, floatable, closable, and
tabifiable. `Favorites` is intentionally empty in this prototype and has no
favoriting behavior yet. The Toolbox contains
the categories `All`, `Video`, `Audio`, `Transitions`, and `Text`. Selecting a
category filters the five currently implemented entries — `Grayscale`, `Gain`,
`Cross Dissolve`, `Fade to Black`, and `Text` — in the Effects dock. The Text
entry is the Timeline text-clip tool: it can be dragged to a track and frame
in the Timeline to create a five-second text clip. The other entries remain a
UI prototype only and cannot be applied from this dock to the Preview,
Timeline, or project. The toolbar `Effects` action activates all
three docks and hides the Media Pool pair, while `View > Effects` controls
`Toolbox`, `Favorites`, and `Effects` individually. The workspace layout is
stored globally in `workspace/dock_layout_state` with layout version 6 and
does not affect project state. The default layout shows Media Pool and keeps
the Effects docks hidden until activated. The native separators are draggable;
the minimum widths are 20 px for `Toolbox` and `Favorites`, and 30 px for
`Effects`. The selected dock sizes are part of the global layout state and are
restored with the workspace.

The media view includes immediate child bins as folder items alongside media.
They use the standard Qt folder icon, are excluded from the media-to-Timeline
drag MIME, and support inline renaming. `New Bin` creates an automatically
named child in the current bin and starts editing it without a dialog. Media
and editable bins can be renamed with double-click or `F2`; `All Media` and
`Unsorted` remain protected. The bin tree remains available for navigation and
filtering and draws visible connectors between nested levels. Selecting a bin
keeps the current tree path and expanded branches while refreshing the media
view. Organizing the Media Pool docks does not change project state.

The bin tree accepts custom drag-and-drop MIME types for imported media and bin
paths. Dropping media onto a bin changes its project bin assignment; dropping
a bin onto another bin reparents the complete subtree. `All Media`, empty tree
space, self/descendant destinations, collisions, and moving `Unsorted` are
rejected. Bin creation from the context menu uses the clicked or selected bin
as the parent, while the existing `Move to Bin` action remains available.

File actions provide New Project, Open Project, Save Project, and Save Project
As. Save prompts are transactional and New, Open, and close use Save, Discard,
and Cancel when the project is dirty.

## User settings

The Main Editor provides a `Settings` action in the main menu bar immediately
to the left of `Help`. It opens a modal settings dialog with `General`,
`Timeline`, and `Shortcuts` tabs. The `Shortcuts` tab exposes every current
Main Editor keyboard action, applies valid changes immediately, permits empty
assignments, rejects duplicate combinations, and provides individual and
global reset actions. The dialog opens at a larger 960x720 layout so the
shortcut list is easier to review. Shortcut values are global user preferences
stored by `QSettings` under `shortcuts/<id>`; mouse gestures are intentionally
excluded.
The existing timeline choices remain in the Edit menu. Opening or closing the
dialog, or changing a shortcut, does not change project data, project dirty
state, undo/redo history, or the `.csp` format. `SettingsDialog` and
`ShortcutManager` are independent Qt components under
`apps/main-editor/src/settings/`; `MainWindow` owns the manager, registers its
actions, and only creates and opens the dialog.

## Timeline interaction

The dock uses its native Timeline title as the only heading. It does not
render a duplicate internal title or the former click-to-select interaction
hint row; the controls and timeline content remain directly below the dock
title.
The Previous Frame, Play/Pause, and Next Frame controls use standard Qt media
icons without visible text; their tooltips and accessible names retain the
full action descriptions.
The normal Selection Tool uses a mouse icon, is selected by default, and
restores ordinary clip selection, movement, trimming, and seeking. The Blade
Tool uses a blade icon without visible text; the two tools are mutually
exclusive, while the existing `Edit > Blade Tool` action remains available.
The Timeline draws one vertical row per video track, with the top row having
the highest visual priority. Video 1 is created first; each newly created track
is inserted above the existing tracks. It preserves absolute positions and
gaps, and allows overlap only across different tracks. It displays a shared
`HH:MM:SS.mmm` timecode ruler, dedicated track headers, clip counters,
track-specific colors, and explicit drop/playhead markers. The dock
provides Add Video Track, Rename Track, Track Up, Track Down, and Remove Track.
Only empty tracks can be removed.
Playback state is shown in a compact fixed footer below the timeline content;
it does not expand with the dock. The current frame and transient Main Window
messages, including `Loading timeline clip...`, share one line in that footer;
the separate global status-bar row is hidden. The timeline receives the
expandable dock space, and its track rows grow within that space while additional rows remain
available through vertical scrolling. Each track row has a provisional maximum
height of 180 pixels; extra space in the timeline remains empty until a later
layout milestone gives it another purpose.
The same footer shows the Main Editor process working-set memory at the right
in the form `RAM: <megabytes> MB`, refreshed every second. Windows reads
the value through `GetProcessMemoryInfo`; platforms without an implementation,
or a failed query, display `RAM: N/A`. This indicator is display-only and
does not affect playback, Timeline data, project dirty state, Undo/Redo, or
`.csp` data.
Clicking the indicator opens a non-modal `Memory Usage` window with separate
System Memory and Main Editor sections. The system section shows total, used,
and available memory in GB with the exact MB value. The Main Editor section
shows Working Set and Private Usage. The window refreshes every second and is
organized so future CPU and GPU sections can be added without changing the
Timeline footer.
The horizontal timeline scale has a one-hour minimum range independent of
the actual clip duration. The one-hour range fills the visible viewport so
short projects keep a stable scale and retain empty space after their last
clip. Projects longer than one hour expand the timeline surface and use the
horizontal scrollbar; the actual content duration still controls playback
limits.
The timeline controls expose a thin zoom slider with the percentage indicator
centered above it, plus a minus button and a plus button without an extra text
label. Zoom levels range from 25% to 51,200%, including frame-level levels
after 800%. Ctrl + mouse wheel, the slider, and the buttons change one level
around the playhead. At frame-level density, the visible timeline draws a
subtle vertical guide for each frame without labeling every frame. The guides
are limited to the current paint region so long timelines remain responsive.
Zoom affects only horizontal timeline presentation and is persisted per project
without creating a clip-edit history entry.

Gesture priority is configurable: by default, normal drag moves clips and
Alt + drag seeks; when the Edit > Require Alt to Move Clips option is enabled,
Alt + drag moves clips and normal drag seeks. Blade Tool click splits and edge
drag trims. Movement, splitting, trimming, and seeking do not decode during
pointer movement. Media drops and the `Text` Effects drop report the target
track and frame; a Text drop creates the five-second clip at that position.

Selecting a timeline clip does not move the playhead by default. The Edit >
Move Playhead to Selected Clip Start preference restores the optional behavior
and is stored as a user preference.

The upper time ruler is also a playhead scrub area. Clicking or dragging it
updates the visible playhead without selecting a clip, then requests the seek
when the pointer is released. Scrubbing remains bounded by the real project
duration even when the visual timeline has empty space beyond the last clip.
The playhead keeps its pointer position while the playback worker processes the
request, and the window converts the absolute ruler frame to the target clip's
local playback frame. Ruler scrubbing does not require a Media Browser or
Timeline item selection: video clips resolve their imported source from the
timeline, while gaps and text clips only move the playhead and do not ask the
video worker to decode. The Play control uses the clip under the current
playhead as its source, so playback remains available after clearing the
current Timeline selection. When playback crosses from a text clip to a video
clip, the destination is published before the asynchronous worker commands
are queued, keeping the requested local frame inside the new segment.

Delete removes the active clip. Ctrl + Left and Ctrl + Right nudge it by one
frame when valid. Ctrl + K splits at the playhead. Undo and Redo pause
playback, invalidate worker generations, and restore Timeline metadata,
selection, active track, and playhead without storing decoded frames.

The Inspector provides `Inspector` and `Audio` tabs. The `Audio` tab exposes
independent Clip and Track gain sliders and mute checkboxes for the active
video clip and its track, grouped vertically. Gains are shown as 0% to 200%;
dragging a slider creates one coalesced Timeline history entry. The active tab
is restored from the global user settings and does not affect project dirty
state or `.csp` data. Audio output follows the worker playback clock when
possible. Missing audio, disabled output, or an unavailable device keeps the
video fallback running and reports a short status message while the detailed
cause goes to the local log.

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
keyframe markers are visual and not draggable in this milestone. Each property
also has a horizontal slider for quick adjustment and an editable numeric field
that remains the precise value display. Slider drags update the preview while
the pointer moves and are stored as one coalesced Undo/Redo edit.

The preview composes all visible tracks in worker-owned code, from the bottom
track to the top track, before handing one frame to the GPU/CPU preview. A gap
or an empty canvas is not an error and uses the dark preview background.

## Text clip editing

The Effects dock provides the `Text` tool. Dragging it to a Timeline track
creates a five-second manual text clip at the drop frame, using the selected
media FPS or a 30 FPS fallback. Text clips have a distinct visual style and
may sit above a video clip in the same track; same-kind overlap is rejected.

When a text occurrence is selected, the Inspector shows a multiline content
editor, font family, pixel size, RGBA color, horizontal alignment, and an
Apply action, followed by the shared transform and keyframe controls. A text
selection keeps the Media Browser selection, current video session, and
playback clock unchanged. Timeline playback is coordinated by the active
composition and does not require a Media Browser item to remain selected;
text-only compositions can also advance through their valid frame range.
Confirmed text/style edits are Timeline Undo/Redo entries and are persisted by
the current `.csp` version 6 format.

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
