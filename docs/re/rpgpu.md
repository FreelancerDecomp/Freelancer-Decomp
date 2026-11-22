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

- `FreelancerRpGpu` turns RPGPU on: `1` by default on x64, whose client has no other renderer,
  `0` on Win32, where RP8 is the renderer. Off, `RPGPU.dll` is not built. Build it on Win32 with
  `-DFreelancerRpGpu=1`.
- SDL3 comes from vcpkg in manifest mode. All port-only modules share one manifest,
  `src/vcpkg.json`, with one `builtin-baseline` pinned to a vcpkg release; each module is a
  feature of it (`rpgpu`), installed only when the module is on.
- The root `CMakeLists.txt` holds the vcpkg wiring: it turns on the manifest features of the
  modules that are on and installs them while it configures, into the build tree's
  `vcpkg_installed/<triplet>/` (`x86-windows`, `x64-windows`). Only port-only modules link
  packages (`fl_vcpkg_package`, which refuses a retail module), so no retail module sees SDL.
  `SDL3.dll` is copied next to `RPGPU.dll`.
- vcpkg is found through `VCPKG_ROOT` (Visual Studio's developer prompt),
  `VCPKG_INSTALLATION_ROOT` (GitHub's Windows runners) or Visual Studio's own copy
  (override: `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`). When RPGPU is on
  and vcpkg is missing, the build skips it with a warning; with `-DFreelancerRequireVcpkg=ON` it
  fails instead.
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
    SDL_GPU device, claims the window, draws each frame into its own colour target, blits that
    to the swap-chain image and presents. SDL picks the GPU API: Direct3D 12, Vulkan or Metal.
    It reads frames back by downloading the colour target ("Drawing").
  - `RpGpuNullBackend.cpp` is the Null backend (`NULL_DEVICE`). Its frame is the clear colour
    at the size `CreateBuffers` asked for, and it reads that back.

## Status: 2D and lit 3D draws

RPGPU opens the device and presents the frames it records. It draws the 2D draws (pre-transformed
vertices: the menus, the HUD) and the 3D draws (transformed vertices, through world, view and
projection), with Direct3D 8's per-vertex lighting, fog, four texture stages, mip maps, culling
and the alpha test; see "Drawing", "Lighting and fog" and "Texture stages".

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
- **Abilities** describe a modern GPU, but report no gamma ramp (SDL3 has none).
- **`LoadCubemap`** follows RP8's: -1 outside the device's lifetime, -7 with the cube-map
  ability (8) off, before the file system is touched; then the file is mapped through the
  `IFileSystem` as RP8 maps it (-8 when it cannot be opened, -6 when it cannot be mapped) and
  loaded as D3DX's `D3DXCreateCubeTextureFromFileInMemory` loads a DDS cube map: in the file's
  format, with the full mip chain, the levels the file lacks made from the ones above
  (TexConvert's box filter, within a tolerance of D3DX's). A file that is not a square DDS cube
  map in a format RPGPU keeps (P8 is not) fails with `D3DXERR_INVALIDDATA`. As in RP8, a lock
  or level upload on a cube names a face, and `GetTextureDim` reports 6 levels.
- **`PrintScreen`** answers RP8's argument checks and writes a 24-bit BMP (`.BMP`, `.DIB`) of
  the backend's last frame. RP8 saves through D3DX in seven formats; RPGPU fails the other
  five. The SDL backend downloads the frame from its colour target (and waits for the GPU to
  do so); the Null backend hands out its clear colour.
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
- `rpgpu_stages.frag` does the texture stages ("Texture stages"), the specular add, fog and
  the alpha test, in Direct3D's order.
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
- `SetWorldN` keeps world matrices 0..255 for vertex blending; a new device starts them as the
  identity. RP8 sends it straight to Direct3D, past its transform cache, so index 0 replaces the
  matrix drawn with until the next store into the world, while `GetWorld` and `GetModelview`
  go on reading `SetWorld`'s, as RP8's cache does. Direct3D refuses an index past 255.
- **Vertex blending** (`VERTEXBLEND` 1..3 weights, or 256, `0WEIGHTS`: each vertex on its
  first indexed matrix; `INDEXEDVERTEXBLENDENABLE`), which
  Deformable2's skinned meshes use: the vertex's first weights from its XYZB position, the last
  one 1 minus their sum, each on a world matrix (the `D3DFVF_LASTBETA_UBYTE4` indices, or 0..3).
  Normals go through the matrices' 3x3 parts, as for rigid bones; Direct3D uses their inverse
  transposes, which differ only for bones scaled unevenly. The matrices go up as one
  block per set of values in a frame. Tweening (`VERTEXBLEND` 255) is not drawn.

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
  `Reset`), with RP8's exception: the flush count restarts at 0 and the records keep their
  queue markers, so a light last queued in generation 0 is not queued again until it changes.
- The flush count is unsigned and wraps after 2^32 draws, where RP8's signed one overflows. A
  record whose marker happens to equal the count after a wrap is skipped once, as on RP8.
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
- Otherwise `FOGVERTEXMODE`, per vertex, on the camera-space depth (or the distance with
  `RANGEFOGENABLE`). With neither, and for pre-transformed vertices, the vertex's specular
  alpha is the fog value, lit or not.
- Exponential, squared exponential and linear, blending the colour (not alpha) towards
  `FOGCOLOR`. `SPECULARENABLE` adds the specular colour before fog.

## Static vertex and index buffers

A vertex buffer that is not a ring (`CreateVb`'s buffers; the pools of `LockVertexMemory` are
rings) is converted to `FRpGpuVertex` once and kept on the GPU as a vertex block
(`IRpGpuBackend::UploadVertexBlock`). Draws from it (`DrawPrimitiveVb`,
`DrawIndexedPrimitiveVb`, `IRPDraw::DrawIndexedPrimitive`) name the block and start at their
base vertex in it, so their indices go up as they are and no vertices are copied.
- A block is made per texture-coordinate signature: which set each stage's `TEXCOORDINDEX`
  names, since the conversion picks each stage's set.
- Only a buffer that has stayed as it is gets a block: while it is locked, and in any frame it
  was written in, it is drawn from the frame's vertices. So a draw never reads half-written
  vertices into a block, and a buffer rewritten every frame is not converted whole per draw.
- Any write retires the buffer's blocks: `LockVb` (and `CopyVertices`, which locks),
  `ResizeVb`, `DestroyVb`. Retired blocks are released after the frame that last used them, as
  the texture versions are.
- `CreateBuffers` retires every block and texture version, so a kept device does not hold on to
  copies RPGPU makes again.
- A block the device cannot make is dropped, and its draws are skipped; a block RPGPU cannot
  convert (out of memory) falls back to copying the draw's vertices.
- **Index buffers** (`CreateIb`'s, not the scratch ring) get a block the same way, and
  `IRPDraw::DrawIndexedPrimitive` reads it when its vertices come from a block too: the indices
  are drawn where they are, from `StartIndex`. Fans, which become generated lists, and draws
  whose vertices go through the frame copy their indices as before. `LockIb` (and
  `CopyIndices`), `UnlockIb`, `ResizeIb` and `DestroyIb` retire the block.

## Texture stages

RPGPU draws four texture stages and reports four as the device's blend-stage count (pipeline
state 6, which RP8 fills from the device's caps): a Direct3D 8 device with four stages, which
the game adapts to.
- **The cascade** (`rpgpu_stages.frag`) runs from stage 0 to the first stage whose `COLOROP`
  is `DISABLE`. Every `D3DTEXTUREOP` is done except the bump-mapping ones (`BUMPENVMAP`,
  `BUMPENVMAPLUMINANCE`) and `PREMODULATE`, which pass the current colour on. `DOTPRODUCT3`
  writes alpha too. Each stage's result is clamped to 0..1.
- **Arguments:** diffuse, current (the diffuse colour at stage 0), texture, `TEXTUREFACTOR`,
  specular and temp, with the complement and alpha-replicate modifiers. `COLORARG0` /
  `ALPHAARG0` feed `MULTIPLYADD` and `LERP`; `RESULTARG` writes temp instead of current.
- `ALPHAOP` `DISABLE` under an enabled `COLOROP` passes the current alpha on.
- With no texture bound (or none on the GPU yet), the texture argument reads the diffuse colour,
  RPGPU's rule since the 2D draws.
- **Coordinates** (`rpgpu_fixed.vert`): each stage reads the set its `TEXCOORDINDEX` names, or
  generates camera-space normals, positions or reflection vectors (`TEXCOORDINDEX`'s high bits).
  Sets are kept to their first two components.
- **Texture transforms:** `SetTextureStageTransform` stores the matrix transposed, as RP8 does,
  and `TEXTURETRANSFORMFLAGS`' count turns it on. A set is padded as Direct3D pads it ((u, v)
  becomes (u, v, 1, 0)); `PROJECTED` divides by the last transformed component.
  Pre-transformed vertices take neither generation nor transforms.
- `SetTextureStageTexture` is ignored, and `GetTextureStageTexture` answers null, while
  pipeline state `TEXTURE` is 0, as in RP8. `CreateBuffers` and `ResetRenderStatesToDefaults`
  unbind every stage and reset the transforms to the identity (RP8's
  `ApplyDefaultTextureStages`).
- Reflection coordinates reflect the camera-to-vertex direction, or camera-space +z when
  `LOCALVIEWER` is off.

## Drawing

RPGPU records each frame and draws it at `SwapBuffers`, which is plan step 5.3.

**Recording** (`RpGpuDraw.cpp`):
- **Draw calls:** `DrawPrimitive`, `DrawIndexedPrimitiveUp`, `DrawPrimitiveVb`,
  `DrawIndexedPrimitiveVb` and `IRPDraw::DrawIndexedPrimitive` convert the game's vertices
  into `FRpGpuVertex` (position, diffuse, specular, normal, and for each stage the texture set
  its `TEXCOORDINDEX` names, as two components).
  The position formats are read as Direct3D 8 lays them out: XYZ, XYZRHW, and XYZB1 to
  XYZB5, whose blend weights are skipped. A format whose layout is longer than RP8's stride
  (`Rp8FvfStride`, which RP8 hands to Direct3D) is not drawn: XYZB2, XYZB3, XYZB5 and any with
  a point size. Each draw records one command with the state of the moment:
  - blending, the depth test, the colour write mask, `CULLMODE`, `FILLMODE` and `ZBIAS`;
  - each texture stage's operations, sampler (with `MIPFILTER`, `MAXMIPLEVEL`,
    `MAXANISOTROPY` and `MIPMAPLODBIAS`), texture and coordinate handling;
  - the alpha test and the viewport;
  - for transformed vertices, world * view * projection and the depth range (`SetDepthRange`).
    Pre-transformed vertices keep their z, as in Direct3D.
- **Indexed draws** copy only the vertices their indices reach, and rebase the indices onto
  them. `MinIndex` and `NumVertices` are Direct3D's hints and not used; an index past the
  vertices given (or past the vertex buffer) drops the draw.
- **Clears:** `ClearBuffers` records a clear in order with the draws. As Direct3D 8's `Clear`,
  it covers the viewport, or the one rectangle RP8 passes, cut to the viewport. A clear of the
  whole target is the render pass's clear; any other is a quad that writes the clear colour
  and depth (no depth test, blending or write mask). Stencil is not kept.
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
  - A cube map goes up as a cube texture, all six faces with their chains. The stage samples it
    with its first three coordinates (a reflection vector or a transformed one), from the
    cube slot the fragment shader has beside each stage's 2D slot.

**The SDL backend's `Present`:**
1. One copy pass uploads the pending textures and the frame's vertices and indices, through
   one transfer buffer.
2. The commands replay on RPGPU's own colour target (B8G8R8A8, the swap chain's size) and a
   depth buffer, in one render pass per clear. The first pass clears colour and depth. The
   target is then blitted to the swap-chain image, pixel for pixel, so the last frame stays
   readable for `PrintScreen`.
3. Pipelines are cached per `FRpGpuPipelineKey`, and samplers per `FRpGpuSamplerKey`.
   - `CULLMODE` culls by the winding on screen, pre-transformed triangles included, as Direct3D
     8 does. RP8's default is 1 (none).
   - `FILLMODE` 2 draws wireframe; points (1) draw solid.
   - `ZBIAS` has no unit in Direct3D 8: each driver picked its own. RPGPU moves depth by one
     16-bit depth step (1 / 65535 of the range) per `ZBIAS` step, towards the viewer.
4. A draw whose texture has no GPU copy samples a 1x1 white texture with the texture flag
   off.

**Known gaps:**
- stages past the fourth, the bump-mapping ops, three- and four-component texture sets (so a
  cube map read with a vertex's own three-component coordinates gets only two of them);
- tweening (`VERTEXBLEND` 255);
- the ring buffers (the vertex pools, the scratch index ring) and user-pointer draws copy their
  vertices and indices into each frame (the static buffers stay on the GPU, "Static vertex and
  index buffers");
- the Direct3D blend factors SDL_GPU lacks (none of the 2D draws use them);
- a border colour, so `BORDER` addressing clamps instead.

