# Arquitetura de Efeitos e Analisadores de Áudio

[English](AUDIO_EFFECTS_ARCHITECTURE.md) · A versão em inglês prevalece em caso de divergência.

## 1. Organização Modular

Todos os painéis de UI de Audio FX, editores de parâmetros, analisadores e ferramentas de estúdio ficam em `apps/video-editor/src/workspaces/audio/effects/` como headers independentes (`<effect_id>_effect.h`), enquanto o processamento DSP em tempo real fica no motor de áudio compartilhado (`libs/media/`) e a persistência de projeto em `apps/video-editor/src/timeline/`.

### Módulos de Efeitos DSP (Cadeia de Timeline e Mixer)
- `gain_effect.h` — Ganho / Trim (`Gain`)
- `limiter_effect.h` — Limitador e Soft Clipper (`Limiter`, `Soft Clipper`)
- `lfe_filter_effect.h` — Filtro Passa-Baixa LFE (`LFE Filter`)
- `delay_effect.h` — Delay e Echo (`Delay`, `Echo`)
- `reverb_effect.h` — Reverb Algorítmico (`Reverb`)
- `modulation_effect.h` — Chorus, Flanger e Modulação (`Chorus`, `Flanger`, `Modulation`)
- `distortion_effect.h` — Saturação Harmônica / Distorção (`Distortion`)
- `stereo_width_effect.h` — Largura Estéreo e Stereo Fixer (`Stereo Width`, `Stereo Fixer`)
- `pitch_effect.h` — Pitch Shifter (`Pitch`)
- Presets de cadeia (`Vocal Channel` / `Chain FX`) e efeitos principais existentes (`Equalizer`, `Compressor`, `De-Esser`, `De-Hummer`, `Noise Reduction`, `Dialogue Processor`, `Multiband Compressor`).

### Diálogos de Analisadores e Ferramentas (Fase 3)
Abertos como diálogos não-modais reutilizáveis a partir de `AudioWorkspace::applyAudioFx(...)`:
- `frequency_analyzer_effect.h` — `FrequencyAnalyzerDialog`: Visualizador de espectro em 31 bandas de oitava com retenção de pico.
- `phase_meter_effect.h` — `PhaseMeterDialog`: Goniômetro / Lissajous de correlação estéreo e medidor de balanço.
- `surround_analyzer_effect.h` — `SurroundAnalyzerDialog`: Radar de energia surround 5.1 (`L`, `C`, `R`, `Ls`, `Rs`, `LFE`).
- `meter_panel_effect.h` — `MeterDetailDialog`: Medidor flutuante de True Peak e Loudness BS.1770-1 (`M`, `S`, `I`, `LRA`).
- `monitor_effect.h` — `MonitorDialog`: Controle de Monitor (`DIM -20 dB`, `Mono Sum`, controle de nível de referência), sincronizado com o botão `DIM` na barra superior do Meter Bridge.
- `foley_sampler_effect.h` — `FoleySamplerDialog`: Sampler Foley de 8 pads com `QMediaPlayer` + `QAudioOutput` (carregamento de arquivos locais e audição ao clicar).

## 2. Alimentação ao Vivo dos Analisadores e Expansão Futura

### Implementação Atual (Alimentação por Picos com Zero Overhead)
Atualmente, o `PlaybackWorker` emite `PlaybackAudioLevelsEvent` contendo `track_peaks`, `bus_peaks` e `master_peak` para `AudioWorkspace::setPeaks(...)`. Os diálogos de analisadores abertos (`FrequencyAnalyzerDialog`, `PhaseMeterDialog`, `SurroundAnalyzerDialog`, `MeterDetailDialog`) recebem atualizações ao vivo diretamente de `setPeaks(...)` junto com o pan da trilha ativa. Isso mantém a latência de reprodução e o uso de CPU inalterados enquanto entrega medição visual responsiva.

### Expansão Futura (Buffers Brutos Estéreo / FFT)
Quando for necessária precisão espectral completa por FFT, correlação de fase inter-canal real ou medição discreta 5.1, o pipeline pode ser expandido sem alterar os contratos de UI dos analisadores:
1. **PlaybackWorker**: Capturar o ring buffer PCM pós-fader do master (ou da trilha selecionada) no `PlaybackWorker` para emitir periodicamente um `PlaybackAudioSpectrumEvent` (ex.: quadros float estéreo/surround de 1024 amostras a ~30 Hz, condicionado à existência de algum analisador aberto).
2. **FFT e Correlação**: Calcular uma FFT real-para-complexo leve (ex.: janela de Hann de 1024 pontos) e a correlação de Pearson L/R exata em um worker ou helper do `AudioWorkspace`, enviando as magnitudes por banda e o coeficiente de correlação para `FrequencyAnalyzerDialog::updateSpectrum(...)` e `PhaseMeterDialog::updateCorrelation(...)`.
3. **Inserção de Foley na Timeline**: Estender o `FoleySamplerDialog` com uma ação opcional "Inserir no Playhead" conectada ao `TimelineCommandService` quando a inserção de clipes a partir do workspace Audio for desejada.
