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

Decode block limits count output sample frames per channel. Codec frames are
fully converted and any unreturned PCM remains owned by the session until the
next call, with contiguous output sample indices. The decoder drains available
frames before reading another packet and drains the resampler before reporting
EOF. Zero-size/cancelled output requests do not consume buffered PCM; seek and
reset discard buffered PCM. Regression coverage compares complete output across
block sizes and mono/stereo resampling rates, including final delayed samples.

Seeking uses decoded timestamps and codec preroll, then discards real samples
up to the requested output index. The resampler retains the phase of the
source/output sample-rate grid. Positions near the beginning reopen the source
to preserve codec priming and demuxer skip metadata. A request for the current
output position continues the existing decoder and pending PCM. Missing seek
timestamps produce a logged error rather than relabeling unrelated samples.
WAV and AAC (48 kHz and 44.1 kHz to 48 kHz stereo) regressions compare the actual
PCM after forward/backward seeks with sequential decoding, including the first
sample, small blocks, and continuous reads after seeking.

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
Video tracks, and visual clips cannot be placed on Audio tracks. Audio clips
can overlap within one track only as the two endpoints of an Audio Crossfade;
other overlaps remain rejected. Clips on different tracks can overlap and are
mixed together.

Clip and track gain use a linear range from `0.0` to `2.0`; either mute flag
silences its source. A linked audio companion owns the clip gain, mute, and
volume envelope for the separated stream. The effective sample gain is
`clip_gain * track_gain * envelope_gain`. The envelope is a clip-local frame
curve with linear interpolation and values from `0.0` to `2.0`; an empty curve
means `1.0`. The Volume tool starts inactive. Its first point creates 100%
points at both clip edges, then adds the clicked value. Dragging points records
one Undo/Redo edit per gesture. Splits and trims preserve the evaluated curve
at new boundaries. The shared sample mixer applies the same curve in Preview
and export. Monitoring volume affects Preview only. Audio edits are persisted
in `.csp` version 15; versions 1 through 14 load with a constant 100% envelope.
Audio Crossfades are persisted in version 16; versions 1 through 15 load
without inferred transitions.
Opening versions 1 through 13 creates companions for online video sources
with audio; offline videos are marked pending until restored. Opening alone
keeps the project clean, and repeated open/save cycles do not duplicate
companions. Older projects use gain `1.0` and mute `false` when those optional
fields are absent.

## Mixer routing and track effects (CSP v42)

The project owns stable-ID audio buses and one Master mix. A track routes its
main output either to Master or one bus. Buses are flat and always feed Master;
there are no nested buses or cycles. A track may send to buses other than its
main output bus. Each send has a `0.0` to `2.0` level and a pre-fader or
post-fader tap; it starts at unity and post-fader. Track mute silences both its
main route and all sends. Pre-fader sends retain clip gain, envelope, and
crossfade behavior, while bypassing track gain and pan.

Track and bus pan range from `-1.0` to `1.0`; zero is centered. Mono sources use
an equal-power pan law. Stereo sources keep their stereo width and use balance.
On multichannel Preview output the track pan affects the first front pair, and
remaining channels pass through unchanged. Buses apply gain, mute, and balance
to their summed input. Master applies gain and mute after all bus and direct
track routes are summed. Track, bus, send, and Master gain values range from
`0.0` to `2.0`; defaults are unity, unmuted, and centered where pan exists.

Multiple tracks and buses can be soloed for Preview. Solo state is transient,
does not enter project files or history, and is applied after mute. Preview and
offline export share `TimelineAudioMixAccumulator` for route, send, pan, gain,
bus, and Master sample processing. Export disables solo and renders the
complete saved mix. Track and bus meters are measured after their local
processing; the Master meter is measured after Master gain/mute and before
monitor gain and output clipping.

Removing a bus sends its main-routed tracks to Master and removes sends that
target it. The operation is one undoable timeline edit. Fader and pan drags are
grouped into one edit per gesture.

Each track can store an ordered chain with at most one three-band equalizer
and one compressor. Effects are disabled by default. The EQ has low and high
shelves plus a peaking mid band; shelf gains and mid gain range from -18 to
18 dB, shelf frequencies from 20 to 500 Hz and 500 Hz to 20 kHz, and the mid
frequency/Q range from 20 Hz to 20 kHz and 0.1 to 10. Defaults are 120 Hz,
1 kHz/Q 1, and 8 kHz, all flat. The compressor ranges are -60 to 0 dB
threshold, 1:1 to 20:1 ratio, 0.1 to 200 ms attack, 10 to 1000 ms release,
and 0 to 24 dB makeup; defaults are -18 dB, 4:1, 10 ms, 100 ms, and 0 dB.

The shared mixer sums each track's clips after clip gain, envelopes, and
crossfades, then applies the enabled effect chain before its main route and
sends. Pre-fader sends tap that processed signal before track gain and pan;
post-fader sends tap it after those controls. Track mute silences both. DSP
filter and compressor state carries across contiguous Preview and export blocks
and resets after a seek, output restart, or discontinuity. Flat or disabled
chains preserve the existing mixer output. Chain edits during playback blend
the previous and new processing over 10 ms. The track Effects editor changes
the ordered chain through one undoable command.

CSP v42 requires the bus and Master settings, track pan/routes/sends, and an
`audio_effects` array on each track. Projects v1-v41 open without effects;
projects v1-v40 also receive no buses or sends, centered pan, neutral Master,
and tracks routed to Master. The Hub accepts v1-v42 and rejects v43.

## Clip EQ and compression (CSP v43)

Each Video or Audio clip may store an ordered chain with up to one three-band
EQ and one compressor. Both start disabled; the EQ is flat. Edit's Inspector
Audio tab exposes this basic chain for the selected clip. The Audio workspace
continues to edit the mixer and per-track chain; additional audio processors
are reserved for that workspace.

The shared mixer processes each decoded source through its clip chain before
clip gain, volume envelope, and crossfade. It then sums clips by track, applies
the track chain, and continues through gain, pan, routes, sends, buses, and
Master. Preview and export use the same sample path. Per-clip DSP state is
retained across contiguous blocks and reset after seeks or source
discontinuities; parameter changes blend over 10 ms. The active source is
processed once when video audio is externalized to a linked Audio companion.
Linked video/audio edits mirror the chain. Split, duplicate, and Copy/Paste
Attributes preserve the ordered chain, with attribute paste participating in
Timeline Undo/Redo.

CSP v43 stores `audio_effects` on every Video and Audio clip. Projects v1-v42
open with empty clip chains; parameter values, effect kinds, uniqueness, and
clip kind are validated. The Hub accepts CSP v1-v44 and rejects v45.

Video export remains a video file. Its duration reaches the end of the longest
Timeline clip. If audio extends beyond visual content, the renderer emits black
frames until the audio ends. Disabling export audio omits the output audio
stream and skips audio-only source preparation, including offline audio files.
The complete Timeline still determines video duration and its black tail. With
audio enabled, an offline audio source fails with a detailed log and preserves
the previous destination. Preview and export share sample scheduling and
mixing for embedded and independent audio sources, including trims, clip
envelopes, crossfades, track effects, track routes and sends, bus processing,
Master gain/mute, and silence. Solo is Preview only. Monitoring volume affects
Preview only.

Audio clip waveforms are derived after insertion and displayed from separate
left/right channel peaks. The global Mono/Stereo preference lives in
`Settings > Timeline`, defaults to Mono, applies immediately, and is saved in
`QSettings`; it does not change project data or the 64 MiB waveform cache.
Mono mode combines each bucket using the larger channel peak. Stereo mode draws
left above right for sources with multiple channels. A mono source remains a
single centered waveform in either display mode. Per-clip volume automation is
implemented. Independent Audio clips on the same Audio track can use an Audio
Crossfade at an adjacent cut. It overlaps the incoming clip by D Timeline
frames and uses equal-power cosine/sine gains per sample in the shared Preview
and export mixer. The transition gain multiplies track gain, clip gain, mute,
and the clip's existing volume envelope. Clips linked to video must be
unlinked before adding the transition. Recording, advanced repair/noise
processing, track automation, and audio-only file export remain future work.

A visual Cross Dissolve changes video timing but keeps embedded video audio as
a hard cut at the original cut: outgoing audio continues through the visual
overlap, and incoming audio starts at the cut using the source position
corresponding to incoming Timeline frame D. Fade to Black also leaves embedded
video audio at the junction cut.

## Dialogue processors and ducking (CSP v44)

`AudioEffectParameters` additionally contains dialogue cleanup (20-300 Hz high
pass, initially 80 Hz; de-esser intensity 0-1, initially 0.5), continuous-noise
reduction (0-30 dB, initially 12 dB; noise floor -80 to -20 dB, initially
-50 dB), and repair (hum Off/50/60 Hz; three Q=30 notches; optional declick,
initially on, detection threshold 1-10, initially 2). A chain accepts one of
each kind, in saved order. All instances start disabled. Basic Edit controls
preserve advanced entries; Audio owns their authoring.

The Video Editor's `AudioFilterSession` owns a move-only libavfilter graph per
active processor. It consumes normalized interleaved float PCM at one output
rate/layout, exposes available output, latency and drain, and checks FFmpeg
errors. Its caller supplies bounded lookahead and silence past the visible
boundary. FFT denoising's intrinsic delay is removed before mixing; overlap-save
click repair preserves first-sample alignment. Persistent ordered pull chains
feed clip processing before gain/envelope/crossfade and track processing after
summation. Parameter changes join the new output over 10 ms. The legacy path
is retained when no enabled advanced processor is present. Seeks/restarts clear
state; clip processor and decoder lifetimes are bounded to their occurrence.

Ducking settings belong to each track, outside its reorderable effects chain.
Modes are Off (default), Automatic, and Curve. One other track supplies a
20 ms RMS detector with 3 dB hysteresis, followed by attack/hold/release.
Defaults are -30 dB threshold, 12 dB reduction, 50 ms attack, 200 ms hold and
500 ms release. Ranges are -60 to 0 dB threshold, 0-36 dB reduction, 1-1000 ms
attack, 0-2000 ms hold and 10-5000 ms release. One scalar reduction applies to
all target channels, before its fader/pan and pre/post sends. All source taps
are captured after track effects and gain but before any ducking or Preview
solo filtering, so ordering and reciprocal source selections cannot create
feedback. A muted or currently silent source releases the detector normally.

Curve points store absolute `time_us` and positive `reduction_db` (0-36 dB),
with strictly increasing times and linear dB interpolation. An empty curve or
positions outside its range apply no reduction. Analysis uses the same detector
at 48 kHz, incrementally simplifies to at most 0.1 dB error, and retains only bounded
PCM blocks plus resulting control points. Cancellation/failure never publishes
a partial curve; project generation and snapshot equality reject stale results.
Curve mode preserves manual points across clip edits and mode switches.
Deleting the source clears its reference and turns Automatic Off, preserving
the saved curve; Undo restores the entire operation.

Filter failures log FFmpeg cause/code plus the clip/track identity. Preview
bypasses advanced processing for the failed chain and emits a warning; export
propagates failure through its existing atomic-destination handling. Expected
analysis cancellation and stale results are control flow, not error entries.
Voice isolation, dereverberation and declipping remain outside this stage.
