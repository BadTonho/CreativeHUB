# Fusion Masks

[Gaussian Blur](GAUSSIAN_BLUR.md) is a static, maskable Fusion-only
catalog node and an editable cscomp v3 stage. It shares CPU/GPU processing with
Motion, persists in CSP v31 and is covered by `creative-suite-blur-interchange`.

Functions (Shift+Space) also lists both masks while in Fusion. Search their names
and use Add, Enter, double click or drag to create a node. A compatible selected
clip is required; masks are hidden from Functions in Edit.

Rectangle Mask and Ellipse Mask are available in Effects > Fusion > Masks while in Fusion for video,
image and independent Fusion Composition graphs. Edit geometry in the Inspector
and press Apply Settings. The Viewer has no geometry handles. VIEW on a mask
shows opaque grayscale coverage and does not modify the project.

Mask outputs and optional Mask inputs are gold, labeled and typed. Image ports
are blue. Each image node accepts one mask on input index 2; image input 0 and
Merge foreground input 1 retain their existing indices. A mask can feed several
receivers. Reconnection replaces the previous mask; incompatible connections
and cycles are refused without logging an error or changing history.

| Property | Default | Range |
| --- | --- | --- |
| Center X / Y | 50% / 50% | −200% to 300% |
| Width / Height | 50% / 50% | 0% to 400% |
| Rotation | 0° | −3600° to 3600° |
| Soft Edge | 0% | 0% to 100% of the shorter receiving dimension |
| Invert | Off | On / Off |

Coordinates refer to the receiving image, not an Input's original media size.
Rotation is applied in receiving pixel coordinates. Zero width or height gives
empty coverage regardless of softness; inverted empty coverage reveals all.
Center, width, height and rotation use the existing keyframe buttons and scalar
curves. Soft Edge and Invert are static. Creation, settings, connections and
removal use graph history and project dirty state; no shortcut is added.

Input, Background, Text and Output multiply image alpha by coverage. Transform,
Color and Effect mix the original and processed image, keeping the original
outside coverage. Merge limits foreground contribution while preserving its
background. Blend uses premultiplied alpha and returns straight RGBA; zero and
full coverage preserve the respective endpoint bytes. Disconnected Mask inputs
retain the previous evaluation path.

The Video Editor owns its graph and editing workflow. Geometry validation,
scalar evaluation and coverage generation are shared with Motion through the
composition/animation libraries. The Preview/Viewer/export rasterizer uses fixed 2×2 samples per pixel. Rectangle
distance is signed distance to the rectangle; ellipse distance is normalized
radial distance scaled by the smaller semiaxis. Softness uses smoothstep across
a band centered on the shape boundary; inversion follows coverage evaluation.
An evaluation-local cache reuses equal type, geometry and receiving dimensions,
limited to four entries / 64 MiB. It is discarded after evaluation, so animated
frames cannot accumulate history. Cancellation is checked per raster row.

Shared composition exposes ApplyMask and MaskedBlend on CPU and OpenGL. The GPU
uploads the same RGBA coverage (red channel) into a third input slot and keeps
image intermediates resident. Coverage uploads are included in resource usage;
existing bounded output pools and complete-graph CPU recovery still apply.
CPU intermediates are released after their last required consumer. Unsupported
or failed GPU operations are diagnosed before recovery; cancellation is normal.

Geometry curves remain clip-local. Splits and trims subdivide partial cubic
Bezier segments and retain linear continuity for ordinary clips and independent
compositions. FPS conversion rescales key times. Independent composition input
offsets continue to preserve the sampling time of internal videos.

CSP v28 adds `rectangle_mask` / `ellipse_mask` nodes, normalized `mask` parameters,
five scalar arrays in `mask_keyframes` (`center_x`, `center_y`, `width`, `height`,
`rotation`) and typed connections to input 2. Mask data is rejected in older
formats. Versions 1–27 remain readable; Hub backups accept v28, preserve masks
and relocate only external Input/media paths. Current Hub backups accept CSP
v37, copy Color Gallery sidecars and reject future v38.

Direct Viewer editing, pen/polygon masks, mask combinations, tracking and masks
on Adjustment Clips remain outside this delivery. See [regression testing](../validation/REGRESSION_TESTING.md)
for automated evidence and separate pending native/platform acceptance.

[Editable interchange](../../COMPOSITION_INTERCHANGE.md) v2 now transfers source,
Color/Effect and Transform masks to Motion with shared identities. Merge and
Output masks remain valid inside Fusion but are refused by this interchange
version. The native CSP schema remains 30.
