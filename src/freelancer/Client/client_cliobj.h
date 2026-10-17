// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_cliobj.h"

// Client ICliObj companions + factory (retail exe DAT_006164a0 / FUN_0040ca50).
// Every retail Archetype::Cli* class (CliSolar, CliShip, CliAsteroid, CliDynamicAsteroid,
// CliEQEngine/Shield/ShieldGenerator/Thruster/Launcher/Gun/Light/Tractor/AttachedFX/
// InternalFX/RepairDroid/TradeLane, CliProj, CliMunition) has its own typed class in
// client_cliobj.cpp; the type -> class table is documented on FCliObjFactory::Create.
// Types with no class of their own get the bare CliRoot (0x54, FUN_004068d0).
// LOD list at +0x04 (FUN_004023f0). See docs/re/common_archetype.md.
//
// All of this is freelancer.exe code in retail; Common.dll only sees ICliObjFactory
// (src/include/common_cliobj.h). Nothing here is exported from any DLL.

struct IFileSystem;
struct FEffectArch; // vis_effect_arch.h
struct EquipDescVector; // common_goods.h

namespace Archetype
{
    // MaterialLib path-cache entry (Client\cmn\MaterialLib.cpp); layout in client_cliobj.cpp.
    struct FCliMaterialEntry;

    // VC6 std::vector empty-allocator shape: 1-byte cookie + 3 pad + begin/end/cap. The
    // companions' material-path list (ICliObj +0x20) and the list Gf::ObjectProp builds from a
    // PetalDB record (FUN_00446160) both have this shape and both reach FUN_0041af40.
    template <typename T>
    struct FCliVc6Vector
    {
        unsigned char Cookie;
        unsigned char Pad01[3];
        T* Begin;
        T* End;
        T* Capacity;
    };

    static_assert(sizeof(FCliVc6Vector<void*>) == 0x10, "VC6 vector header 0x10");

    // MaterialLib path pair (stride 8). First dword is char* path into FUN_0041aa50 /
    // MaterialLib::Load — not a CreateID hash (RE doc correction). Arg is FUN_0041aa50's third
    // argument (-1 from FUN_00446160).
    struct FCliMaterialPathPair
    {
        char const* Path;
        unsigned Arg;
    };

    static_assert(sizeof(FCliMaterialPathPair) == 8, "material path pair stride 8");

    // FUN_0040d830 insert-at-end for a material path list (stride 8).
    void PushMaterialPathPair(FCliVc6Vector<FCliMaterialPathPair>* Vec, char const* Path,
                              unsigned Arg);

    // FUN_0041aa50 (cdecl) — find-or-load the MaterialLib cache entry for Path, ++RefCount.
    // bForceApply = retail arg 2, Arg = retail arg 3 (-1 from FUN_00420dd0 / FUN_00420f30).
    FCliMaterialEntry* ResolveMaterialEntry(char const* Path, bool bForceApply, unsigned Arg);

    // FUN_0041ade0 (cdecl) — the same cache lookup for a file system the caller already
    // opened (FUN_0041c540 passes the .cmp's own): find-or-insert Path, and for a new entry
    // load its textures, materials, material animations and meshes from Filesys.
    FCliMaterialEntry* ResolveMaterialEntryFromFs(IFileSystem* Filesys, char const* Path,
                                                  bool bForceApply, unsigned Arg);

    // FUN_0041af40 (cdecl) — resolve at most Max entries of Paths through FUN_0041aa50 (with
    // bForceApply) into Out, FDUMP an error first when Paths holds more than Max. Returns the
    // number written; 0 for a null Paths.
    int ResolveMaterialEntryList(FCliVc6Vector<FCliMaterialPathPair> const* Paths,
                                 FCliMaterialEntry** Out, int Max);

    // FUN_0041b030 — DecRef a cache entry through its slot; frees libs + erases at 0.
    int ReleaseMaterialEntrySlot(FCliMaterialEntry** Slot);

    // FUN_0041b150 (cdecl) — client shutdown (0x005b22f6): give every cached entry's texture,
    // material, material-animation and mesh handles back to their libraries regardless of the
    // reference counts, empty the cache, return how many entries there were.
    int FlushMaterialCache();

    // The companion (ICliObj) fields and methods other client code reads by raw offset in
    // retail: +0x10 the envmap material name (CacheString), +0x20 the material path list, and
    // FUN_00406cd0, the explicit-parameter LOD recompute.
    char const* CliObjEnvmapMaterial(ICliObj* Companion);
    FCliVc6Vector<FCliMaterialPathPair> const* CliObjMaterialPaths(ICliObj* Companion);
    // A solar archetype's companion (CliSolar) +0x60: its shape_name text. Companion must be a
    // CliSolar (retail reads the offset without a type test).
    char const* CliSolarShapeName(ICliObj* Companion);
    void CliObjRecomputeLodPickExplicit(ICliObj* Companion, long EngineIndex,
                                        float const* BlendSource, bool bSolarOrShip);
    // FUN_00406c50 -- recompute the companion's LodPick from its bound archetype (kept as is
    // while that archetype is not loaded). Thiscall on the companion in retail.
    void CliObjRecomputeLodPick(ICliObj* Companion, float const* BlendSource);
    // FUN_004022b0 (cdecl) -- store the two ArchLod globals: DAT_00616470 (byte) and
    // DAT_006101d4 (float, the LOD distance scale). Caller: the level_of_detail apply
    // LAB_005b4560.
    void CliObjSetArchLodGlobals(unsigned char bMute, float Scale);
    // FUN_004022d0 (cdecl) -- *OutScale = DAT_006101d4 when OutScale is not null; returns the
    // byte DAT_00616470. Caller: the gameobj draw submit FUN_00539980 (0x005399f9).
    unsigned char CliObjGetArchLodGlobals(float* OutScale);

    class FCliObjFactory final : public ICliObjFactory
    {
    public:
        void Create(int TypeId, ICliObj** Out) override;
    };

    // Retail DAT_006164a0 — the factory WinMain hands to DataConfig::startup.
    extern FCliObjFactory GCliObjFactory;

    // FUN_00407310 — non-virtual thunk into FUN_00407100 (build the companion's material
    // cache; retail's zero applies now, nonzero defers application). Caller: FUN_00553100.
    bool CliObjPreloadMaterials(ICliObj* Companion, bool bDeferApply);

    // FUN_0040c690 -- a TradeLane companion's (CliEQTradeLane) effect reference: the first one
    // resolves its five player-or-ship EffectArch pointers and AddRefs them. Caller: the
    // TradeLane equipment FX constructor FUN_00516c30 (0x00516cb0).
    void CliTradeLaneAddEffectRef(ICliObj* Companion);
    // FUN_0040c7d0 -- the matching release; the last one releases and clears the five.
    // Returns the remaining count. Caller: the TradeLane equipment FX dtor FUN_00516bc0.
    int CliTradeLaneReleaseEffectRef(ICliObj* Companion);

    // FUN_00407450 (thiscall on the companion, RET 4) -- CliRoot's distance-record pick: a
    // random live effect of the record the distance falls into, or null. Callers: GameObject
    // slot 104 (0x00539d50) and slot 105 (0x00539dd3).
    FEffectArch* CliObjPickDistanceRecord(ICliObj* Companion, float Distance);
    // FUN_00409f10 (thiscall on the companion, RET 4) -- the same pick over a
    // CliEQShieldGenerator's shield_hit_effects. Callers 0x0052e81a / 0x0052e96a.
    FEffectArch* CliShieldGeneratorPickHitEffect(ICliObj* Companion, float Distance);
    // FUN_00408a70 (thiscall on the companion) -- a random entry of a CliMunition's live
    // effects, or null. Callers 0x0052ea06 / 0x00539e2e.
    FEffectArch* CliMunitionPickLiveEffect(ICliObj* Companion);
    // A CliShip companion's bay door sound ids: +0x58 bay_doors_open_snd when bOpen, else +0x5c
    // bay_doors_close_snd. Reader: the ship notify FUN_00545670 case 2.
    ID_String const* CliShipBayDoorSound(ICliObj* Companion, bool bOpen);
    // A CliSolar companion's +0x54 open_sound when bOpen, else +0x58 close_sound. Reader: the
    // solar notify FUN_005478f0 codes 0 and 1.
    ID_String const* CliSolarDoorSound(ICliObj* Companion, bool bOpen);
    // A CliEQEngine companion's +0x9c flame_effect, +0x60 rumble_sound and +0x78
    // character_loop_sound. Companion must be a CliEQEngine (retail reads the offsets after an
    // Engine class test on the archetype). Readers: Gf ObjectShip FUN_00446ca0 (0x00446cfd) and
    // FUN_00447f90 (0x0044804a / 0x0044804d).
    ID_String const* CliEngineFlameEffect(ICliObj* Companion);
    ID_String const* CliEngineRumbleSound(ICliObj* Companion);
    ID_String const* CliEngineCharacterLoopSound(ICliObj* Companion);

    // A CliProj companion's (projectiles: CliMunition and the rest) +0x58 const_effect and
    // +0x54 const_effect_delay. Readers: Projectile slot 92 FUN_005426e0 (0x0054270a /
    // 0x00542719) and slot 93 FUN_00542790.
    ID_String const* CliProjConstEffect(ICliObj* Companion);
    float CliProjConstEffectDelay(ICliObj* Companion);
    // A CliMunition companion's +0x60 HP_trail_parent (CacheString text; "" when unset, never
    // null after the ctor's clear). Reader: Guided slot 112 LAB_0053f030.
    char const* CliMunitionHpTrailParent(ICliObj* Companion);
    // A CliDynamicAsteroid companion's +0x58 flag and +0x54 particle_effect. Reader: the
    // dynamic asteroid update FUN_00525c80 (0x00526089 / 0x005260c2), no class test.
    bool CliDynamicAsteroidHasParticleEffect(ICliObj* Companion);
    ID_String const* CliDynamicAsteroidParticleEffect(ICliObj* Companion);

    // FUN_004252d0 (cdecl) -- Load(LoadMode) the companion (Root +0x04, vt+0x20) of every
    // mounted, non-internal equipment in the vector whose archetype resolves. Callers: the
    // player ship build FUN_0054b520 (0x0054b7ee, LoadMode 1), 0x0053b073, 0x0053b6f1,
    // 0x00548c51.
    void LoadMountedEquipmentCompanions(EquipDescVector const* Items, int LoadMode);
    // FUN_00425480 (cdecl) -- hook up the materials (IMatHookUp slot 3) of an engine instance
    // and of every instance IEngine::GetRelatedInstance(Instance, 1, ...) walks to. Callers:
    // FUN_0054b520 (0x0054b7fe) and eight Gameobj/equipment paths.
    void HookUpInstanceTreeMaterials(long EngineIndex);
}
