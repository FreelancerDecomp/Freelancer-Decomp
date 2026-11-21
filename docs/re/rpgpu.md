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

`NULL_DEVICE = 1` in that section (RPGPU's own key, default 0) runs RPGPU without a device: the
Null backend accepts every call, reports a fixed list of display modes (1920x1080 down to
640x480, 60 Hz) and presents nothing. It is for runs without a GPU, such as a CI smoke test and
screenshot comparison. The key is read at `Startup`; a change replaces the backend, and the
client's next `CreateBuffers` brings the new one up.

## Build

- `FreelancerRpGpu` (`src/Directory.Build.props`) turns RPGPU on: `1` by default on x64, whose
  client has no other renderer, `0` on Win32, where RP8 is the renderer. Off, the project is an
  empty Utility build. Build it on Win32 with `-p:FreelancerRpGpu=1`.
- SDL3 comes from vcpkg in manifest mode. All port-only modules share one manifest,
  `src/vcpkg.json`, with one `builtin-baseline` pinned to a vcpkg release; each module is a
  feature of it (`rpgpu`), installed only when the module is on.
- `src/Vcpkg.props` and `src/Vcpkg.targets` hold the vcpkg wiring. Only port-only projects
  import them, so no retail module sees SDL. vcpkg installs into `src/vcpkg_installed/<triplet>/`
  (`x86-windows`, `x64-windows`), one copy for every consumer, and copies `SDL3.dll` next to the
  game.
- vcpkg is found through `VCPKG_ROOT` (Visual Studio's developer prompt),
  `VCPKG_INSTALLATION_ROOT` (GitHub's Windows runners) or Visual Studio's own copy
  (override: `-p:FlVcpkgRoot=...`). When RPGPU is on and vcpkg is missing, the build skips it
  with a warning; with `-p:FreelancerRequireVcpkg=true` it fails instead.
- `RP_DIRECT3D9` selects the Direct3D 9 branch of `render/d3d8_min.h`, so the interface types
  come from the Windows SDK's `d3d9.h`, which has the same layouts. No DirectX 8 SDK is needed.
- CI builds RPGPU on both platforms with `FreelancerRpGpu=1` and `FreelancerRequireVcpkg=true`.
  It caches vcpkg's binary packages (`VCPKG_DEFAULT_BINARY_CACHE`) per triplet, runner image and
  manifest, so SDL3 is compiled once per compiler and manifest, not per run.

## Structure

- `RpGpu.*` holds the DACOM factory, the aggregation controller and the component class. Its
  .cpp has the interface lookup, the device and frame calls that go to the backend, the draw
  calls and `DllMain`.
- `RpGpuState.*` holds the settings and RP8's defaults, the pipeline, render and texture-stage
  state, the viewport, the transforms and the lights.
- `RpGpuResources.*` holds the texture and buffer handle tables, the texture images, the
  vertex and index buffers with RP8's lock and offset conventions, the pool and scratch rings,
  and the IRPTexture loaders.
- `RpGpuDds.*` reads the DDS header for `GetImageInfo`; the texture decoder (plan step 4.1)
  will grow here.
- `RpGpuCommon.h` holds the helpers every file shares: the GENRESULT values, the -1 handle and
  the log.
- `backend/` is the seam between the pipeline and the screen:
  - `RpGpuBackend.h`: `IRpGpuBackend` brings the device and swap chain up, lists display modes,
    clears and presents, and reads the last frame back.
  - `RpGpuSdlBackend.*` is the SDL backend and the only code that includes SDL. It wraps the
    client's window in an SDL window (`SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER`), creates the
    SDL_GPU device, claims the window, and clears and presents. SDL picks the GPU API: Direct3D
    12, Vulkan or Metal. It cannot read frames back yet: the swap-chain image is not readable,
    and that changes once RPGPU draws into its own target (plan step 5.3).
  - `RpGpuNullBackend.cpp` is the Null backend (`NULL_DEVICE`). Its frame is the clear colour
    at the size `CreateBuffers` asked for, and it reads that back.

## Status: skeleton

The first version opens the device and presents a cleared frame; it draws nothing else.

- **State the game reads back is kept:** pipeline state, render and texture-stage state, the
  projection (stored as D3D's row-major matrix, handed over transposed, as RP8 does) and the
  viewport.
- **Settings and defaults are RP8's,** from `render/rp8_semantics.h`, the tables and the value
  parser RP8 itself uses (so `0x1C` reads 1 in both). `Startup` reads, from the `[RenderPipeline]`
  section or the section the aggregation descriptor names, the keys RP8 reads: the pipeline
  states (`BROKEN_MULTITEXTURE` is then overwritten with the blend-stage count, 7, as in RP8),
  the ability keys (each one present replaces `QueryDeviceAbility`'s answer) and the render
  states. RP8's device-creation keys (`DeviceId`, `PUREDEVICE`, the vertex-processing keys and
  the like) mean nothing to SDL_GPU and are not read; neither are the texture-stage keys, which
  RP8 does not read either. `CreateBuffers` and `ResetRenderStatesToDefaults` restore the
  defaults; every stage starts with COLOROP and ALPHAOP 1 (disabled), as RP8's Initialize leaves
  them.
- **Buffer sessions follow RP8's.** `BeginScene` and `SwapBuffers` fail between
  `DestroyGpuResources` and the next successful `CreateBuffers`. A failed present returns a
  failure, so the client re-creates its buffers, and the device is rebuilt on that call.
- **Textures and buffers live in CPU memory,** with RP8's handle, lock and offset semantics:
  - vertex-buffer lock offsets go in as bytes and come back as vertex indices;
  - the pool vertex buffers and the scratch index buffer (handle -1) are rings;
  - `IsVbValid` and `IsIbValid` answer 1 for a valid buffer.
- **Texture levels are converted** by TexConvert ([texconvert.md](texconvert.md)), which does
  what D3DX's `D3DXLoadSurfaceFromMemory` does for RP8.
  - **The goal:** byte for byte for same-size loads into an uncompressed format and for DXT
    copies into the same DXT format. TexConvertTest checks this against D3DX 8.1 and reports
    where it doesn't hold yet.
  - **Close to D3DX, not identical (also a goal):** resized loads, and any load that has to
    compress into DXT, even at the same size. TexConvertTest measures only part of this; see
    the test scope in [texconvert.md](texconvert.md).
- **The draw calls** are accepted and dropped.
- **Abilities** describe a modern GPU, but report no gamma ramp (SDL3 has none) and no cube
  maps until `LoadCubemap` loads them.
- **`PrintScreen`** answers RP8's argument checks and writes a 24-bit BMP (`.BMP`, `.DIB`) of
  the backend's last frame. RP8 saves through D3DX in seven formats; RPGPU fails the other
  five, and fails on a backend that cannot read frames back.
- **`GetImageInfo`** reads the DDS header itself, which is what Shading needs from D3DX.

## x64

Callers keep texture and buffer handles in 32-bit fields, so on x64 the handle objects are
allocated from LowArena (`x64_low_arena.h`), below 2 GB. The texture images live on the ordinary
heap: `LockTexture` hands their address out in `FRpTextureLock::Bits`
(`render/IRenderPipeline.h`), which is a pointer, so nothing cuts it to 32 bits. On Win32 the
record keeps retail's five-dword layout.
