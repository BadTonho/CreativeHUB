# Audio Workspace Roadmap

Status: **the editable Audio workspace, one-level mixer, track EQ/compressor, per-clip EQ/compressor, dialogue cleanup, noise reduction, repair, and automatic/editable-curve ducking are implemented; device and native UI acceptance remain pending**.

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
- Edit exposes a clip's basic EQ and compressor chain in the Inspector Audio
  tab. The Audio workspace continues to own mixer and track processing; future
  audio processors belong there. Each clip accepts at most one EQ and one
  compressor, in either order, and both are bypassed by default.
- Clip effects process decoded source audio before clip gain, volume envelope,
  and crossfade. The resulting clip signals are summed, then the track effect
  chain runs before routing and sends. Preview and export share this order and
  preserve DSP state across contiguous blocks.
- CSP v43 stores the ordered per-clip chain. Projects v1-v42 load clips without
  effects; Hub backup and restore preserve v43 and reject v44. Linked video and
  audio clips mirror their chain, and split/duplicate plus Copy/Paste Attributes
  preserve it.
- Bus removal returns routed tracks to the Master and removes sends to that
  bus as one undoable edit. Fader and pan drags on tracks, buses, sends, and
  Master form one history edit per gesture.
- CSP v41 stores buses, Master settings, track pan and routes, and sends.
  Projects v1-v40 load with no buses or sends, centered pan, all tracks routed
  to Master, and a neutral Master.

## Dialogue processing and ducking (CSP v44)

- Audio owns ordered clip and track chains with up to one each of EQ,
  compressor, dialogue cleanup, noise reduction, and repair. New processors
  start disabled. Edit retains basic EQ/compressor editing and preserves
  Audio-only processors as read-only entries.
- Dialogue cleanup combines a high-pass filter and de-esser. Noise reduction
  treats continuous noise using a fixed noise floor. Repair offers optional
  50/60 Hz hum removal (fundamental plus two harmonics) and click removal.
- Preview/export share persistent, worker-owned FFmpeg filter sessions and
  bounded pull processing. Lookahead compensates filter delay before clip
  envelopes and track summation; visible clip/project durations are unchanged.
- Per-track ducking has Off, Automatic and Curve modes. One source track feeds
  an RMS detector before ducking/solo, after its own effects and gain. Source
  mute silences detection. Ducking precedes target fader/pan and both send taps.
- Generate Curve analyzes a project snapshot at 48 kHz with progress/cancel.
  Only a complete, still-current result becomes one undoable edit. A generated
  curve has absolute microsecond positions and linear dB interpolation, with
  incremental simplification bounded to 0.1 dB error. Switching modes preserves
  the saved curve and never applies both modes together.
- Curve mode exposes a ducking lane in the Audio Timeline. Click to add a point,
  drag to move it, or use its right-click removal action. Each gesture is one
  history edit. Moving/replacing clips leaves this track curve fixed; regenerating
  replaces it and can be undone.
- CSP v1-v43 migrate with ducking Off. CSP v44 persists expanded clip/track
  chains, ducking source/settings and curve. Hub backup/restoration preserves
  v44 and rejects v45. Application version metadata is unchanged.

## Audio workspace acceptance

- Automated regressions cover mixer model/commands, Undo/Redo, routing and
  sends, solo and mute rules, EQ/compressor response and state, meters, CSP v42
  and v43 migrations, clip processing order and state, linked clips,
  Copy/Paste Attributes, Preview/export mix parity, Inspector and Audio UI, and
  Hub backup/restore.
- The reproducible manual mixer workflow is documented in the
  [Video Editor regression guide](validation/REGRESSION_TESTING.md#audio-workspace).
  Device playback and native visual acceptance remain pending for Windows,
  macOS, and Linux environments.
- Keep Edit as startup. Restore the previous dock arrangement after leaving
  Audio and preserve project dirty state during workspace navigation.

## Remaining candidates

1. **Advanced processing:** voice isolation, dereverberation, and clipped-audio
   restoration after a separate quality, cost, dependency and licensing review.
2. **Measurement and automation:** loudness, true-peak analysis, normalization,
   track automation, and richer clip automation.
3. **Production workflows:** recording, voiceover/ADR, and a locally managed
   sound library. Define device, permission, latency, and asset ownership rules.
4. **Delivery and formats:** audio-only export and, after a separate format and
   device review, surround workflows.

Future work needs a scoped workflow, compatibility assessment for persisted
changes, playback/export parity coverage, and relevant device acceptance.
Existing Video Editor stability remains a release gate.
