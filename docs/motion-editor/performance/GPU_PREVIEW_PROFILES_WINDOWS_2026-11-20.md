# Motion Studio GPU Preview Profiles - Windows Results, 2026-11-20

Status: **reference-PC evidence supporting the provisional default-on decision**.
It does not establish AMD/Intel, Linux, macOS, packaged-runtime or export-throughput
acceptance. Raw data: [GPU_PREVIEW_PROFILES_WINDOWS_2026-11-20.csv](GPU_PREVIEW_PROFILES_WINDOWS_2026-11-20.csv).

## Environment and method

Windows 11 Pro 10.0.26300; AMD Ryzen 5 3600; about 32 GB RAM; NVIDIA GeForce GTX
1660 SUPER; Qt 6.7.2; MSVC Release. The tool is
`creative-suite-motion-editor-gpu-preview-benchmark` (see
[the direct-preview contract](../gpu/GPU_TEXTURE_PREVIEW.md)); it was extended with
three fixtures, selectable by an optional first argument:

| Profile | Canvas | Effects on the full-frame raster background |
| --- | --- | --- |
| heavy | 1920 x 1080 | Gaussian Blur (radius 10) then Color Adjustment |
| medium | 1920 x 1080 | Color Adjustment only |
| light | 1280 x 720 | none |

Every profile also has a 640 x 360 generated video layer, a translucent still, static
text and a moving translucent ellipse, as in the
[2026-10-10 fixture](GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). Each run warms up for
two seconds and measures ten seconds; backends are interleaved (CPU, GPU with RGBA
readback, GPU direct texture) over three trials at 30 and 60 fps. No other workload
was started during the run. Figures are medians of the three trial values; a median
p95 is not a pooled percentile. Latency is request-to-viewer-paint; render is
frame-render wall time. Fixtures are synthetic, not representative acceptance projects.

## Median results

| Profile | Target fps | Backend | Presented fps | Latency avg / p95 (ms) | Render avg (ms) | CPU % |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| heavy | 30 | cpu | 4.5 | 240.1 / 255.8 | 219.3 | 235.6 |
| heavy | 30 | gpu_rgba | 30.0 | 15.4 / 17.0 | 12.4 | 52.5 |
| heavy | 30 | gpu_texture | 30.0 | 11.3 / 12.9 | 8.3 | 40.3 |
| heavy | 60 | cpu | 4.6 | 232.2 / 241.7 | 220.3 | 234.4 |
| heavy | 60 | gpu_rgba | 60.0 | 13.2 / 16.4 | 10.2 | 90.8 |
| heavy | 60 | gpu_texture | 60.0 | 9.4 / 12.6 | 6.4 | 72.5 |
| medium | 30 | cpu | 6.5 | 171.9 / 187.2 | 153.5 | 102.0 |
| medium | 30 | gpu_rgba | 30.0 | 12.0 / 13.4 | 9.1 | 45.9 |
| medium | 30 | gpu_texture | 30.0 | 9.5 / 11.1 | 6.5 | 35.2 |
| medium | 60 | cpu | 6.5 | 166.3 / 176.2 | 154.4 | 101.4 |
| medium | 60 | gpu_rgba | 60.0 | 10.1 / 13.2 | 7.1 | 75.0 |
| medium | 60 | gpu_texture | 60.0 | 7.3 / 10.2 | 4.4 | 53.8 |
| light | 30 | cpu | 30.0 | 11.4 / 12.8 | 8.5 | 32.8 |
| light | 30 | gpu_rgba | 30.0 | 9.8 / 11.4 | 7.0 | 34.4 |
| light | 30 | gpu_texture | 30.0 | 8.5 / 10.0 | 5.5 | 30.9 |
| light | 60 | cpu | 60.0 | 9.6 / 12.5 | 6.7 | 53.0 |
| light | 60 | gpu_rgba | 60.0 | 7.9 / 11.0 | 5.0 | 57.0 |
| light | 60 | gpu_texture | 60.0 | 6.8 / 9.6 | 3.7 | 41.6 |

All 54 runs recorded zero GPU failures. The benchmark rejects a GPU run that fell back
to CPU or, for direct texture, performed readback.

## Reading

- **Heavy and medium:** the CPU compositor sustains only 4.5-6.5 fps at 1080p, while GPU
  composition holds 30 and 60 fps with 7-15 ms latency.
- **Light:** the CPU already holds the rate; GPU direct delivery still lowers median
  latency by about 25% at 30 fps and 29% at 60 fps. GPU with RGBA readback has about
  the same process CPU use as the CPU path, so no profile regressed.
- Scope: preview only, warm composition, one GPU and driver. Export throughput was not
  measured here; the historical 3,405-frame export acceptance remains pending.
