# TexConvert: texture conversion without D3DX (port only)

## Purpose

RP8 hands every texture level to D3DX 8.1's `D3DXLoadSurfaceFromMemory`: Shading's DDS and TGA
loaders and ximage call `SetTextureLevelData`. D3DX 8.1 has no 64-bit version and no other
platforms, so RPGPU uses TexConvert instead (`src/TexConvert/`, a static library with no
Direct3D or platform headers). It is step 4.1 of [x64-client-plan.md](../x64-client-plan.md).

## What it does

`TexConvert::Load` converts one image from a source format and size into a destination format
and size. The formats are RP8's pixel-format numbers: P8 with an RGB palette, R8G8B8, R5G6B5,
X1R5G5B5, A4R4G4B4, A1R5G5B5, A8R8G8B8, X8R8G8B8, and DXT1 to DXT5.

RP8 calls D3DX in two ways, and TexConvert's two paths follow them.

| Path | When | RP8's D3DX call | Parity |
|---|---|---|---|
| Exact | same size, uncompressed target | `D3DX_FILTER_NONE`: a per-pixel format conversion | byte for byte with D3DX 8.1 |
| Exact | same size, DXT into the same DXT format | the same | a block copy |
| Filtered | different size, or a DXT target from another format | `D3DX_FILTER_TRIANGLE`, D3DX's DXT compressor | within a tolerance |

At the default texture detail, textures load at their own size, so the game uses the exact
path. Lower detail settings shrink textures, and only then does the filtered path run.

**The exact path.** These are D3DX 8.1's rules. TexConvertTest's first run established them,
and later runs confirm them.
- **Uncompressed formats:** each channel goes through a float in 0..1: an n-bit value becomes
  value / (2^n - 1), and a float becomes round(value * (2^n - 1)).
  - Formats without alpha read as opaque.
  - X1R5G5B5 and X8R8G8B8 write their unused bits as 0.
  - A P8 palette is opaque.
- **The same format at the same size** is a plain byte copy, unused bits included. For DXT it
  is a copy of the blocks.
- **DXT decoding:**
  - The endpoints are widened to 8 bits by repeating their top bits.
  - The in-between entries are interpolated in integers and rounded down.
  - The three-colour mode (colour 0 <= colour 1: a half-way entry and black) applies in every
    DXT format, not only DXT1. Index 3 is transparent only in DXT1.
  - DXT5 alpha uses eight or six interpolated steps, again rounded down.
  - DXT2 and DXT4 come back with their colour multiplied by alpha.
- **The pitch Shading passes for DXT.** Shading's DDS loader passes linear size / width in
  blocks, which is retail's own calculation, and D3DX uses it as given. So for a mip whose
  block grid is not square, RP8 reads the blocks with that pitch. TexConvert reads a DXT source
  with its real stride instead, so its reads stay inside the mip. This is the one known place
  where RPGPU and RP8 disagree on purpose.

**The filtered path.** This is our own code, not a copy of D3DX's. Encoding into DXT2 or DXT4
multiplies colour by alpha.
- an area-weighted (box) resample;
- a range-fit DXT encoder: bounding-box endpoints and nearest palette entries, DXT1's
  three-colour mode for transparent pixels, and min/max alpha endpoints for DXT5.

The step 4.2 screenshot comparison measures how far this path is from D3DX.

## The check: TexConvertTest

`TexConvertTest.exe` (Win32 only) runs a fixed set of conversions through both D3DX 8.1 and
TexConvert and compares the bytes:
- every source format into every uncompressed target, and each DXT format into itself;
- 256x64 images, with two patterns: a byte ramp and random bytes;
- D3DX writes into a system-memory image surface of a Direct3D 8 device.

It also decodes single DXT blocks with hand-picked endpoints through both. When they differ
it prints the block and both results, so a rounding difference can be read off the log.

CI runs it in a report-only job. Exit code 0 means everything was identical, 1 means a
difference (it prints the first differing bytes), and 2 means nothing was compared: no
Direct3D 8 device, or no case could run.
