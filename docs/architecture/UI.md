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
frame-and-seconds ruler, dedicated track headers, clip counters, track-specific
colors, visible gap regions, and explicit drop/playhead markers. The dock
provides Add Video Track, Rename Track, Track Up, Track Down, and Remove Track.
Only empty tracks can be removed.
Playback state is shown in a compact fixed footer below the timeline content;
it does not expand with the dock. The timeline receives the expandable dock
space, and its track rows grow within that space while additional rows remain
available through vertical scrolling. Each track row has a provisional maximum
height of 180 pixels; extra space in the timeline remains empty until a later
layout milestone gives it another purpose.

Gesture priority is Alt + drag for moving, Blade Tool click for splitting,
edge drag for trimming, interior drag for seeking, and simple click for
selection. Movement, splitting, trimming, and seeking do not decode during
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
