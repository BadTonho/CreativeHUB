# Audio Boundary

Status: provisional.

The Video Editor supports the embedded audio stream of an imported video. Audio
is decoded and resampled in the application-local media layer with FFmpeg and
`libswresample`. It is not an independent audio-only source or an independent
audio track.

## Decode and clock

`AudioPlaybackSession` owns the FFmpeg format context, decoder, packet, frame,
and resampler through RAII. It exposes standard C++ PCM chunks in signed
16-bit interleaved form. The playback worker is the only owner that opens a
session or decodes audio; the UI never performs audio work.

When embedded audio streams and an output device are available, the playback
worker mixes every active video clip's audio into one output stream, then uses
that stream as the clock for Timeline playback. This includes clips on tracks
whose video is covered by a higher-priority track. Source sessions are opened
for clips needed by the upcoming audio buffer; a failure in one source is
logged and does not silence the remaining sources. Silent gaps remain in the
mixed stream so the audio clock stays aligned with the Timeline. If no usable
audio stream or output device is available, playback uses the video timer.

`QAudioSink` is confined to the Qt playback adapter. Its device format selects
the resampler output rate and channel count. `CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1`
forces the deterministic video-clock fallback for diagnostics and tests. A
missing device or output failure preserves video playback, reports a concise
warning, and writes an actionable `audio` log entry.

## Gain and persistence

Each clip and video track owns a linear gain in the range `0.0` to `2.0` and a
mute flag. The effective gain is `clip_gain * track_gain`; either mute flag
silences that source. The worker applies these values per source and sums all
unmuted active clips before clipping the final PCM output. Monitoring volume
affects Preview only. Volume and mute changes are valid Timeline edits,
are coalesced while a slider is dragged, enter bounded Undo/Redo, and are
stored as optional fields in `.csp` version 2. Older projects use the defaults
`1.0` and `false`.

Audio-only media, independent audio tracks, advanced mixing, waveforms,
automation, recording, and audio crossfades remain future work. A Cross
Dissolve changes video timing but keeps audio as a hard cut at the original
cut: outgoing audio continues through the visual overlap, and incoming audio
starts at the cut using the source position corresponding to incoming Timeline
frame D. Fade to Black also leaves the audio cut at the junction.
