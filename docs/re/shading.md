# Shading: textures, materials, FLAppearance

This page covers the two DLLs that together provide materials to the client: `Shading.dll` (texture and
material libraries, the batcher, material animation, base material types) and `flmaterials.dll` (the Freelancer
game materials and the `FLAppearance` component). It is meant to explain where a given
material comes from, what the `IMaterial` contract is, and where the data lives. For the overall stack layout and the pipeline see
[render.md](render.md). Function statuses, slots and addresses are in decomp-tracker; open divergences from retail are
in [deviations.md](deviations.md).

Sources: `src/Shading/`, `src/flmaterials/`; public interfaces are `src/include/render/IMaterial.h`,
`IBatchedMaterial.h`, `IFLAppearance.h`, `shading.h`, `factory_names.h`. `Deformable2.dll` implements
its own `EyeMaterial` of the same interface (`src/Deformable2/EyeMaterial.*`).

## Model: fixed-function pipeline, not shaders

`shading.dll` and `flmaterials.dll` contain no HLSL and no vertex/pixel shaders. On bind a material sets
Direct3D 8 states through the pipeline (`set_material`, `set_render_state`, `set_texture_stage_state`,
`set_texture_stage_texture`, `set_texture_stage_transform`) and on submit calls draw. The role of a "technique"
is played by the **material type**, the name of a registered DACOM factory (`DcDt`, `Nebula`, `NomadMaterial`, etc.).
Retail never expands the abbreviations in the names (`Dc`, `Dt`, `Ec`, `Et`, `Oc`, `Ot`, `Bt`); judging by the property
names, `Dc`/`Dt` are diffuse color/texture, `Ec`/`Et` are emissive, `Bt` is detail texture, and `Oc`/`Ot` are apparently
opacity. The suffix `Two` is a separate registered type (probably a two-sided variant).

## Where a material comes from

1. A mesh or room refers to a material by name (the CRC of the name is the key in `IMaterialLibrary`).
2. `IMaterialLibrary` loads the UTF node `Material library` (`Material count`, then child directory nodes;
   each has a `Type` field) through `IFileSystem` and creates the object via `CreateInstance` by the type name.
3. The type can be overridden by the `[MaterialMap]` section in `DACOM.INI`: lines `Type = Type` (renaming) and
   `name = <regex> = Type` (substitution by material name). For example `^nomad.*$` →
   `NomadMaterialNoBendy`, `^ui_.*` → `HUDIconMaterial`, `^sea_anim.*$` → `PlanetWaterMaterial`.
   The Shading and flmaterials factories register in one shared DACOM registry, so the map does not distinguish
   which DLL a type belongs to.
4. `[BatchedMaterials]` (`Type = .*OcOt`, `Name = ...`) holds the `MaterialBatcher` rules: matching materials
   are sent to the batcher instead of being drawn immediately.
5. `FLAppearance.Initialize` creates the *override materials* through the library — `ColorizeMaterial`,
   `WireMaterial`, `WireMaterial2`, `CloakMaterial`, `EnvMapMaterial`, `BackdropMaterial`,
   `StarsphereMaterial`, `ShadowMaterial`, `VisibleCounterMaterial` — and registers them by name CRC.
   The engine accesses them by index (`IFLAppearance::GetOverrideMaterial`).

## Factories

**Shading** (`RegisterComponent`, priority `0x80000000`): services `TextureLibrary`, `MaterialLibrary`,
`MaterialBatcher`, `MaterialAnimation`, `BatchedMaterial`; materials `DcDt`, `DcDtEc`, `DcDtOcOt`,
`DcDtEcOcOt`, their `…Two` variants, `DcDtEt`, `EcEt`, `EcEtTwo`, `AtmosphereMaterial`, `Nebula`,
`NebulaTwo`, `NullMaterial`, `LightMap`, `DetailMapMaterial`, `DetailMap2Dm1Msk2PassMaterial`,
`IllumDetailMapMaterial`, `Masked2DetailMapMaterial`, `BtDetailMapMaterial`, `BtDetailMapTwoMaterial`.
The names `DcDtEtTwo`, `EcEtOcOt`, `DcDtEcEt` have no factories; they are only remap sources in `[MaterialMap]`.

**flmaterials**: `NomadMaterial`, `NomadMaterialNoBendy`, `HUDIconMaterial`, `HUDAnimMaterial`,
`ColorizeMaterial`, `CloakMaterial`, `WireMaterial`, `WireMaterial2`, `ExclusionZoneMaterial`,
`GlassMaterial`, `HighGlassMaterial`, `GFGlassMaterial`, `EnvMapMaterial`, `BackdropMaterial`,
`StarsphereMaterial`, `ShadowMaterial`, `EngineGlowMaterial`, `VisibleCounterMaterial`,
`PlanetWaterMaterial` — 19 types — and the `FLAppearance` component (an engine aggregate, `[Engine]`).

Several names map to one class: Backdrop and Starsphere; Glass, HighGlass and GFGlass; Nomad and NomadNoBendy
(they differ only in a flag set by the initialization slot from the name in the descriptor). All material
factories require an `AGGDESC` of size `0x14` with the type name at `+4`; `FLAppearance` requires `0x18`.

## The `IMaterial` contract

The interface is declared once (`render/IMaterial.h`), 32 slots; it is implemented by Shading, flmaterials and
Deformable2, and called by RendComp, Alchemy and the client. The slot order is fixed by the retail tables; **slot
names are given by behaviour**, not from the original (retail does not publish them), and together with the signatures are kept in
the interface card in the tracker.

Main lifecycle: `Initialize(outer)` (slot 3: QI `ITextureLibraryA`, `IRenderPipeline8B`, `IRPDraw`,
`IVertexBufferManager` on the outer object) → `LoadFromFileSystem` (4, reads properties from UTF) → per frame
`Bind(8) → Submit(10) → Teardown(9)`. `ApplyVerify` (6) wraps bind and the pipeline state check
(`verify_state`). `Submit` takes a context, the primitive type, the vertex start and count, the index start and
length, and flags (7 arguments); the flag `0x100` sends drawing to `IRPDraw`, otherwise to the pipeline slot
`draw_indexed_primitive_vb`. The context is `FMaterialSubmitContext` (0x1C), the structure is declared in
`shading.h`.

**Calling conventions are mixed within one table.** Slots 3..19 (including bind/submit/teardown 8, 9, 10) are
`__stdcall` with an explicit `this` (DACOM style, `DACOM_API`), while slots 20..28 (class name, destructor,
`InitializeFromDescriptor`, `ApplyInitialRenderStates`, `Setup`, property range check, value
write, parameter table, property address) are `__thiscall` in retail (`this` in ECX), and the header
declares them exactly that way; 29..31 are the aggregate's internal QI/AddRef/Release. This was determined from the caller
side (`RET n` alone does not distinguish the conventions); a slot whose convention is not confirmed by a call must not be
guessed. `ResolveTextures(float Delta)` takes `float` by value.

**Parameter system.** Most materials describe their properties with a table of records of `0x4C` bytes
(`FShadingPropertyRecord`: four service dwords and three typed values of `0x14` bytes each —
current, default, saved). The value type tag: 1/8/9/10 — 32-bit word; 2 — float; 3 and 4 —
a vector of three / four floats; 5 and 7 — an owned string (process heap); 6 — `IDAComponent*` with
`AddRef`/`Release`. Slots 16/17/18/19 are the parameter count, the descriptor, and reading and writing a value by index;
27 returns the table and the record count, 25 the range check, 26 writes a value into a table record,
28 returns a field address. The parameter loader reads typed
UTF leaves (`Dc`, `Ac`, `Dt_name`, `Dt_flags`, `Opacity`, `Texture Name`, `NomadTexture Name` …) through
`IFileSystem::OpenByDesc` / `Read` / `CloseFile`; saving (`SaveProperties`) writes them back.

**The common bind prefix** in the DcDt, LightMap, Nebula, Atmosphere family: `set_material(this+0x20)`,
`COLORVERTEX` when vertex colors are present, binding of the stage-0 texture, `TEXCOORDINDEX` and U/V addressing from
the texture block. After that each family sets its own RS/TSS (blend mode, `LIGHTING`, a second stage
for detail maps). The concrete value tables are in the tracker cards.

## TextureLibrary

`TextureLibrary` (`ITextureLibraryA` + `ITextureLibrary2`) stores named textures and groups.
Loading from the UTF node `Texture library` (`.txm`): for each child entry it reads either `MIPS` (DDS,
DXT1–5 and uncompressed 16/24/32 bit), or `MIP0..MIP9` (TGA types 2/3/10/11, 8–32 bit), or `CUBE`
(cubemap, through the D3DX loader of `IRPTexture`). TGA levels are uploaded in the original pixel format (with a palette for 8-bit) through
`create_texture` / `set_texture_level_data`. Entries of one load form a **group** with an increasing
number; an entry can be animated (several frames, FPS, frame rectangles — "Texture count",
"Frame count", "Frame rects"), in which case the library keeps the playback state separately from the
canonical entry.

The pixel format is chosen by a table filled with 8 default rows and the
`TextureFormatMaps` section from the INI (FourCC/mask pairs → internal format id; `DXT1–5`, `DAOP`/`DAOT`/`DAAA`/
`DAAL`/`DAA1`/`DAA4`/`DAA8`). The keys `[TextureLibrary] TEXTURE_LOD_LOAD` and `TEXTURE_LOD_LOAD_MIN`
limit the loaded levels. Lookup is by name CRC; `FindTextureByCrc` returns an entry with a
reference, `GetTextureFrame` returns the GPU handle and the frame UVs.

## MaterialBatcher and MaterialAnimation

`MaterialBatcher` (`IMaterialBatcher2`) reads `BufferSize` and `Bin1SortOrder` and the `[BatchedMaterials]` rules
(regex on `Type` or `name`), accumulates matching materials in two "bins" and sorts them on flush
(`IMB_SO_PRIM_BACK_TO_FRONT`, etc.). An `IMaterialLibrary` lives inside it. `BatchedMaterial` is a wrapper
that redirects the `IMaterial` slots to an inner material.

`MaterialAnimation` (`IMatAnimLibrary`) loads UV animation data from `MaterialAnim` nodes
(`MACount`, `MAFlags`, `MADeltas`, `MAKeys`) and creates instances updated by the tick; a material with
active animation enables the texture coordinate transform (`TEXTURETRANSFORMFLAGS`).

## FLAppearance (`IFLAppearance`)

An engine aggregate: it holds an id→value map (EnvMap stores a pointer, Nomad a float), the directional light
of the scene, an array of override materials, and the draw methods for service objects: backdrop (`DrawBackdrop`), sky
(`DrawStarsphere`), shadows (`DrawFadingShadows`, `DrawFixedShadows`) and the visible geometry counter
(`CountVisibleGeometry`). The shadow texture is taken by the name `efxshadow` from `DATA/FX/efx.txm`.

## Known limitations

- Per-function status (verified / simplified / partial) is in decomp-tracker. What is visible at the architecture
  level: property schemas have not been ported for all material families (see the cards of slots 17/18/25/26/27),
  and the RS/TSS values per family were checked against the listing but have not been compared on a real render.
- Pixel comparison with retail and running the client is done by the project owner.
- Details and divergences are in the Shading and flmaterials sections of [deviations.md](deviations.md).
