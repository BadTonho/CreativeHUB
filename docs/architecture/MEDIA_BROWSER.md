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

The Browser provides a bin tree, a media list, a `New Bin` button, and context
menus for renaming media or bins, moving media, removing media from the
Browser, and restoring an available offline item. Media Browser items can be
dragged to the Timeline through the internal
`application/x-creative-suite-media-path` MIME type. Only already imported
items participate in this drag-and-drop flow; operating-system file drops and
full manual relinking are future work.

Media organization changes mark the project dirty but do not create entries in
the Timeline Undo/Redo history. Selection, bin filtering, and tree expansion
are view state and do not mark the project dirty.
