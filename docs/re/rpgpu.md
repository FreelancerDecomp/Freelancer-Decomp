# RPGPU.dll: the SDL3 + SDL_GPU render pipeline (port only)

## Purpose

`RPGPU.dll` is a port-only render pipeline; retail has none. It answers the same interface IDs
as `RP8.dll`: `RenderPipeline8B`, `VertexBufferManager`, `RPDraw`, `RPIndexBuffer`,
`RPVertexBuffer`, `GammaControl`, `RPTexture` and `AggregateComponent`. It registers as the
`SDLGPU` component with RP8's priority, `0x40000000`. The game's modules find the pipeline by
IID, so none of them change.

RPGPU is the x64 client's renderer: RP8 cannot be built for x64, because D3DX8 has no 64-bit
version. On Win32 it is an option next to RP8, which stays the default. See
[x64-client-plan.md](../x64-client-plan.md), phase 5.

## Selecting it

In `bin/DACOM.INI`, uncomment `RPGPU.dll` in `[Libraries]`, and in `[System]` replace
`DirectX8` with `SDLGPU`. It reads the same `[RenderPipeline]` section as RP8.

## Build

- SDL3 comes from vcpkg in manifest mode: `src/RPGPU/vcpkg.json`, with `builtin-baseline`
  pinned to a vcpkg release.
- Only `RPGPU.vcxproj` enables vcpkg, so no other module sees SDL. vcpkg builds SDL3 for
  `x86-windows` and `x64-windows` and copies `SDL3.dll` next to the game.
- vcpkg is found through `VCPKG_ROOT` (Visual Studio's developer prompt),
  `VCPKG_INSTALLATION_ROOT` (GitHub's Windows runners) or Visual Studio's own copy. Without it
  the project builds nothing and says so; nothing else depends on it.
- `RP_DIRECT3D9` selects the Direct3D 9 branch of `render/d3d8_min.h`, so the interface types
  come from the Windows SDK's `d3d9.h`, which has the same layouts. No DirectX 8 SDK is needed.
- CI caches vcpkg's binary packages (`VCPKG_DEFAULT_BINARY_CACHE`), so SDL3 is compiled once
  per manifest and platform.

## Structure

- `RpGpuDevice.*` is the only code that includes SDL. It wraps the client's window in an SDL
  window (`SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER`), creates the SDL_GPU device, claims the
  window, and clears and presents. SDL picks the backend: Direct3D 12, Vulkan or Metal.
- `RpGpu.*` holds the DACOM factory, the aggregation controller and the component that
  implements the interfaces.

## Status: skeleton

The first version opens the device and presents a cleared frame; it draws nothing else.

- **State the game reads back is kept:** pipeline state, render and texture-stage state, the
  projection (stored as D3D's row-major matrix, handed over transposed, as RP8 does) and the
  viewport.
- **Textures and buffers live in CPU memory,** with RP8's handle, lock and offset semantics:
  - vertex-buffer lock offsets go in as bytes and come back as vertex indices;
  - the pool vertex buffers and the scratch index buffer (handle -1) are rings;
  - `IsVbValid` and `IsIbValid` answer 1 for a valid buffer.
- **The draw calls** are accepted and dropped.
- **Abilities** describe a modern GPU, but report no gamma ramp (SDL3 has none) and no cube
  maps until `LoadCubemap` loads them.
- **`GetImageInfo`** reads the DDS header itself, which is what Shading needs from D3DX.

## x64

Callers keep texture handles in 32-bit fields, and the font atlas reads a locked texture's
address from an `unsigned long[5]`. So on x64 the texture and buffer handles and the texture
images are allocated from LowArena (`x64_low_arena.h`), below 2 GB.
