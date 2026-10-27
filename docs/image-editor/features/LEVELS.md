# Levels adjustment layers

## Editing contract

Layers > New Adjustment Layer > Levels creates the fifth adjustment kind. The
channel selector edits the RGB master or one of the independent Red, Green and
Blue sets. The master is applied first to all encoded-sRGB channels; each
individual set then maps only its corresponding channel. All four sets default
to identity. Reset changes only the selected set.

| Control | Range | Default |
| --- | --- | --- |
| Input Black | 0–254, below Input White | 0 |
| Input White | 1–255, above Input Black | 255 |
| Midtone Gamma | 0.10–9.99, step 0.01 | 1.00 |
| Output Black | 0–255, no higher than Output White | 0 |
| Output White | 0–255, no lower than Output Black | 255 |

Numeric fields and sliders stay synchronized and enforce paired bounds
independently for each set. A drag or continuous field edit is one Undo. Reset
is atomic; no-op edits preserve Redo. Switching channels, focus, layer or tab
finishes an edit and retains every channel's values. Masks, visibility, opacity,
clipping, six blend modes, history and stack operations follow the existing
adjustment contract. Mask selection retains owner controls. Levels has no
painted content, outline, individual transform or isolated Quick Export;
document/group exports and linked PNG include its result. No shortcut is added.

The panel shows a fixed 256-bin combined RGB histogram of the pixels entering
this Levels layer, before its own mask and parameters. Each nontransparent
sample contributes once to each color channel. At most one million regularly
spaced pixels are sampled. The input is captured at the layer's actual root,
isolated-group or clipping-chain position. Histogram rendering runs on a
background worker, cancels superseded work and discards stale tab/layer results;
channel and parameter edits retain the current histogram. The histogram is
reference-only and is not persisted or used by the adjustment calculation.

## Processing and ownership

`effects::LevelsParameters` is the fifth `AdjustmentParameters` alternative.
Its five existing fields remain the RGB master; `channels[0..2]` hold the
Red/Green/Blue parameter sets. The shared Qt-free evaluator validates all four
sets. For each straight, normalized, encoded-sRGB channel `c`, each set
calculates

`n = clamp((255*c - input_black)/(input_white-input_black), 0, 1)`

and maps it to

`round(output_black + (output_white-output_black)*n^(1/gamma))`.

The master output is rounded to a byte before the matching individual set is
applied. CPU and OpenGL use this same two-stage byte contract. The adjustment
preserves backdrop alpha and neutral Levels in Normal mode keeps the original
byte path. Other blend modes retain their existing rules. Image Editor owns
creation, masks, stack scope, history, persistence and technical errors.
Parameter edits reuse raw content and mask caches; channel values are sent as
adjustment uniforms without re-uploading content. The existing CPU/GPU budgets,
cancellation and recovery behavior remain. Video/Motion adjustment registries
and document formats do not change; their linked-image workflows consume the
flattened PNG.

## Format and compatibility

Levels was introduced in `.cimg` v24. Version 27 stores the five master fields
at the top level and three five-field parameter objects in `channels.red`,
`channels.green`, and `channels.blue`:

```json
{
  "type": "levels",
  "input_black": 20,
  "input_white": 230,
  "gamma": 1.4,
  "output_black": 10,
  "output_white": 240,
  "channels": {
    "red": {"input_black": 0, "input_white": 255, "gamma": 1.0, "output_black": 0, "output_white": 255},
    "green": {"input_black": 0, "input_white": 255, "gamma": 1.0, "output_black": 0, "output_white": 255},
    "blue": {"input_black": 0, "input_white": 255, "gamma": 1.0, "output_black": 0, "output_white": 255}
  }
}
```

Every field is required. Input/output points are integers in [0,255], with
input black strictly below input white and output black no higher than output
white. Gamma is a finite JSON number in [0.10,9.99]. Reject missing or extra
fields, invalid bounds, incompatible adjustment data, and this schema before
v27. Version 24–26 Levels migrate the existing five values to the RGB master
and initialize all individual sets to identity. Failed Open leaves the active
document unchanged. Recovery envelope v1, application version and linked PNG
protocol remain unchanged. Earlier binaries cannot open v27; there is no
downgrade writer. Hub accepts Image v1–30, rejects v31, and retains all four
Levels sets, adjustment masks and media paths across backup and relocation.
Curves v26 data remains readable and unchanged.

## Regression and acceptance

`creative-suite-image-editor-levels` covers mapping order, intermediate byte
rounding, identity, independent channel sets, bounds, gamma, alpha, masks,
modes, histogram input, history, mixed stacks, v24–26 migration, v27 round-trip,
recovery, export and cache reuse. `creative-suite-image-editor-levels-ui`
covers the selector, channel switching, controls and paired bounds, selected
channel Reset, history, persistence and combined histogram stability. Native
`gpuLevels` compares CPU/OpenGL pixels and checks channel edits, uploads, groups,
clipping, cancellation and resource limits. Hub format/manager tests and linked
PNG consumers cover their boundaries. Record actual execution results in the
roadmap.

The [manual checklist](../validation/MANUAL_VALIDATION.md#rgb-and-per-channel-levels-adjustments)
and native/platform acceptance remain pending for owner validation.
