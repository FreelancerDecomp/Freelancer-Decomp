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
- `RP_PORT_TYPES` makes `render/IRenderPipeline.h` carry the port's own types
  (`render/rp_types.h`: light, material, render-state id, window handle and image info, with
  the D3D layouts) instead of the D3D ones, so RPGPU includes no Direct3D header. Every other
  module still sees the D3D types under the same names; RP8 and RP9 assert that the layouts
  match.
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

## Status: 2D and lit 3D draws

RPGPU opens the device and presents the frames it records. It draws the 2D draws (pre-transformed
vertices: the menus, the HUD) and the 3D draws (transformed vertices, through world, view and
projection), with Direct3D 8's per-vertex lighting, fog, texture stage 0, mip maps, culling and
the alpha test; see "Drawing" and "Lighting and fog". The texture stages above 0 are not drawn
yet.

- **State the game reads back is kept:** pipeline state, render and texture-stage state, the
  transforms and the viewport. The transforms follow RP8's stores ("Transforms").
- **The stream selection is RP8's:** `SelectVb` and `SelectIb` (with its base vertex) choose
  what `IRPDraw::DrawIndexedPrimitive` draws from; the user-pointer draws clear the selection,
  and the vertex-buffer draws leave theirs selected, `DrawIndexedPrimitiveVb` with the scratch
  index buffer, into which it copies its indices as RP8 does.
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
- **The draw calls** record the frame and draw it at `SwapBuffers` ("Drawing").
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

## Shaders

RPGPU replaces Direct3D 8's fixed-function pipeline with shaders (plan step 5.3). They are
written in HLSL in `src/RPGPU/shaders/`: `name.vert.hlsl` for vertex shaders, `name.frag.hlsl`
for fragment shaders. They follow SDL_GPU's binding conventions:

| Resource | Register |
|---|---|
| vertex uniforms | `b<n>, space1` |
| fragment textures and samplers | `t<n>` / `s<n>, space2` |
| fragment uniforms | `b<n>, space3` |

They are compiled offline, not at run time, and the bytecode is committed:
- `tools/rpgpu_shaders.ps1` compiles each shader with SDL_shadercross into SPIR-V (Vulkan),
  DXIL (Direct3D 12) and MSL (Metal).
- The results go into one generated file, `RpGpuShaderBytecode.generated.inc`.
  `FindRpGpuShader(name)` (`RpGpuShaders.h`) looks a shader up by its file name without
  `.hlsl`.
- shadercross comes from the `shader-tools` feature of `src/vcpkg.json`. No build installs
  it; only the "RPGPU shaders" CI job does.
- That job compiles everything again and fails when the committed file differs. It uploads
  the fresh file as the `rpgpu-shader-bytecode` artifact, ready to commit.

The shaders so far:
- `rpgpu_fixed.vert` is Direct3D 8's vertex stage. It maps `D3DFVF_XYZRHW` positions from
  pixels to clip space, and other positions through world * view * projection; both land on
  Direct3D 8's pixel centres (integer coordinates, half a pixel from today's). It lights
  transformed vertices and computes vertex fog ("Lighting and fog").
- `rpgpu_stage0.frag` does texture stage 0 (disable, select texture, select diffuse,
  modulate), the specular add, fog and the alpha test, in Direct3D's order.
- Their uniform blocks are `FRpGpuTransformUniforms`, `FRpGpuLightingUniforms` and
  `FRpGpuStageUniforms` (`backend/RpGpuBackend.h`): float4s and 4x4 matrices only, so the C++
  and HLSL layouts agree.

## Transforms

RPGPU keeps the transforms as RP8 does (`RpGpuState.cpp`, after `RP8.cpp`'s `SetWorld` to
`SetLookat`):
- The game's 12-float transforms (rotation rows, then translation) are stored as Direct3D's
  4x4 matrices, for row vectors.
- `SetView` stores the view with RP8's z flip (the third rotation row and the z translation
  negated), which takes the game's right-handed space to Direct3D's left-handed one. `GetView`
  undoes it.
- `SetModelview` puts the model-view into the world, flipped when `VIEWSPACE_LIGHTS` is on, and
  makes the view the identity (flipped when it is off). It sets the view only when no
  `SetModelview` or `SetLookat` has since the last draw that committed a changed view: RP8's
  apply marker.
- `SetLookat` builds the camera transform from eye, target and up, and stores it as the world
  (with `VIEWSPACE_LIGHTS`) or the view.
- `SetOrtho` and `SetPerspective` build RP8's matrices, with its double intermediates.
- `CreateBuffers` resets all three to RP8's identity transforms.
- RP8 multiplies through the 3DMathEngine; RPGPU does the same products itself, with the same
  double sums, so `GetModelview` can differ from RP8's in the last bit.
- `SetWorldN` (the vertex-blend matrices) and the texture transforms are not kept yet.

## Lighting and fog

**Lights** keep RP8's light manager (`RpGpuLighting.cpp`, after `FRp8LightManager`):
- `AddLight` makes a record whose data is the caller's own light (the handle), read again at
  every flush that finds it dirty (`UpdateLight`). `SetLight` writes the light at once and keeps
  no data.
- `SetLightEnable` only records the request; `GetLightEnable` answers the request, not the
  committed state. The queue goes out at the next draw (or `VerifyState`), as RP8's
  `FlushDrawState` sends it; `RemoveLight` and `DestroyLight` disable, flush and drop.
- Light indices come from RP8's pool (freed indices first). What the flushes leave at each index
  is Direct3D's light table; `GetLight` reads it with the handle as the index, as RP8 passes it
  to Direct3D. `CreateBuffers` empties the table and queues every light again (RP8's
  `Reset`).
- A draw is lit by the enabled entries in index order, at most 8. Enabling an index that has
  no light installs Direct3D's default one (white, directional, along +z).
- Every light call needs RP8's device lifetime (from `CreateBuffers` to shutdown) and fails
  outside it, as RP8's do.
- `SetMaterial` and `GetMaterial` work within a buffer session, as in RP8; a new device's
  material is zero.

**Lighting** (`LIGHTING` on, transformed vertices) is Direct3D 8's per-vertex model:
- emissive + ambient * `AMBIENT` + for each light, attenuation * spot * (ambient * light
  ambient + diffuse * light diffuse * N.L); specular with the half vector, only where N.L > 0;
  colours clamped to 0..1, alpha from the diffuse material;
- the material colours come from `DIFFUSE`/`AMBIENT`/`SPECULAR`/`EMISSIVEMATERIALSOURCE` when
  `COLORVERTEX` is on and the vertex has that colour;
- point and spot lights reach `Range` and no further; spots fall off between theta and phi with
  `Falloff`;
- `NORMALIZENORMALS` and `LOCALVIEWER` as Direct3D (a non-local viewer looks along camera z).
- It runs in world space: the view is rigid (RP8's z flip included), so distances and angles are
  Direct3D's camera-space ones. Normals go through the inverse transpose of the world.

**Fog** (`FOGENABLE`):
- `FOGTABLEMODE` set: per pixel, on w when the projection is not affine and on the depth-buffer
  z when it is (Direct3D's rule). `FOGSTART` and `FOGEND` are in those units.
- Otherwise `FOGVERTEXMODE`, per vertex, on camera-space z (or the distance with
  `RANGEFOGENABLE`). With neither, and for pre-transformed vertices, the specular alpha is the
  fog value.
- Exponential, squared exponential and linear, blending the colour (not alpha) towards
  `FOGCOLOR`. `SPECULARENABLE` adds the specular colour before fog.

## Drawing

RPGPU records each frame and draws it at `SwapBuffers`, which is plan step 5.3.

**Recording** (`RpGpuDraw.cpp`):
- **Draw calls:** `DrawPrimitive`, `DrawIndexedPrimitiveUp`, `DrawPrimitiveVb`,
  `DrawIndexedPrimitiveVb` and `IRPDraw::DrawIndexedPrimitive` convert the game's vertices
  into `FRpGpuVertex` (position, diffuse, the texture set stage 0's `TEXCOORDINDEX` names).
  The position formats are read as Direct3D 8 lays them out: XYZ, XYZRHW, and XYZB1 to
  XYZB5, whose blend weights are skipped. A format whose layout is longer than RP8's stride
  (`Rp8FvfStride`, which RP8 hands to Direct3D) is not drawn: XYZB2, XYZB3, XYZB5 and any with
  a point size. Each draw records one command with the state of the moment:
  - blending, the depth test, the colour write mask, `CULLMODE`, `FILLMODE` and `ZBIAS`;
  - texture stage 0's operations, sampler (with `MIPFILTER` and `MAXMIPLEVEL`) and texture;
  - the alpha test and the viewport;
  - for transformed vertices, world * view * projection and the depth range (`SetDepthRange`).
    Pre-transformed vertices keep their z, as in Direct3D.
- **Indexed draws** copy only the vertices their indices reach, and rebase the indices onto
  them. `MinIndex` and `NumVertices` are Direct3D's hints and not used; an index past the
  vertices given (or past the vertex buffer) drops the draw.
- **Clears:** `ClearBuffers` records a clear in order with the draws.
- **Triangle fans** become indexed triangle lists, since SDL_GPU has none.
- **Not drawn yet:** points.
- **Textures:** a texture with a changed level (`SetTextureLevelData`, `UnlockTexture`) goes to
  the backend when it is next drawn, every level of it, as a new image version: draws recorded
  before the change keep the image they saw. Replaced versions, and the image of a texture
  destroyed during the frame, are released only after `SwapBuffers` has presented the frame.
  After a new device every texture goes up again.
  - DXT textures go up as their blocks (BC1, BC2, BC3), as Direct3D 8 hardware samples them,
    when the device samples those formats and level 0 is a whole number of 4x4 blocks
    (Direct3D 12 requires it). Otherwise, and for every other format, TexConvert converts each
    level to B, G, R, A bytes. DXT2 and DXT4 are sampled as their stored (premultiplied)
    colours, as hardware did; TexConvert's decode divides by alpha instead (texconvert.md).
  - Levels the game never filled (`TEXTURE_LOD` off) stay zero, as RP8's managed textures
    start; with `MIPFILTER` none only level 0 is sampled.
  - A cube map goes up as its first face, one level, until cube maps are drawn.

**The SDL backend's `Present`:**
1. One copy pass uploads the pending textures and the frame's vertices and indices, through
   one transfer buffer.
2. The commands replay on the swap-chain image and a depth buffer, in one render pass per
   clear. The first pass clears colour and depth.
3. Pipelines are cached per `FRpGpuPipelineKey`, and samplers per `FRpGpuSamplerKey`.
   - `CULLMODE` culls by the winding on screen, pre-transformed triangles included, as Direct3D
     8 does. RP8's default is 1 (none).
   - `FILLMODE` 2 draws wireframe; points (1) draw solid.
   - `ZBIAS` has no unit in Direct3D 8: each driver picked its own. RPGPU moves depth by one
     16-bit depth step (1 / 65535 of the range) per `ZBIAS` step, towards the viewer.
4. A draw whose texture has no GPU copy samples a 1x1 white texture with the texture flag
   off.

**Known gaps** for the later parts of step 5.3:
- clear rectangles (the whole target is cleared);
- texture stages above 0, texture transforms and texture-coordinate generation
  (`TEXCOORDINDEX`'s high bits);
- vertex blending (`SetWorldN`, `VERTEXBLEND`: Deformable2's skinned meshes draw unblended);
- `MIPMAPLODBIAS` (the game does not set it) and cube maps;
- every draw's vertices are copied into the frame; vertex and index buffers that stay on the
  GPU are plan step 5.4;
- the Direct3D blend factors SDL_GPU lacks (none of the 2D draws use them);
- a border colour, so `BORDER` addressing clamps instead.

