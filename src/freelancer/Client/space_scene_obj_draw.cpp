// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "space_scene_obj_draw.h"

#include "alchemy.h"
#include "beam_effect.h"
#include "client_gameobj.h"
#include "client_cliobj.h" // CliObjGetArchLodGlobals (FUN_004022d0), CliObjEnvmapMaterial
#include "cli_asteroid.h"
#include "cli_nebula.h"
#include "cli_projectile_fx.h" // CliEffectCompanionClear (FUN_00502d90)
#include "client_equip_fx.h"   // RemoveAppearanceInstanceValue (FUN_00532170)
#include "client_objdb.h"
#include "common_archetype.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "common_misc.h"
#include "common_universe.h"
#include "effect_base.h"
#include "effect_vertex_batch.h"
#include "engine_light.h"
#include "effect_type_info.h"
#include "effect_type_list.h"
#include "game_options.h"
#include "gate_tunnel.h"    // FGateTunnel::Draw (FUN_004ffab0)
#include "globals.h"
#include "hud_element_list.h"
#include "jump_effect.h"    // FJumpShipEffect::Tunnel (+0x24)
#include "hud_waypoint.h"   // HudWaypointGetObjectCount / HudWaypointGetObject (FUN_004c83c0 / 83e0)
#include "menu_ui_draw.h"
#ifdef FREELANCER_RP9
#include "nebula_volume.h"  // NebulaVolume (port-only Direct3D 9 volumetric nebula)
#endif
#include "part_cache.h"
#include "space_camera.h"
#include "space_camera_game.h"
#include "player_behavior.h"
#include "perf_options.h"
#ifdef FREELANCER_RP9
#include "render_ex.h"      // RenderEx::AfterSpaceScene (port-only Direct3D 9 extensions)
#endif
#include "screen_quad_state.h"
#include "space_scene_draw.h"
#include "star_sphere.h"    // StarSphereList (FUN_0050c620)
#include "star_sys.h"
#include "texture_panels.h"
#include "win3d_immediate.h"
#include "win3d_render.h"

#include "DALib.h"
#include "dacom.h"
#include "dacom_iids.h"
#include "engine/engbase.h"
#include "common_csys.h"
#include "x86math.h"
#include "render/IFLAppearance.h"
#include "render/rendcomp.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "soundmanager.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

// ============================================================================
// Retail chain (listing-verified entry points):
//   FUN_0051b250  camera +0x78
//   FUN_0051c200  GetBucket(0xfdff) + Fill(DAT_00613ec8) + enqueue FUN_0051f2c0
//   FUN_0051eda0  sort: MSVC6 introsort DistSq descending (far first)
//                 n<17 insertion; else FUN_0051fd90 partition + FUN_0051fc90
//   FUN_0051f810  FUN_00422910 perspective re-push, then drain nodes
//                 + MaterialBatcher FlushAll(+0x28) per object
//   FUN_00539980  LOD via +0x17c (FUN_00539820) → get_index → Renderer Slot7
//
// Note: +0x17c/+0x180/+0x184 on Gameobj are VTABLE slots, not data fields.
//   +0x17c = FUN_00539820 (LOD pick)
//   +0x180 = empty stub (IDPMsgHandler::OnDisconnect)
//   +0x184 = FUN_00539b40 (vtable method — lighting prep → FUN_005554d0)
// Data fields used by the draw path: +0x34/+0x38/+0x3c on FClientGameobj.
// ============================================================================

namespace
{
    // DAT_006752f0.. — five growable pointer vectors (sort walks until > 0x675343).
    constexpr unsigned kDrawBucketCount = 5;
    constexpr unsigned kObjDbTypeMask = 0xfdffu;
    // DAT_00613ec8 — Fill query radius (listing 0051c43d).
    constexpr float kObjDbFillRange = 10000.0f;
    // DAT_005dc35c — DistSq threshold for near/far bucket bit.
    constexpr float kNearBucketDistSq = 40000.0f;
    // DAT_006164d0 default (FUN_00410130); distance gate in FUN_0051f2c0.
    float G_SpaceDrawRange = 20000.0f;

    // The queue's nodes are retail's ZItem family (RTTI: abstract base `Zitem`, TD 0x00613678,
    // no COL / vftable of its own; four leaves, each CHD {leaf, Zitem}). Two slots each; the
    // push site stores the vftable inline:
    //   ZItemNebula        vftable 0x005dc01c (COL 0x005edd60) {LAB_0051ba00 Draw, LAB_00517320}
    //                      size 0xc  (0x0051b8d2 / 0x0051b952 / 0x0051b98d)
    //   ZItemAsteroidField vftable 0x005dc034 (COL 0x005edda8) {LAB_0051bdf0 Draw, LAB_00517320}
    //                      size 0x10 (0x0051bbf7 ..)
    //   ZItemPlanetRing    vftable 0x005dc06c (COL 0x005eddf0) {LAB_0051c1e0 Draw, LAB_00517320}
    //                      size 0x10 (0x0051bfa7 ..)
    //   ZItemGameObject    vftable 0x005dc364 (COL 0x005ee00c) {LAB_0051f7a0 Draw, LAB_0051f780}
    //                      size 0x10 (0x0051f405 / 0x0051f5db)
    // Allocation is FUN_0051ec80 (ZItemAlloc) by size; release is plain free() (FUN_0051ed20,
    // FUN_0051fbc0) -- no destructor slot, and nothing ever returns a node to either Heap.
    // The drain FUN_0051f810 calls slot 0 (vt[0], 0x0051f872). Slot 1 has no caller in the
    // draw-queue range 0x0051b000..0x0051fd00 (every vt+4 call there is on another receiver);
    // its body is ported as is, the name stays Slot04 until a caller settles it.
    enum EDrawNodeKind : uintptr_t
    {
        kDrawNodeGameobj = 0,
        kDrawNodeAsteroidFar = 1,
        kDrawNodeTexturePanelPiece = 2,
    };
    // Zitem (abstract): vftable +0, sort key +4, the drawn object +8.
    struct FZItem
    {
        virtual void Draw(FSpaceCamera* Cam) = 0; // vt+0x00
        virtual int Slot04() = 0;                 // vt+0x04
        float DistSq;           // +0x04  sort key
        void* Object;           // +0x08  Gameobj* / FNebulaEffect* / FCliAsteroid* / FTexturePanels*
    };
    static_assert(sizeof(FZItem) == 0xc, "Zitem sizeof 0xc");

    // ZItemNebula, vftable 0x005dc01c -- sizeof 0xc (FUN_0051ec80(0xc) at 0x0051b8b5 /
    // 0x0051b935 / 0x0051b97b), Object = the StarSys nebula.
    struct FZItemNebula final : FZItem
    {
        // LAB_0051ba00 (thiscall, RET 4): Object vt+0x10 (DrawFogShell)(Cam), then Object
        // vt+0x14 (DrawBoltsPass)(Cam), each through a fresh load of +8 and its vftable. No
        // test of the object or either slot.
        void Draw(FSpaceCamera* Cam) override;
        // LAB_00517320: return 2.
        int Slot04() override { return 2; }
    };
    static_assert(sizeof(FZItemNebula) == 0xc, "ZItemNebula sizeof 0xc");

    // ZItemGameObject, vftable 0x005dc364.
    struct FZItemGameObject final : FZItem
    {
        // LAB_0051f7a0 (thiscall, RET 4): Object->vt+0x188(&<the Cam argument's own stack
        // slot> as the discarded OutLod, Cam, LodWord, 0, 0).
        void Draw(FSpaceCamera* Cam) override;
        // LAB_0051f780 (raw bytes 8b4108 8b401c 83f801 7503 33c0 c3 33c9 83f802 0f95c1 41
        // 8bc1 c3): Object +0x1c (ManagerKind) 1 -> 0, 2 -> 1, anything else -> 2.
        int Slot04() override;
        unsigned LodWord;       // +0x0c  packed LOD
    };
    static_assert(sizeof(FZItemGameObject) == 0x10, "ZItemGameObject sizeof 0x10");

    // ZItemAsteroidField, vftable 0x005dc034.
    struct FZItemAsteroidField final : FZItem
    {
        // LAB_0051bdf0 (thiscall, RET 4): FUN_00522850(this = Object, Cam, LodWord).
        void Draw(FSpaceCamera* Cam) override;
        // LAB_00517320: return 2.
        int Slot04() override { return 2; }
        unsigned LodWord;       // +0x0c  piece index
    };
    static_assert(sizeof(FZItemAsteroidField) == 0x10, "ZItemAsteroidField sizeof 0x10");

    // ZItemPlanetRing, vftable 0x005dc06c (the object is the ring's FTexturePanels).
    struct FZItemPlanetRing final : FZItem
    {
        // LAB_0051c1e0 (RET 4): Object vt+0x10 (slot 4 DrawPiece)(Cam, LodWord), dispatched
        // through the object's vtable by TexturePanelsDrawPiece (texture_panels.h).
        void Draw(FSpaceCamera* Cam) override;
        // LAB_00517320: return 2.
        int Slot04() override { return 2; }
        unsigned LodWord;       // +0x0c  piece index
    };
    static_assert(sizeof(FZItemPlanetRing) == 0x10, "ZItemPlanetRing sizeof 0x10");

    struct FDrawBucket
    {
        std::vector<FZItem*> Nodes;
    };

    // DAT_00675350 / DAT_00675340 -- Heap {ElementSize 0x10 / 0xc, FreeList 0, MaxFreeCount -1,
    // CurrentFreeCount 0}, set by the static initializers LAB_0051ec20 / LAB_0051ec50.
    Heap G_DrawNodeHeap(0x10u, -1);
    Heap G_DrawNodeHeap12(0xcu, -1);
    // DAT_006752f0..0x0067533f -- five 0x10-byte vectors (LAB_0051ecb0 constructs them).
    FDrawBucket G_Buckets[kDrawBucketCount];

    // FUN_0051ec80 (cdecl) -- 0xc from DAT_00675340, 0x10 from DAT_00675350, anything else
    // malloc (all three as tail jumps). Every caller passes 0xc or 0x10.
    void* ZItemAlloc(size_t Size)
    {
        if (Size == 0xc)
            return G_DrawNodeHeap12.Malloc();
        if (Size == 0x10)
            return G_DrawNodeHeap.Malloc();
        return malloc(Size);
    }
    // DAT_0067524c / DAT_00675244 / DAT_00675248 / DAT_00675230 — field enclosure.
    // 4c: 0=none, 1=partial, 2=fully inside. 44: selected field object. 48: radius². 30: best score.
    int G_FieldEnclosure = 0;
    FNebulaEffect* G_FieldController = nullptr;
    float G_FieldRadiusSq = 0.0f;
    // DAT_00675230 is seeded at load by the static initializer LAB_0051b2d0 (XC 0x0060fa08):
    // DAT_006164d0 * DAT_006164d0, where DAT_006164d0 was already set to 20000 by LAB_00410120
    // (XC 0x0060f054, earlier in the table).
    float G_FieldBestScore = 20000.0f * 20000.0f;
    // DAT_0067517c (FUN_0050c620) is the live star list; it lives in star_sphere.cpp
    // (StarSphereList), filled by FUN_0050c630 from the solar ini reader's "star" key.

    // DAT_00675368 (draw-queue object DAT_00675360 +8) — the pass's far plane, written only by
    // FUN_0051c200 (0x0051c3fb) and read back by FUN_0051f2c0 / FUN_0051f950.
    float G_DrawCtxZFar = 250000.0f;
    // DAT_00675364 (draw-queue object +4) — FUN_0051c200's second argument (0x0051c348). No
    // reader in the queue's methods FUN_0051ed20..FUN_0051fc90 or anywhere else in the listing.
    unsigned G_DrawCtxParam2 = 0;
    // DAT_00610534
    constexpr float kDefaultZFar = 250000.0f;
    // DAT_006164d4 — FUN_00410130: 1000+20000; jump-arm SetNearFar far arg.
    float G_SpaceDrawFar = 21000.0f;
    // DAT_00675360 (draw-queue object +0) — FUN_0051c200's first argument (0x0051c342).
    unsigned G_SceneLayerFlags = 0;

    // Effect type live lists: G_EffectTypeBuckets in effect_type_list.cpp (DAT_00674ce0..).

    // DAT_006798f8/f9/fa — [Ambient] color bytes, and DAT_0067986c/6d/6e — the working copy
    // after the AMBIENT option add. Both are StarSys globals (star_sys.cpp); FUN_005554d0
    // below is a StarSys.cpp function that reads and writes them.
    unsigned char (&G_AmbientBaseRgb)[3] = G_StarSysAmbientRgb;
    unsigned char (&G_AmbientWorkRgb)[3] = G_StarSysAmbientWorkRgb;
    // DAT_00679868/69/6a — lerped ambient when DrawStateA object supplies RGB.
    unsigned char G_AmbientLerpRgb[3] = {0, 0, 0};

    // DAT_00678980..9c — embedded light-grid root node (FUN_00537c30 / FUN_00537d20).
    // Layout: Child[4] @+0, Payload @+0x10, CellX @+0x14, CellZ @+0x18, HalfSize @+0x1c.
    struct FLightGridPayload
    {
        FLightGridPayload* Next; // +0
        FEngineLight* Light;     // +4
        int Ix;                  // +8
        int Iz;                  // +0xc
        int Range;               // +0x10
    };
    struct FLightGridNode
    {
        FLightGridNode* Child[4]; // +0,+4,+8,+0xc
        FLightGridPayload* Payload; // +0x10
        int CellX;                // +0x14
        int CellZ;                // +0x18
        int HalfSize;             // +0x1c
    };
    static_assert(sizeof(FLightGridPayload) == 0x14, "payload node 0x14");
    static_assert(sizeof(FLightGridNode) == 0x20, "tree node 0x20");

    FLightGridNode G_LightGridRoot = {};
    // DAT_00675578 — 256×0x14 payload pool; free head DAT_00676978.
    FLightGridPayload G_LightPayloadPool[256] = {};
    FLightGridPayload* G_LightPayloadFree = nullptr;
    // DAT_0067697c — 256×0x20 tree pool; free head DAT_0067897c.
    FLightGridNode G_LightTreePool[256] = {};
    FLightGridNode* G_LightTreeFree = nullptr;

    bool CameraHasWatchedObject(FSpaceCamera* Cam)
    {
        // FUN_0051b250's head (0x0051b253..0x0051b262): the watcher at +0xc0 set, and the object
        // it yields (watcher - 8) non-null. There is no test of `this` -- the one that stood here
        // was the frame's own caller test (0x0054b0f9), which now lives in UpdateSpaceState.
        FGameCamera* Gc = reinterpret_cast<FGameCamera*>(Cam);
        const void* Watched = Gc->Watched.target();
        if (Watched == nullptr)
            return false;
        if (reinterpret_cast<uintptr_t>(Watched) == 8u)
            return false;
        return true;
    }

    // FUN_0051ed20 (thiscall on the queue DAT_00675360, no arguments) -- for each of the five
    // bucket vectors (0x006752f8 step 0x10 while < 0x00675348): free() every pointer in
    // [begin, end) -- null included, the CRT ignores it -- then erase the whole range (end =
    // begin; the storage stays). The nodes came from Heap::Malloc, which on an empty free list
    // is malloc(ElementSize), and nothing ever pushes onto those free lists, so free() is the
    // matching release.
    void ClearBuckets()
    {
        for (unsigned B = 0; B < kDrawBucketCount; ++B)
        {
            FDrawBucket& Buck = G_Buckets[B];
            for (size_t I = 0; I < Buck.Nodes.size(); ++I)
                free(Buck.Nodes[I]);
            Buck.Nodes.clear();
        }
    }

    // FUN_0051f140 (thiscall on the queue DAT_00675360, RET 4) -- push_back of Node onto bucket
    // vector 2 (DAT_00675310: begin 0x00675314, end 0x00675318, capacity 0x0067531c; the VC6
    // grow-by-max(size, 1) path at 0x0051f165). The pointer is appended as given, null too.
    void ZItemQueuePushBucket2(FZItem* Node)
    {
        G_Buckets[2].Nodes.push_back(Node);
    }

    // The ZItemNebula push inlined three times in FUN_0051b730 (0x0051b8b5..0x0051b8e5,
    // 0x0051b935..0x0051b965, 0x0051b97b..0x0051b9a3): FUN_0051ec80(0xc); a non-null block gets
    // {vftable 0x005dc01c, +4 Score, +8 Nebula}; the pointer -- null when the heap failed -- goes
    // to bucket 2 through FUN_0051f140.
    void PushNebulaZItem(float Score, void* Nebula)
    {
        void* const Mem = ZItemAlloc(0xc);
        FZItem* Node = nullptr;
        if (Mem != nullptr)
        {
            Node = new (Mem) FZItemNebula();
            Node->DistSq = Score;
            Node->Object = Nebula;
        }
        ZItemQueuePushBucket2(Node);
    }

    // FUN_0051b730 (thiscall on the camera) -- pick the field controller DAT_00675244 among the
    // StarSys nebulae; every other one is queued as a ZItemNebula in bucket 2 (FUN_0051f140),
    // drawn by FUN_0051f8d0's far pass. No test of the camera, a nebula, its zone or any vtable
    // entry.
    void SelectFieldControllers(FSpaceCamera* Cam)
    {
        // 0x0051b730..0x0051b759: best score = DAT_006164d0 squared (stored float); controller,
        // quality and radius squared cleared.
        G_FieldBestScore = static_cast<float>(static_cast<double>(G_SpaceDrawRange) *
                                              G_SpaceDrawRange);
        G_FieldController = nullptr;
        G_FieldEnclosure = 0;
        G_FieldRadiusSq = 0.0f;

        // 0x0051b74b..0x0051b77a: [EFFECTS] and NEBULA.
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;
        if (GetGameOption(kGameOptNebula) == 0)
            return;

        typedef void*(__thiscall* FFieldGetZoneFn)(void* Self);                // +0x4c
        typedef float(__thiscall* FFieldEncloseFn)(void* Self, float const* Pos); // +0x20
        typedef float(__thiscall* FFieldRadiusFn)(void* Self);                // +0x40
        typedef float const*(__thiscall* FZoneGetPosFn)(void* Self);          // IZone +0x10
        typedef float(__thiscall* FZoneProbeFn)(void* Self, float const* Dir3); // IZone +0x40

        ICamera* const Camera = AsCamera(Cam);

        // 0x0051b780..0x0051b794: FUN_005553a0's list (&DAT_006798c4), walked head->next on.
        std::list<FNebulaEffect*>& Nebulae = StarSysNebulaList();
        for (std::list<FNebulaEffect*>::iterator It = Nebulae.begin(); It != Nebulae.end(); ++It)
        {
            FNebulaEffect* const Field = *It;

            // 0x0051b7a0..0x0051b7cf: zone (field vt+0x4c), camera position (vt+0xc), zone
            // position (IZone vt+0x10); d = zone - camera, kept unrounded.
            Universe::IZone* const Zone = Field->GetZone();
            float const* const CamPos = Camera->GetPosition();
            float const* const ZonePos = &Zone->GetPos().x;
            const double Dz = static_cast<double>(ZonePos[2]) - CamPos[2];
            const double Dy = static_cast<double>(ZonePos[1]) - CamPos[1];
            const double Dx = static_cast<double>(ZonePos[0]) - CamPos[0];

            // 0x0051b7d1..0x0051b818: |d| by one Newton step of the 0x5f3759df estimate --
            // sum x*x + y*y + z*z stored float for the bit trick; y1 = y0 * (1.5f - 0.5f * sum *
            // y0 * y0); Dist = y1 * sum, stored float.
            const double SumSq = Dx * Dx + Dy * Dy + Dz * Dz;
            const float SumSqF = static_cast<float>(SumSq);
            int SumBits = 0;
            memcpy(&SumBits, &SumSqF, sizeof(float));
            const int EstimateBits = 0x5f3759df - (SumBits >> 1);
            float Estimate = 0.0f;
            memcpy(&Estimate, &EstimateBits, sizeof(float));
            const double Newton =
                (1.5f - static_cast<double>(Estimate) * Estimate * SumSq * 0.5f) * Estimate;
            const float Dist = static_cast<float>(Newton * SumSq);

            // 0x0051b81a..0x0051b842: 1.0f / Dist stored float; the direction's components
            // (x, y, z) are d * that, each stored float.
            const float InvDist = static_cast<float>(1.0 / Dist);
            float Dir[3];
            Dir[0] = static_cast<float>(InvDist * Dx);
            Dir[1] = static_cast<float>(InvDist * Dy);
            Dir[2] = static_cast<float>(InvDist * Dz);

            // 0x0051b846..0x0051b86c: the zone's extent along that direction (IZone vt+0x40);
            // Dist beyond it (TEST AH,0x41 / JP: greater or unordered) scores (Dist - hit)^2,
            // otherwise 0.
            const float ZoneHit = (float)Zone->GetRadiusAlong(*(Vector const*)Dir);
            float Score = 0.0f;
            if (!(Dist <= ZoneHit))
            {
                const double Beyond = static_cast<double>(Dist) - ZoneHit;
                Score = static_cast<float>(Beyond * Beyond);
            }

            // 0x0051b870..0x0051b879: once a controller level is set, every further nebula is
            // deferred with its score.
            if (G_FieldEnclosure != 0)
            {
                PushNebulaZItem(Score, Field);
                continue;
            }

            // 0x0051b87f..0x0051b88b: the enclosure at the camera position (field vt+0x20).
            const float Enclose = Field->ZoneExteriorFactor(Camera->GetPosition());

            // 0x0051b88e..0x0051b899 -> 0x0051b9c7: >= 1.0f (TEST AH,1 / JZ) -- fully inside:
            // quality 2, radius squared from vt+0x40, the queue emptied (FUN_0051ed20), done.
            if (Enclose >= 1.0f)
            {
                G_FieldEnclosure = 2;
                G_FieldController = Field;
                const double Radius = Field->GetFloat2B0();
                G_FieldRadiusSq = static_cast<float>(Radius * Radius);
                ClearBuckets();
                return;
            }

            // 0x0051b89f..0x0051b90d: strictly above 0 (TEST AH,0x41 / JNZ sends <= 0 and
            // unordered on) -- partial: the previous controller is deferred with the best
            // score, this one becomes it at quality 1.
            if (Enclose > 0.0f)
            {
                if (G_FieldController != nullptr)
                    PushNebulaZItem(G_FieldBestScore, G_FieldController);
                G_FieldEnclosure = 1;
                G_FieldController = Field;
                const double Radius = Field->GetFloat2B0();
                G_FieldRadiusSq = static_cast<float>(Radius * Radius);
                continue;
            }

            // 0x0051b912..0x0051b979: outside -- a score strictly below the best (TEST AH,5 /
            // JP defers >= and unordered) replaces it, deferring the previous controller.
            if (!(Score < G_FieldBestScore))
            {
                PushNebulaZItem(Score, Field);
                continue;
            }
            if (G_FieldController != nullptr)
                PushNebulaZItem(G_FieldBestScore, G_FieldController);
            G_FieldBestScore = Score;
            G_FieldController = Field;
        }
    }



    // DAT_006789d0 -- Gameobj.cpp's lazily acquired math engine (FUN_005b73e0 on first use,
    // 0x00539864..0x00539872; the same pattern at 0x0053a0a2 / 0x0053a0cf / 0x0053a278 ..).
    I3DMathEngine* G_GameobjMathEngine = nullptr;
}

    // FUN_00533880 (cdecl) -- QI DALib::Engine (no null test, 0x0053389e) for IFLAppearance
    // into a zeroed local. QI < 0 (0x005338c2 JL): release the local if it is non-null and
    // return 0. Otherwise Slot3 (+0xc) (arg1, arg2) with no null test on the interface, BL =
    // SETGE (result >= 0), release if non-null, return BL. The local is a smart pointer in
    // retail (EH frame, handler LAB_005c08f8); nothing between QI and the release can throw.
    bool SetAppearanceInstanceValue(void* InstId, void* ArchPath)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
        {
            if (Ifl != nullptr)
                Ifl->Release();
            return false;
        }
        bool const bOk = Ifl->SetInstanceValue(static_cast<int>(reinterpret_cast<intptr_t>(InstId)), ArchPath) >= 0;
        if (Ifl != nullptr)
            Ifl->Release();
        return bOk;
    }

    // FUN_00533930 (cdecl) -- the same shape as FUN_00533880 around Slot8 (+0x20) (arg1, arg2).
    bool GetAppearanceOverrideMaterial(unsigned long Index, void** Out)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
        {
            if (Ifl != nullptr)
                Ifl->Release();
            return false;
        }
        bool const bOk = Ifl->GetOverrideMaterial(Index, Out) >= 0;
        if (Ifl != nullptr)
            Ifl->Release();
        return bOk;
    }

namespace
{

    // FUN_00532170 (callers 0x00532037, 0x00533847) is RemoveAppearanceInstanceValue in
    // client_equip_fx.cpp, next to its other caller, the cloak wrapper's notify.


    // FUN_004f70c0 — FLD DAT_006129c8; FSUBR double 1.0 (DAT_005c89b8): the difference is
    // returned on the FPU, never rounded to a float.
    double EffectPerfOneMinus()
    {
        return 1.0 - static_cast<double>(GetPerfGenericEffectPerf());
    }

    // The body shared by LAB_00537ba0, FUN_00537c30 and FUN_005382f0: the root's first seven
    // dwords (DAT_00678980..98) zeroed, HalfSize (DAT_0067899c) set, and each pool rethreaded
    // through its first dword only (element i -> element i-1, the head the last element).
    void LightGridResetPools(int RootHalfSize)
    {
        G_LightGridRoot.Child[0] = nullptr;
        G_LightGridRoot.Child[1] = nullptr;
        G_LightGridRoot.Child[2] = nullptr;
        G_LightGridRoot.Child[3] = nullptr;
        G_LightGridRoot.Payload = nullptr;
        G_LightGridRoot.CellX = 0;
        G_LightGridRoot.CellZ = 0;
        G_LightGridRoot.HalfSize = RootHalfSize;

        FLightGridPayload* PayloadPrev = nullptr;
        for (int I = 0; I < 256; ++I)
        {
            G_LightPayloadPool[I].Next = PayloadPrev;
            PayloadPrev = &G_LightPayloadPool[I];
        }
        G_LightPayloadFree = PayloadPrev;

        FLightGridNode* TreePrev = nullptr;
        for (int I = 0; I < 256; ++I)
        {
            G_LightTreePool[I].Child[0] = TreePrev;
            TreePrev = &G_LightTreePool[I];
        }
        G_LightTreeFree = TreePrev;
    }

    // DAT_006789a0 -- the grid extent: 0x8ca0 from the static initializer, 0x30d40 once
    // FUN_00537c30 has run. FUN_005382f0 halves it into the root's HalfSize.
    int G_LightGridExtent = 0;

    // LAB_00537ba0 (XC static initializer): extent 0x8ca0, HalfSize 0x4650, pools threaded.
    bool LightGridStaticInit()
    {
        G_LightGridExtent = 0x8ca0;
        LightGridResetPools(0x4650);
        return true;
    }
    bool const G_bLightGridStaticInit = LightGridStaticInit();

    FLightGridNode* LightGridAllocTreeNode()
    {
        FLightGridNode* Node = G_LightTreeFree;
        if (Node == nullptr)
            return nullptr;
        G_LightTreeFree = reinterpret_cast<FLightGridNode*>(Node->Child[0]);
        Node->Child[0] = nullptr;
        Node->Child[1] = nullptr;
        Node->Child[2] = nullptr;
        Node->Child[3] = nullptr;
        Node->Payload = nullptr;
        return Node;
    }

    FLightGridPayload* LightGridAllocPayload()
    {
        FLightGridPayload* Node = G_LightPayloadFree;
        if (Node == nullptr)
            return nullptr;
        G_LightPayloadFree = Node->Next;
        Node->Next = nullptr;
        return Node;
    }

    // FUN_00537d20 — insert light record into the XZ quadtree (thiscall on pool owner).
    void LightGridInsert(FLightGridNode* Node, int const* Record5)
    {
        if (Node == nullptr || Record5 == nullptr)
            return;

        int Radius = Record5[4];
        FLightGridNode* Cur = Node;
        while (Radius <= Cur->HalfSize)
        {
            int RelX = Record5[2] - Cur->CellX;
            int RelZ = Record5[3] - Cur->CellZ;

            if (RelX < Radius)
            {
                if (RelZ < Radius)
                {
                    FLightGridNode* Child = Cur->Child[1];
                    if (Child == nullptr)
                    {
                        Child = LightGridAllocTreeNode();
                        if (Child == nullptr)
                            goto AfterChild1;
                        int Half = Cur->HalfSize / 2;
                        Child->HalfSize = Half;
                        Child->CellX = Cur->CellX - Half;
                        Child->CellZ = Cur->CellZ - Half;
                        Cur->Child[1] = Child;
                    }
                    LightGridInsert(Child, Record5);
                }
            AfterChild1:
                if (-RelZ < Radius)
                {
                    FLightGridNode* Child = Cur->Child[3];
                    if (Child == nullptr)
                    {
                        Child = LightGridAllocTreeNode();
                        if (Child == nullptr)
                            goto AfterChild3;
                        int Half = Cur->HalfSize / 2;
                        Child->HalfSize = Half;
                        Child->CellX = Cur->CellX - Half;
                        Child->CellZ = Cur->CellZ + Half;
                        Cur->Child[3] = Child;
                    }
                    LightGridInsert(Child, Record5);
                }
            }
        AfterChild3:
            if (Radius <= -RelX)
                return;
            if (RelZ < Radius)
            {
                FLightGridNode* Child = Cur->Child[0];
                if (Child == nullptr)
                {
                    Child = LightGridAllocTreeNode();
                    if (Child == nullptr)
                        goto AfterChild0;
                    int Half = Cur->HalfSize / 2;
                    Child->HalfSize = Half;
                    Child->CellX = Cur->CellX + Half;
                    Child->CellZ = Cur->CellZ - Half;
                    Cur->Child[0] = Child;
                }
                LightGridInsert(Child, Record5);
            }
        AfterChild0:
            if (Radius <= -RelZ)
                return;
            {
                FLightGridNode* Child = Cur->Child[2];
                if (Child == nullptr)
                {
                    Child = LightGridAllocTreeNode();
                    if (Child == nullptr)
                        return;
                    int Half = Cur->HalfSize / 2;
                    Child->HalfSize = Half;
                    Child->CellX = Cur->CellX + Half;
                    Child->CellZ = Cur->CellZ + Half;
                    Cur->Child[2] = Child;
                }
                Radius = Record5[4];
                Cur = Child;
            }
        }

        FLightGridPayload* Payload = LightGridAllocPayload();
        if (Payload == nullptr)
            return;
        Payload->Next = Cur->Payload;
        Payload->Light = reinterpret_cast<FEngineLight*>(static_cast<uintptr_t>(
            static_cast<unsigned>(Record5[1])));
        Payload->Ix = Record5[2];
        Payload->Iz = Record5[3];
        Payload->Range = Record5[4];
        // Listing copies 5 dwords then sets Next = old head — Light already from Record5[1].
        Cur->Payload = Payload;
    }

    // FUN_00537c30 — reset pools + insert every Flags&8 light from DAT_00675570.
    void LightGridRebuildImpl()
    {
        // 0x00537c37..0x00537caa: DAT_006789a0 = 0x30d40, root HalfSize = 0x186a0, pools.
        G_LightGridExtent = 0x30d40;
        LightGridResetPools(0x186a0);

        for (FEngineLight* Light = EngineLightGameListHead(); Light != nullptr;
             Light = EngineLightGameListNext(Light))
        {
            if ((Light->Flags & 8u) == 0)
                continue;
            int Record[5];
            Record[0] = 0;
            Record[1] = static_cast<int>(reinterpret_cast<uintptr_t>(Light));
            Record[2] = static_cast<int>(Light->Position[0]);
            Record[3] = static_cast<int>(Light->Position[2]);
            Record[4] = static_cast<int>(Light->D3d.Range);
            LightGridInsert(&G_LightGridRoot, Record);
        }
    }
} // namespace

void LightGridRebuild()
{
    LightGridRebuildImpl();
}

void LightGridReset()
{
    // FUN_005382f0 (.text:005382f0-00538363): HalfSize = DAT_006789a0 / 2 (CDQ; SUB; SAR --
    // signed, rounds toward zero), then the same root/pool reset.
    LightGridResetPools(G_LightGridExtent / 2);
}

namespace
{
    // DAT_00674f3c -- the screen-quad list key the effects pass flushes (0x004f774d); its
    // static initializer LAB_004f6ed0 sets it to -1 and nothing else writes it (XREFs 004f6ed0 W,
    // 004f774d R). -1 is FUN_00403ea0's "current list" key DAT_0061648c: the container of the
    // current material key is flushed.
    unsigned long G_EffectsListKey = 0xffffffffu;

    // FUN_00538200 (thiscall on the pool DAT_00675578, RET 0xc: out slot, node, query),
    // listing-traced 0x00538200..0x005382e4. `this` is only handed on to the recursion, so the
    // port drops it. A null node returns 0 (0x00538209, retail's own test); nothing else is
    // tested. The node itself goes first when it holds a payload (+0x10), then the children by
    // the query's x / z offset from the node's cell (+0x14 / +0x18) against the radius
    // (query +0x10), all signed JGE: x < r -> child 1 (z < r), child 3 (-z < r); -x < r ->
    // child 0 (z < r), child 2 (-z < r); each writes after what is already collected.
    int LightTreeCollect(FLightGridNode** OutSlot, FLightGridNode* Node, int const* Query)
    {
        if (Node == nullptr)
            return 0;
        int Count = 0;
        if (Node->Payload != nullptr)
        {
            *OutSlot = Node;
            Count = 1;
        }
        int const RelX = Query[2] - Node->CellX;
        int const RelZ = Query[3] - Node->CellZ;
        int const Radius = Query[4];
        if (RelX < Radius)
        {
            if (RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[1], Query);
            if (-RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[3], Query);
        }
        if (-RelX < Radius)
        {
            if (RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[0], Query);
            if (-RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[2], Query);
        }
        return Count;
    }

    // FUN_00537f40 (cdecl: out slots, max, position, radius, flag), listing-traced
    // 0x00537f40..0x005381f0. Nothing is tested and nothing is capped but the sorted list (0x100).
    int CollectNearbyLights(void** OutSlots, int MaxSlots, float const* Pos3, float Radius,
                            unsigned char Flag)
    {
        // 0x00537f4e..0x00537f80: the query {?, ?, ftol x, ftol z, ftol radius}; its first two
        // dwords are never written or read.
        int const Ix = static_cast<int>(Pos3[0]);
        int const Iz = static_cast<int>(Pos3[2]);
        int const Ir = static_cast<int>(Radius);
        int Query[5] = {};
        Query[2] = Ix;
        Query[3] = Iz;
        Query[4] = Ir;

        // 0x00537f7a..0x00538053: the node list at [ESP+0x2c] -- the root first when it holds
        // a payload (DAT_00678990), then FUN_00538200 into its children in the same x / z order
        // as the recursion. Retail's frame holds 0x101 dwords there (local_800 .. local_3fc) and
        // bounds none of the appends.
        FLightGridNode* Scratch[0x101];
        int Collected = 0;
        if (G_LightGridRoot.Payload != nullptr)
        {
            Scratch[0] = &G_LightGridRoot;
            Collected = 1;
        }
        int const RelX = Ix - G_LightGridRoot.CellX;
        int const RelZ = Iz - G_LightGridRoot.CellZ;
        if (RelX < Ir)
        {
            if (RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[1], Query);
            if (-RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[3], Query);
        }
        if (-RelX < Ir)
        {
            if (RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[0], Query);
            if (-RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[2], Query);
        }

        // 0x00538057..0x0053810d: every payload of every collected node (the chain through
        // +0), skipped when a payload of the same light (+4) is already listed, else inserted
        // by binary search on the range (+0x10) -- descending: a range below the middle one
        // moves the low bound past it (JGE: greater or equal moves the high bound down). The
        // count stops at 0x100; an insertion point at the full count is dropped.
        FLightGridPayload* Sorted[0x100];
        int SortedCount = 0;
        for (int I = 0; I < Collected; ++I)
        {
            for (FLightGridPayload* Pay = Scratch[I]->Payload; Pay != nullptr; Pay = Pay->Next)
            {
                bool bListed = false;
                for (int J = 0; J < SortedCount; ++J)
                {
                    if (Sorted[J]->Light == Pay->Light)
                    {
                        bListed = true;
                        break;
                    }
                }
                if (bListed)
                    continue;

                int Lo = 0;
                int Hi = SortedCount;
                while (Lo < Hi)
                {
                    int const Mid = (Lo + Hi) / 2;
                    if (Pay->Range < Sorted[Mid]->Range)
                        Lo = Mid + 1;
                    else
                        Hi = Mid;
                }
                if (SortedCount < 0x100)
                    ++SortedCount;
                FLightGridPayload* Carry = Pay;
                for (int K = Hi; K < SortedCount; ++K)
                {
                    FLightGridPayload* const Next = Sorted[K];
                    Sorted[K] = Carry;
                    Carry = Next;
                }
            }
        }

        // 0x00538113..0x0053819d: in sorted order, until MaxSlots are taken: with Flag clear a
        // light whose +0xb4 bit 0 is set is passed over; the rest are kept when
        // (x - px)^2 + (z - pz)^2 is below (range + radius)^2 -- all of it on the FPU from the
        // FILDed ints (FCOMPP, TEST AH,0x41 / JNZ: not-greater and unordered drop the light).
        // Kept lights go straight into OutSlots.
        int FilterCount = 0;
        for (int I = 0; I < SortedCount; ++I)
        {
            if (FilterCount >= MaxSlots)
                break;
            FLightGridPayload* const Pay = Sorted[I];
            if (Flag == 0 && (Pay->Light->Flags & 1u) != 0)
                continue;
            double const Dx = static_cast<double>(Pos3[0]) - static_cast<double>(Pay->Ix);
            double const Dz = static_cast<double>(Pos3[2]) - static_cast<double>(Pay->Iz);
            double const Reach = static_cast<double>(Pay->Range) + static_cast<double>(Radius);
            if (Reach * Reach > Dz * Dz + Dx * Dx)
            {
                OutSlots[FilterCount] = Pay->Light;
                ++FilterCount;
            }
        }

        // 0x005381a1..0x005381e2: compacted in place through FUN_00410a40(position, radius) on
        // each kept light; the count of survivors is returned.
        int OutCount = 0;
        for (int I = 0; I < FilterCount; ++I)
        {
            FEngineLight* const Light = static_cast<FEngineLight*>(OutSlots[I]);
            if (EngineLightAffectsPoint(Light, Pos3, Radius))
            {
                OutSlots[OutCount] = Light;
                ++OutCount;
            }
        }
        return OutCount;
    }

    // FUN_00422440 is FogApplyColorAndRange (cli_nebula.cpp) and FUN_004102c0 is
    // EngineLightApplyList (engine_light.cpp). The local copies that stood here
    // (ApplyFloatRgbToFogStates, ApplyCollectedLights with an extra StateObj argument) were
    // removed 2026-09-25 (EXE-W2-E).

    // FUN_005554d0 (cdecl, 6 args; callers 00518574 / 005227c1 / 00539bb1, ADD ESP,0x18):
    //   a0 Pos3, a1 Radius, a2 Camera (FUN_004102c0's 3rd arg, handed on to FUN_004104e0),
    //   a3 the containing nebula (Gameobj +0x38; nebula vtable PTR_DAT_005dacbc),
    //   a4 the float lerp factor (Gameobj +0x3c), a5 Flag (FUN_00537f40's last arg).
    void StarSysApplyObjectLighting(float const* Pos3, float Radius, void* Camera,
                                    unsigned DrawStateA, unsigned DrawStateB,
                                    unsigned char Flag)
    {
        Win3DDisable(kWin3DFog); // 0x005554d8 FUN_00421ed0(0xb60)

        if (GetGameOption(kGameOptLighting) == 0)
        {
            EngineLightClearRegisteredList();
            unsigned char White[3] = {0xff, 0xff, 0xff};
            EngineLightSetAmbientRgbBytes(White);
            return;
        }

        int const AmbientOpt = GetGameOption(kGameOptAmbient);
        for (int C = 0; C < 3; ++C)
        {
            int Sum = static_cast<int>(G_AmbientBaseRgb[C]) + AmbientOpt;
            if (Sum > 0xff)
                Sum = 0xff;
            G_AmbientWorkRgb[C] = static_cast<unsigned char>(Sum);
        }

        // 00555578: EBX = &DAT_0067986c; 00555576/0055557d: no nebula -> straight to the tail.
        unsigned char const* AmbientPtr = G_AmbientWorkRgb;
        FNebulaEffect* const Nebula = (FNebulaEffect*)(uintptr_t)DrawStateA;
        if (Nebula != nullptr)
        {

            // 00555588: +0x48 (LAB_00509770) -- the nebula's ambient bytes, or null (0055558f
            // skips the lerp but not the fog block).
            using FSlot48 = unsigned char const*(__thiscall*)(void*);
            unsigned char const* const Src = Nebula->GetAmbientBytes();
            if (Src != nullptr)
            {
                // 00555595..00555622, per channel: FILD (src - work) * T (float [ESP+0x40] =
                // a4) + FIADD work + FADD 0.5 (double DAT_005c75e8), ftol, store AL. No
                // clamp: the low byte of the truncated int is kept. The product of a 9-bit int
                // and a float is exact in double, as in the x87 register.
                float T = 0.0f;
                memcpy(&T, &DrawStateB, sizeof(float));
                for (int C = 0; C < 3; ++C)
                {
                    int const Work = static_cast<int>(G_AmbientWorkRgb[C]);
                    int const Diff = static_cast<int>(Src[C]) - Work;
                    double const V = static_cast<double>(Diff) * static_cast<double>(T) +
                                     static_cast<double>(Work) + 0.5;
                    G_AmbientLerpRgb[C] = static_cast<unsigned char>(static_cast<int>(V));
                }
                AmbientPtr = G_AmbientLerpRgb; // 00555627: EBX = &DAT_00679868
            }

            // 0055562c..00555655: FUN_00422440(+0x30(&Out), +0x38(), +0x3c(Pos3)) with the
            // arguments evaluated right to left:
            //   +0x3c FUN_00509680 (thiscall, RET 4): the fog end for a position;
            //   +0x38 LAB_00509660 (thiscall, RET 0): the fog start (+0x2ac);
            //   +0x30 LAB_00509480 (thiscall, RET 4): copy the fog colour (+0x2a0) to Out.
            using FSlot3c = float(__thiscall*)(void*, float const*);
            using FSlot38 = float(__thiscall*)(void*);
            using FSlot30 = float const*(__thiscall*)(void*, float*);
#ifdef FREELANCER_RP9
            // Port-only: while the Direct3D 9 volumetric nebula draws this nebula's inside
            // (nebula_volume.cpp) it fogs the object by its depth; the D3D fog stays off (the
            // FUN_00421ed0(0xb60) above). The ambient lerp above is kept.
            if (!NebulaVolume::ReplacesObjectFog(Nebula))
#endif
            {
                float const FogEnd = Nebula->FogFactor(Pos3);
                float const FogStart = Nebula->GetFloat2AC();
                float FogColor[3];
                float const* const FogRgb = Nebula->CopyFogRgb(FogColor);
                FogApplyColorAndRange(FogRgb, FogStart, FogEnd);
            }
        }

        // LAB_00555659 (shared tail): FUN_00410240(EBX); FUN_00537f40(buf, 4, a0, a1, a5);
        // FUN_004102c0(buf, count, a2, a0).
        EngineLightSetAmbientRgbBytes(AmbientPtr);

        void* LightSlots[4] = {};
        int const Count = CollectNearbyLights(LightSlots, 4, Pos3, Radius, Flag);
        EngineLightApplyList(reinterpret_cast<FEngineLight* const*>(LightSlots),
                             static_cast<unsigned>(Count), static_cast<ICamera*>(Camera), Pos3);
    }

    // The GameObject+0x28 list's 0x18-byte node: next, prev, then the 0x10-byte effect
    // companion {slot0, ISoundSource*, particle, GameLight} that FUN_00502d90 clears.
    struct FSweepEffectNode
    {
        FSweepEffectNode* Next;  // +0x00
        FSweepEffectNode* Prev;  // +0x04
        void* Slot0;             // +0x08  companion +0
        ISoundSource* Sound;     // +0x0c  companion +4
        unsigned char* Particle; // +0x10  companion +8
        FGameLight* Light;       // +0x14  companion +0xc
    };
    static_assert(sizeof(FSweepEffectNode) == 0x18, "effect list node 0x18");

    // FUN_004fd040 (thiscall on the list, GameObject+0x28; callers 0x005265ea, 0x005399d2) --
    // walk from the sentinel's next ([+4]->[0]) until the node is the sentinel again (re-read
    // from [+4] each lap, 0x004fd0be). A node whose particle is live (non-null, +8 bit 1 clear,
    // 0x004fd05e) is kept. So is one whose sound's IsActive (vt+0x14, 0x004fd06a) returns
    // non-zero, and one whose light's anim keys (+0xd4) are null (0x004fd080). Every other node
    // is unlinked (prev->next = next, next->prev = prev), its companion cleared (FUN_00502d90),
    // the node freed (FUN_005b7e1d), and the count (+8) decremented. A kept node with a particle
    // gets particle +8 |= 1 and FUN_004f7af0. No test of the list or the sentinel.
    void GameobjSweepEffectList(FGameobjEffectList* List)
    {
        FSweepEffectNode* Node = static_cast<FSweepEffectNode*>(List->Head)->Next;
        while (Node != static_cast<FSweepEffectNode*>(List->Head))
        {
            bool bKeep = Node->Particle != nullptr && (Node->Particle[8] & 2u) == 0;
            if (!bKeep && Node->Sound != nullptr && Node->Sound->IsActive() != 0)
                bKeep = true;
            if (!bKeep && Node->Light != nullptr && Node->Light->Anim.Keys == nullptr)
                bKeep = true;

            if (!bKeep)
            {
                FSweepEffectNode* const Next = Node->Next;
                Node->Prev->Next = Next;
                Node->Next->Prev = Node->Prev;
                CliEffectCompanionClear(&Node->Slot0);
                ::operator delete(Node);
                List->Count = List->Count - 1;
                Node = Next;
                continue;
            }

            if (Node->Particle != nullptr)
            {
                Node->Particle[8] = static_cast<unsigned char>(Node->Particle[8] | 1u);
                EffectTypeListLink(Node->Particle);
            }
            Node = Node->Next;
        }
    }

    // FUN_00539b40 (__thiscall, RET 4) — Gameobj vtable +0x184 (NOT a data field). No null
    // tests: 0x00539b46 reads the vtable straight away.
    //   0x00539b4f: radius = own slot +0x178(&center) (FSTP float);
    //   0x00539b62: get_type (+0x88); flag = (type & 0x807) == 0 (NEG/SBB/INC);
    //   0x00539b7c: FUN_004f70c0 = 1.0 - perf, left on the FPU, FCOMP double 0.999
    //               (DAT_005c8b68), TEST AH,5 / JP -- strictly below clears the flag;
    //   0x00539bae: get_position (slot 0), then FUN_005554d0(pos, radius, Camera (this
    //               method's own stack argument), +0x38, +0x3c, flag).
    void GameobjUpdateDrawLighting(FClientGameobj* Self, void* Camera)
    {
        Vector Center;
        float const Radius = Self->Slot178(Center);

        unsigned Type;
        Self->get_type(Type);
        unsigned char Flag = static_cast<unsigned char>(((Type & 0x807u) == 0) ? 1 : 0);
        if (EffectPerfOneMinus() < 0.999)
            Flag = 0;

        Vector const& Pos = Self->get_position();
        StarSysApplyObjectLighting(&Pos.x, Radius, Camera, Self->DrawStateA, Self->DrawStateB,
                                   Flag);
    }


    // FUN_0051f2c0's node push (0x0051f3e4..0x0051f431 / 0x0051f5ba..0x0051f6c1): Heap::Malloc
    // on DAT_00675350; a node gets {vtable 0x005dc364, DistSq, Gameobj, LOD}; the pointer --
    // null too, when the heap failed -- is appended to bucket vector Bucket.
    void PushNode(unsigned Bucket, IObjInspect* Inspect, float DistSq, unsigned LodWord)
    {
        void* const Mem = G_DrawNodeHeap.Malloc();
        FZItem* Node = nullptr;
        if (Mem != nullptr)
        {
            FZItemGameObject* const GameObjectItem = new (Mem) FZItemGameObject();
            GameObjectItem->DistSq = DistSq;
            GameObjectItem->Object = Inspect;
            GameObjectItem->LodWord = LodWord;
            Node = GameObjectItem;
        }
        G_Buckets[Bucket].Nodes.push_back(Node);
    }

    // The ZItemAsteroidField / ZItemPlanetRing pushes inlined in FUN_0051ba20 / FUN_0051be10
    // (FUN_0051ec80(0x10) at 0x0051bbde, 0x0051bc23, 0x0051bd37, 0x0051bd99, 0x0051bf88,
    // 0x0051bfd0, 0x0051c111, 0x0051c182): a non-null block gets {vftable 0x005dc034 /
    // 0x005dc06c, +4 key, +8 object, +0xc piece}; the pointer -- null when the heap failed --
    // is appended to bucket 0 (FUN_0051efc0) or 2 (FUN_0051f140), the literal each site picks.
    // No test of the object or the bucket at any of the eight sites; Kind stands for the
    // vftable constant the site stores.
    void PushTypedNode(unsigned Bucket, EDrawNodeKind Kind, void* Object, float DistSq,
                       unsigned PieceOrLod)
    {
        void* const Mem = ZItemAlloc(0x10);
        FZItem* Node = nullptr;
        if (Mem != nullptr)
        {
            if (Kind == kDrawNodeAsteroidFar)
            {
                FZItemAsteroidField* const FieldItem = new (Mem) FZItemAsteroidField();
                FieldItem->LodWord = PieceOrLod;
                Node = FieldItem;
            }
            else
            {
                FZItemPlanetRing* const RingItem = new (Mem) FZItemPlanetRing();
                RingItem->LodWord = PieceOrLod;
                Node = RingItem;
            }
            Node->DistSq = DistSq;
            Node->Object = Object;
        }
        G_Buckets[Bucket].Nodes.push_back(Node);
    }

    // FUN_0040fb60 — thiscall on camera, RET 8: write ZNear (+0xa4) and ZFar (+0xa8). No test of
    // `this` (0x0040fb60..0x0040fb74).
    void CameraSetNearFar(FSpaceCamera* Cam, float ZNear, float ZFar)
    {
        Cam->ZNear = ZNear;
        Cam->ZFar = ZFar;
    }

    // FUN_0051f2c0 (thiscall on the queue DAT_00675360, RET 8): (Gameobj, camera). No test of
    // either pointer.
    void EnqueueObject(FClientGameobj* Go, FSpaceCamera* Cam)
    {
        ICamera* const Camera = AsCamera(Cam);

        // 0x0051f2ce..0x0051f318: camera position (vt+0xc) copied; object position (vt+0);
        // the difference built through FUN_00422b80 from three float-rounded components
        // (z, y, x in that order).
        float const* const CamPosPtr = Camera->GetPosition();
        const float CamX = CamPosPtr[0];
        const float CamY = CamPosPtr[1];
        const float CamZ = CamPosPtr[2];
        Vector const& ObjPos = Go->get_position();
        const float Dz = static_cast<float>(static_cast<double>(ObjPos.z) - CamZ);
        const float Dy = static_cast<float>(static_cast<double>(ObjPos.y) - CamY);
        const float Dx = static_cast<float>(static_cast<double>(ObjPos.x) - CamX);

        // 0x0051f31d..0x0051f352: y*y + z*z + x*x, stored float; the unrounded sum FCOMPP'd
        // against DAT_006164d0 squared, TEST AH,1 / JNZ: below (or unordered) is near.
        const double DistSqRaw = static_cast<double>(Dy) * Dy + static_cast<double>(Dz) * Dz +
                                 static_cast<double>(Dx) * Dx;
        float DistSq = static_cast<float>(DistSqRaw);
        const double RangeSq = static_cast<double>(G_SpaceDrawRange) * G_SpaceDrawRange;

        if (DistSqRaw >= RangeSq)
        {
            // 0x0051f358..0x0051f36e: far -- only a ManagerKind 2 object (a solar), and only
            // outside a field's core.
            if (Go->ManagerKind != 2)
                return;
            if (G_FieldEnclosure == 2)
                return;

            // 0x0051f374..0x0051f3b0: the LOD pick (vt+0x17c, no cached LOD) against the 250000
            // far plane, then the plane back to the pass's far (queue +8).
            CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
            unsigned Lod = 0;
            Go->Slot17c(&Lod, Cam, nullptr);
            CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);

            // 0x0051f3b5..0x0051f3c7: low word >= 3 (unsigned), high word < 2 (signed).
            if (static_cast<unsigned short>(Lod) < 3)
                return;
            if (static_cast<short>(Lod >> 16) >= 2)
                return;

            // 0x0051f3cd..0x0051f3de: vt+0x18c (DrawStateA) must be 0.
            unsigned DrawStateB = 0;
            if (Go->Slot18c(&DrawStateB) != 0)
                return;

            PushNode(2, Go, DistSq, Lod); // DAT_00675310, bucket 2
            return;
        }

        // 0x0051f440..0x0051f497: a solar (kind 2) whose narrowed CSolar (0x303, else null --
        // read unchecked) has any of TypeBits 0..2 and whose +0xe4 is 0: DistSq -= r * r with r
        // from vt+0x178, stored float.
        if (Go->ManagerKind == 2)
        {
            CObject* const Obj = Go->Object;
            CSolar* Solar = nullptr;
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
                Solar = static_cast<CSolar*>(Obj);
            if ((static_cast<unsigned char>(Solar->TypeBits) & 7u) != 0 &&
                static_cast<FClientSolarGameobj*>(Go)->Field_0xe4 == 0)
            {
                Vector Center;
                const double Radius = Go->Slot178(Center);
                DistSq = static_cast<float>(static_cast<double>(DistSq) - Radius * Radius);
            }
        }

        // 0x0051f499..0x0051f538: with a field controller, vt+0x18c's second word (written to
        // a float local that starts at 0) is the enclosure E.
        unsigned Bucket = 0;
        if (G_FieldController != nullptr)
        {
            unsigned EncloseBits = 0;
            const unsigned DrawStateA = Go->Slot18c(&EncloseBits);
            float Enclose = 0.0f;
            memcpy(&Enclose, &EncloseBits, sizeof(float));

            float E;
            if (reinterpret_cast<void*>(static_cast<uintptr_t>(DrawStateA)) != G_FieldController)
            {
                // 0x0051f4c3..0x0051f4de: another enclosure -- above 0.25 (double 0x005cbf58)
                // is dropped (TEST AH,0x41 / JZ); otherwise E = 0.0f.
                if (static_cast<double>(Enclose) > 0.25)
                    return;
                E = 0.0f;
            }
            else
            {
                // 0x0051f4e0..0x0051f4f1: this controller's enclosure -- strictly above 0 picks
                // bucket 1.
                E = Enclose;
                if (Enclose > 0.0f)
                    Bucket = 1;
            }

            if (G_FieldEnclosure == 2)
            {
                // 0x0051f500..0x0051f50b: FCOMP 0.0f, TEST AH,0x41 / JNP -- below or equal is
                // dropped; above (or unordered) goes on.
                if (E <= 0.0f)
                    return;
            }
            else if (G_FieldEnclosure == 0)
            {
                // 0x0051f513..0x0051f522 + 0x0051f538: E >= 1.0f is dropped (TEST AH,1 / JZ).
                if (E >= 1.0f)
                    return;
            }
            else
            {
                // 0x0051f524..0x0051f538: E >= 1.0f and DistSq strictly above DAT_00675248 is
                // dropped (TEST AH,0x41 / JZ); E below 1.0f (or unordered) goes on.
                if (E >= 1.0f && DistSq > G_FieldRadiusSq)
                    return;
            }
        }

        // 0x0051f53e..0x0051f561: the LOD pick (vt+0x17c, no cached LOD); low word >= 3, high
        // word < 2.
        unsigned Lod = 0;
        Go->Slot17c(&Lod, Cam, nullptr);
        if (static_cast<unsigned short>(Lod) < 3)
            return;
        if (static_cast<short>(Lod >> 16) >= 2)
            return;

        // 0x0051f567..0x0051f584: DistSq strictly below 40000 (.rdata 0x005dc35c; TEST AH,5 /
        // JP keeps the bucket on >= and unordered) -> bucket = (DAT_0067524c != 0).
        if (DistSq < kNearBucketDistSq)
            Bucket = (G_FieldEnclosure != 0) ? 1u : 0u;

        // 0x0051f586..0x0051f5b7: a CEqObj (0x103) whose archetype (CObject+0x88, read as the
        // field) has the nomad byte (+0x80) set goes three buckets up.
        CObject* const Obj = Go->Object;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u)
        {
            Archetype::EqObj const* const Arch =
                static_cast<Archetype::EqObj const*>(Obj->Archetype);
            if (Arch != nullptr && Arch->bNomad)
                Bucket += 3;
        }

        PushNode(Bucket, Go, DistSq, Lod);
    }

    // FUN_0051f7d0 (thiscall on the queue, RET 8): the ObjDB bucket head for ManagerKind
    // (FUN_00540fc0, handed the argument's own address), then every Gameobj down the +0x14
    // chain to FUN_0051f2c0. No other test (0x0051f7d0..0x0051f803).
    void EnqueueByManagerKind(int ManagerKind, FSpaceCamera* Cam)
    {
        FClientGameobj* Go = ClientObjDb_GetKindHead(&ManagerKind);
        for (; Go != nullptr; Go = Go->ListNext)
            EnqueueObject(Go, Cam);
    }

    // MSVC6 _ISORT_MAX — retail compares element count to 0x11 / byte span to 0x40.
    constexpr int kSortInsertThreshold = 0x11; // 17

    float NodeKey(FZItem* Node)
    {
        return Node->DistSq;
    }

    // Median of {A,B,C} by DistSq — listing FUN_0051fc90 (0051fcb8..0051fd0c)
    // and FUN_0051eda0 (0051ee40..0051ee9f). Same three-way result.
    FZItem* MedianOfThree(FZItem* FirstNode, FZItem* MidNode,
                                  FZItem* LastNode)
    {
        float const KFirst = NodeKey(FirstNode);
        float const KMid = NodeKey(MidNode);
        float const KLast = NodeKey(LastNode);
        FZItem* Pivot = FirstNode;
        if (KFirst <= KMid)
        {
            if (KFirst <= KLast)
            {
                Pivot = MidNode;
                if (KLast < KMid)
                    Pivot = LastNode;
            }
        }
        else
        {
            Pivot = MidNode;
            if (KMid <= KLast)
            {
                Pivot = FirstNode;
                if (KLast < KFirst)
                    Pivot = LastNode;
            }
        }
        return Pivot;
    }

    // FUN_0051fd90 — Hoare-style partition, DistSq descending (far first).
    // Pivot is a node object*; returns the split iterator.
    FZItem** PartitionDescending(FZItem** First, FZItem** Last,
                                         FZItem* Pivot)
    {
        float const PivotKey = NodeKey(Pivot);
        while (true)
        {
            while (PivotKey < NodeKey(*First))
                ++First;
            do
            {
                --Last;
            } while (NodeKey(*Last) < PivotKey);
            if (Last <= First)
                break;
            FZItem* Tmp = *First;
            *First = *Last;
            *Last = Tmp;
            ++First;
        }
        return First;
    }

    // Guarded insertion for [First, Last): rotate-to-front when key > *First.
    // Used for n < 17 and for the first 16 of the introsort finish.
    void InsertionSortGuarded(FZItem** First, FZItem** Last)
    {
        if (First == Last)
            return;
        FZItem** Next = First + 1;
        while (Next != Last)
        {
            FZItem* Key = *Next;
            FZItem** Hole = Next;
            if (NodeKey(*First) < NodeKey(Key))
            {
                for (; First != Hole; --Hole)
                    *Hole = *(Hole - 1);
                *First = Key;
            }
            else
            {
                while (true)
                {
                    if (!(NodeKey(*(Hole - 1)) < NodeKey(Key)))
                        break;
                    *Hole = *(Hole - 1);
                    --Hole;
                }
                *Hole = Key;
            }
            ++Next;
        }
    }

    // Unguarded insertion from First to Last (retail final pass after first 16 sorted).
    void InsertionSortUnguarded(FZItem** First, FZItem** Last)
    {
        for (FZItem** Next = First; Next != Last; ++Next)
        {
            FZItem* Key = *Next;
            FZItem** Hole = Next;
            while (true)
            {
                if (!(NodeKey(*(Hole - 1)) < NodeKey(Key)))
                    break;
                *Hole = *(Hole - 1);
                --Hole;
            }
            *Hole = Key;
        }
    }

    // FUN_0051fc90 — recursive introsort body; leaves runs < 17 for the top finish.
    void SortRangeIntrosort(FZItem** First, FZItem** Last)
    {
        for (;;)
        {
            int const Count = static_cast<int>(Last - First);
            if (Count < kSortInsertThreshold)
                return;

            FZItem* Pivot = MedianOfThree(*First, *(First + Count / 2), *(Last - 1));

            FZItem** Mid = Last;
            FZItem** Left = First;
            float const PivotKey = NodeKey(Pivot);
            while (true)
            {
                while (PivotKey < NodeKey(*Left))
                    ++Left;
                do
                {
                    --Mid;
                } while (NodeKey(*Mid) < PivotKey);
                if (Mid <= Left)
                    break;
                FZItem* Tmp = *Left;
                *Left = *Mid;
                *Mid = Tmp;
                ++Left;
            }

            // Recurse on the smaller partition; iterate on the larger (retail).
            if ((Left - First) < (Last - Left))
            {
                SortRangeIntrosort(First, Left);
                First = Left;
            }
            else
            {
                SortRangeIntrosort(Left, Last);
                Last = Left;
            }
        }
    }

    // FUN_0051eda0 — per-bucket DistSq descending. n<17: guarded insertion;
    // else partition (FUN_0051fd90) + recurse (FUN_0051fc90) until ≤16, then
    // guarded insertion of first 16 + unguarded insertion of the rest.
    void SortBucketNodes(std::vector<FZItem*>& Nodes)
    {
        FZItem** First = Nodes.empty() ? nullptr : &Nodes[0];
        FZItem** Last = First + static_cast<int>(Nodes.size());
        int const Count = static_cast<int>(Nodes.size());
        if (Count < 2)
            return;

        if (Count < kSortInsertThreshold)
        {
            InsertionSortGuarded(First, Last);
            return;
        }

        FZItem** Lo = First;
        FZItem** Hi = Last;
        while ((Hi - Lo) > 16)
        {
            int const Span = static_cast<int>(Hi - Lo);
            FZItem* Pivot = MedianOfThree(*Lo, *(Lo + Span / 2), *(Hi - 1));
            FZItem** Mid = PartitionDescending(Lo, Hi, Pivot);
            if ((Mid - Lo) < (Hi - Mid))
            {
                SortRangeIntrosort(Lo, Mid);
                Lo = Mid;
            }
            else
            {
                SortRangeIntrosort(Mid, Hi);
                Hi = Mid;
            }
        }

        InsertionSortGuarded(First, First + 16);
        InsertionSortUnguarded(First + 16, Last);
    }

    void SortBuckets()
    {
        for (unsigned B = 0; B < kDrawBucketCount; ++B)
            SortBucketNodes(G_Buckets[B].Nodes);
    }

    // FUN_00422910 — re-push Win3D perspective from camera slots before each drain.
    // Listing: GetZFar(+0x24), GetZNear(+0x20), GetAspect(+0x28), GetFovY(+0x1c)
    // leave floats on the stack (stdcall RET 4) for FUN_00422820(Fov,Aspect,Near,Far).
    void CameraApplyPerspective(FSpaceCamera* Cam)
    {
        float const ZFar = Cam->GetZFar();
        float const ZNear = Cam->GetZNear();
        float const Aspect = Cam->GetAspect();
        float const FovY = Cam->GetFovYDeg();
        Win3DSetPerspective(FovY, Aspect, ZNear, ZFar);
    }

    // FUN_004ffab0 / FUN_004ffbb0 / FUN_004ffe50 (GateTunnel draw, frame, layer strips) are
    // FGateTunnel::Draw / BuildFrame / EmitLayer in gate_tunnel.cpp.

    // FUN_004f7db0 lives in effect_base.cpp as EffectFreezeToWorld (2026-09-25). The twin that
    // was here read the watcher at +0x44 as a "mode", sampled the instance TRANSFORM's
    // translation instead of CObject::get_velocity and used the gate-tunnel math cache instead
    // of DAT_00674f50.

    // FUN_004f7810(Cam, Dt, TypeIndex) — walk DAT_00674ce4[Type] with death/LOD arms.
    void EffectTypeBucketWalk(FSpaceCamera* Cam, float Dt, int TypeIndex)
    {
        // 004f781a: `MOV EAX,[TypeIndex*0xc + DAT_00674ce4]` -- no range test.
        void* Node = G_EffectTypeBuckets[TypeIndex].Head;
        while (Node != nullptr)
        {
            unsigned char* const N = static_cast<unsigned char*>(Node);
            void* const Next = *reinterpret_cast<void**>(N + 4);
            void* const Effect = *reinterpret_cast<void**>(N + 8);
            Node = Next;

            // 0x004f783b: no null test on the node's effect or on anything read through it.
            unsigned char* const E = static_cast<unsigned char*>(Effect);
            if ((*reinterpret_cast<unsigned char*>(E + 8) & 2u) != 0)
            {
                // LAB_004f7910 — already dead: unlink + release (signed DEC, slot +0x20 at <= 0).
                EffectTypeListUnlinkAndRelease(Effect);
                continue;
            }

            // 0x004f7849..0x004f787d: EngineIndex == -1 -> translation (+0x34) += Dt * velocity
            // (+0x4c). The x and y products stay on the FPU (exact in double); the z product is
            // stored to a float local first (FSTP [local_4]) and then added.
            if (*reinterpret_cast<int*>(E + 0x40) == -1)
            {
                float* const Pos = reinterpret_cast<float*>(E + 0x34);
                float const* const Vel = reinterpret_cast<float const*>(E + 0x4c);
                float const StepZ = Dt * Vel[2];
                Pos[0] = static_cast<float>(static_cast<double>(Dt) * Vel[0] + Pos[0]);
                Pos[1] = static_cast<float>(static_cast<double>(Dt) * Vel[1] + Pos[1]);
                Pos[2] = StepZ + Pos[2];
            }

            // 0x004f788e: slot +0x14 (update/visibility) with (Dt, Cam); BL keeps its byte.
            typedef unsigned char(__thiscall * FSlot14)(void*, float, FSpaceCamera*);
            typedef unsigned char(__thiscall * FSlot18)(void*, float, FSpaceCamera*);
            typedef void(__thiscall * FSlot1c)(void*, FSpaceCamera*);
            typedef unsigned char(__thiscall * FSlot10)(void*);
            FEffectBase* const TypedEffect = (FEffectBase*)Effect;
            unsigned char const UpdateOk = TypedEffect->UpdateVisibility(Dt, Cam);

            // 0x004f7897: FUN_004fd560(+0x5c)->+0x14 (EXIST_OFFSCREEN), read without a null test.
            FEffectTypeInfo const* const Info =
                EffectTypeInfoByIndex(*reinterpret_cast<int*>(E + 0x5c));
            if (Info->VisibilityOffscreen == 0 && UpdateOk == 0)
            {
                // 0x004f78aa..0x004f78ce: the inlined FUN_004f7af0 link (same two tests).
                EffectTypeListLink(Effect);
                EffectFreezeToWorld(static_cast<FEffectBase*>(Effect));   // 0x004f78d3
                *reinterpret_cast<unsigned char*>(E + 8) =
                    static_cast<unsigned char>(*reinterpret_cast<unsigned char*>(E + 8) | 2u);
            }
            else
            {
                // 0x004f78e8: slot +0x18 (tick/draw) with (Dt, Cam); slot +0x1c (Cam) only when
                // both it and the +0x14 result are non-zero.
                // Each call re-reads the vtable from [ESI] (0x004f78e2, 0x004f78f7).
                unsigned char const DrawOk =
                    TypedEffect->TickDraw(Dt, Cam);
                if (DrawOk != 0 && UpdateOk != 0)
                {
                    TypedEffect->BuildGeometry(Cam);
                }
            }

            // 0x004f78ff: a dead effect goes straight to the unlink; a live one asks slot +0x10
            // (AlwaysAlive/IsAlive) and is unlinked only when that returns 0. The vtable is
            // re-read (MOV EDX,[ESI]) as retail does.
            if ((*reinterpret_cast<unsigned char*>(E + 8) & 2u) == 0)
            {
                if (TypedEffect->IsAlive() != 0)
                    continue;
            }
            EffectTypeListUnlinkAndRelease(Effect);   // LAB_004f7910
        }
    }

    // FUN_004f7480(Cam, Dt, TypeIndex) — effects RS/Alchemy frame; beam batch reset+flush.
    // Call sites: 0051c2bc (Type=0x25), 0051c2d0 (0x1b), 0051c7c2 (-1).
    void SpaceEffectsDrawFrame(FSpaceCamera* Cam, float Dt, int TypeIndex)
    {
        // 0x004f749c..0x004f74a8: option 0x25 gate. No camera / pipeline null checks in retail.
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;

        CameraApplyPerspective(Cam);   // FUN_00422910

        // 0x004f74b8..0x004f752b: saver A {Pipeline, 0x17 -> 4}, saver B {Pipeline, 0x1a -> 0}.
        IRenderPipeline* const Pipe = DALib::Pipeline;
        int SavedRs17 = 0;
        int SavedRs1a = 0;
        Pipe->GetRenderState(D3DRS_ZFUNC, &SavedRs17);
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        Pipe->GetRenderState(D3DRS_DITHERENABLE, &SavedRs1a);
        Pipe->SetRenderState(D3DRS_DITHERENABLE, 0);

        ScreenQuadSetMaterialKey(DACOM_CRC::GetCRC32("effects"));

        IAlchemy* Alchemy = DALib::Alchemy;
        if (Alchemy != nullptr)
        {
            unsigned char const UseBatcher =
                static_cast<unsigned char>(GetGameOption(kGameOptBatchParticles) != 0 ? 1 : 0);
            Alchemy->SetUseMaterialBatcher(UseBatcher);

            unsigned char Flags[2];
            Flags[0] = static_cast<unsigned char>(GetGameOption(kGameOptUseDynVb) == 1 ? 1 : 0);
            Flags[1] = static_cast<unsigned char>(GetGameOption(kGameOptCopyToDynVb) == 1 ? 1 : 0);
            Alchemy->SetDrawFlags(Flags);
        }

        // 0x004f75d6..0x004f75e4: MaterialBatcher +0x20 (1, -1), no null check.
        DALib::MaterialBatcher->SetBinOrder(1u, -1);

        float ClampedDt = Dt;
        if (ClampedDt > 0.1f)
            ClampedDt = 0.1f;

        unsigned char bDidBeamBatch = 0;

        if (TypeIndex < 0x2b)
        {
            if (GetGameOption(kGameOptParticles) != 0 && Alchemy != nullptr)
            {
                GENRESULT const BeginHr = Alchemy->BeginDraw();
                if (static_cast<long>(BeginHr) >= 0)
                {
                    if (TypeIndex >= 0)
                    {
                        EffectTypeBucketWalk(Cam, ClampedDt, TypeIndex);
                    }
                    else
                    {
                        for (int T = 0; T < 0x2b; ++T)
                            EffectTypeBucketWalk(Cam, ClampedDt, T);
                    }
                    Alchemy->EndDraw();
                }
            }
        }

        // 0x004f7663..0x004f76d2: TypeIndex < 0 (signed JGE skips) walks types 0x2c..0x2e. With
        // DAT_00674f58 (live beams) and DAT_00674f54 (the "code_beam" shape) both non-zero the
        // batch DAT_00674c7c is reset inline first -- +0x08 VertCount, +0x29 strip byte, +0x28
        // byte, +0x2c strip counter, +0x14 IndexCount all 0 -- then FUN_0046ecd0 (grow
        // vertices) and FUN_004fcb40(1) (ensure one index), and BL = 1 sends it through
        // FUN_004f9de0(Cam) after the walk; otherwise (0x004f77fe) BL = 0 and no flush.
        if (TypeIndex < 0)
        {
            if (G_BeamEffectLiveCount != 0 && G_BeamEffectTexture != nullptr)
            {
                bDidBeamBatch = 1;
                G_EffectVertexBatch.VertCount = 0;
                G_EffectVertexBatch.bStripMode = 0;
                G_EffectVertexBatch.Flag28 = 0;
                G_EffectVertexBatch.StripCounter = 0;
                G_EffectVertexBatch.IndexCount = 0;
                EffectVertexBatchGrowVertices(&G_EffectVertexBatch);
                EffectVertexBatchEnsureIndices(&G_EffectVertexBatch, 1);
            }
            for (int T = 0x2c; T < 0x2f; ++T)
                EffectTypeBucketWalk(Cam, ClampedDt, T);
            if (bDidBeamBatch != 0)
            {
                EffectVertexBatchFlush(AsCamera(Cam));
            }
        }

        // 0x004f76d5..0x004f7747: saver C {Pipeline, 0x17 -> 4}, saver D {Pipeline, 0xe -> 0}.
        int SavedRs17Inner = 0;
        Pipe->GetRenderState(D3DRS_ZFUNC, &SavedRs17Inner);
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        int SavedRs0e = 0;
        Pipe->GetRenderState(D3DRS_ZWRITEENABLE, &SavedRs0e);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);

        ScreenQuadFlush(Pipe, G_EffectsListKey); // FUN_00403ea0(Pipeline, DAT_00674f3c)

        // 0x004f7766..0x004f779a: D (0xe) restored, then C (0x17).
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, SavedRs0e);
        Pipe->SetRenderState(D3DRS_ZFUNC, SavedRs17Inner);

        // 0x004f77a0..0x004f77ad: MaterialBatcher +0x1c (1, 2). The 2 is EBX, loaded with the
        // literal 2 at 0x004f7729 (it doubles as the EH state byte) -- not the beam flag, whose
        // BL was overwritten there. No null check.
        DALib::MaterialBatcher->SetBinSortKind(1u, reinterpret_cast<void*>(static_cast<uintptr_t>(2u)));

        // 0x004f77b0..0x004f77e5: B (0x1a) restored, then A (0x17).
        Pipe->SetRenderState(D3DRS_DITHERENABLE, SavedRs1a);
        Pipe->SetRenderState(D3DRS_ZFUNC, SavedRs17);
    }

    void FZItemGameObject::Draw(FSpaceCamera* Cam)
    {
        // 0x0051f7a0..0x0051f7b9: the OutLod written by the slot lands in the caller's argument
        // slot and is never read.
        unsigned OutLod = 0;
        static_cast<FClientGameobj*>(static_cast<IObjInspect*>(Object))
            ->Slot188(&OutLod, Cam, LodWord, nullptr, nullptr);
    }

    int FZItemGameObject::Slot04()
    {
        int const ManagerKind = static_cast<FClientGameobj*>(static_cast<IObjInspect*>(Object))->ManagerKind;
        if (ManagerKind == 1)
            return 0;
        return (ManagerKind != 2) ? 2 : 1;
    }

    void FZItemNebula::Draw(FSpaceCamera* Cam)
    {
        // LAB_0051ba00: ECX = [this+8], [vt+0x10](Cam); ECX = [this+8] again, [vt+0x14](Cam).
        typedef void(__thiscall* FNebulaCamSlot)(void* Self, FSpaceCamera* C);
        ((FNebulaEffect*)Object)->DrawFogShell(Cam);
        ((FNebulaEffect*)Object)->DrawBoltsPass(Cam);
#ifdef FREELANCER_RP9
        // Port-only: a nebula the Direct3D 9 volumetric nebula draws from beyond the opaque
        // scene's far plane is ray-marched here, in this bucket's painter order (its retail
        // fill sheets and bits above are then left out by their own gates). A no-op otherwise.
        NebulaVolume::DrawBackground((FNebulaEffect*)Object, AsCamera(Cam));
#endif
    }

    void FZItemAsteroidField::Draw(FSpaceCamera* Cam)
    {
        CliAsteroidDrawFarPiece(static_cast<FCliAsteroid*>(Object), Cam, static_cast<int>(LodWord));
    }

    void FZItemPlanetRing::Draw(FSpaceCamera* Cam)
    {
        ((FTexturePanels*)Object)->DrawPiece(Cam, (int)LodWord);
    }

    // FUN_0051f810 (RET 0xc; Cam, Bucket, bToggleDepthRs) -- FUN_00422910(Cam), then every node
    // of bucket vector Bucket (DAT_006752f0 + Bucket * 0x10: begin +4 / end +8, both read once
    // at 0x0051f829 / 0x0051f82f): with bToggleDepthRs Pipeline vt+0xf0 (RS 7, 0) and (RS 0xe,
    // 0); node vt[0](Cam) (0x0051f872); MaterialBatcher +0x28 FlushAll(1). After the loop, with
    // bToggleDepthRs, RS 7 = 1 and RS 0xe = 1 (the last as a tail jump, 0x0051f8c5). No test of
    // the bucket index, a node, the pipeline or the batcher.
    void DrainBucket(FSpaceCamera* Cam, unsigned Bucket, bool bToggleDepthRs)
    {
        CameraApplyPerspective(Cam);

        std::vector<FZItem*>& Nodes = G_Buckets[Bucket].Nodes;
        size_t const Count = Nodes.size();
        for (size_t I = 0; I < Count; ++I)
        {
            if (bToggleDepthRs)
            {
                DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
                DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
            }
            Nodes[I]->Draw(Cam);
            DALib::MaterialBatcher->FlushAll(1u);
        }
        if (bToggleDepthRs)
        {
            DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 1);
            DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        }
    }

    // FUN_0051f8d0 (thiscall on the queue, RET 4; Cam) -- bucket 2's begin null or the vector
    // empty returns (0x0051f8d3..0x0051f8e9). Otherwise Pipeline vt+0xf0 (RS 7, 0) and (RS 0xe,
    // 0); FUN_0040fb60(Cam, Cam vt+0x20 GetZNear(), DAT_00610534 250000.0f); FUN_0051f810(Cam,
    // 2, 1). No test of the camera or the pipeline.
    void DrainBucket2Special(FSpaceCamera* Cam)
    {
        if (G_Buckets[2].Nodes.empty())
            return;
        IRenderPipeline* const Pipe = DALib::Pipeline;
        Pipe->SetRenderState(D3DRS_ZENABLE, 0);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        CameraSetNearFar(Cam, AsCamera(Cam)->GetZNear(), kDefaultZFar);
        DrainBucket(Cam, 2, true);
    }

    // FUN_0051f950 (thiscall on the queue DAT_00675360, RET 4; Cam; caller 0x0051c6f4) -- the
    // near drain around the field controller DAT_00675244. Every near/far switch is
    // FUN_0040fb60(Cam, Cam vt+0x20 GetZNear(), far) with far = DAT_00610534 (250000) or the
    // queue's +8 (DAT_00675368). No test of the camera, the pipeline, the controller's vtable
    // entries, a star entry or its slot.
    void DrainMeshAndAsteroids(FSpaceCamera* Cam)
    {
        typedef void(__thiscall* FFieldCamSlot)(void* Self, FSpaceCamera* C);
        typedef float(__thiscall* FFieldFloatSlot)(void* Self);

        ICamera* const Camera = AsCamera(Cam);
        IRenderPipeline* const Pipe = DALib::Pipeline;

        // 0x0051f956..0x0051f9a9: near/far to the pass far plane; RS 7 = 1, 0xe = 1, 0x17 = 4.
        CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
        Pipe->SetRenderState(D3DRS_ZENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);

        // 0x0051f9af..0x0051f9b6: DAT_0067524c non-zero -> the in-field path (0x0051fa72).
        if (G_FieldEnclosure == 0)
        {
            // 0x0051f9bc..0x0051fa3e: with a controller -- far, vt+0x10, pass far, drain 1,
            // far, vt+0x14, pass far. The controller's vftable is reloaded for each call.
            if (G_FieldController != nullptr)
            {
                CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
                G_FieldController->DrawFogShell(Cam);
                CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
                DrainBucket(Cam, 1, false);
                CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
                G_FieldController->DrawBoltsPass(Cam);
                CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
            }
            // 0x0051fa43..0x0051fa6f: drain 0, FUN_00509f30 (TexturePanels list, slot 5),
            // FUN_00520170, drain 3.
            DrainBucket(Cam, 0, false);
            TexturePanelsDrawAll(Cam);
            CliAsteroidTickFields(Cam);
            DrainBucket(Cam, 3, false);
            return;
        }

        // 0x0051fa72..0x0051fa85: drain 0, drain 3.
        DrainBucket(Cam, 0, false);
        DrainBucket(Cam, 3, false);

        // 0x0051fa8a..0x0051fac7: partial enclosure (== 1) -- far, controller vt+0x10, pass far.
        if (G_FieldEnclosure == 1)
        {
            CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
            G_FieldController->DrawFogShell(Cam);
            CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
        }

        // 0x0051facc..0x0051fad6: controller vt+0x0c(Cam).
        G_FieldController->DrawFogOverlay(Cam);

        // 0x0051fad9..0x0051fb3a: FUN_0050c620's star list, head->next until the sentinel.
        // Arguments are evaluated right to left: B = controller vt+0x2c() is pushed first,
        // then A = float(vt+0x24() * float(vt+0x28())) (vt+0x28 first, stored float; vt+0x24
        // after it); then star vt+0x18 (DrawBillboard, LAB_005118d0)(Cam, A, B).
        FStarSphereListNode* const Sentinel = StarSphereList()->Sentinel;
        for (FStarSphereListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            FStarSphere* const Star = Node->Sphere;
            float const B = G_FieldController->GetFloat2C8();
            float const Scale28 = G_FieldController->GetFloat2C4();
            float const Factor24 = G_FieldController->GetFloat294();
            float const A = static_cast<float>(static_cast<double>(Factor24) * Scale28);
            Star->DrawBillboard(Cam, A, B);
        }

        // 0x0051fb3b..0x0051fb79: partial enclosure -- far, controller vt+0x14, pass far.
        if (G_FieldEnclosure == 1)
        {
            CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
            G_FieldController->DrawBoltsPass(Cam);
            CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
        }

        // 0x0051fb7e..0x0051fbb4: drain 1, controller vt+0x18, FUN_00520170, drain 4, controller
        // vt+0x1c.
        DrainBucket(Cam, 1, false);
        G_FieldController->DrawExclusionShell(Cam);
        CliAsteroidTickFields(Cam);
        DrainBucket(Cam, 4, false);
#ifdef FREELANCER_RP9
        // Port-only: the volumetric nebula (nebula_volume.cpp) replaces the interior puffs.
        if (!NebulaVolume::ReplacesClouds(G_FieldController))
#endif
            G_FieldController->DrawCloudsPanels(Cam);
    }

    // DAT_00675240 -- the lazily acquired 3DMathEngine (FUN_005b73e0) shared by FUN_0051ba20
    // (0x0051bc93 / 0x0051bca1) and FUN_0051be10 (0x0051c079 / 0x0051c087); no other XREF.
    I3DMathEngine* G_PanelPieceMath = nullptr;

    // FUN_0051ba20 (thiscall on the camera, plain RET; caller FUN_0051c200 at 0x0051c37a): every
    // CliAsteroid field (FUN_005201d0's registry) becomes ZItemAsteroidField nodes. A one-piece
    // field is one node scored by its zone's exterior distance; a multi-piece field is one node
    // per piece that survives the camera's sphere test. Near go to bucket 0 (FUN_0051efc0), far
    // to bucket 2 (FUN_0051f140).
    void EnqueueAsteroidFarHelpers(FSpaceCamera* Cam)
    {
        // 0x0051ba29..0x0051ba55: RangeSq = float(DAT_006164d0 * DAT_006164d0); NearThresh =
        // float((DAT_006164d0 * 0.25 (double DAT_005cbf58))^2), squared in x87. Both are
        // computed before the option tests.
        float const RangeSq = static_cast<float>(static_cast<double>(G_SpaceDrawRange) *
                                                 G_SpaceDrawRange);
        double const NearRange = static_cast<double>(G_SpaceDrawRange) * 0.25;
        float const NearThresh = static_cast<float>(NearRange * NearRange);

        // 0x0051ba57..0x0051ba73: FUN_005792b0(0x25) and FUN_005792b0(0x2c).
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;
        if (GetGameOption(kGameOptAsteroidExt) == 0)
            return;

        struct FCtx
        {
            FSpaceCamera* Cam;
            float RangeSq;
            float NearThresh;
        } Ctx = {Cam, RangeSq, NearThresh};

        // 0x0051ba79..0x0051ba93 / 0x0051bdd4..0x0051bde2: the registry list walked head->next.
        // [node+8] is read unchecked, as CliAsteroidForEach does.
        CliAsteroidForEach(
            [](FCliAsteroid* Field, void* Raw) {

                FCtx* const C = static_cast<FCtx*>(Raw);
                FSpaceCamera* const LocalCam = C->Cam;
                // 0x0051baa3: zone = field +0x1c, no test.
                Universe::IZone* const Zone = Field->Zone;

                // 0x0051baa8: FUN_00524730 piece count.
                int const Pieces = CliAsteroidPieceCount(Field);
                if (Pieces == 1)
                {
                    // 0x0051babc..0x0051bb03: zone vt+0x10 position, camera vt+0x0c position;
                    // the difference built as a float Vector (FUN_00422b80).
                    Vector const& ZonePos = Zone->GetPos();
                    float const ZoneX = ZonePos.x;
                    float const ZoneY = ZonePos.y;
                    float const ZoneZ = ZonePos.z;
                    float const* const CamPos =
                        LocalCam->GetPosition();
                    float const Dz = ZoneZ - CamPos[2];
                    float const Dy = ZoneY - CamPos[1];
                    float const Dx = ZoneX - CamPos[0];

                    // 0x0051bb08..0x0051bb62: (y*y + z*z) + x*x kept in x87 (FST float for the
                    // bits); one Newton step of the 0x5f3759df estimate times the sum gives the
                    // length (FSTP float).
                    double const SumSq = (static_cast<double>(Dy) * Dy +
                                          static_cast<double>(Dz) * Dz) +
                                         static_cast<double>(Dx) * Dx;
                    float const SumSqF = static_cast<float>(SumSq);
                    int Bits = 0;
                    memcpy(&Bits, &SumSqF, sizeof(float));
                    Bits = 0x5f3759df - (Bits >> 1);
                    float Estimate = 0.0f;
                    memcpy(&Estimate, &Bits, sizeof(float));
                    float const Length = static_cast<float>(
                        (1.5 - static_cast<double>(Estimate) * Estimate * SumSq * 0.5) *
                        Estimate * SumSq);

                    // 0x0051bb64..0x0051bb8c: 1.0f / Length kept in x87 scales the vector in
                    // place (each component stored float).
                    double const InvLength = 1.0 / static_cast<double>(Length);
                    Vector Dir;
                    Dir.x = static_cast<float>(Dx * InvLength);
                    Dir.y = static_cast<float>(Dy * InvLength);
                    Dir.z = static_cast<float>(Dz * InvLength);

                    // 0x0051bb8e..0x0051bbb4: zone vt+0x40. FCOMP Length vs hit, TEST AH,0x41 /
                    // JP: greater or unordered -> (Length - hit)^2 stored float; otherwise the
                    // score is NearThresh.
                    float const Surface = Zone->GetRadiusAlong(Dir);
                    float Score = C->NearThresh;
                    if (!(Length <= Surface))
                    {
                        double const Over = static_cast<double>(Length) - Surface;
                        Score = static_cast<float>(Over * Over);
                    }

                    // 0x0051bbb6..0x0051bbc9: TEST AH,5 / JP keeps >= and unordered; strictly
                    // below NearThresh the score becomes NearThresh.
                    if (Score < C->NearThresh)
                        Score = C->NearThresh;

                    // 0x0051bbcd..0x0051bc63: TEST AH,5 / JP -- >= RangeSq or unordered to
                    // bucket 2, strictly below to bucket 0; node {0x005dc034, Score, field, 0}.
                    if (Score < C->RangeSq)
                        PushTypedNode(0, kDrawNodeAsteroidFar, Field, Score, 0);
                    else
                        PushTypedNode(2, kDrawNodeAsteroidFar, Field, Score, 0);
                    return;
                }

                // 0x0051bc68..0x0051bc80: camera vt+0x0c position and vt+0x40 view (both
                // __stdcall, one argument); a count <= 0 (JLE) enqueues nothing.
                float const* const CamPos =
                    LocalCam->GetPosition();
                FTransform const* const View =
                    (FTransform const*)LocalCam->GetViewTransform();
                for (int Piece = 0; Piece < Pieces; ++Piece)
                {
                    // 0x0051bc86..0x0051bc8e: FUN_00524740(&World, piece); the result is not
                    // tested.
                    float World[3] = {};
                    CliAsteroidGetPiecePos(Field, Piece, World);

                    // 0x0051bc93..0x0051bcb8: math vt+0x28 TransformXform(out, view, world).
                    if (G_PanelPieceMath == nullptr)
                        G_PanelPieceMath = Acquire3DMathEngine();
                    FVector ViewPoint;
                    G_PanelPieceMath->TransformXform(ViewPoint, *View,
                                                     *reinterpret_cast<FVector const*>(World));
                    float ViewPoint3[3] = {ViewPoint.X, ViewPoint.Y, ViewPoint.Z};

                    // 0x0051bcd8..0x0051bcee: FUN_005247f0 radius, camera vt+0x3c
                    // ClassifySphere(&view point, radius); below 3 (AX, JC) skip the piece.
                    float const Radius = CliAsteroidGetPieceRadius(Field, Piece);
                    unsigned short const Class =
                        static_cast<unsigned short>(LocalCam->ClassifySphere(ViewPoint3, Radius));
                    if (Class < 3)
                        continue;

                    // 0x0051bcf4..0x0051bd24: (z*z + x*x) + y*y of world - camera in x87,
                    // FSTP double.
                    double const Px = static_cast<double>(World[0]) - CamPos[0];
                    double const Py = static_cast<double>(World[1]) - CamPos[1];
                    double const Pz = static_cast<double>(World[2]) - CamPos[2];
                    double DistSq = (Pz * Pz + Px * Px) + Py * Py;

                    // 0x0051bd26..0x0051bd70: FCOMP RangeSq vs DistSq, TEST AH,5 / JP: RangeSq
                    // strictly below DistSq -> bucket 2 with float(DistSq), piece index.
                    if (static_cast<double>(C->RangeSq) < DistSq)
                    {
                        PushTypedNode(2, kDrawNodeAsteroidFar, Field, static_cast<float>(DistSq),
                                      static_cast<unsigned>(Piece));
                        continue;
                    }

                    // 0x0051bd72..0x0051bd93: TEST AH,0x41 / JNZ -- only NearThresh strictly
                    // above DistSq rescales: (NearThresh - DistSq) * DAT_005dc028 + NearThresh,
                    // stored double. DAT_005dc028 is the double 0x3fa99999a0000000 -- 0.05f
                    // widened, not 0.05. Then bucket 0 with its float.
                    if (static_cast<double>(C->NearThresh) > DistSq)
                        DistSq = (static_cast<double>(C->NearThresh) - DistSq) *
                                     static_cast<double>(0.05f) +
                                 C->NearThresh;
                    PushTypedNode(0, kDrawNodeAsteroidFar, Field, static_cast<float>(DistSq),
                                  static_cast<unsigned>(Piece));
                }
            },
            &Ctx);
    }

    char const kGamecamCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Gamecam.cpp"; // 005dbfe8

    // FUN_0051be10 (__thiscall on the camera, called from FUN_0051c200 at 0051c381): every
    // TexturePanels instance (FUN_00509f20's list) becomes draw nodes of kind PTR_LAB_005dc06c.
    // A one-piece ring is one node scored by its zone's exterior distance; a multi-piece ring
    // is one node per piece that survives the camera's sphere test. Near pieces go to bucket 0
    // (FUN_0051efc0), far ones to bucket 2 (FUN_0051f140). FTexturePanels has no live vtable in
    // the port, so slots 10 / 6 / 7 / 8 (+0x28 / +0x18 / +0x1c / +0x20) are called by name.
    void EnqueueTexturePanelPieces(FSpaceCamera* Cam)
    {
        // 0051be19..0051be35: (DAT_006164d0 - 5000.0f (DAT_005d3d24))^2, squared in x87 and
        // stored as a float. The difference of two floats squared is exact in a double.
        double const RangeDiff = static_cast<double>(G_SpaceDrawRange) - 5000.0;
        float const Thresh = static_cast<float>(RangeDiff * RangeDiff);

        // 0051be2d..0051be41: FUN_005792b0(0x25).
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;


        // 0051be47..0051be57: FUN_00509f20 -> [+4] sentinel; walk until back at it.
        FTexturePanelsGlobalNode* const Sentinel = TexturePanelsListSentinel();
        for (FTexturePanelsGlobalNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            FTexturePanels* const Panels = Node->Panels;
            Universe::IZone const* const Zone = Panels->GetZone(); // vt+0x28
            int const Pieces = Panels->GetPieceCount();            // vt+0x18

            if (Pieces == 1)
            {
                // 0051be83..0051beb1: camera vt+0x0c, zone vt+0x10; the difference built as a
                // float Vector (FUN_00422b80).
                float const* const CamPos = Cam->GetPosition();
                Vector const& ZonePos = Zone->GetPos();
                float const Dx = ZonePos.x - CamPos[0];
                float const Dy = ZonePos.y - CamPos[1];
                float const Dz = ZonePos.z - CamPos[2];

                // 0051beb6..0051bf36: z*z + y*y + x*x kept in x87 (FST float for the bits),
                // one Newton step of the 0x5f3759df estimate times the sum gives the length
                // (FSTP float), 1/length scales the vector in place.
                double const SumSq = static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy +
                                     static_cast<double>(Dx) * Dx;
                float const SumSqF = static_cast<float>(SumSq);
                int Bits = 0;
                memcpy(&Bits, &SumSqF, sizeof(float));
                Bits = 0x5f3759df - (Bits >> 1);
                float Estimate = 0.0f;
                memcpy(&Estimate, &Bits, sizeof(float));
                float const Length = static_cast<float>(
                    (1.5 - static_cast<double>(Estimate) * Estimate * SumSq * 0.5) * Estimate *
                    SumSq);
                double const InvLength = 1.0 / static_cast<double>(Length);
                Vector Dir;
                Dir.x = static_cast<float>(Dx * InvLength);
                Dir.y = static_cast<float>(Dy * InvLength);
                Dir.z = static_cast<float>(Dz * InvLength);

                // 0051bf3c..0051bf6f: zone vt+0x40. Past the zone surface (or unordered) the
                // score is the squared overshoot, floored at 1.0f; otherwise 1.0f.
                float const Surface = Zone->GetRadiusAlong(Dir);
                float Score = 1.0f;
                if (!(Length <= Surface))
                {
                    double const Over = static_cast<double>(Length) - Surface;
                    Score = static_cast<float>(Over * Over);
                    if (Score < 1.0f)
                        Score = 1.0f;
                }

                // 0051bf77..0051c013: below the threshold -> bucket 0, else (or unordered)
                // bucket 2; node +0x04 = score, +0x08 = panels, +0x0c = 0.
                if (Score < Thresh)
                    PushTypedNode(0, kDrawNodeTexturePanelPiece, Panels, Score, 0);
                else
                    PushTypedNode(2, kDrawNodeTexturePanelPiece, Panels, Score, 0);
                continue;
            }

            // 0051c018..0051c032: camera position and view transform (vt+0x0c, vt+0x40).
            float const* const CamPos = Cam->GetPosition();
            FTransform const* const View = (FTransform const*)Cam->GetViewTransform();
            for (int Piece = 0; Piece < Pieces; ++Piece)
            {
                // 0051c038..0051c076: vt+0x20 radius, vt+0x1c centre; a null centre is only
                // reported (line 0x1ca) -- retail goes on and reads through it.
                float const Radius = Panels->GetPieceFloat(Piece);
                float const* const Center = Panels->GetPiece(Piece);
                if (Center == nullptr)
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kGamecamCppPath, 0x1ca,
                          "ring piece returned null position pointer");
                }

                // 0051c079..0051c0ca: math vt+0x28 TransformXform into view space, camera
                // vt+0x3c ClassifySphere; below 3 (AX, unsigned) the piece is skipped.
                if (G_PanelPieceMath == nullptr)
                    G_PanelPieceMath = Acquire3DMathEngine();
                FVector ViewCenter;
                G_PanelPieceMath->TransformXform(ViewCenter, *View,
                                                 *reinterpret_cast<FVector const*>(Center));
                float ViewCenter3[3] = {ViewCenter.X, ViewCenter.Y, ViewCenter.Z};
                unsigned short const Class =
                    static_cast<unsigned short>(Cam->ClassifySphere(ViewCenter3, Radius));
                if (Class < 3)
                    continue;

                // 0051c0d0..0051c0fc: (x*x + z*z) + y*y of centre - camera, in x87 (FST double).
                double const Px = static_cast<double>(Center[0]) - CamPos[0];
                double const Py = static_cast<double>(Center[1]) - CamPos[1];
                double const Pz = static_cast<double>(Center[2]) - CamPos[2];
                double const DistSq = Px * Px + Pz * Pz + Py * Py;

                // 0051c0fe..0051c14d: strictly past the threshold -> bucket 2 with the distance.
                if (DistSq > static_cast<double>(Thresh))
                {
                    PushTypedNode(2, kDrawNodeTexturePanelPiece, Panels, static_cast<float>(DistSq),
                                  static_cast<unsigned>(Piece));
                    continue;
                }

                // 0051c14f..0051c1ae: otherwise bucket 0 with the distance past the piece
                // radius (radius^2 subtracted), 0 when inside it.
                double const RadiusSq = static_cast<double>(Radius) * Radius;
                double Near = 0.0;
                if (!(DistSq < RadiusSq))
                    Near = DistSq - RadiusSq;
                PushTypedNode(0, kDrawNodeTexturePanelPiece, Panels, static_cast<float>(Near),
                              static_cast<unsigned>(Piece));
            }
        }
    }
}

I3DMathEngine* GameobjMathEngine()
{
    if (G_GameobjMathEngine == nullptr)
        G_GameobjMathEngine = Acquire3DMathEngine();
    return G_GameobjMathEngine;
}

void GameobjSweepEffectListRetail(FGameobjEffectList* List)
{
    // FUN_004fd040 -- the body is GameobjSweepEffectList above.
    GameobjSweepEffectList(List);
}

void GameobjUpdateDrawLightingRetail(FClientGameobj* Self, void* Camera)
{
    // FUN_00539b40 -- the body is GameobjUpdateDrawLighting above.
    GameobjUpdateDrawLighting(Self, Camera);
}

double SpaceEffectPerfOneMinus()
{
    // FUN_004f70c0 -- the body is EffectPerfOneMinus above.
    return EffectPerfOneMinus();
}

void SpaceApplyObjectLighting(float const* Pos3, float Radius, void* Camera, unsigned DrawStateA,
                              unsigned DrawStateB, unsigned char Flag)
{
    // FUN_005554d0 -- the body is StarSysApplyObjectLighting above.
    StarSysApplyObjectLighting(Pos3, Radius, Camera, DrawStateA, DrawStateB, Flag);
}

void SpaceCameraSetNearFar(FSpaceCamera* Cam, float ZNear, float ZFar)
{
    // FUN_0040fb60 -- the body is CameraSetNearFar above.
    CameraSetNearFar(Cam, ZNear, ZFar);
}

unsigned* GameobjPickLodRetail(FClientGameobj* Self, unsigned* OutLod, void* Camera,
                               unsigned const* CachedLod)
{
    // FUN_00539820 (thiscall, RET 0xc). 0x00539827..0x0053983a: a cached LOD whose low word is
    // non-zero is the answer.
    if (CachedLod != nullptr && static_cast<unsigned short>(*CachedLod) != 0)
    {
        *OutLod = *CachedLod;
        return OutLod;
    }

    // 0x00539843..0x00539854: the radius about Center through this object's own vt+0x178.
    Vector Center;
    const float Radius = Self->Slot178(Center);

    // 0x00539858..0x00539885: the camera's view transform (vt+0x40) applied to Center through
    // DAT_006789d0 vt+0x28 (TransformXform); the result is copied to a second local.
    ICamera* const Cam = static_cast<ICamera*>(Camera);
    FTransform const* const View = reinterpret_cast<FTransform const*>(Cam->GetViewTransform());
    I3DMathEngine* const Math = GameobjMathEngine();
    FVector Local;
    Math->TransformXform(Local, *View, reinterpret_cast<FVector const&>(Center));
    const FVector ViewPoint = Local;

    // 0x005398a0..0x005398c0: ClassifySphere (vt+0x3c) -> low word; the high word is 0xffff
    // and stays so when the class is below 3.
    const int Classify = Cam->ClassifySphere(&ViewPoint.X, Radius);
    unsigned short LodLow = static_cast<unsigned short>(Classify);
    unsigned short LodHigh = 0xffffu;
    if (LodLow >= 3)
    {
        // 0x005398c6..0x005398ee: high word 0; distance squared y*y + z*z + x*x, stored float.
        LodHigh = 0;
        const float DistSq = static_cast<float>(
            static_cast<double>(ViewPoint.Y) * ViewPoint.Y +
            static_cast<double>(ViewPoint.Z) * ViewPoint.Z +
            static_cast<double>(ViewPoint.X) * ViewPoint.X);

        // 0x005398ca..0x0053990c: the archetype's companion (+4), or 0 without an object or an
        // archetype; its +0x08 / +0x0c are read with no further test.
        unsigned char const* Companion = nullptr;
        if (Self->Object != nullptr && Self->Object->get_archetype() != nullptr)
            Companion = reinterpret_cast<unsigned char const*>(
                Self->Object->get_archetype()->Companion);
        const float DistRender = *reinterpret_cast<float const*>(Companion + 0x08);
        const float DistLod = *reinterpret_cast<float const*>(Companion + 0x0c);

        // 0x0053990c..0x00539932: +0x08 > 0 (TEST AH,0x41 / JNZ skips <= and unordered):
        // DistSq strictly below its square ends the pick (TEST AH,5 / JNP); otherwise high = 1.
        bool bDone = false;
        if (DistRender > 0.0f)
        {
            const double RenderSq = static_cast<double>(DistRender) * DistRender;
            if (DistSq < RenderSq)
                bDone = true;
            else
                LodHigh = 1;
        }
        // 0x00539939..0x0053995f: +0x0c > 0 and DistSq strictly above its square -> high = 2.
        if (!bDone && DistLod > 0.0f)
        {
            const double LodSq = static_cast<double>(DistLod) * DistLod;
            if (DistSq > LodSq)
                LodHigh = 2;
        }
    }

    // 0x00539966..0x00539970.
    *OutLod = static_cast<unsigned>(LodLow) | (static_cast<unsigned>(LodHigh) << 16);
    return OutLod;
}

unsigned* GameobjSubmitDrawRetail(FClientGameobj* Self, unsigned* OutLod, void* Camera,
                                  unsigned CachedLod, void* Matrix, void* Extra)
{
    // FUN_00539980 (thiscall, RET 0x14). 0x00539987: own vt+0x180.
    Self->Slot180();

    // 0x0053998d..0x005399ac: *vt+0x17c(&Local, Camera, &CachedLod) is written back over the
    // CachedLod argument, which every later read uses.
    unsigned PickedLod = 0;
    CachedLod = *Self->Slot17c(&PickedLod, Camera, &CachedLod);
    unsigned short const Lo = static_cast<unsigned short>(CachedLod);
    short const Hi = static_cast<short>(CachedLod >> 16);

    // 0x005399a8..0x005399c6: low word below 3 (CMP CX,3 / JC, unsigned) or high word 2 and
    // up (CMP AX,2 / JL, signed) -> *OutLod = the LOD, return.
    if (Lo < 3 || Hi >= 2)
    {
        *OutLod = CachedLod;
        return OutLod;
    }

    // 0x005399c9..0x005399d2: high word below 1 -> FUN_004fd040 on the list at +0x28.
    if (Hi < 1)
        GameobjSweepEffectList(&Self->EffectList);

    // 0x005399d7..0x005399de: own vt+0x184(Camera).
    Self->Slot184(Camera);

    // 0x005399e4..0x00539a2d: 8 unless this object is the player's IObjRW (FUN_0054baf0). For
    // the player, FUN_004022d0(&Scale) (DAT_006101d4) FCOMP double 0.75 (DAT_005debf8), TEST
    // AH,0x41 / JP: greater or unordered -> 0; otherwise FUN_00402580(get_index (vt+0x1c), 1, 0)
    // with its result unused, and 0x80.
    unsigned long Flags = 0;
    if (static_cast<void*>(static_cast<IObjRW*>(Self)) != GetPlayerObjRW())
    {
        Flags = 8u;
    }
    else
    {
        float Scale = 0.0f;
        Archetype::CliObjGetArchLodGlobals(&Scale);
        if (!(static_cast<double>(Scale) <= 0.75))
        {
            Flags = 0u;
        }
        else
        {
            BindRendererLods(reinterpret_cast<void*>(static_cast<intptr_t>(Self->get_index())), 1,
                             nullptr);
            Flags = 0x80u;
        }
    }

    // 0x00539a2f..0x00539a3f: low word 4 and high word not above 0 -> |= 0x100.
    if (Lo == 4 && Hi <= 0)
        Flags |= 0x100u;

    // 0x00539a45..0x00539a59: the matrix argument, when non-null, goes to Slot7 with |= 0x120.
    void* SubmitMatrix = nullptr;
    if (Matrix != nullptr)
    {
        SubmitMatrix = Matrix;
        Flags |= 0x120u;
    }

    // 0x00539a5f..0x00539a68: EBX = get_index (vt+0x1c).
    long const Inst = Self->get_index();

    // 0x00539a66..0x00539a8d: FUN_005b3510(0) FCOMP 0.0f (DAT_005c7474), TEST AH,0x41 / JNZ
    // (not above, NaN included, skips); high word below 1; Extra null.
    if (GetPerfOptionSlot(0) > 0.0f && Hi < 1 && Extra == nullptr)
    {
        // 0x00539a8f..0x00539ab4: the archetype companion (Root +4), 0 without an object or an
        // archetype, then its +0x10 envmap name read with no test; an empty name skips.
        Archetype::ICliObj* Companion = nullptr;
        if (Self->Object != nullptr && Self->Object->get_archetype() != nullptr)
            Companion = Self->Object->get_archetype()->Companion;
        if (Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0')
        {
            // 0x00539ab6..0x00539acf: the same lookup again for the name that is passed on.
            Archetype::ICliObj* NameCompanion = nullptr;
            if (Self->Object != nullptr && Self->Object->get_archetype() != nullptr)
                NameCompanion = Self->Object->get_archetype()->Companion;
            char const* const Envmap = Archetype::CliObjEnvmapMaterial(NameCompanion);

            // 0x00539ad2..0x00539aec: FUN_00533930(4, &Extra); on success
            // FUN_00533880(Inst, name).
            if (GetAppearanceOverrideMaterial(4u, &Extra))
                SetAppearanceInstanceValue(reinterpret_cast<void*>(static_cast<intptr_t>(Inst)),
                                   const_cast<char*>(Envmap));
        }
    }

    // 0x00539aef..0x00539b0f: DALib::Renderer vt+0x1c (Slot7)(Camera, Inst, 1.0f, Flags,
    // SubmitMatrix, Extra); no test of the renderer.
    int const Result = DALib::Renderer->RenderInstance(Camera, reinterpret_cast<void*>(static_cast<intptr_t>(Inst)),
                                              1.0f, Flags, SubmitMatrix, Extra);

    // 0x00539b14..0x00539b27: 3 and up (CMP ESI,3 / JL, signed) -> MaterialBatcher +0x28
    // FlushAll(1).
    if (Result >= 3)
        DALib::MaterialBatcher->FlushAll(1u);

    // 0x00539b2a..0x00539b32: low word = the result, high word 0xffff.
    *OutLod = static_cast<unsigned>(static_cast<unsigned short>(Result)) | 0xffff0000u;
    return OutLod;
}

namespace
{
    // The inline CEqObj fetch at 0x00533784..0x005337a6 / 0x005337e0..0x00533800: the object
    // at +0x10 when its class has all of 0x103, else null.
    CEqObj* EquippedObjEqObj(FClientEquippedObj* Self)
    {
        CObject* const Obj = Self->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
            return nullptr;
        return static_cast<CEqObj*>(Obj);
    }
}

unsigned* EquippedObjSubmitDrawRetail(FClientEquippedObj* Self, unsigned* OutLod, void* Camera,
                                      unsigned CachedLod, void* Matrix, void* Extra)
{
    // FUN_00533780 (thiscall, RET 0x14). 0x00533784..0x005337a6: the CEqObj (retail also tests
    // `this` for null there; a virtual call cannot arrive with a null `this`). Its +0x140 is
    // loaded with no test (0x005337aa) and compared with 0.0f (DAT_005c7474) by FCOM, TEST
    // AH,0x41 / JNZ: not above (NaN included) goes to 0x00533827.
    CEqObj* const EqObj = EquippedObjEqObj(Self);
    float const Fade = EqObj->CloakPercent;
    if (Fade > 0.0f)
    {
        // 0x005337bd..0x005337db: FSUBR double 1.0 (DAT_005c89b8), +0xe0 = 1, FSTP float; then
        // FUN_00533880(get_index (vt+0x1c), the float's bits).
        float const Opacity = static_cast<float>(1.0 - static_cast<double>(Fade));
        Self->Field_0xe0 = 1;
        long const Inst = Self->get_index();
        void* OpacityBits = nullptr;
        memcpy(&OpacityBits, &Opacity, sizeof(float));
        SetAppearanceInstanceValue(reinterpret_cast<void*>(static_cast<intptr_t>(Inst)), OpacityBits);

        // 0x005337e0..0x00533825: the CEqObj again; its archetype (+0x88) non-null and +0x80
        // (bNomad) clear -> FUN_00533930(3, &Extra), the result ignored.
        CEqObj* const Again = EquippedObjEqObj(Self);
        if (Again != nullptr)
        {
            Archetype::EqObj* const Arch = Again->eqobjarch();
            if (Arch != nullptr && !Arch->bNomad)
                GetAppearanceOverrideMaterial(3u, &Extra);
        }
    }
    else if (Self->Field_0xe0 != 0)
    {
        // 0x00533827..0x0053384c: +0xe0 = 0; get_index != -1 -> FUN_00532170(index).
        Self->Field_0xe0 = 0;
        long const Inst = Self->get_index();
        if (Inst != -1)
            RemoveAppearanceInstanceValue(static_cast<int>(Inst));
    }

    // 0x0053384f..0x0053386f: FUN_00539980 called directly (not through the vtable) with the
    // five arguments, Extra as possibly rewritten above; returns OutLod.
    GameobjSubmitDrawRetail(Self, OutLod, Camera, CachedLod, Matrix, Extra);
    return OutLod;
}

namespace
{
    // FUN_00504890 (thiscall on the Ship's +0x188 JumpShipEffect object, RET 4): the gate
    // tunnel at +0x24 (built at 0x005037b5), when there is one, draws for Cam -- a tail JMP to
    // FUN_004ffab0.
    void JumpShipEffectDrawTunnel(void* JumpFx, FSpaceCamera* Cam)
    {
        FGateTunnel* const Tunnel = static_cast<FJumpShipEffect*>(JumpFx)->Tunnel;
        if (Tunnel != nullptr)
            Tunnel->Draw(Cam);
    }

    // FUN_00504880: the dword at +0x20. On the JumpShipEffect object it is the descriptor's
    // colour bytes (descriptor + 0x63, stored at 0x0050360b). The nebula's vtable shares the
    // same body as its GetZone slot.
    unsigned char const* JumpShipEffectColor(void* JumpFx)
    {
        return *reinterpret_cast<unsigned char const**>(static_cast<unsigned char*>(JumpFx) +
                                                        0x20);
    }

    // The field controller's (DAT_00675244, a nebula) slots this pass calls, retail arity.
    typedef float(__thiscall* FFieldFloatFn)(void* Self);                  // +0x24, +0x38
    typedef void(__thiscall* FFieldRgbFn)(void* Self, float* OutRgb3);     // +0x34 (RET 4)
    typedef float(__thiscall* FFieldFogEndFn)(void* Self, float* CamPos3); // +0x3c (RET 4)
    typedef float(__thiscall* FFieldFarFn)(void* Self, float Unused);      // +0x44 (RET 4)


}

void SpaceObjDrawPass(FSpaceCamera* Cam, unsigned LayerFlags, unsigned Param2)
{
    // FUN_0051c200 (thiscall on the camera, RET 8). No test of `this`: the callers -- GameCamera
    // slot 0x78, FUN_0051b250 and the first-person scene slot FUN_00518420 -- run on a live
    // camera.
    FGameCamera* const Gc = reinterpret_cast<FGameCamera*>(Cam);
    ICamera* const Camera = AsCamera(Cam);
    IRenderPipeline* const Pipe = DALib::Pipeline;

    // 0x0051c210..0x0051c22f: the player's IObjRW (FUN_0054baf0); its +0x158 state 2 or 3 takes
    // the jump arm. The state is read without a class test.
    void* const PlayerRW = GetPlayerObjRW();
    if (PlayerRW != nullptr)
    {
        FClientShipGameobj* const Ship =
            static_cast<FClientShipGameobj*>(static_cast<IObjRW*>(PlayerRW));
        if (Ship->Field_0x158 == 2 || Ship->Field_0x158 == 3)
        {
            // 0x0051c235..0x0051c24d: DAT_00675244 = 0; FUN_0040fb60(GetZNear, DAT_006164d4).
            G_FieldController = nullptr;
            CameraSetNearFar(Cam, Camera->GetZNear(), G_SpaceDrawFar);

            // 0x0051c252..0x0051c26e: GetHalfFovXDeg (vt+0x18) FCOMP +0xc8, TEST AH,0x44 / JNP:
            // anything but equal (NaN included) re-applies the base through FUN_0040f8c0.
            if (Camera->GetHalfFovXDeg() != Gc->BaseHalfFovXDeg)
                SpaceCameraSetHalfFovX(Cam, Gc->BaseHalfFovXDeg);

            // 0x0051c273..0x0051c27a: FUN_00504890 on the +0x188 object, used unchecked.
            JumpShipEffectDrawTunnel(Ship->Field_0x188, Cam);

            // 0x0051c27f..0x0051c29c: the player's own draw, vt+0x188(&Lod, this, Lod, 0, 0)
            // with Lod = {word 0, word 0xffff} -- the Ship's override FUN_00546050.
            unsigned Lod = 0xffff0000u;
            Ship->Slot188(&Lod, Cam, Lod, nullptr, nullptr);

            // 0x0051c2a2..0x0051c2d5: [EFFECTS] -> FUN_004f7480(this, frame time, 0x25), then
            // the same with 0x1b; FUN_0042d680 is read afresh for each call.
            if (GetGameOption(kGameOptEffectsGroup) != 0)
            {
                SpaceEffectsDrawFrame(Cam, static_cast<float>(GetClientFrameTimeCapped()), 0x25);
                SpaceEffectsDrawFrame(Cam, static_cast<float>(GetClientFrameTimeCapped()), 0x1b);
            }

            // 0x0051c2d8..0x0051c315: FUN_00504880's colour, when there is one, becomes the
            // clear colour (+0xe8 |= 1, +0xe9..+0xeb); +0xbc = 0 on both exits (0x0051c893).
            unsigned char const* const Rgb = JumpShipEffectColor(Ship->Field_0x188);
            if (Rgb != nullptr)
            {
                const unsigned char B2 = Rgb[2];
                const unsigned char B1 = Rgb[1];
                const unsigned char B0 = Rgb[0];
                Gc->Flags = static_cast<unsigned char>(Gc->Flags | 0x1u);
                Gc->ClearColor[0] = B0;
                Gc->ClearColor[1] = B1;
                Gc->ClearColor[2] = B2;
            }
            Cam->FlagBC = 0;
            return;
        }
    }

    // 0x0051c328..0x0051c34d: FUN_0051ed20 empties the queue object DAT_00675360, which then
    // takes both arguments (+0 flags, +4 Param2); FUN_0051b730 picks the field controller.
    ClearBuckets();
    G_SceneLayerFlags = LayerFlags;
    G_DrawCtxParam2 = Param2;
    SelectFieldControllers(Cam);
#ifdef FREELANCER_RP9
    // Port-only: which nebulae the Direct3D 9 volumetric nebula draws this frame, from inside
    // and from outside (nebula_volume.cpp); the opaque scene outside a nebula's core is drawn
    // out to DAT_006164d4. A no-op on RP8 and with no nebula in range.
    NebulaVolume::BeginFrame(Camera, G_FieldController, G_FieldEnclosure,
                             GetGameOption(kGameOptEffectsGroup) != 0 &&
                                 GetGameOption(kGameOptNebula) != 0,
                             G_SpaceDrawFar);
#endif

    // 0x0051c352..0x0051c398: outside a field's core, the far helpers enqueue against the
    // 250000 far plane (DAT_00610534), which then drops back to DAT_006164d4.
    if (G_FieldEnclosure != 2)
    {
        CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
        EnqueueAsteroidFarHelpers(Cam);   // FUN_0051ba20
        EnqueueTexturePanelPieces(Cam);   // FUN_0051be10
        CameraSetNearFar(Cam, Camera->GetZNear(), G_SpaceDrawFar);
    }

    // 0x0051c39d..0x0051c3a3: GetZFar (vt+0x24), its result popped unread.
    Camera->GetZFar();

    // 0x0051c3a5..0x0051c3db: the pass's far plane -- DAT_006164d4, or inside a field's core
    // the controller's vt+0x44 (handed DAT_006164d4, which it ignores), FCOMP DAT_006164d4,
    // TEST AH,0x41 / JNZ: only a strictly larger value is cut back to DAT_006164d4.
    float Far;
    if (G_FieldEnclosure != 2)
    {
        Far = G_SpaceDrawFar;
    }
    else
    {
        Far = G_FieldController->FogFarClamp(G_SpaceDrawFar);
        if (Far > G_SpaceDrawFar)
            Far = G_SpaceDrawFar;
#ifdef FREELANCER_RP9
        // Port-only: the volumetric nebula's thinner haze would show objects popping in at
        // retail's clamp; while it is drawn the plane goes out to where its haze hides
        // everything (nebula_volume.cpp). Unchanged on RP8 / with the volume off.
        Far = NebulaVolume::InsideFarPlane(Far, G_SpaceDrawFar);
#endif
    }

    // 0x0051c3df..0x0051c3fb: FUN_0040fb60(GetZNear, far); the queue keeps it at +8.
    CameraSetNearFar(Cam, Camera->GetZNear(), Far);
    G_DrawCtxZFar = Far;

    // 0x0051c3f9..0x0051c518: [OBJECTS] and SOLAR -- the object DB's spatial bucket (slot 0xc,
    // mask 0xfdff) filled around the camera position (vt+0xc) out to DAT_00613ec8 = 10000.
    if (GetGameOption(kGameOptObjectsGroup) != 0 && GetGameOption(kGameOptSolar) != 0)
    {
        ISpatialPartition* const Bucket =
            static_cast<ISpatialPartition*>(GetClientObjDb()->GetBucket(kObjDbTypeMask));
        if (Bucket != nullptr)
        {
            Geometry::Sphere Query;
            Query.Radius = kObjDbFillRange;
            float const* const CamPos = Camera->GetPosition();
            Query.Center.x = CamPos[0];
            Query.Center.y = CamPos[1];
            Query.Center.z = CamPos[2];
            FSpatialQueryResult Result; // retail: [ESP+0x28], inside the 0x2020-byte frame
            Result.Capacity = 0x800;
            Result.Count = 0;
            // 0x0051c485..0x0051c48f: JBE on Fill's result, then on the hit count.
            if (SpatialFill(Bucket, &Query, &Result) != 0 && Result.Count != 0)
            {
                for (unsigned Index = 0; Index < Result.Count; ++Index)
                {
                    // 0x0051c4a7..0x0051c4d8: __RTDynamicCast IObjRW -> GameObject; a hit that
                    // is a GameObject and not one of the large solars (FUN_00540a40, walked
                    // below) goes to FUN_0051f2c0.
                    FClientGameobj* const Go = dynamic_cast<FClientGameobj*>(
                        static_cast<IObjRW*>(Result.Items[Index]));
                    if (Go != nullptr && !ClientObjDb_IsLargeSolar(Go))
                        EnqueueObject(Go, Cam);
                }
            }
        }

        // 0x0051c4f1..0x0051c518: then FUN_004c83c0() more objects, each FUN_004c83e0(i) to
        // FUN_0051f2c0 -- the HUD "Waypoint" item's (DAT_00674bc8) watched object, one at most
        // (hud_waypoint.h). Reached also when the bucket or its fill came back empty.
        int const WaypointObjects = HudWaypointGetObjectCount();
        for (int Index = 0; Index < WaypointObjects; ++Index)
        {
            IObjRW* const WaypointObj = HudWaypointGetObject(Index);
            EnqueueObject(static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(WaypointObj)),
                          Cam);
        }
    }

    // 0x0051c51a..0x0051c5ac: layer bit 0x40 with SOLAR and [OBJECTS] -- every large solar
    // (FUN_00540a00 / FUN_00540a20 walk DAT_00678ac0); with PLANETS off, one whose solar
    // TypeBits (+0xe0) has any of bits 0..2 is left out. The solar pointer is narrowed from the
    // CObject at +0x10 (class 0x303, else null) and read without a test.
    if ((LayerFlags & 0x40u) != 0 && GetGameOption(kGameOptSolar) != 0 &&
        GetGameOption(kGameOptObjectsGroup) != 0)
    {
        size_t It = 0;
        for (FClientGameobj* Go = ClientObjDb_LargeSolarFirst(&It); Go != nullptr;
             Go = ClientObjDb_LargeSolarNext(&It))
        {
            if (GetGameOption(kGameOptPlanets) == 0)
            {
                CObject* const Obj = Go->Object;
                CSolar* Solar = nullptr;
                if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
                    Solar = static_cast<CSolar*>(Obj);
                if ((static_cast<unsigned char>(Solar->TypeBits) & 7u) != 0)
                    continue;
            }
            EnqueueObject(Go, Cam);
        }
    }

    // 0x0051c5ae..0x0051c695: with [OBJECTS], the ManagerKind walks FUN_0051f7d0 -- each one
    // behind its own option read: bit 0x20 + SHIPS -> 1; bit 0x80 + PROJECTILES -> 4, 5, 6;
    // bit 0x800 + DEBRIS -> 7; bit 0x400 + DEBRIS -> 10.
    if (GetGameOption(kGameOptObjectsGroup) != 0)
    {
        if ((LayerFlags & 0x20u) != 0 && GetGameOption(kGameOptShips) != 0)
            EnqueueByManagerKind(1, Cam);
        const unsigned ProjectileLayer = LayerFlags & 0x80u; // EDI, kept across the calls
        if (ProjectileLayer != 0 && GetGameOption(kGameOptProjectiles) != 0)
            EnqueueByManagerKind(4, Cam);
        if (ProjectileLayer != 0)
        {
            if (GetGameOption(kGameOptProjectiles) != 0)
                EnqueueByManagerKind(5, Cam);
            if (GetGameOption(kGameOptProjectiles) != 0)
                EnqueueByManagerKind(6, Cam);
        }
        if ((LayerFlags & 0x800u) != 0 && GetGameOption(kGameOptDebris) != 0)
            EnqueueByManagerKind(7, Cam);
        if ((LayerFlags & 0x400u) != 0 && GetGameOption(kGameOptDebris) != 0)
            EnqueueByManagerKind(10, Cam);
    }

    // 0x0051c69a..0x0051c6b3: FUN_0051eda0 sorts; Pipeline vt+0xf0 (0x16, 2).
    SortBuckets();
    Pipe->SetRenderState(D3DRS_CULLMODE, 2);

    // 0x0051c6b9..0x0051c6e0: the background star sphere, unless the camera sits fully inside a
    // nebula (DAT_0067524c == 2), BACKGROUND (option 0x19) is off, or layer bit 0 is clear.
#ifdef FREELANCER_RP9
    // Port-only: while the volumetric nebula is drawn with real_background, also fully inside
    // (its haze fogs the stars with distance instead).
    if ((G_FieldEnclosure != 2 || NebulaVolume::ShowsRealBackground()) &&
        GetGameOption(kGameOptBackgroundGroup) != 0 && (LayerFlags & 1u) != 0)
#else
    if (G_FieldEnclosure != 2 && GetGameOption(kGameOptBackgroundGroup) != 0 && (LayerFlags & 1u) != 0)
#endif
        StarSphereDraw(Cam); // FUN_00552e70

    DrainBucket2Special(Cam);   // FUN_0051f8d0
    DrainMeshAndAsteroids(Cam); // FUN_0051f950

#ifdef FREELANCER_RP9
    // Port-only: the volumetric nebula over the opaque scene, before the effects (which retail
    // also draws over its fog). The scene above was drawn with near = GetZNear, far = Far.
    NebulaVolume::Draw(Camera, Camera->GetZNear(), Far);
#endif

    // 0x0051c6f9..0x0051c7d8: layer bit 0x100 -- the effects.
    if ((LayerFlags & 0x100u) != 0)
    {
        EngineLightClearRegisteredList(); // FUN_004101e0
        Pipe->SetRenderState(D3DRS_LIGHTING, 0);
        if (GetGameOption(kGameOptEffectsGroup) != 0)
        {
            if (G_FieldEnclosure != 0)
            {
                // 0x0051c741..0x0051c78e: FUN_00422440(&{0,0,0}, controller vt+0x38(),
                // controller vt+0x3c(camera position)) -- vt+0x3c is evaluated first.
                float Rgb[3] = {0.0f, 0.0f, 0.0f};
                float* const CamPos = const_cast<float*>(Camera->GetPosition());
                const float FogEnd =
                    G_FieldController->FogFactor(CamPos);
                const float FogStart =
                    G_FieldController->GetFloat2AC();
                FogApplyColorAndRange(Rgb, FogStart, FogEnd);
            }
            else
            {
                Win3DDisable(kWin3DFog); // FUN_00421ed0(0xb60)
            }
            // 0x0051c7a0..0x0051c7d1: back to the pass's far plane, FUN_004f7480(this, frame
            // time, -1), FUN_00421ed0(0xb60).
            CameraSetNearFar(Cam, Camera->GetZNear(), Far);
            SpaceEffectsDrawFrame(Cam, static_cast<float>(GetClientFrameTimeCapped()), -1);
            Win3DDisable(kWin3DFog);
        }
        // 0x0051c7d4..0x0051c7d8: GameCamera slot 0x50 (no arguments) -- FUN_0051c8b0, or the
        // empty 0x00401e90 on FirstPerson. Port camera slots take `this` explicitly (__stdcall).
        ((FGameCamera*)Cam)->ScaleEnvironmentEffects();
    }

    // 0x0051c7db..0x0051c7ed: Pipeline vt+0xf0 (0x8b, -1).
    Pipe->SetRenderState(D3DRS_AMBIENT, -1);

    // 0x0051c7f3..0x0051c894: the clear colour -- the system's space_color (FUN_00555460),
    // or, with a field controller whose vt+0x24 is >= 1.0f (FCOMP 1.0f, TEST AH,1 / JNZ skips
    // below and unordered), its vt+0x34 colour * 255.0f through ftol, byte by byte.
    unsigned char const* const SpaceColor = StarSysSpaceColor();
    unsigned char ClearR = SpaceColor[0];
    unsigned char ClearG = SpaceColor[1];
    unsigned char ClearB = SpaceColor[2];
    if (G_FieldController != nullptr)
    {
        // Port-only (FREELANCER_RP9): with the volumetric nebula's real_background the clear
        // stays the space colour; FillFogRgb still runs (it advances the background-lightning
        // timer and rand()).
        if (G_FieldController->GetFloat294() >= 1.0f)
        {
            float Col[3];
            G_FieldController->FillFogRgb(Col);
#ifdef FREELANCER_RP9
            if (!NebulaVolume::ShowsRealBackground())
#endif
            {
                ClearR = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Col[0]) * 255.0f));
                ClearG = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Col[1]) * 255.0f));
                ClearB = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Col[2]) * 255.0f));
            }
        }
    }
    Gc->Flags = static_cast<unsigned char>(Gc->Flags | 0x1u);
    Gc->ClearColor[0] = ClearR;
    Gc->ClearColor[1] = ClearG;
    Gc->ClearColor[2] = ClearB;
    Cam->FlagBC = 0; // 0x0051c894
}

void SpaceCameraRenderSlot78(FSpaceCamera* Cam, unsigned LayerFlags, unsigned Param2)
{
    // FUN_0051b250(LayerFlags, Param2) — RET 8. In-space frame: 0xffff, 0.
    if (!CameraHasWatchedObject(Cam))
        return;

    SpaceObjDrawPass(Cam, LayerFlags, Param2); // 0x0051b26e..0x0051b270

#ifdef FREELANCER_RP9
    // Port-only (no retail counterpart): the Direct3D 9 extension passes that need the finished
    // 3D scene and its depth, before the HUD clears the depth buffer below. A no-op on RP8.
    RenderEx::AfterSpaceScene();
#endif

    if (GetGameOption(kGameOptHud) != 0)
        ClearWin3DBuffers(kWin3DClearDepth);

    // 0x0051b290..0x0051b294: a direct (non-virtual) CALL FUN_0051daa0(LayerFlags, Param2) on
    // this camera -- HUDCamera's slot 0x78 body, which reads only GameCamera fields. Its two
    // stack arguments are never read. (The inline copy that stood here applied the camera
    // through the camera-less ApplyWin3DCameraToPipeline instead of FUN_00422950(this).)
    (void)Param2;
    HudCameraDraw(reinterpret_cast<FGameCamera*>(Cam));
}

void SpaceEnvFxBeginCockpitPass(FSpaceCamera* Cam)
{
    // FUN_0051c970 (thiscall on the camera). Only with options 0x25, 0x24 and 0x28 all on, in
    // that order, each result tested before the next is read.
    if (GetGameOption(0x25) == 0)
        return;
    if (GetGameOption(0x24) == 0)
        return;
    if (GetGameOption(0x28) == 0)
        return;

    // 0x0051c99f..0x0051c9d4: every entry of FUN_0050c620()'s list (+4 sentinel). Fully inside a
    // field (DAT_0067524c == 2) the entry's +0x34 is set to 1.0f; otherwise its slot 0x10 is
    // called with (camera, 1.0f).
    FStarSphereListNode* const Sent = StarSphereList()->Sentinel;
    for (FStarSphereListNode* N = Sent->Next; N != Sent; N = N->Next)
    {
        FStarSphere* const Item = N->Sphere;
        if (G_FieldEnclosure == 2)
        {
            Item->ZoneOcclusion = 1.0f;
        }
        else
        {
            Item->Draw(Cam, 1.0f);
        }
    }
}

void SpaceEnvFxEndCockpitPass(FSpaceCamera* Cam)
{
    // FUN_0051c9e0 (thiscall on the camera): the same three option tests, then slot 0x14 with
    // (camera, 1.0f) on every entry -- no field-quality branch here.
    if (GetGameOption(0x25) == 0)
        return;
    if (GetGameOption(0x24) == 0)
        return;
    if (GetGameOption(0x28) == 0)
        return;

    FStarSphereListNode* const Sent = StarSphereList()->Sentinel;
    for (FStarSphereListNode* N = Sent->Next; N != Sent; N = N->Next)
    {
        FStarSphere* const Item = N->Sphere;
        Item->DrawFlare(Cam, 1.0f);
    }
}

int SpaceCollectNearbyLights(void** OutSlots, int MaxSlots, float const* Pos3, float Radius,
                             unsigned char Flag)
{
    return CollectNearbyLights(OutSlots, MaxSlots, Pos3, Radius, Flag);
}

void SpaceResetFarClip()
{
    // FUN_00410130: DAT_006164d0 = 20000 (DAT_005c8910), DAT_006164d4 = 1000 + 20000.
    G_SpaceDrawRange = 20000.0f;
    G_SpaceDrawFar = 1000.0f + 20000.0f;
}

void SpaceSetFarClip(float FarClip)
{
    // FUN_00410150: FCOMP 1000 / TEST AH,0x41 / JP keeps FarClip only when it is > 1000 (or
    // unordered); below or equal it becomes 1000. Then DAT_006164d4 = FarClip and
    // DAT_006164d0 = FarClip - 1000.
    if (FarClip < 1000.0f)
        FarClip = 1000.0f;
    G_SpaceDrawFar = FarClip;
    G_SpaceDrawRange = FarClip - 1000.0f;
}

void* GetSpaceFieldController()
{
    // DAT_00675244.
    return G_FieldController;
}

int GetSpaceFieldEnclosure()
{
    // DAT_0067524c.
    return G_FieldEnclosure;
}

float GetSpaceFieldRadiusSq()
{
    // DAT_00675248.
    return G_FieldRadiusSq;
}

void SpaceDrawQueueRelease()
{
    // FUN_0051fbc0 (no arguments): Heap::Shrink on DAT_00675350 then DAT_00675340
    // (0x0051fbcb / 0x0051fbd2); then, per bucket vector (0x006752f8 step 0x10 while
    // < 0x00675348), free() every pointer in [begin, end) and erase the whole range
    // (0x0051fbe4..0x0051fc41; the element destroy at 0x0051fc39 is a no-op on pointers).
    // A second pass (0x0051fc4c..0x0051fc79) erases [begin, end) of every bucket again, without
    // the first pass's null / size tests -- a no-op after the first. Neither pass releases the
    // vectors' storage.
    G_DrawNodeHeap.Shrink();
    G_DrawNodeHeap12.Shrink();
    for (unsigned B = 0; B < kDrawBucketCount; ++B)
    {
        FDrawBucket& Buck = G_Buckets[B];
        for (size_t I = 0; I < Buck.Nodes.size(); ++I)
            free(Buck.Nodes[I]);
        Buck.Nodes.clear();
    }
    for (unsigned B = 0; B < kDrawBucketCount; ++B)
        G_Buckets[B].Nodes.clear();
}
