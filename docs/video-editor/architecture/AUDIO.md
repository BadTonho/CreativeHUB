# Audio Boundary

Status: provisional.

The Main Editor supports the embedded audio stream of an imported video. Audio
is decoded and resampled in the application-local media layer with FFmpeg and
`libswresample`. It is not an independent audio-only source or an independent
audio track.

## Decode and clock

`AudioPlaybackSession` owns the FFmpeg format context, decoder, packet, frame,
and resampler through RAII. It exposes standard C++ PCM chunks in signed
16-bit interleaved form. The playback worker is the only owner that opens a
session or decodes audio; the UI never performs audio work.

When an embedded audio stream and an output device are available, the audio
clock drives video frame progression. Segment transitions reopen audio at the
correct source offset. Gaps pause both streams and the final video frame stays
visible at the end of the Timeline. Videos without audio are an expected
fallback and continue on the existing video timer.

`QAudioSink` is confined to the Qt playback adapter. Its device format selects
the resampler output rate and channel count. `CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1`
forces the deterministic video-clock fallback for diagnostics and tests. A
missing device or output failure preserves video playback, reports a concise
warning, and writes an actionable `audio` log entry.

## Gain and persistence

Each clip and video track owns a linear gain in the range `0.0` to `2.0` and a
mute flag. The effective gain is `clip_gain * track_gain`; either mute flag
silences the output. Only the highest-priority visible video clip contributes
audio while tracks overlap. Volume and mute changes are valid Timeline edits,
are coalesced while a slider is dragged, enter bounded Undo/Redo, and are
stored as optional fields in `.csp` version 2. Older projects use the defaults
`1.0` and `false`.

Audio-only media, independent audio tracks, mixing, waveforms, automation,
recording, audio crossfades, and export remain future work. Timeline video and
text transitions therefore do not change the audio cut: the visible clip's
audio follows the normal endpoint transition at the junction.
