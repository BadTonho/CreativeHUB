# Audio Workspace Roadmap

Status: **first editing and mixing stage implemented; further capabilities are candidates, not release commitments**.

The Audio workspace is a fourth view of the Video Editor's current project. It
uses the same timeline document, clip and track identities, edit history,
playhead, playback and export mix as Edit. Its first stage places live meters
beside the shared video Preview, shows only Audio tracks in the editable
Timeline, and provides a mixer for each Audio track's existing gain and mute.
The output meter displays the summed floating-point mix before monitor volume
and output clipping. Track meters display each track's post-gain, post-mute
mix. Meter work is enabled only while Audio is selected and never writes to the
project. Level delivery is limited to 20 updates per second. The layout is
original and takes workflow guidance, not assets or
branding, from the supplied reference.

## First-stage acceptance

- Edit audio clips through the existing move, trim, split, volume-envelope,
  Audio Crossfade, selection and Undo/Redo paths. Switching between Edit and
  Audio must retain clip identity and playhead position.
- Mixer gain and mute must affect the same track fields used by Preview and
  export, survive save/reopen, and participate in Undo/Redo. The first stage
  has a fixed summed output; it adds no buses or project schema fields.
- Empty and muted tracks show zero. Overlapping tracks contribute separately
  to their meters and together to the output meter. Pausing, stopping, seeking,
  changing project or leaving Audio clears stale readings.
- Keep Edit as startup. Restore the previous dock arrangement after leaving
  Audio; preserve the project dirty state during workspace navigation.

## Candidate expansion, no committed dates

1. **Mixing controls:** solo, pan, track routing, buses, submixes and sends.
   Design the routing contract and persisted format before implementation.
2. **Processing:** track and clip EQ, dynamics, effect chains, dialogue
   cleanup, noise reduction, repair and ducking. Keep preview and export equal.
3. **Measurement and automation:** loudness, true-peak analysis,
   normalization, track automation and richer clip automation.
4. **Production workflows:** recording, voiceover/ADR and a locally managed
   sound library. Define device, permission, latency and asset ownership rules.
5. **Delivery and formats:** audio-only export and, after a separate format and
   device review, surround workflows.

Each expansion requires a small validated workflow, format-compatibility
assessment if persistence changes, playback/export parity tests and relevant
device acceptance. Existing Video Editor stability remains a release gate.
