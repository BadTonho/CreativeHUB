# Audio Boundary

Status: provisional.

The Video Editor supports embedded audio streams in video sources and
independent audio-only media on dedicated Timeline tracks. FFmpeg and
`libswresample` provide probing, decoding, and conversion. Audio-only media has
no visual frame or source frame rate; it is never assigned a fictitious FPS.

## Decode and clock

`AudioPlaybackSession` owns the FFmpeg format context, decoder, packet, frame,
and resampler through RAII. It exposes standard C++ PCM chunks in signed
16-bit interleaved form. The playback worker is the only owner that opens a
session or decodes audio; the UI never performs audio work.

During composition playback, the worker mixes every active independent audio
clip and every active video's embedded audio into one output stream. This
includes clips whose video is covered by a higher-priority visual track.
Audio-only clip source positions and durations use microseconds; their
Timeline start and duration remain project frames. Video source audio keeps
using the video source's frame-rate mapping. The worker opens sessions only
for sources needed by the upcoming audio buffer; a source failure is logged
without silencing the remaining sources. Silent gaps keep the audio clock
aligned with the Timeline. If no usable audio stream or output device is
available, playback uses the video timer.

`QAudioSink` is confined to the Qt playback adapter. Its device format selects
the resampler output rate and channel count. `CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1`
forces the deterministic video-clock fallback for diagnostics and tests. A
missing device or output failure preserves video playback, reports a concise
warning, and writes an actionable `audio` log entry.

## Tracks, editing, and export

Dropping audio-only media onto an Audio track places it on that track. Dropping
it onto a Video track creates a new `Audio N` track at the end of the track
list. Audio clips cannot be placed on Video tracks, and visual clips cannot be
placed on Audio tracks. Clips cannot overlap within one Audio track; clips on
different tracks can overlap and are mixed together.

Clip and track gain use a linear range from `0.0` to `2.0`; either mute flag
silences its source. The effective gain is `clip_gain * track_gain`. Monitoring
volume affects Preview only. Gain and mute are Timeline edits, enter bounded
Undo/Redo, and are persisted in `.csp` version 13. Older projects use gain
`1.0` and mute `false` when those optional fields are absent.

Video export remains a video file. Its duration reaches the end of the longest
Timeline clip. If audio extends beyond visual content, the renderer emits black
frames until the audio ends. Disabling export audio omits the output audio
stream. Preview and export share sample scheduling for embedded and
independent audio sources, including trims, gains, mutes, and silence.

Waveforms, automation, recording, audio crossfades, advanced mixing, and
audio-only file export remain future work. A Cross Dissolve changes video
timing but keeps audio as a hard cut at the original cut: outgoing audio
continues through the visual overlap, and incoming audio starts at the cut
using the source position corresponding to incoming Timeline frame D. Fade to
Black also leaves the audio cut at the junction.
