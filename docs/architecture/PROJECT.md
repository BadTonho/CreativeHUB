# Project Document and Persistence

Status: **provisional**.

The Main Editor stores its first editable project format in a local `.csp`
file. The application-local implementation lives under
`apps/main-editor/src/project/`. The document model is Qt-independent:

```text
ProjectDocument
  media: canonical path, display name, bin path, online/offline state
  timeline_clips: source path, source start frame, segment duration
```

Timeline start frames are intentionally not serialized. They are recalculated
from clip order when a project is opened. Repeated source paths remain separate
Timeline occurrences.

## Version 1 JSON

The root object contains the format identifier, an integer version, imported
media, and Timeline segments:

```json
{
  "format": "creative-suite.main-editor",
  "version": 1,
  "media": [
    {
      "path": "media/video.mkv",
      "name": "Intro Video",
      "bin": "Footage/Scenes",
      "offline": false
    }
  ],
  "timeline": {
    "clips": [
      {
        "source": "media/video.mkv",
        "source_start_frame": 30,
        "duration_frames": 60
      }
    ]
  }
}
```

Paths are encoded as UTF-8 with `/` separators. A media path is stored
relative to the project directory when it is inside that directory; media
outside it is stored as an absolute path. On open, paths are resolved and
canonicalized before they are used.

The optional `name`, `bin`, and `offline` media fields are backward compatible
with version 1 files containing only `path`. `Unsorted` is the default bin.
Explicitly offline or missing media is preserved as offline so the Timeline
and project context are not lost. Existing media is probed and its first frame
is decoded; a corrupt or unsupported existing file aborts the complete open.

## Save and open rules

`QJsonDocument` and `QSaveFile` are confined to the application serialization
adapter. Saving writes a temporary file and commits it atomically, so an
existing project remains intact if writing or replacement fails. The Save
commands keep the current project path only after a successful commit.

Opening is transactional. The file format, version, required fields, frame
values, and segment durations are validated first. Every imported media source
is then probed with FFmpeg and its first frame is decoded. Timeline segments
are checked against the current media timing metadata. The current Main Editor
session is replaced only after all media and segments pass validation. A
corrupt, audio-only, unsupported, or incompatible existing media source aborts
the complete open and leaves the current project unchanged. Missing media is
loaded as offline and can be restored later.

Technical failures are logged under the `project` subsystem with the project
path, related media or clip index when available, cause, and error code. Dialog
cancellation and choosing Discard are intentional control-flow outcomes and
are not errors.

## Non-persisted state

The project file does not contain the selected Media Browser row, active clip,
playhead, dock layout, Undo/Redo history, decoded pixel buffers, FFmpeg
sessions, or Qt resources. A successful open starts paused on the first
Timeline clip, or the first imported media if the Timeline is empty. Imported
media without a Timeline clip is still persisted.

Autosave, recovery, media copying, manual relinking, shared projects, and
project-wide history remain future work.
