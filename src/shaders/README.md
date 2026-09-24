# Per-clip effect shaders (export only)

The SkSL fragments of the per-clip effects libopenshot runs on the GPU and the web editor does not
share: `chroma_key`, `color_adjustment`, `color_map` (the LUT), `enhancement`, `light_adjustment`,
`mask`. Each is the GPU twin of its effect class in `src/effects/` and is held to it by
`tests/gpu/openshot-gpu-effect-parity`.

They follow the contract of the shared transition shaders in
`src/effects/image-processing-lib/shaders/README.md` — the same `_prelude.sksl` is prepended, the
frame is `osSrc` (premultiplied, NEAREST, pixel units), values are resolved on the host, and a
rasteriser's or a transcendental's decision arrives as a texture (`contrastLut`, `maskImage`, the
LUT atlas). The build embeds both directories into one header (`cmake/scripts/embed_shaders.cmake`);
a stem may exist in only one of them.

`enhancement.sksl`'s grain mode is the one fragment here that is **not** a parity twin: the hash is
float where the C++'s is double, so it is the same grain at a different random phase (owner
decision, 2026-09-24). It is gated statistically — `unit.gpu_grain`, and the parity tool's
statistical cases — never byte for byte.
