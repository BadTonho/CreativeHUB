# Color Sizing Dock

[English](COLOR_SIZING_DOCK.md) · A versão em inglês prevalece em caso de divergência.

Status: **A Etapa 2 (funcionalidade real e efeitos DSP) está implementada para as páginas 1 (`Curves - Custom`), 2 (`Color Slice`), 4 (`Qualifier - HSL`), 5 (`Window`), 6 (`Tracker - Window`), 8 (`Blur - Blur`), 9 (`Key`) e 10 (`Sizing - Input Sizing` / `Node Sizing`).**

As páginas **3 (`Color Warp - Chroma Warp`)** e **7 (`Smart Mask`)**, bem como os demais modos de Sizing (`Edit`, `Output`, `Reference`) e a seção `Blanking` da página 10, permanecem em **Etapa 1 (apenas visual)** e estão documentadas para fases futuras:

- **Página 1 (`Curves - Custom`)**: Ligada ao nó `color::CurvesNode` (`Master`, `Hue vs Hue`, `Hue vs Sat`, `Hue vs Lum`, `Lum vs Sat`, `Sat vs Sat`). Os controles de *Strength* e *Soft Clip* permanecem pendentes de extensão no schema `VideoCurvesParameters`.
- **Página 2 (`Color Slice`)**: Ligada ao efeito `video.color_slice` (7 fatias: Red, Skin, Yellow, Green, Cyan, Blue, Magenta + controles globais). Por definição do mantenedor, a barra `SliderA` controla **Density** e `SliderB` controla **Saturation**.
- **Página 3 (`Color Warp - Chroma Warp`)**: *Fase dedicada futura* — requer motor de deformação 2D em espaço cromático (ex.: OkLab/YUV) e definição das ferramentas *Add pin pair* e *Pick*.
- **Página 4 (`Qualifier - HSL`)**: Ligada a `creative_suite::effects::TonalQualifier` no nó de Curvas.
- **Página 5 (`Window`)**: Ligada à API `color::PowerWindow` (`Rectangle`/`Ellipse`, suavidade, inversão, geometria e exclusão).
- **Página 6 (`Tracker - Window`)**: Ligada ao `PowerWindowTracker` existente (`Track Forward`, `Track Backward`, `Stop`, `Clear`).
- **Página 7 (`Smart Mask`)**: *Fase futura* — um Smart Mask automático real exige motor de segmentação local (ex.: ONNX/SAM) ou fluxo óptico, que ainda não existe no repositório.
- **Página 8 (`Blur - Blur`)**: Ligada ao efeito `video.color_blur` com modos **Blur**, **Sharpen** e **Mist** por canal (`R`, `G`, `B`) e controles `Coring Softness`, `Level` e `Mix`.
- **Página 9 (`Key`)**: Ligada ao efeito `video.color_key` (`Key Input`, `Key Output` e `Qualifier` gain/offset/blur).
- **Página 10 (`Sizing - Input Sizing`)**: Ligada ao efeito `video.input_sizing` para os modos **Input Sizing** e **Node Sizing** (`Pan`, `Tilt`, `Zoom`, `Rotate`, `Width`, `Height`, `Pitch`, `Yaw`, `Flip H/V`). Os modos `Edit Sizing`, `Output Sizing`, `Reference Sizing` e as unidades de `Blanking` ficam documentados para fase futura.
