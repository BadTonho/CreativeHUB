# Levels adjustment layers

## Editing contract

Layers > New Adjustment Layer > Levels creates the fifth adjustment kind. It
applies the same encoded-sRGB mapping to red, green and blue. Individual channel
controls remain planned; [Curves](CURVES.md) is a separate adjustment kind. A single selected layer,
Background or group is required. The layer is inserted above the selected
sibling or clipping chain, retains group ownership, and captures an active area
selection as its independent initial mask; otherwise its mask starts white.

| Control | Range | Default |
| --- | --- | --- |
| Input Black | 0–254, below Input White | 0 |
| Input White | 1–255, above Input Black | 255 |
| Midtone Gamma | 0.10–9.99, step 0.01 | 1.00 |
| Output Black | 0–255, no higher than Output White | 0 |
| Output White | 0–255, no lower than Output Black | 255 |

Numeric fields and sliders stay synchronized and enforce the paired bounds.
A drag or continuous field edit is one Undo. Reset restores all five controls
atomically; no-op edits preserve Redo. Switching focus, layer or tab finishes an
edit. Masks, visibility, opacity, clipping, six blend modes, history and stack
operations follow the existing adjustment contract. Mask selection retains the
owner controls. Levels has no painted content, outline, individual transform or
isolated Quick Export; document/group exports and linked PNG include its result.
No shortcut is added.

The panel shows a 256-bin combined RGB histogram of the pixels entering this
Levels layer, before its own mask and parameters. Each nontransparent sampled
pixel contributes once to each color channel. At most one million regularly
spaced pixels are sampled. The input is captured at the layer's actual root,
isolated-group or clipping-chain position. Histogram rendering runs on a
background worker, cancels superseded work and discards stale tab/layer results;
changing only Levels
parameters retains the current histogram. The histogram is reference-only and
is not persisted or used by the adjustment calculation.

## Processing and ownership

`effects::LevelsParameters` is the fifth `AdjustmentParameters` alternative.
The shared Qt-free evaluator validates the five controls. For each straight,
normalized, encoded-sRGB channel `c`, calculate

`n = clamp((255*c - input_black)/(input_white-input_black), 0, 1)` and
`result = round(output_black + (output_white-output_black)*n^(1/gamma))`.

The CPU adapter and OpenGL shader quantize mapped RGB before the existing
masked blend and preserve the backdrop alpha. Neutral Levels in Normal mode
keeps the original byte path. Other blend modes retain their existing rules.
Image Editor owns creation, masks, stack scope, history, persistence and
technical errors. Parameter edits reuse raw content and mask caches; the
existing CPU/GPU budgets, cancellation and recovery behavior remain.
Video/Motion adjustment registries and document formats do not change.

## Format and compatibility

Levels was introduced in `.cimg` v24; the current v26 writer reads v1–25. A Levels layer stores
`{"kind":"adjustment","adjustment":{"type":"levels","input_black":20,"input_white":230,"gamma":1.4,"output_black":10,"output_white":240}}`.
All five values are required. Reject the type before v24, fractional or
out-of-range integer controls, nonfinite/out-of-range gamma, crossed points,
extra adjustment fields, incompatible Background/content/outline fields and
future versions. A failed Open leaves the active document unchanged. Recovery
envelope v1, application version and linked PNG protocol remain unchanged.
Earlier binaries cannot open v24 and there is no downgrade writer. Hub accepts
Image v1–26, rejects v27, and retains adjustment masks and media paths across
backup and relocation.

## Regression and acceptance

`creative-suite-image-editor-levels` covers the numeric reference, identity,
bounds, alpha, modes, histogram input, history, persistence/recovery, export and
cache reuse. `creative-suite-image-editor-levels-ui` covers creation, selection
mask, histogram, five controls, Reset and editing history. Native
`gpuLevels` compares CPU/OpenGL pixels, uploads, groups, clipping, cancellation
and capacity recovery. Hub format/manager and linked-image consumer gates cover
their affected boundaries. Record actual execution results in the roadmap.

The [manual checklist](../validation/MANUAL_VALIDATION.md#global-levels-adjustments)
and native/platform acceptance remain pending for owner validation.
