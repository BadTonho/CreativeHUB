# Voice isolation

Status: implemented locally; automated execution evidence is recorded in the
regression guide. Native device/UI and macOS/Linux runtime acceptance remain
separate and pending.

## Workflow and ownership

Audio provides reversible clip and track voice isolation. Configure enable and
0-100% intensity, confirm once, or cancel without editing. Instances default to
Off at 100%. The original media remains authoritative. Generate Again forces a
new result. Cancel Processing cancels the current isolation queue; it retains
settings and suppresses retry until an input edit or Generate Again. Status and
progress are transient. Edit identifies enabled isolation as Audio-owned.

This is speech enhancement: it attenuates background sounds around speech. It
does not select a speaker, separate singing, recover clipping, or implement
dedicated dereverberation.

`VoiceIsolationSettings` belongs to each clip/track. The Timeline command service
owns edits, linked-clip mirroring, history, dirty state and attribute paste.
Cache completion changes none of those. The UI-thread coordinator snapshots
inputs, debounces edits for 300 ms, and owns a one-worker queue. Superseded or
cancelled work cannot publish into playback; callbacks are generation checked.
Source file notifications invalidate the affected snapshot. Technical failures
log processor operation, model/cache key and clip/track context; expected
cancellation does not log an error or publish partial PCM.

## Signal contract

Order: clip isolation, clip effects, gain/envelope/crossfade, track sum, track
isolation, track effects, ducking, fader/pan, routes/sends, buses and Master.
The track preparation pass reuses the shared mixer with neutral track effects,
gain/pan and routing. Required clip artifacts finish first. Forcing a clip also regenerates its
isolated receiving track. Preview preparation, export and curve analysis share
a single cancellable inference lane. Original and fully
processed audio are mixed linearly; changing the stage's own intensity reuses
its wet artifact but can invalidate the receiving track's input.

The fixed `rnnoise-std-v1` model runs through libavfilter `arnndn`, on CPU at
48 kHz with independent channel states. There is no inference in the playback
callback, runtime model download, Python, Rust, GPU requirement or new neural
runtime. Context of up to 250 ms precedes source clips. Generation compensates
the 480-sample algorithm delay, pads/drains the final block and preserves the
visible interval. Cache readers retain resampling state and reset it on seeks.
Preview uses the current original signal while a valid artifact is unavailable;
ready publication joins the new output over 10 ms. Invalid cache reads bypass
isolation in Preview and report a warning. Export prepares all enabled stages
before encoding, propagates failure, and preserves the previous destination.
Ducking analysis uses the same prepared signal.

## Derived cache

The application lazily extracts its embedded model atomically and verifies the
SHA-256 documented with the [model asset](../../../apps/video-editor/resources/audio-models/rnnoise-std-v1/README.md).
It retains upstream BSD-3-Clause notices. `arnndn` is provided by the existing
LGPL FFmpeg build. Model/notice files accompany build and installation outputs.

Artifacts live under `QStandardPaths::CacheLocation/voice-isolation-v1`.
`CREATIVE_SUITE_VOICE_ISOLATION_CACHE_ROOT` overrides that location for isolated
regressions. It is not a project field. A `.vpcm` file begins with a 512-byte
space-padded JSON header (`voice-pcm-v1`, key, first/count as decimal strings,
channel count, SHA-256), followed by interleaved little-endian float32 at 48 kHz.
Clip coordinates are source samples; track coordinates are absolute Timeline
samples. Complete files are committed atomically and validated before leasing.
Consumers retain immutable leases; generation and reads use bounded blocks.

Keys include the fixed model/processing version, FFmpeg component versions,
format, canonical source paths, sizes and modification timestamps, and the
audio recipe before the relevant stage. Clip movement reuses source isolation.
Track keys include clip placement, gain/mute/envelopes/effects/transitions and
upstream isolation, excluding subsequent track effects, fader/pan, ducking and
routes. LRU pruning limits unused artifacts to 2 GiB; active leases are protected
and may exceed that unused-file budget. Disk failure is reported and does not
replace a completed artifact with a partial one. Cache eviction is reversible
through regeneration and never changes project content.

## Persistence

CSP v45 stores `voice_isolation` with required `enabled`, finite `amount` in
0-1 and fixed `model_id: "rnnoise-std-v1"` on tracks and Video/Audio clips.
Unknown models/types and invalid amounts are rejected. Older projects receive
default Off settings. Mapping, snapshots, Save As, autosave/recovery, linked
clips, split/duplicate and Audio Effects attribute paste retain configurations.
Caches and progress are excluded from documents and Hub backups. A restored
project regenerates from its original media and the bundled model. Hub accepts
v1-v45 and refuses future v46; application versions remain unchanged.
