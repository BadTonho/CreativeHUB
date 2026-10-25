# Audio Workspace Roadmap

Status: **the editable Audio workspace, one-level mixer, and track EQ/compressor are implemented; device and native UI acceptance remain pending**.

The Audio workspace is a fourth view of the Video Editor's current project. It
uses the Edit timeline document, stable clip and track identities, shared edit
history, playhead, Preview and export. The page shows an audio-only editable
Timeline beside the shared video Preview and provides live track, bus and
Master meters.

## Implemented mixer

- Create, rename, and remove one-level buses. Buses feed the Master; they
  cannot contain other buses.
- Route each track to the Master or one bus. Add sends from a track to other
  buses, with a level and pre-fader or post-fader tap. Sends start at unity,
  post-fader. Track mute silences the main route and every send.
- Adjust track and bus gain from `0.0` to `2.0`, pan from `-1.0` to `1.0`,
  mute, and temporary multi-solo. The Master has gain and mute. Defaults are
  unity gain, centered pan, and mute off.
- Mono track sources use equal-power pan. Stereo sources retain their width
  with balance. Multichannel Preview pan affects the first front pair and
  leaves remaining channels unchanged.
- Solo changes Preview audition only. Mutes still apply. Export ignores solo
  and uses the full saved mix.
- Preview and export use the shared sample mixer for clip gains and envelopes,
  crossfades, track routes, sends, bus processing, and Master processing.
- Add one equalizer and one compressor per track, in either saved order. The
  track editor exposes enable/bypass, parameter editing, insertion, removal,
  and reordering. Effects process the summed track signal after clip envelopes
  and crossfades and before main routing and pre/post-fader sends.
- CSP v42 stores effect chains. Projects v1-v41 load with empty chains; Hub
  backup and restore preserve v42 and reject v43.
- Bus removal returns routed tracks to the Master and removes sends to that
  bus as one undoable edit. Fader and pan drags on tracks, buses, sends, and
  Master form one history edit per gesture.
- CSP v41 stores buses, Master settings, track pan and routes, and sends.
  Projects v1-v40 load with no buses or sends, centered pan, all tracks routed
  to Master, and a neutral Master.

## Audio workspace acceptance

- Automated regressions cover mixer model/commands, Undo/Redo, routing and
  sends, solo and mute rules, EQ/compressor response and state, meters, CSP v42
  migration, Preview/export mix parity, UI controls, and Hub backup/restore.
- The reproducible manual mixer workflow is documented in the
  [Video Editor regression guide](validation/REGRESSION_TESTING.md#audio-workspace).
  Device playback and native visual acceptance remain pending for Windows,
  macOS, and Linux environments.
- Keep Edit as startup. Restore the previous dock arrangement after leaving
  Audio and preserve project dirty state during workspace navigation.

## Remaining candidates

1. **Processing:** clip-level EQ/dynamics, dialogue cleanup, noise reduction,
   repair, and ducking. Keep Preview and export equal.
2. **Measurement and automation:** loudness, true-peak analysis, normalization,
   track automation, and richer clip automation.
3. **Production workflows:** recording, voiceover/ADR, and a locally managed
   sound library. Define device, permission, latency, and asset ownership rules.
4. **Delivery and formats:** audio-only export and, after a separate format and
   device review, surround workflows.

Future work needs a scoped workflow, compatibility assessment for persisted
changes, playback/export parity coverage, and relevant device acceptance.
Existing Video Editor stability remains a release gate.
