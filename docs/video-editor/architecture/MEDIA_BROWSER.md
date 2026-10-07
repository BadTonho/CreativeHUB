# Media Browser

Status: **provisional**.

The Video Editor's Media Pool UI and project integration use the shared,
application-independent catalog and media import services in `libs/media/`.
Each catalog entry contains a canonical source path, a project-owned display
name, a hierarchical bin path, a media kind, and an online/offline state. Image
Editor link references remain in the Video Editor project adapter, outside
the shared catalog. The default bin is
`Unsorted`; bin paths use `/`, and selecting a parent bin includes all of its
sub-bins.

Motion Studio uses the same catalog and import processor in its own standalone
Media Pool. It has a separate UI and stores pool contents with the current
composition. Importing an item does not place it automatically; dragging an
image or video from the Media Pool to the timeline creates a new composition
layer at the drop position.

Renaming changes only the label stored in the project. It never renames the
physical file. Removing an item from the Browser marks it offline, keeps it
visible, does not delete a file, and does not remove Timeline clips. If the
same canonical path is imported again, the existing offline entry is restored
instead of creating a duplicate.

Online video and still-image entries keep their metadata and cached first frame
in the application session. Offline entries retain their path, name, bin, and
Timeline context but
do not provide preview or playback until restored. Missing media encountered
while opening a project is loaded as offline; an existing but unreadable media
file remains a technical open failure and the current project is preserved.

The Browser belongs to the logical `Media Pool` group. `Bins` and `Media`
are independent dockable panels: `Bins` contains the hierarchical bin tree,
and `Media` contains a view that can switch between compact list mode and
fixed-size block mode. Bin creation remains available through the existing
context menus. The selected view
is a global user preference stored in `QSettings` at
`media_browser/view_mode`; the first-run default is list mode. The icon scale
is a global preference stored at `media_browser/icon_scale_percent`, with a
default of 100%, a range of 50% to 150%, and a step of 10%. It applies to both
list and block modes while preserving their respective proportions. Block mode
uses the cached first frame already held by the media item and does not decode a
new frame when the view or icon scale changes. Items use compact names and video
summaries; the visible labels in the Media panel are limited to the first seven
characters and use `...` when the original name is longer. The technical
summary is not shown below the label; the information icon still exposes the
complete metadata. Inline editing uses the full original name, and the large
technical-details panel is intentionally not part of the Browser layout.

`Bins` and `Media` are independent `QDockWidget` panels. They can be moved,
resized, floated, closed, re-docked, tabified, or split side by side through
the native Video Editor workspace. The default layout places `Bins` above
`Media` on the left. The Effects group uses the same left workspace area when
activated: `Toolbox` is placed above the empty `Favorites` dock, with `Effects`
beside that column, and the Media Pool pair is hidden. The separators between
these three Effects docks are draggable, with minimum widths of 20 px for
`Toolbox` and `Favorites`, and 30 px for `Effects`. The complete workspace
state is stored globally in `QSettings` at
`workspace/dock_layout_state`, so docking, visibility, floating, tabification,
and sizes are restored when the editor opens again. The `Media Pool` and
`Effects` toolbar actions switch between the two groups, while the
`View > Media Pool` and `View > Effects` submenus retain individual dock
visibility controls, including the three individual Effects docks. The layout
state uses version 7; older states fall back
to the default Media Pool layout.

The media view also includes the immediate child bins of the active location as
folder items alongside the media entries. Visible folder labels follow the same
seven-character compact form, while the bin tree keeps full names for
navigation. Folder items use the standard Qt folder icon, can be dragged to
show a visual folder preview, but do not produce the media MIME accepted by the
Timeline. They can be renamed inline. The bin tree remains available for
direct navigation and filtering; the existing parent-bin filter still includes
its descendants.
The tree draws visible branch connectors in the indentation area so nested bins
can be followed quickly without changing their navigation behavior.

Each item draws a small information icon in its upper-right corner. Hovering
that icon shows the complete technical summary: name, format, codec,
resolution, frame rate, duration, frame count, audio, and source path, or the
offline status, bin, and path for unavailable media. This information is view
state and changing list/block mode does not mark the project dirty.

Right-clicking the bin tree or the media area opens the existing context menu.
`New Bin` creates a child below the clicked or selected bin without opening a
dialog. It uses the first available name from `New Bin`, `New Bin 2`, and so on,
then starts inline editing in the media view. Bins and media are renamed with
double-click or `F2`; `All Media` and `Unsorted` are not editable. Bin
renaming preserves descendants and empty bins. The tree remains the navigation
surface, while the media view keeps the current folder items alongside media.

Media items can be dragged one at a time from the Browser list onto a real bin
to move them. Bins can be dragged onto another real bin to reparent the whole
bin subtree, including empty bins and the media assigned to it. `All Media` and
blank tree space are not drop targets. `Unsorted` can receive media but cannot
be renamed or moved. Invalid self, descendant, and collision drops are ignored;
the tree remains alphabetically displayed and does not support manual sibling
reordering or bin deletion in this milestone.

Context menus support creating bins, moving media, removing media from the
Browser, and restoring an available offline item. Media Browser items can be
dragged to the Timeline through the internal
`application/x-creative-suite-media-path` MIME type. Local files can also be
dropped from the operating system onto the Media list or a bin. The list uses
the selected bin; a bin drop assigns that destination directly. Multi-file
drops keep the source order, ignore folders and non-local URLs, and use the
existing asynchronous importer. Per-file import failures do not discard other
successful files. Duplicate paths retain their catalog identity; a duplicate
dropped onto a bin is assigned to that bin. Full manual relinking remains
future work.

Still images imported through Open Media use the cached RGBA frame as a visual
thumbnail and can be dropped into the Timeline as five-second static clips.
They have no audio and the same frame is reused during playback. The Open Media
image filter follows the deployed `QImageReader` formats and adds WebP/TIFF
when their FFmpeg fallback decoders are available. This includes a single-frame
GIF when the runtime can read GIF; files with multiple frames are rejected by
content regardless of their extension.

After a left-button press moves past the platform drag threshold, the Browser
starts the native drag explicitly and supplies a native Qt drag preview
using the cached thumbnail (or the standard folder icon) and the compact visible
name. The preview is limited to a 128x72 image area, follows the cursor, and
does not decode media or change project state. Folder previews are visual only
for Timeline drops because folders intentionally carry no media path MIME.
The Timeline's scrollable viewport forwards valid media and effect drag events
to the Timeline content and converts the pointer position before calculating
the destination frame. The track header, ruler, and empty viewport space do not
accept drops.

Operating-system file drops onto the Timeline capture the target track and
absolute frame before asynchronous import starts. Accepted files are placed in
the original drop order, with each next clip starting at the end of the
previous clip. Audio-only media uses a compatible Audio track or creates one;
videos with audio receive their linked Audio companions. The complete Timeline
placement is preflighted and committed as one Undo/Redo edit. If any imported
item is incompatible, overlaps existing content, or the target track no longer
exists, none of the batch is placed; successful imports remain in the Media
Browser. If the project changes during import, the stale drop is discarded.

Media organization changes mark the project dirty but do not create entries in
the Timeline Undo/Redo history. Selection, bin filtering, and tree expansion
are view state and do not mark the project dirty. Selecting a bin preserves
the selected path and the expansion state of the existing tree while the media
view is refreshed.
