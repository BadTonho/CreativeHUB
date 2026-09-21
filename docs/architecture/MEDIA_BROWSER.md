# Media Browser

Status: **provisional**.

The Main Editor keeps an application-local, Qt-independent media library. Each
entry contains a canonical source path, a project-owned display name, a
hierarchical bin path, and an online/offline state. The default bin is
`Unsorted`; bin paths use `/`, and selecting a parent bin includes all of its
sub-bins.

Renaming changes only the label stored in the project. It never renames the
physical file. Removing an item from the Browser marks it offline, keeps it
visible, does not delete a file, and does not remove Timeline clips. If the
same canonical path is imported again, the existing offline entry is restored
instead of creating a duplicate.

Online entries keep their metadata and cached first frame in the application
session. Offline entries retain their path, name, bin, and Timeline context but
do not provide preview or playback until restored. Missing media encountered
while opening a project is loaded as offline; an existing but unreadable media
file remains a technical open failure and the current project is preserved.

The Browser provides a bin tree, a `New Bin` button, and a media view that can
switch between compact list mode and fixed-size block mode. The selected view
is a global user preference stored in `QSettings` at
`media_browser/view_mode`; the first-run default is list mode. Block mode uses
the cached first frame already held by the media item and does not decode a new
frame when the view changes. Items use compact names and video summaries; the
large technical-details panel is intentionally not part of the Browser
layout.

The media view also includes the immediate child bins of the active location as
folder items alongside the media entries. Folder items use the standard Qt
folder icon, do not participate in the media-to-Timeline drag operation, and
open that bin on double-click. The bin tree remains available for direct
navigation and filtering; the existing parent-bin filter still includes its
descendants.

Each item draws a small information icon in its upper-right corner. Hovering
that icon shows the complete technical summary: name, format, codec,
resolution, frame rate, duration, frame count, audio, and source path, or the
offline status, bin, and path for unavailable media. This information is view
state and changing list/block mode does not mark the project dirty.

Right-clicking the bin tree or the media area opens the existing context menu.
`New Bin` creates a bin below the clicked or selected bin; in the media area it
uses the currently selected bin as the parent. Slash-separated relative paths
can create multiple child levels in one operation.

Media items can be dragged one at a time from the Browser list onto a real bin
to move them. Bins can be dragged onto another real bin to reparent the whole
bin subtree, including empty bins and the media assigned to it. `All Media` and
blank tree space are not drop targets. `Unsorted` can receive media but cannot
be renamed or moved. Invalid self, descendant, and collision drops are ignored;
the tree remains alphabetically displayed and does not support manual sibling
reordering or bin deletion in this milestone.

Context menus support renaming media or bins, moving media, removing media
from the Browser, and restoring an available offline item. Media Browser items
can be dragged to the Timeline through the internal
`application/x-creative-suite-media-path` MIME type. Only already imported
items participate in this drag-and-drop flow; operating-system file drops and
full manual relinking are future work.

Media organization changes mark the project dirty but do not create entries in
the Timeline Undo/Redo history. Selection, bin filtering, and tree expansion
are view state and do not mark the project dirty.
