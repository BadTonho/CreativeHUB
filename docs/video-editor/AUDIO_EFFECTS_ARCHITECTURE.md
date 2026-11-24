# Audio Effects & Analyzers Architecture

[Português (pt-BR)](AUDIO_EFFECTS_ARCHITECTURE.pt-BR.md) · English is authoritative if the versions differ.

## 1. Modular Layout

All Audio FX UI panels, effect parameter editors, analyzers, and studio tools live under `apps/video-editor/src/workspaces/audio/effects/` as self-contained headers (`<effect_id>_effect.h`), while real-time DSP processing lives in the shared audio engine (`libs/media/`) and project persistence in `apps/video-editor/src/timeline/`.

### DSP Effect Modules (Timeline & Mixer Chain)
- `gain_effect.h` — Gain / Trim (`Gain`)
- `limiter_effect.h` — Limiter & Soft Clipper (`Limiter`, `Soft Clipper`)
- `lfe_filter_effect.h` — LFE Low-Pass Filter (`LFE Filter`)
- `delay_effect.h` — Delay & Echo (`Delay`, `Echo`)
- `reverb_effect.h` — Algorithmic Reverb (`Reverb`)
- `modulation_effect.h` — Chorus, Flanger & Modulation (`Chorus`, `Flanger`, `Modulation`)
- `distortion_effect.h` — Harmonic Saturation / Distortion (`Distortion`)
- `stereo_width_effect.h` — Stereo Width & Stereo Fixer (`Stereo Width`, `Stereo Fixer`)
- `pitch_effect.h` — Pitch Shifter (`Pitch`)
- Built-in chain presets (`Vocal Channel` / `Chain FX`) and existing core effects (`Equalizer`, `Compressor`, `De-Esser`, `De-Hummer`, `Noise Reduction`, `Dialogue Processor`, `Multiband Compressor`).

### Analyzer & Utility Tool Dialogs (Phase 3)
Opened as non-modal, reusable dialogs from `AudioWorkspace::applyAudioFx(...)`:
- `frequency_analyzer_effect.h` — `FrequencyAnalyzerDialog`: 31-band octave spectrum bar visualizer with peak hold.
- `phase_meter_effect.h` — `PhaseMeterDialog`: Goniometer / Lissajous stereo correlation and balance meter.
- `surround_analyzer_effect.h` — `SurroundAnalyzerDialog`: 5.1 surround channel energy radar (`L`, `C`, `R`, `Ls`, `Rs`, `LFE`).
- `meter_panel_effect.h` — `MeterDetailDialog`: High-precision True Peak and BS.1770-1 Loudness (`M`, `S`, `I`, `LRA`) floating meter.
- `monitor_effect.h` — `MonitorDialog`: Control Room Monitor (`DIM -20 dB`, `Mono Sum`, reference level slider), synchronized with the `DIM` pill in the Meter Bridge top bar.
- `foley_sampler_effect.h` — `FoleySamplerDialog`: 8-pad Foley audition sampler powered by `QMediaPlayer` + `QAudioOutput` (local file loading and click-to-audition).

## 2. Live Analyzer Feed & Future Expansion

### Current Implementation (Zero-Overhead Peak Feed)
Currently, `PlaybackWorker` emits `PlaybackAudioLevelsEvent` containing `track_peaks`, `bus_peaks`, and `master_peak` to `AudioWorkspace::setPeaks(...)`. Open analyzer dialogs (`FrequencyAnalyzerDialog`, `PhaseMeterDialog`, `SurroundAnalyzerDialog`, `MeterDetailDialog`) receive live updates directly from `setPeaks(...)` along with the active track pan. This keeps playback latency and CPU overhead unchanged while providing responsive visual metering.

### Future Expansion (Raw Stereo / FFT Buffer Feed)
When full spectral FFT accuracy, true inter-channel phase correlation, or discrete 5.1 surround metering is required, the pipeline can be expanded without changing the analyzer UI contracts:
1. **PlaybackWorker**: Tap the post-fader master (or selected track) PCM ring buffer in `PlaybackWorker` to emit a periodic `PlaybackAudioSpectrumEvent` (e.g., 1024-sample stereo/surround float frames at ~30 Hz, gated by whether any analyzer dialog is open).
2. **FFT & Correlation**: Compute a lightweight real-to-complex FFT (e.g., 1024-point Hann-windowed) and exact L/R Pearson correlation in a worker or `AudioWorkspace` helper, passing the resulting band magnitudes and correlation coefficient into `FrequencyAnalyzerDialog::updateSpectrum(...)` and `PhaseMeterDialog::updateCorrelation(...)`.
3. **Foley Timeline Insertion**: Extend `FoleySamplerDialog` with an optional "Insert at Playhead" action wired to `TimelineCommandService` when clip insertion from the Audio workspace is needed.
