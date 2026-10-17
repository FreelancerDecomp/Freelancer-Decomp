# Render stack: RP8, RendComp, ximage

This page describes how the graphics stack of the Freelancer client is organised in our port: the Direct3D 8
pipeline (`RP8.dll`), the mesh and renderer layer (`RendComp.dll`), the large-image tiler (`ximage.dll`)
and how they connect to materials. It is for whoever is about to change rendering or needs to find which
DLL is responsible for a particular stage of the frame. Materials, textures and their factories are described in
[shading.md](shading.md); effects and particles in [alchemy.md](alchemy.md).

The source of behaviour is the retail DLL listings (`EXE/Disasm/*.dll.txt`); function statuses, slot numbers and
addresses are in decomp-tracker, and the slot headers live in `src/include/render/`. The port's debt relative to
retail is kept in [deviations.md](deviations.md). The D3D9 / volumetric nebula branch (RP9) is suspended and is not
described here: everything below applies to the D3D8 pipeline that is actually built from `src/`.

## Composition

| DLL | Sources | What it does |
|-----|---------|--------------|
| `RP8.dll` | `src/RP8/` | Direct3D 8 wrapper: device, textures, VB/IB, states, lights, draw. The `DirectX8` factory |
| `Shading.dll` | `src/Shading/` | Texture and material libraries, batcher, material animation — see [shading.md](shading.md) |
| `RendComp.dll` | `src/RendComp/` | `RenderManager` (the `IRenderer` interface), mesh plugins `VMesh` / `TriMesh` / `SphereMesh`, `VMeshLibrary`, `VMeshWire` |
| `ximage.dll` | `src/ximage/` | `BigImage`: cuts a raster into texture tiles and hands them to the client callback |
| `flmaterials.dll` | `src/flmaterials/` | Freelancer game materials and `FLAppearance` — see [shading.md](shading.md) |

All five are DACOM plugins without named exports: `DllMain` only registers factories
(`RegisterComponent`), and objects are created later through `CreateInstance` by name. They do not link
against each other; the connection goes through DACOM `QueryInterface` by string IIDs (`Iids::*` from
`src/include/dacom_iids.h`). Public interfaces are declared once in `src/include/render/`
(`IRenderPipeline.h`, `shading.h`, `rendcomp.h`, `IMaterial.h`, `IFLAppearance.h`, `mesh_iface.h`) and
in `src/include/ximage.h`.

## How the stack is assembled at startup

`bin/DACOM.INI` loads `[Libraries]` in this order: `ReadFile`, `x86math`, `EngBase`, `system`, `RP8`,
`SoundStreamer`, `SoundManager`, `Deformable2`, `Thorn`, `Shading`, `RendComp`, `alchemy`, `ximage`,
`flmaterials` (all from `bin\`). Then `DALib::Startup` creates two aggregates, and they read their INI sections:

```
SystemContainer  ([System])
  VMeshLibrary                      RendComp   IVMeshLibrary
  DirectX8                          RP8        IRenderPipeline8B, IVertexBufferManager, ...
  TextureLibrary                    Shading    ITextureLibrary2
  MaterialAnimation                 Shading    IMatAnimLibrary
  MaterialBatcher                   Shading    IMaterialBatcher2 + nested IMaterialLibrary
  Alchemy, FxRuntime                alchemy

Engine           ([Engine])
  RenderManager   ([RenderManager] -> VMesh, TriMesh, SphereMesh)      RendComp   IRenderer2
  FLAppearance                                                         flmaterials
  VMeshWire                                                            RendComp
  Animation, Hardpoint, Deformable                                     EngBase / Deformable2

On demand (not in [System]/[Engine]):
  BigImage         ximage       created by the client's Intro / RichDisplay
  <material type>  Shading / flmaterials   creates a MaterialLibrary per [MaterialMap]
```

On the client there is no separate `MaterialLibrary` entry in `[System]`: the material library lives inside
`MaterialBatcher` (QI `IMaterialLibrary` returns the nested object). The server INI lists
`MaterialLibrary` separately.

Who requests which interfaces from whom in `Initialize`:

| Component | Takes |
|-----------|-------|
| `DALib` | `IRenderPipeline8B` (then, from the pipeline, `IRPDraw`, `IRPIndexBuffer`, `IRPVertexBuffer`) and `IVertexBufferManager` from the container |
| `RenderManager` | from the parent `IRenderPipeline8B`, `IVertexBufferManager`, `IMaterialLibrary`; from the pipeline `IRPVertexBuffer`, `IRPIndexBuffer`, `IRPDraw`; from itself `IEngine`. A miss on `IVertexBufferManager`, VB/IB/Draw or `IEngine` makes `Initialize` return `-1`; without `IMaterialLibrary` it only writes `FDUMP` |
| Materials (`IMaterial::Initialize`) | `ITextureLibraryA`, `IRenderPipeline8B`, `IRPDraw`, `IVertexBufferManager` |
| `FLAppearance` | `IMaterialLibrary`, `IRenderPipeline8B`, `IVertexBufferManager`, `ITextureLibraryA`, `IRenderer` |
| `BigImage` | from the pipeline in the descriptor: `query_device_ability`, `ITextureLibraryA` (the request goes to SystemContainer and lands in Shading) |

## RP8: the Direct3D 8 pipeline

A single `DirectX8` object provides eight interfaces at once (each is its own sub-object with its own vtable):
`IRenderPipeline8B`, `IVertexBufferManager`, `IRPDraw`, `IRPIndexBuffer`, `IRPVertexBuffer`,
`IGammaControl`, `IRPTexture`, `IAggregateComponent`. The main interface has 77 slots; slot names,
signatures and retail addresses are in `src/include/render/IRenderPipeline.h`, `slots.h` and in the
decomp-tracker cards. A method name is known only where the body of the retail function contains an `FDUMP` string with it.

Device lifecycle:

- `Startup` (slot 3, calls `engine_init`) creates `IDirect3D8`, enumerates adapters, reads the
  `[RenderPipeline]` section and MGSDB, but **does not create the device**.
- `CreateBuffers` (slot 12) is the only place where `IDirect3DDevice8` is created/reset.
  It is called by the client's mode-selection code (`RenderMgrCreateBuffers`, retail `FUN_004249c0`), not by
  DALib. Device readiness is a bit in the pipeline state; methods that need the device check
  it under three different conditions, each with its own `FDUMP` text (outside of startup / create_buffers /
  device lifetime).
- Frame: `begin_scene` (57) / `end_scene` (58) are called by the client; `swap_buffers` does `Present`.
- Slot 4 shuts the pipeline down (DALib `Shutdown`), slot 14 releases GPU resources without touching the device.

The device is configured by the `[RenderPipeline]` keys (`DeviceId`, `DeviceType`, vertex processing
flags, `FPU_PRESERVE`, `MULTITHREADED`, default state keys) and the MGSDB file
(`FLConfigDatabase.txt`): a list of "bad" drivers/cards by which the pipeline sets workaround flags
(`FL_BAD_*`, `NoAlphaTest`, `DefaultBltNotFlip`, etc.) and the array of device capabilities.
Capabilities are read via `query_device_ability(index)`: ximage uses indices 5/6/7
(`TEXTURE_SQUARE_ONLY`, `TEXTURE_MAX_WIDTH`, `TEXTURE_MAX_HEIGHT`), cubemap loading checks
`TEXTURE_CUBEMAPS`.

What to know before editing RP8 code:

- **Textures.** A texture handle is an 8-byte wrapper `{IDirect3DTexture8* | cube, kind}`. File loading
  (`IRPTexture`) goes through statically linked D3DX 8.1 (`d3dx8.lib` from `dx8\lib`);
  retail also linked D3DX statically (retail has no `d3dx8.dll` import). The file bytes are read
  through the supplied `IFileSystem` (map/view), not copied.
- **States.** Render/texture-stage state requests are deferred and merged in `FlushDrawState`
  before every draw (as in retail); `get_pipeline_state` reads a separate mirror, not the queue.
  The light manager (`StateCache`) repeats retail: light entries, a pool of 64 indices, deferred
  `LightEnable`.
- **Vertex buffers.** `create_vb` with the pool flag returns buffers by FVF from the shared pool of
  `IVertexBufferManager` (a `DISCARD` / `NOOVERWRITE` ring); the index buffer for
  `draw_indexed_primitive_vb` is an internal scratch IB, not the one `create_ib` creates.
- **Draw.** Slots 69–72 (`draw_primitive`, `draw_indexed_primitive`, `draw_*_vb`) and `IRPDraw` slot 3
  flush the deferred state before every draw (`FlushDrawState`: streams, matrices, lights). The matrices
  (`set_world`, `set_view`, `set_modelview`, `set_projection`) accept 12/16 floats; `set_view` /
  `set_modelview` apply ZFlip.

## RendComp: renderer and meshes

`RenderManager` implements `IRenderer` / `IRenderer2` (23 slots, one vtable at object offset `+4`)
and owns the list of `IRenderComponent` plugins read from `[RenderManager]` in INI order
(**VMesh, TriMesh, SphereMesh**). For a new archetype the engine asks the plugins in turn (slot 3
of each is a "claim"); the first that does not return `-1` gets the archetype and creates instances. The
LOD tables `Switch2` / `MultiLevel` / `Level<N>` in the UTF are supported as well.

Mesh drawing is a single chain: `IRenderer` slot 7 → `IRenderComponent` slot 17 (bind) and 16 (submit) →
`IMaterial` slots **8 (bind) → 10 (submit primitives) → 9 (teardown)**. Before submit the plugin sets the
matrices: if pipeline state 7 (`VIEWSPACE_LIGHTS`) is zero — `set_world(world)`, otherwise it
computes `view × world` via `3DMathEngine` (`x86math.dll`) and calls `set_modelview`. The material
is found by the CRC of its name in `IMaterialLibrary` (`FindMaterialByCrc`, without creating); on claim the plugin itself
loads `Texture library` / `Material library` from the same UTF.

Who draws GPU primitives:

- `VMesh` and `SphereMesh` do not draw primitives themselves: the material's submit slot (10) goes to pipeline slot 72
  (`draw_indexed_primitive_vb`) or, if the submit has the flag `0x100` set (`VMesh` adds `0x10C`),
  to `IRPDraw` slot 3.
- `IRPDraw` is called directly only by `VMeshWire` (wireframe/collision, primitive `2`) and the client callback of
  `BigImage` (screen tiles).
- The submit context is the `FMaterialSubmitContext` structure (0x1C bytes); its fields (indices, VB listing,
  an 8-byte scratch with the index base) are declared in `shading.h` / `rendcomp.h` and filled by the plugins
  following the retail layout.

### Mesh formats read by RendComp

All are UTF nodes (`.cmp`, `.3db`, `.vms`), read through `IFileSystem` (ReadFile.dll).

**VMesh.** The `VMeshPart` node points to a `VMeshRef` record (0x3C bytes: start vertex/index,
vertex count, facegroup range, bounding data); the vertices and indices themselves live in `VMeshData` from
`VMeshLibrary` (a cache keyed by the CRC32 of the name). The `VMeshData` header: `+0x00` flags (bit 0 = "new format",
otherwise FDUMP "mesh in old format"), `+0x08` facegroup count, `+0x0A` index count, `+0x0C` FVF bit mask,
`+0x0E` vertex count; then facegroups of 0xC bytes (material CRC, vertex range, index length), `uint16`
indices, vertices. The layout of the FVF mask and the offset tables are in `src/include/render/rendcomp.h` and
`src/RendComp/VMeshLibrary.cpp`.

**TriMesh.** The `Mesh` node is mandatory. Inside `Geometry`: the pools `Points` (3 floats), `Vertex_normals`,
`UV0`/`UV1` (2 floats), `Color0` (ARGB) and the paired `*_indices` — `uint32` arrays of "corners" with a common count
(a corner = an index into each pool). `Face_groups`: `Count`, then `Group<N>`, each with `Material_name`
(CRC of the lower-cased name), `Tristrip_indices` (checked first) or `Face_indices`
(`uint16`, corner numbers), optionally `Edge_indices` / `Edge_angles`. UV animation — `UV0_anim_lookup`,
`UV0_frame_count`, `UV0_anim`, `UV0_fps`, `UV0_interpolate`. `[TriMesh] tristrips = 0` forces
strips to be converted into triangle lists. During reverse engineering no `Mesh`/`Geometry` nodes were found in the shipped `DATA/`, so
this branch has been checked only against the listing, not on real content. Vertices are drawn from the locked memory
of `IVertexBufferManager` (`CopyVertexData` every frame); TriMesh has no VB/IB of its own.

**SphereMesh.** The `Sphere` node with `Radius`, `Sides` (the number of `M<N>` materials), optional `M0..`; the mesh
is built at runtime (cube→sphere, six faces, the level of detail is limited). If `Sides > 6`, the second
loop draws the atmosphere with the `AtmosphereMaterial` material.

## ximage: BigImage

The module does not decode files (TGA/DDS is read by Shading, the decoded raster arrives ready). It
accepts a raster (a raw buffer or an `IBigImageSource`), cuts it into tiles by the device limits
(`query_device_ability` 6/7; without `TEXTURE_SQUARE_ONLY` the trailing tiles are rounded up to a power
of two), creates one texture per tile through the pipeline (`create_texture`, `set_texture_level_data`) and
stores UV quads (8 floats per tile). `Draw` computes UVs for a pair of rectangles and for each tile
calls the client cdecl callback (`FBigImageDrawTile`: handle, packed ARGB, 4 screen coordinates with a
shift of `-0.5`, 8 UVs). The drawing itself is done by the client's 2D batcher, not by ximage.

The creation descriptor is `FBigImageDesc` (0x2C bytes, **not** the common 0x14-byte `AGGDESC`), see
`src/include/ximage.h`. `IBigImage2`: 12 slots, `IBigImage` is the same object. Consumers in the client:
`Intro` (logo) and `RichDisplay`.

## Known limitations

- The per-function outcome (verified / simplified / partial) is in decomp-tracker; open deviations from
  retail are in [deviations.md](deviations.md) (the RP8, RendComp, Shading sections).
- The repository has no pixel comparison with retail: the reference GPU captures of the menu (DXVK `.rdc`) live in the gitignored `EXE\`, and
  the frame comparison is done by the owner.
- `jflp.dll` and `HudShift.dll` must not be added to `bin/DACOM.INI`: they patch fixed addresses
  (including inside RendComp) and crash a non-retail exe.
- Volumetric nebulae and D3D9 (the RP9 branch) are absent from this tree.
