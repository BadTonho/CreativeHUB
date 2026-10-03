# Audio Boundary

Status: provisional.

The Video Editor supports audio embedded in video sources and independent
audio-only media on dedicated Timeline tracks. A video with an audio stream
gets a synchronized Audio clip linked to its video clip. The stream is
externalized to that companion in Preview and export, so it is mixed exactly
once and can be edited with the existing Audio track controls. FFmpeg and
`libswresample` provide probing, decoding, and conversion. Audio-only media has
no visual frame or source frame rate; it is never assigned a fictitious FPS.

## Decode and clock

`AudioPlaybackSession` owns the FFmpeg format context, decoder, packet, frame,
and resampler through RAII. It exposes standard C++ PCM chunks in signed
16-bit interleaved form. The playback worker is the only owner that opens a
session or decodes audio; the UI never performs audio work.

During composition playback, the worker mixes active Audio clips and embedded
video streams only for video clips whose audio has not been externalized. This
includes clips whose video is covered by a higher-priority visual track. Audio
companion source positions and durations use microseconds; their Timeline
start and duration remain project frames. Their video and audio clips are
linked by stable IDs and move, trim, split, and delete together until
unlinked. Unlinking leaves the audio externalized on its Audio track; removing
that clip does not reactivate the embedded stream. The worker opens sessions
only for sources needed by the upcoming audio buffer; a source failure is
logged without silencing the remaining sources. Silent gaps keep the audio
clock aligned with the Timeline. If no usable audio stream or output device
is available, playback uses the video timer.

`QAudioSink` is confined to the Qt playback adapter. Its device format selects
the resampler output rate and channel count. `CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1`
forces the deterministic video-clock fallback for diagnostics and tests. A
missing device or output failure preserves video playback, reports a concise
warning, and writes an actionable `audio` log entry.

## Tracks, editing, and export

Dropping audio-only media onto an Audio track places it on that track. Dropping
it onto a Video track creates a new `Audio N` track at the end of the track
list. Adding video with an audio stream creates a linked Audio companion on a
compatible Audio track, or appends a new `Audio N` track when needed. Video
without audio does not create an Audio track. Audio clips cannot be placed on
Video tracks, and visual clips cannot be placed on Audio tracks. Clips cannot
overlap within one Audio track; clips on different tracks can overlap and are
mixed together.

Clip and track gain use a linear range from `0.0` to `2.0`; either mute flag
silences its source. A linked audio companion owns the clip gain and mute
controls for the separated stream. The effective gain is
`clip_gain * track_gain`. Monitoring volume affects Preview only. Gain and mute
are Timeline edits, enter bounded Undo/Redo, and are persisted in `.csp` version
14. Opening versions 1 through 13 creates companions for online video sources
with audio; offline videos are marked pending until restored. Opening alone
keeps the project clean, and repeated open/save cycles do not duplicate
companions. Older projects use gain `1.0` and mute `false` when those optional
fields are absent.

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
