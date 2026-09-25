# src/shaders — the FluidWallpaper HLSL

These files used to be hand-split C++ raw-string literals in `src/shaders.h`. The build now
embeds them: `cmake/embed_hlsl.cmake` (run by `add_custom_command` in CMakeLists.txt, DEPENDS on
every `.hlsl`) writes `<build dir>/generated/shaders_gen.h`, which `src/fluid.cpp` includes. The
generated header is not committed. It holds `static const char* k<Name>Src`, split into raw-string
pieces of ≤16000 bytes on line boundaries (MSVC caps one literal at 16380 bytes), with CRLF
normalised to LF. **There is no manual literal cap any more.** Runtime compile is unchanged:
D3DCompile on these strings, with the defines passed as `D3D_SHADER_MACRO` (`kAcidSlotMacros`,
`INK`, `DROP_COMPACT`, `BN_OPTICS`). Each file is the embedded text **byte for byte**: the leading
blank line and the blank lines where the old literal seams were stay in, because moving a byte can
move the DXBC.

| file | variable | what |
|---|---|---|
| `compute.hlsl` | `kComputeSrc` | every compute pass; root constants at b0 (keep in step with `struct SimCB` in fluid.cpp) |
| `display.hlsl` | `kDisplaySrc` | fullscreen triangle sampling the dye, pseudo-normal shading; linear scRGB out (1.0 = 80 nits). One source, three PSOs: fluid, `INK`, `LIQUID_ACID` (+ `LA_*` slot macros) |
| `post.hlsl` | `kPostSrc` | the `[post]` image-space camera pass (the lens in front of the dish), `BN_OPTICS` variant built on demand; the rig block b3 (`rg0..rg4`, see `src/rig_slots.h`) |
| `gradient.hlsl` | `kGradientSrc` | the M1 HDR diagnostic gradient (`--gradient`) |

Checks (no GPU): `python tools/shaders-identity.py <old shaders.h> <generated header>` (byte
identity), `tools/slot-check.ps1` (acid cbuffer + LA_ names, reads display.hlsl),
`tools/rig-check.ps1` (rig block b3), `python tools/dxbc-cmp.py <old> <new> <acid_slots.h>`
(DXBC per PSO; each side is an old `shaders.h` or a `src/shaders` directory).
