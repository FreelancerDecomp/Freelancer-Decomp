// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "cli_projectile_fx.h"

// Before anything that could instantiate std::vector<unsigned short>: Common.dll exports that
// instantiation (extern template in common_fuses.h); CliShipFireWeapons' shot-id vector must
// import it, not define its members here (LNK2005 against Common.lib otherwise).
#include "common_fuses.h"

#include "client_gameobj.h"
#include "client_objdb.h"
#include "effect_base.h"          // EffectAlloc (FUN_004f79a0)
#include "effect_vertex_batch.h"
#include "engine_light.h"
#include "globals.h"
#include "hud_command_dispatch.h"
#include "menu_sound_arch.h"
#include "nn_messaging.h"         // NnVoiceEnqueue (FUN_00489e80) -- launcher-family notifies
#include "particle_effect.h"
#include "player_behavior.h"
#include "player_spawn.h"
#include "server_connect.h"
#include "space_camera.h"
#include "star_sys.h"             // StarSysNoteTargetChange (FUN_005532d0) -- launcher fire
#include "character_select_equip.h" // FUN_004c45d0 / FUN_004c4320 -- launcher ammo
#include "vis_effect_arch.h"

#include "DALib.h"
#include "dacom.h"
#include "common_archetype.h"
#include "common_csys.h"
#include "common_iface.h"
#include "common_misc.h"
#include "common_animdb.h"
#include "common_server.h"        // IServer (DAT_0067ecd0) -- CliShipActivateEquip
#include "common_server_types.h"
#include "engine/engbase.h"
#include "soundmanager.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace
{
    constexpr float kProgressLengthPad = 7.5f; // DAT_005d9e0c
    constexpr float kProgressFallback = 50.0f; // DAT_005d440c

    // Each retail translation unit keeps its own lazily filled I3DMathEngine cache
    // (FUN_005b73e0 on first use). CliBeamActivateEffects reads IClientS.cpp's, DAT_00678a28
    // (FUN_0053f340 0x0053f3f2..0x0053f400; raw PE scan: no other reference).
    // CliExplosionAttachLocalXform shares the game light unit's DAT_006789c0 with
    // GameLightTick -- G_GameLightMath, engine_light.h.
    I3DMathEngine* G_BeamActivateMath = nullptr; // DAT_00678a28

    // s_E:\FL\Scratch\Source\Client\Space\CliLauncher.cpp_005dd484 (the FDUMP file argument).
    char const kCliLauncherCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\CliLauncher.cpp";

    void MatrixStore9(FMatrix* Dst, float A0, float A1, float A2, float A3, float A4, float A5,
                      float A6, float A7, float A8)
    {
        Dst->D[0][0] = A0;
        Dst->D[0][1] = A1;
        Dst->D[0][2] = A2;
        Dst->D[1][0] = A3;
        Dst->D[1][1] = A4;
        Dst->D[1][2] = A5;
        Dst->D[2][0] = A6;
        Dst->D[2][1] = A7;
        Dst->D[2][2] = A8;
    }

    void MatrixSetIdentity(FMatrix* Out)
    {
        std::memset(Out, 0, sizeof(FMatrix));
        Out->D[0][0] = 1.0f;
        Out->D[1][1] = 1.0f;
        Out->D[2][2] = 1.0f;
    }

    void MatrixCopyFromCommon(FMatrix* Dst, Matrix const& Src)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Dst->D[Row][Col] = Src.E[Row][Col];
        }
    }

    // The object narrowed by (ObjectClass & Mask) == Mask, null otherwise -- the test every
    // Beam function makes on +0x10 before using it.
    CObject* NarrowBeamObject(FClientBeamGameobj const* Beam, unsigned Mask)
    {
        CObject* const Obj = Beam->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }

    void CopyOwnerOrientation(FMatrix* Out, CObject* OwnerObj)
    {
        // Listing copies 9 floats from owner+8 (EngineObject orientation block).
        float const* Src = reinterpret_cast<float const*>(reinterpret_cast<unsigned char*>(OwnerObj) + 8);
        std::memcpy(Out, Src, sizeof(FMatrix));
    }

    float OwnerVelocityLength(CObject* OwnerObj)
    {
        // Retail: vtable+0x54 GetVelocity → float[3]. CObject has get_velocity via physics.
        Vector Vel = OwnerObj->get_velocity();
        return sqrtf(Vel.x * Vel.x + Vel.y * Vel.y + Vel.z * Vel.z);
    }

    // Effect-list node: next/prev + FUN_004fd480 payload (4 dwords = slots +0xC0..+0xCC).
    struct FEffectListNode
    {
        FEffectListNode* Next; // +0x00
        FEffectListNode* Prev; // +0x04
        void* Slot0;           // +0x08
        void* SlotOther;       // +0x0C
        FEffectBase* Effect;   // +0x10
        void* SlotExplosion;   // +0x14
    };
    static_assert(sizeof(FEffectListNode) == 0x18, "effect list node 0x18");

}

void BeamEffectSetProgressFromSpeed(FBeamEffect* This, float Speed)
{
    FBeamEffectArch* Arch = This->Arch;
    if (Arch != nullptr)
    {
        This->ProgressDenom80 =
            (Arch->RadiusMid + Arch->Radius1 + kProgressLengthPad) / Speed;
        return;
    }
    This->ProgressDenom80 = kProgressFallback / Speed;
}

void BeamEffectSetHitFlags(FBeamEffect* This, bool bHit, bool bHitPlayer)
{
    // FUN_004fc8f0: ((arg1 << 1) ^ f) & 2 ^ f, then ((arg2 << 2) ^ that) & 4 ^ that.
    unsigned char Flags = This->Flags70;
    Flags = static_cast<unsigned char>(((((bHit ? 1u : 0u) << 1) ^ Flags) & 2u) ^ Flags);
    Flags = static_cast<unsigned char>(((((bHitPlayer ? 1u : 0u) << 2) ^ Flags) & 4u) ^ Flags);
    This->Flags70 = Flags;
}

void CliEffectCompanionInit(void* Companion16, void* Arch, FEffectBase* Particle,
                            ISoundSource* Sound, FGameLight* Light)
{
    // FUN_005154a0 (thiscall on the companion, RET 0x10): +0x00 = Arch, +0x04 = Sound (third
    // argument), +0x08 = Particle (second), +0x0c = Light; then AddRef each non-null one --
    // the particle through FUN_004f7a80, the sound through its slot 1, the light through
    // FUN_00537ac0. Returns this.
    void** const Slots = static_cast<void**>(Companion16);
    Slots[1] = Sound;
    Slots[0] = Arch;
    Slots[2] = Particle;
    Slots[3] = Light;
    if (Particle != nullptr)
        EffectAddRef(Particle);
    if (Sound != nullptr)
        Sound->AddRef();
    if (Light != nullptr)
        GameLightAddRef(Light);
}

void CliEffectListCopyPayload(void* DstSlots, void const* SrcSlots)
{
    // FUN_004fd480 — listing 004fd480..004fd4f6 (cdecl, plain RET; the companion copy
    // constructor run as a placement new). Only the destination is tested (0x004fd49f, the
    // placement-new null test); the source is read unconditionally.
    if (DstSlots == nullptr)
        return;

    void** Dst = static_cast<void**>(DstSlots);
    void* const* Src = static_cast<void* const*>(SrcSlots);
    Dst[0] = Src[0];
    Dst[1] = Src[1];
    Dst[2] = Src[2];
    Dst[3] = Src[3];

    if (Dst[2] != nullptr)
    {
        // 0x004fd4c9: FUN_004f7a80 — RefCount at +0x58 (BeamEffect / ParticleEffect shared).
        EffectAddRef(static_cast<FEffectBase*>(Dst[2]));
    }
    if (Dst[1] != nullptr)
    {
        // 0x004fd4d5..0x004fd4d8: `MOV ECX,[EAX]; PUSH EAX; CALL [ECX+4]` -- the sound's
        // IDAComponent::AddRef (stdcall COM slot 1), no test of the vtable.
        static_cast<ISoundSource*>(Dst[1])->AddRef();
    }
    if (Dst[3] != nullptr)
        GameLightAddRef(static_cast<FGameLight*>(Dst[3])); // 0x004fd4e2: FUN_00537ac0
}

void GameLightSetFreeMotionOffset(FGameLight* Light, float const* Offset3)
{
    // FUN_005377a0 (thiscall, RET 4) -- copy 3 floats to Light+0x120, the free-motion
    // velocity FUN_00537860 integrates. No null tests.
    Light->Field120 = Offset3[0];
    Light->Field124 = Offset3[1];
    Light->Field128 = Offset3[2];
}

void CliEffectListInsert(void* ListOwnerPlus28, void const* Payload16)
{
    // FUN_004fcea0 — listing 004fcea0..004fcee3 (thiscall on the list, RET 4). this = list base
    // (+0 allocator byte, +4 head, +8 count). No null tests: the head is read at 0x004fcea5 and
    // its Prev at 0x004fcea8 unconditionally -- the owner's constructor allocates the head
    // (FGameobjEffectListStorage).
    unsigned char* ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    int& CountRef = *reinterpret_cast<int*>(ListBase + 8);

    FEffectListNode* Head = *reinterpret_cast<FEffectListNode**>(ListBase + 4);
    FEffectListNode* OldPrev = Head->Prev;
    FEffectListNode* Node =
        static_cast<FEffectListNode*>(::operator new(sizeof(FEffectListNode)));
    Node->Next = Head;
    if (OldPrev == nullptr)
        OldPrev = Node;
    Node->Prev = OldPrev;
    Head->Prev = Node;
    Node->Prev->Next = Node;

    CliEffectListCopyPayload(&Node->Slot0, Payload16);
    CountRef = CountRef + 1;
}

void CliEffectCompanionClear(void* Companion16)
{
    // FUN_00502d90 — listing 00502d90..00502ddd. No test of the companion pointer (ESI is
    // dereferenced at 0x00502d93); every caller passes an embedded member's address.
    void** const Slots = static_cast<void**>(Companion16);
    if (Slots[2] != nullptr)
    {
        EffectRelease(static_cast<FEffectBase*>(Slots[2]));
        Slots[2] = nullptr;
    }
    if (Slots[1] != nullptr)
    {
        ISoundSource* const Sound = static_cast<ISoundSource*>(Slots[1]);
        Sound->Release();
        Slots[1] = nullptr;
    }
    if (Slots[3] != nullptr)
    {
        GameLightRelease(static_cast<FGameLight*>(Slots[3]));
        Slots[3] = nullptr;
    }
    Slots[0] = nullptr;
}

void CliEffectCompanionArmStop(void* Companion16)
{
    // Prelude shared by FUN_004fcef0 / FUN_005139d0 / FUN_005137c0 before clear; the
    // JumpShipEffect dtor FUN_00503a80 inlines the same sequence three times (0x00503aa2..,
    // 0x00503b15.., 0x00503b93..). Published (was file-local) for jump_effect.cpp. No inlined
    // copy tests the companion pointer itself (0x00503aa2 reads the particle at +0x30 directly).
    void** const Slots = static_cast<void**>(Companion16);
    // 0x004fcf5f..0x004fcf68: `MOV EDX,[ECX]; CALL [EDX+4]` -- the effect's own slot 1, not a
    // particle function: the slot holds a particle, a beam or a CliEquipLight (0x00531e11),
    // whose slot 1 is FUN_004f7bd0 (EffectKill).
    if (Slots[2] != nullptr)
    {
        typedef void(__thiscall * FEffectSlot1)(void*);
        ((FEffectBase*)Slots[2])->Kill();
    }
    if (Slots[1] != nullptr)
    {
        ISoundSource* const Sound = static_cast<ISoundSource*>(Slots[1]);
        float Ident[12];
        Ident[0] = 1.0f;
        Ident[1] = 0.0f;
        Ident[2] = 0.0f;
        Ident[3] = 0.0f;
        Ident[4] = 1.0f;
        Ident[5] = 0.0f;
        Ident[6] = 0.0f;
        Ident[7] = 0.0f;
        Ident[8] = 1.0f;
        Ident[9] = 0.0f;
        Ident[10] = 0.0f;
        Ident[11] = 0.0f;
        Sound->SetPose(-1, Ident);
        ((ISoundSource*)Slots[1])->Stop();
    }
    // 0x004fcfe9..0x004fcffa: FUN_005379b0, then the light's own vtable slot 2 (+8,
    // `MOV EDX,[ECX]; CALL [EDX+8]`) -- FUN_005376f0 on a GameLight and on the CliEquipLight
    // table PTR_FUN_005da424 alike, but dispatched, not called by name.
    if (Slots[3] != nullptr)
    {
        FGameLight* const Light = static_cast<FGameLight*>(Slots[3]);
        GameLightAnimDetach(Light);
        GameLightVirtualDisable((FGameLight*)Slots[3]);
    }
}

unsigned char CliEffectListRemove(void* ListOwnerPlus28, void* Payload16)
{
    // FUN_004fcef0 — listing 004fcef0..004fd035. thiscall ECX = list base, arg = companion.
    // No test of the list, its head (0x004fcef0 MOV ECX,[ECX+4]; 0x004fcef8 MOV ESI,[ECX]) or
    // the companion (0x004fcf03 reads +8 once the list is non-empty). The node is not unlinked.
    unsigned char* const ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    FEffectListNode* const Head = *reinterpret_cast<FEffectListNode**>(ListBase + 4);

    void** const Want = static_cast<void**>(Payload16);
    for (FEffectListNode* Cursor = Head->Next; Cursor != Head; Cursor = Cursor->Next)
    {
        if (Cursor->Effect != Want[2] || Cursor->SlotOther != Want[1] ||
            Cursor->SlotExplosion != Want[3])
            continue;

        // Clear node-owned copies first (004fcef0 004fcf2c..).
        void* NodeSlots[4] = {Cursor->Slot0, Cursor->SlotOther, Cursor->Effect,
                              Cursor->SlotExplosion};
        if (NodeSlots[2] != nullptr)
        {
            EffectRelease(static_cast<FEffectBase*>(NodeSlots[2]));
            Cursor->Effect = nullptr;
        }
        if (NodeSlots[1] != nullptr)
        {
            static_cast<ISoundSource*>(NodeSlots[1])->Release();
            Cursor->SlotOther = nullptr;
        }
        if (NodeSlots[3] != nullptr)
        {
            GameLightRelease(static_cast<FGameLight*>(NodeSlots[3]));
            Cursor->SlotExplosion = nullptr;
        }
        Cursor->Slot0 = nullptr;

        CliEffectCompanionArmStop(Payload16);
        CliEffectCompanionClear(Payload16);
        return 1;
    }
    return 0;
}

void CliEffectListClear(void* ListOwnerPlus28, void* ArchFilter)
{
    // FUN_004fd0d0 — listing 004fd0d0..004fd226. ECX = the list (+0 allocator byte, +4 head,
    // +8 count). No null tests: the head is always there (the owner's ctor allocates it).
    unsigned char* const ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    FEffectListNode* const Head = *reinterpret_cast<FEffectListNode**>(ListBase + 4);
    int& CountRef = *reinterpret_cast<int*>(ListBase + 8);

    // Pass 1 (004fd0f0..004fd1db). With a filter, a node whose slot 0 is null or does not hold
    // the filter in its first dword is skipped (004fd0f4..004fd101). Every other node gets the
    // stop sequence -- particle vt+4, sound SetPose(-1, identity) + Stop, light FUN_005379b0 +
    // vt+8 (004fd107..004fd18e, the same sequence CliEffectCompanionArmStop runs) -- and then
    // the release sequence -- particle FUN_004f7a90, sound Release, light FUN_00537ad0, slot 0
    // cleared (004fd191..004fd1d3, which is FUN_00502d90's body).
    for (FEffectListNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (ArchFilter != nullptr)
        {
            if (Node->Slot0 == nullptr || *static_cast<void**>(Node->Slot0) != ArchFilter)
                continue;
        }
        CliEffectCompanionArmStop(&Node->Slot0);
        CliEffectCompanionClear(&Node->Slot0);
    }

    // Pass 2 (004fd1e1..004fd21d): erase every node, filtered or not -- unlink, FUN_00502d90 on
    // the companion (a no-op for the ones pass 1 already cleared), free, --count.
    FEffectListNode* Node = Head->Next;
    while (Node != Head)
    {
        FEffectListNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        CliEffectCompanionClear(&Node->Slot0);
        ::operator delete(Node);
        CountRef = CountRef - 1;
        Node = Next;
    }
}

namespace
{
    // FUN_004fd420 (ECX = list, RET 0xc: out iterator, Where, payload): a new 0x18 node
    // linked in before Where, the payload copied in with its references taken
    // (FUN_004fd480), ++count. A node with a null Prev links to itself (0x004fd437).
    FEffectListNode* EffectListInsertBefore(void* ListBase, FEffectListNode* Where,
                                            void const* Payload16)
    {
        int& CountRef = *reinterpret_cast<int*>(static_cast<unsigned char*>(ListBase) + 8);
        FEffectListNode* const Node =
            static_cast<FEffectListNode*>(::operator new(sizeof(FEffectListNode)));
        Node->Next = Where;
        FEffectListNode* Prev = Where->Prev;
        if (Prev == nullptr)
            Prev = Node;
        Node->Prev = Prev;
        Where->Prev = Node;
        Node->Prev->Next = Node;
        CliEffectListCopyPayload(&Node->Slot0, Payload16);
        CountRef = CountRef + 1;
        return Node;
    }

    // FUN_004f7d80 (ECX = particle, RET 4): point the particle's owner watcher (+0x44) at the
    // new owner's Watchable (Gameobj+8, null stays null). The compare-then-set_pointer is
    // BaseWatcher::set.
    void ParticleEffectSetOwner(FEffectBase* Particle, FClientGameobj* Owner)
    {
        Watchable const* const Target =
            (Owner != nullptr) ? static_cast<Watchable const*>(static_cast<IObjRW*>(Owner))
                               : nullptr;
        Particle->Watcher.set(Target);
    }
}

void CliEffectListDetachPart(void* ListOwnerPlus28, long PartIndex, FClientGameobj* NewOwner)
{
    // FUN_004fd230 (thiscall on the list, RET 8). Every effect node attached to PartIndex or
    // one of its descendants leaves this list: with NewOwner, one that is still alive moves to
    // the end of NewOwner's list; everything else is stopped and released.
    unsigned char* const ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    FEffectListNode* const* const HeadSlot = reinterpret_cast<FEffectListNode**>(ListBase + 4);
    int& CountRef = *reinterpret_cast<int*>(ListBase + 8);

    FEffectListNode* Next = (*HeadSlot)->Next;
    while (Next != *HeadSlot)
    {
        FEffectListNode* const Node = Next;
        Next = Node->Next;
        void** const Companion = reinterpret_cast<void**>(&Node->Slot0);
        FEffectBase* const Particle = static_cast<FEffectBase*>(Companion[2]);
        ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);

        // 0x004fd250..0x004fd287: the node's engine instance -- the particle's (+0x40), else the
        // sound's attach handle (slot 39), else, for a light-only node, the dword at absolute
        // address 0x40 (`a1 40 00 00 00` at 0x004fd27f: retail faults there). A node with
        // nothing, or with instance -1, stays.
        long InstIndex = 0;
        if (Particle != nullptr)
            InstIndex = Particle->EngineIndex;
        else if (Sound != nullptr)
            InstIndex = Sound->GetAttachHandle();
        else if (Companion[3] != nullptr)
            InstIndex = *reinterpret_cast<long volatile*>(static_cast<uintptr_t>(0x40));
        else
            continue;
        if (InstIndex == -1)
            continue;

        // 0x004fd28d..0x004fd29e: IsDescendant(PartIndex, instance).
        if (!IsDescendant(PartIndex, InstIndex))
            continue;

        bool bMoved = false;
        if (NewOwner != nullptr)
        {
            // 0x004fd31a..0x004fd347: still alive -- a particle without the deactivated bit
            // (Flags08 & 2), else a sound whose IsActive (slot 5) says so, else a light with no
            // animation keys (+0xd4).
            bool bAlive = false;
            if (Particle != nullptr && (Particle->Flags08 & 2u) == 0u)
                bAlive = true;
            else if (Sound != nullptr && Sound->IsActive() != 0)
                bAlive = true;
            else if (Companion[3] != nullptr &&
                     static_cast<FGameLight*>(Companion[3])->Anim.Keys == nullptr)
                bAlive = true;

            if (bAlive)
            {
                // 0x004fd349..0x004fd36f: FUN_004fd420 at the end of NewOwner+0x28, then the
                // particle's owner watcher (FUN_004f7d80).
                EffectListInsertBefore(&NewOwner->EffectList,
                                       static_cast<FEffectListNode*>(NewOwner->EffectList.Head),
                                       Companion);
                if (Particle != nullptr)
                    ParticleEffectSetOwner(Particle, NewOwner);
                bMoved = true;
            }
        }

        if (!bMoved)
        {
            // 0x004fd2af..0x004fd315 / 0x004fd371..0x004fd3d0: the stop sequence, then
            // FUN_00502d90 (0x004fd3d5).
            CliEffectCompanionArmStop(Companion);
            CliEffectCompanionClear(Companion);
        }

        // 0x004fd3da..0x004fd403: unlink, FUN_00502d90 (drops this node's references after a
        // move), free, --count.
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        CliEffectCompanionClear(Companion);
        ::operator delete(Node);
        CountRef = CountRef - 1;
    }
}

void CliExplosionAttachLocalXform(void* ExplosionFx, int EngineIndex, FTransform const* LocalXform)
{
    // FUN_005377c0 (thiscall on the game light, RET 8). No null test inside: all eight retail
    // call sites (0x0050314a, 0x005042c8, 0x0050442b, 0x005045e4, 0x0053f575, 0x005441c2,
    // 0x00545961, 0x005480fa) test the light (TEST/CMP ECX + JZ) before the CALL.
    unsigned char* const Base = static_cast<unsigned char*>(ExplosionFx);

    // 0x005377d1: +0xec = the engine index; 0x005377d7..0x005377e4: REP MOVSD 0xc -> +0xf0.
    *reinterpret_cast<int*>(Base + 0xec) = EngineIndex;
    std::memcpy(Base + 0xf0, LocalXform, sizeof(FTransform));

    // 0x005377e6..0x005377f1: DALib::Engine vt+0xa0 (GetInstanceTransform) on the index.
    float* const InstRaw =
        DALib::Engine->GetInstanceTransform(reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)));

    // 0x005377f9..0x00537807: DAT_006789c0, FUN_005b73e0 when still null.
    if (G_GameLightMath == nullptr)
        G_GameLightMath = Acquire3DMathEngine();

    // 0x0053780c..0x00537816: vt+0x1c MulTransform(temp, instance, +0xf0).
    FTransform World;
    G_GameLightMath->MulTransform(World, *reinterpret_cast<FTransform const*>(InstRaw),
                                  *reinterpret_cast<FTransform const*>(Base + 0xf0));

    // 0x00537819..0x0053782f: the result copied to a second local, FUN_00410fe0(this, &copy)
    // -- the {Orient[9], Pos[3]} block is the FTransform's own layout.
    FTransform const Copy = World;
    EngineLightSetTransform(static_cast<FEngineLight*>(ExplosionFx), &Copy.Rotation.D[0][0],
                            &Copy.Translation.X);

    // 0x00537835..0x00537845: +0x120/+0x124/+0x128 = 0.
    *reinterpret_cast<float*>(Base + 0x120) = 0.0f;
    *reinterpret_cast<float*>(Base + 0x124) = 0.0f;
    *reinterpret_cast<float*>(Base + 0x128) = 0.0f;
}

void CliBeamActivateEffects(FClientBeamGameobj* This, Vector const* LookDir,
                            unsigned char bUseLookDir)
{
    // FUN_0053f340 (thiscall on the Beam gameobj, RET 8) -- listing 0053f340..0053f60b.
    // 0x0053f34a..0x0053f36f: the class & 7 object's archetype companion (no null test on the
    // object); a null companion goes straight to Projectile's slot 92.
    Archetype::ICliObj* const Companion = NarrowBeamObject(This, 0x7u)->Archetype->Companion;
    if (Companion != nullptr)
    {
        FTransform LocalXform;
        if (bUseLookDir == 0)
        {
            // 0x0053f431..0x0053f471.
            MatrixSetIdentity(&LocalXform.Rotation);
        }
        else
        {
            // 0x0053f384..0x0053f42f: LookMatrix(*LookDir); the CBeam's orientation (+0x08,
            // class & 0x67, no null test) transposed through FUN_00447b60; the math engine
            // DAT_00678a28 (FUN_005b73e0 on first use) vt+0x20 multiplies the two.
            Matrix const Look = LookMatrix(*LookDir);
            FMatrix OwnerOri;
            CopyOwnerOrientation(&OwnerOri, NarrowBeamObject(This, 0x67u));
            FMatrix OwnerT;
            MatrixStore9(&OwnerT, OwnerOri.D[0][0], OwnerOri.D[1][0], OwnerOri.D[2][0],
                         OwnerOri.D[0][1], OwnerOri.D[1][1], OwnerOri.D[2][1], OwnerOri.D[0][2],
                         OwnerOri.D[1][2], OwnerOri.D[2][2]);
            FMatrix LookM;
            MatrixCopyFromCommon(&LookM, Look);
            if (G_BeamActivateMath == nullptr)
                G_BeamActivateMath = Acquire3DMathEngine();
            FMatrix Combined;
            G_BeamActivateMath->MulMatrix(Combined, OwnerT, LookM);
            LocalXform.Rotation = Combined;
        }
        // 0x0053f479..0x0053f4bd: zero translation.
        LocalXform.Translation.X = 0.0f;
        LocalXform.Translation.Y = 0.0f;
        LocalXform.Translation.Z = 0.0f;

        // 0x0053f4a8..0x0053f4c9: the companion's +0x58 const_effect; empty -> slot 92 only.
        ID_String const* const ConstEffect = Archetype::CliProjConstEffect(Companion);
        if (!ConstEffect->IsEmpty())
        {
            // 0x0053f4cf..0x0053f4e6: FUN_0040f080 -- no null test before VisBeam (+0x14) and
            // its nickname's first character.
            FEffectArch* const EffectArch = FindEffectArch(&ConstEffect->Id);
            if (EffectArch->VisBeam != nullptr && EffectArch->VisBeam->Nickname[0] != '\0')
            {
                // 0x0053f4e8..0x0053f506: FUN_004f79a0(0x84) + FUN_004f9540(0) into +0xd0.
                void* const Mem = EffectAlloc(0x84u);
                FBeamEffect* Beam = nullptr;
                if (Mem != nullptr)
                {
                    Beam = static_cast<FBeamEffect*>(Mem);
                    BeamEffectConstruct(Beam, 0);
                }
                This->BeamAlloc = Beam;
                // 0x0053f50c..0x0053f528: Flags70 bit 3 = bUseLookDir; Companion[2] (+0xc8) =
                // the beam, AddRef -- no null test on the allocation.
                Beam->Flags70 = static_cast<unsigned char>(
                    (((bUseLookDir << 3) ^ Beam->Flags70) & 8) ^ Beam->Flags70);
                This->Companion[2] = This->BeamAlloc;
                BeamEffectAddRef(This->BeamAlloc);
            }

            // 0x0053f52d..0x0053f532: get_index.
            int const EngineIndex = static_cast<int>(This->get_index());
            FBeamEffect* const LiveBeam = static_cast<FBeamEffect*>(This->Companion[2]);
            // 0x0053f535..0x0053f54e: the beam (FUN_004f7d20) on this gameobj.
            if (LiveBeam != nullptr)
                BeamEffectAttach(LiveBeam, This, EngineIndex, &LocalXform);
            // 0x0053f553..0x0053f562: the sound's slot +0x94 SetPose.
            ISoundSource* const Sound = static_cast<ISoundSource*>(This->Companion[1]);
            if (Sound != nullptr)
                Sound->SetPose(EngineIndex, reinterpret_cast<float const*>(&LocalXform));
            // 0x0053f568..0x0053f575: the light, FUN_005377c0.
            if (This->Companion[3] != nullptr)
                CliExplosionAttachLocalXform(This->Companion[3], EngineIndex, &LocalXform);

            // 0x0053f57a..0x0053f5e7: start the beam (its slot 0); on success its progress
            // from the CBeam's speed (vt+0x54 get_velocity, class & 0x67, no null test) into
            // +0xd0 (FUN_004fc8b0); on failure release it and clear Companion[2].
            if (This->Companion[2] != nullptr)
            {
                FBeamEffect* const StartBeam = static_cast<FBeamEffect*>(This->Companion[2]);
                if (BeamEffectStart(StartBeam, EffectArch) != 0)
                {
                    float const Speed = OwnerVelocityLength(NarrowBeamObject(This, 0x67u));
                    BeamEffectSetProgressFromSpeed(This->BeamAlloc, Speed);
                }
                else
                {
                    BeamEffectRelease(StartBeam);
                    This->Companion[2] = nullptr;
                }
            }

            // 0x0053f5f1..0x0053f5f5: FUN_004fcea0(&EffectList, &Companion).
            CliEffectListInsert(&This->EffectList, This->Companion);
        }
    }

    // 0x0053f5fa..0x0053f5fc: Projectile's slot 92 FUN_005426e0, called directly.
    This->FClientProjectileGameobj::Slot170();
}

void CliLauncherFireProjectiles(FCliLauncherFx* Launcher, Vector const* Target)
{
    // FUN_0052dde0 — listing 0052dde0..0052e094.
    // 0x0052de1a..0x0052de33: the launcher's (+0x04, no null test) vt+0x98
    // ComputeLaunchInfo(&Infos, *Target); stride 0x44.
    CELauncher* const Launch = Launcher->LaunchEquip();
    std::vector<ProjLaunchInfo> Infos;
    Launch->ComputeLaunchInfo(Infos, *Target);
    unsigned const BarrelCount = static_cast<unsigned>(Infos.size());

    for (unsigned Barrel = 0; Barrel < BarrelCount; ++Barrel)
    {
        ProjLaunchInfo const& Info = Infos[Barrel];
        // 0x0052de80..0x0052ded9: operator new(0xd4) + the Beam constructor.
        FClientBeamGameobj* const Beam = new FClientBeamGameobj();

        // 0x0052dedf..0x0052df0a: the launcher's projectile archetype (+0x4c), Root vt+0x0c
        // load(), the Beam's slot 90 create(Arch), Archetype::Root::Release.
        Archetype::Equipment* const ProjArch = Launch->Projectile;
        ProjArch->load();
        Beam->create(ProjArch);
        ProjArch->Release();

        // 0x0052df10..0x0052dfc9: the CBeam (class & 0x67) gets vt+0xa0 init with FUN_0052e0a0's
        // head, Id 0, ObjDb PTR_DAT_00613ed0, OwnerPlayer DAT_00673344, the barrel's position,
        // orientation and velocity, and +0x5c the owner's CSimple id (launcher +0x04 CEquip::Owner
        // -> +0xb0).
        CProjectile* const BeamObj = static_cast<CProjectile*>(NarrowBeamObject(Beam, 0x67u));
        CProjectile::CreateParms Parms;
        Parms.Id = 0;
        Parms.ObjDb = GetClientObjDb();
        Parms.OwnerPlayer = GetPlayerConnectionId();
        Parms.Pos = Info.Pos;
        Parms.Orient = Info.Orient;
        Parms.Velocity = Info.Velocity;
        Parms.OwnerId = static_cast<CSimple*>(Launch->Owner)->Id;
        BeamObj->init(Parms);

        // 0x0052dfcf..0x0052e025: the player's own launcher (+0x08 == FUN_0054baf0) takes the
        // barrel's velocity from ComputeProjectilePosAndVelocity_OneBarrel and looks along it;
        // any other launcher looks along the launch velocity.
        Vector LookDir = Info.Velocity;
        unsigned char bUseLook = 0;
        if (static_cast<void*>(Launcher->Host) == GetPlayerObjRW())
        {
            Vector Pos;
            bUseLook = 1;
            Launch->ComputeProjectilePosAndVelocity_OneBarrel(Pos, LookDir,
                                                              static_cast<int>(Barrel), *Target);
        }

        // 0x0052e029..0x0052e035: FUN_0053f340(&LookDir, bUseLook).
        CliBeamActivateEffects(Beam, &LookDir, bUseLook);

        // 0x0052e03a..0x0052e041: the projectile archetype's companion (Root +0x04) vt+0x20
        // Load(1).
        ProjArch->Companion->Load(1);
        // 0x0052e044..0x0052e047: FUN_00540c30(Beam, 0).
        ClientObjDb_RegisterGameobj(Beam, false);
    }
    // 0x0052e074: the vector's buffer freed (the local's destructor).
}

unsigned char FCliLauncherFx::TryFire(Vector const* Target)
{
    // FUN_0052d410 (RET 4) -- listing 0x0052d410..0x0052d50a. Slot 3 of the launcher / mine /
    // CM tables; the gun's slot 3 (FUN_0052dd00) calls it directly. No test of the equipment.
    CELauncher* const Launch = LaunchEquip();
    // 0x0052d418..0x0052d420: CELauncher::Fire (vtable +0x90).
    FireResult const Result = Launch->Fire(*Target);
    if (Result == FireOk)
    {
        // 0x0052d42f..0x0052d43b: the muzzle (FUN_0052cfd0), then vtable +0x94.
        CliLauncherPlayMuzzleFx(this);
        Launch->ConsumeFireResources();
        // 0x0052d441..0x0052d499: the player's ship (FUN_00548eb0 on the host id, vtable
        // +0x20) firing a munition that requires ammo (+0x78) takes the per-fire rounds out of
        // its inventory item: FUN_004c45d0 on the munition's nickname id (+0x08), then
        // FUN_004c4320(count > per-fire (signed JLE), item id +0x02, per-fire).
        if (IsPlayerShipId(Host->get_id()))
        {
            Archetype::Projectile const* const Munition =
                static_cast<Archetype::Projectile const*>(Launch->Projectile);
            if (Munition->bRequiresAmmo)
            {
                EquipDesc* const Item = FindPlayerNonMissionEquipRetail(Munition->get_id());
                if (Item != nullptr)
                {
                    int const PerFire = static_cast<int>(Launch->GetProjectilesPerFire());
                    unsigned char const bMoreThanOneShot = (Item->get_count() > PerFire) ? 1 : 0;
                    RemovePlayerEquipRetail(bMoreThanOneShot, Item->get_id(), PerFire);
                }
            }
        }
        // 0x0052d49c..0x0052d4aa: FUN_005532d0 with the host id (vtable +0x20).
        StarSysNoteTargetChange(Host->get_id());
        return 1;
    }
    if (Result == FireNoAmmo)
    {
        // 0x0052d4e1..0x0052d503: the player's own launcher (host == FUN_0054baf0) seen from
        // the first-person camera (FUN_0054bc80) plays its dry-fire sound (+0x14) through
        // FUN_00428680 with no key, no handle, no alternate start.
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) == GetPlayerObjRW() &&
            IsFirstPersonCameraCurrent())
        {
            PlayClientSoundArch(DryFireSoundArch, nullptr, nullptr, 0);
        }
        return 0;
    }
    if (Result > FireMuzzleCone && Result <= FireOwnerBlocked)
    {
        // 0x0052d4ba..0x0052d4de: cruise / tradelane / owner-blocked (6..8) on the player's
        // own host -> its vtable +0x1d0 (the ship's LAB_00545cc0).
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) == GetPlayerObjRW())
            static_cast<FClientShipGameobj*>(Host)->Slot1d0();
    }
    return 0;
}

unsigned char FCliGunFx::TryFire(Vector const* Target)
{
    // FUN_0052dd00 (RET 4) -- slot 3 of the gun table PTR_LAB_005dd5d8.
    // 0x0052dd0a: FUN_0052d410 called directly.
    unsigned char const bFired = FCliLauncherFx::TryFire(Target);
    if (bFired != 0)
    {
        // 0x0052dd15..0x0052dd33: CEGun::MunitionArch (its receiver null-tested, the +0x90
        // Seeker read regardless); unguided -> FUN_0052dde0.
        CEGun const* const Gun = static_cast<CEGun const*>(Equip);
        if (Gun->MunitionArch()->Seeker == 0)
            CliLauncherFireProjectiles(this, Target);
    }
    return bFired;
}

void FCliLauncherFx::FireDirect(Vector const* Target)
{
    // FUN_0052d510 (RET 4) -- slot 5 of the launcher / mine / CM tables. Target is not read.
    (void)Target;
    if (Equip == nullptr)
        return;
    // 0x0052d51b..0x0052d52e: the muzzle; vtable +0x94 only outside single player.
    CliLauncherPlayMuzzleFx(this);
    if (!SinglePlayer())
        LaunchEquip()->ConsumeFireResources();
    // 0x0052d534..0x0052d53d: FUN_005532d0 with the host id.
    StarSysNoteTargetChange(Host->get_id());
}

void FCliGunFx::FireDirect(Vector const* Target)
{
    // FUN_0052dd40 (RET 4) -- slot 5 of the gun table.
    if (Equip == nullptr)
        return;
    // 0x0052dd4f..0x0052dd71: FUN_0052d510's body inlined.
    CliLauncherPlayMuzzleFx(this);
    if (!SinglePlayer())
        LaunchEquip()->ConsumeFireResources();
    StarSysNoteTargetChange(Host->get_id());

    // 0x0052dd76..0x0052dda7: the host object's owner (class & 3: CSimple +0xb4, else
    // FUN_0054bb00 = DAT_00673344); a non-zero one sets the gun's target point.
    unsigned Owner = 0;
    CObject* const Object = Host->Object;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3u) == 3u)
        Owner = static_cast<CSimple*>(Object)->OwnerPlayer;
    else
        Owner = GetPlayerConnectionId();
    CEGun* const Gun = static_cast<CEGun*>(Equip);
    if (Owner != 0)
        Gun->SetTargetPoint(*Target);
    // 0x0052ddad..0x0052ddcb: an unguided munition launches its projectiles (FUN_0052dde0).
    if (Gun->MunitionArch()->Seeker == 0)
        CliLauncherFireProjectiles(this, Target);
}

unsigned char FCliLauncherFx::FireBarrelAim()
{
    // FUN_0052d280 (no args) -- listing 0x0052d280..0x0052d3e7; slot 4 of every table.
    CELauncher* const Launch = LaunchEquip();
    Vector const Pos = Launch->GetAvgBarrelPosWS();
    Vector Dir = Launch->GetAvgBarrelDirWS();
    // 0x0052d2b8..0x0052d303: 1e10 (DAT_005dd55c) over sqrt(y*y + z*z + x*x) on the FPU, no
    // zero test; every component scaled and stored back as a float.
    double const Length = std::sqrt((static_cast<double>(Dir.y) * Dir.y +
                                     static_cast<double>(Dir.z) * Dir.z) +
                                    static_cast<double>(Dir.x) * Dir.x);
    double const Scale = 1.0e10 / Length;
    Dir.x = static_cast<float>(Dir.x * Scale);
    Dir.y = static_cast<float>(Dir.y * Scale);
    Dir.z = static_cast<float>(Dir.z * Scale);
    // 0x0052d305..0x0052d325: the aim point = barrel position + that.
    Vector Aim;
    Aim.x = Pos.x + Dir.x;
    Aim.y = Pos.y + Dir.y;
    Aim.z = Pos.z + Dir.z;
    // 0x0052d329: slot 3 (vtable +0xc) on it.
    unsigned char const bFired = TryFire(&Aim);
    if (bFired != 0)
    {
        // 0x0052d338..0x0052d364: the record's inlined constructor (id 0, target FLT_MAX x3,
        // empty vector). 0x0052d371: ObjectId = host id (vtable +0x20). 0x0052d37d..0x0052d395:
        // COMMON vector<ushort>::insert(_Last, 1, equipment id (+0x08)). 0x0052d39b..0x0052d3bf:
        // TargetPos = Aim. 0x0052d3c6: DAT_0067ecd0 slot 0 FireWeapon(DAT_00673344, &info).
        // 0x0052d3cd: the vector's block freed -- ~XFireWeaponInfo (FUN_0052d3f0 on unwind,
        // Unwind@005c0490).
        XFireWeaponInfo Info;
        Info.ObjectId = Host->get_id();
        Info.Hardpoints.insert(Info.Hardpoints.end(), 1u, Equip->GetID());
        Info.TargetPos = Aim;
        static_cast<IServer*>(GetLocalServerInterface())
            ->FireWeapon(GetPlayerConnectionId(), Info);
    }
    return bFired;
}

ID_String FCliLauncherFx::OneShotSoundId() const
{
    // FUN_0052cf80 (RET 4, hidden ID_String return). 0x0052cf84: a local ID_String cleared.
    ID_String Cleared;
    Cleared.clear();
    ID_String Result = Cleared;
    // 0x0052cf8e..0x0052cfa6: with a one-shot sound, its slot 12 (+0x30, the lod link); a
    // non-null link gives its +0x04 nickname id, else the cleared one.
    if (OneShotSoundArch != nullptr)
    {
        void* const Linked = GetClientSoundArchLinked(OneShotSoundArch);
        if (Linked != nullptr)
            Result.Id = GetClientSoundArchId(Linked);
    }
    return Result;
}

void FCliLauncherFx::PlayFireSound(Vector const* Pos, int ShotCount)
{
    // FUN_0052ced0 (RET 8). Nothing without a one-shot sound (+0x18).
    if (OneShotSoundArch == nullptr)
        return;
    // 0x0052cede..0x0052cefe: the fire voice made on first use -- FUN_00428f80(0x64) +
    // FUN_00428790 (a Source3D), not tested afterwards.
    if (FireVoice == nullptr)
        FireVoice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
    // 0x0052cf01..0x0052cf0c: vtable +0x8c with the position.
    FireVoice->Slot35(const_cast<Vector*>(Pos));
    // 0x0052cf12..0x0052cf38: attenuation = -(archetype slot 6 * (ShotCount - 1)) (IMUL, NEG,
    // FILD) through vtable +0x80.
    Archetype::ISound* const Arch = static_cast<Archetype::ISound*>(OneShotSoundArch);
    int const Attenuation = -(Arch->GetAttenuation() * (ShotCount - 1));
    FireVoice->SetAttenuation(static_cast<float>(Attenuation));
    // 0x0052cf3e..0x0052cf52: the archetype's random pitch (slot 11) through vtable +0x98.
    FireVoice->SetPitch(Arch->GetRandomPitch());
    // 0x0052cf58..0x0052cf78: an idle voice starts on the archetype (vtable +0x58, 0), a
    // playing one is rewound (vtable +0x64, 0).
    if (!FireVoice->IsActive())
        FireVoice->Start(OneShotSoundArch, 0);
    else
        FireVoice->SetStartOffset(0);
}

namespace
{
    CEqObj* ResolveEqObjFromHost(FClientEquippedObj* Host)
    {
        if (Host == nullptr || Host->Object == nullptr)
            return nullptr;
        CObject* Obj = Host->Object;
        if ((static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
            return nullptr;
        return static_cast<CEqObj*>(Obj);
    }

    // DAT_00675540 -- the fire dispatcher's 3DMathEngine, FUN_005b73e0 on first use
    // (0x00534c4e..0x00534c67).
    I3DMathEngine* G_FireLeadMath = nullptr;

    CObject* ResolvePlayerShipObject()
    {
        void* const PlayerRw = GetPlayerObjRW();
        if (PlayerRw == nullptr || PlayerRw == reinterpret_cast<void*>(static_cast<intptr_t>(-0xc)))
            return nullptr;
        unsigned char* RwBytes = static_cast<unsigned char*>(PlayerRw);
        CObject* Nested = *reinterpret_cast<CObject**>(RwBytes + 0x10);
        if (Nested == nullptr ||
            ((static_cast<unsigned>(Nested->ObjectClass) & 0x503u) != 0x503u))
            return nullptr;
        return Nested;
    }

    FCliGunFxCompanion* GunCompanionFx(CELauncher* Launch)
    {
        if (Launch == nullptr || Launch->Arch == nullptr)
            return nullptr;
        Archetype::Root* Root = Launch->Arch;
        if (Root->Companion == nullptr)
            return nullptr;
        return reinterpret_cast<FCliGunFxCompanion*>(Root->Companion);
    }

    unsigned HostObjectId(FClientEquippedObj* Host)
    {
        // The host's IObjInspect vt+0x20 get_id (005469b3 / 005352b0), nothing else.
        IObjInspect* Inspect = Host;
        return Inspect->get_id();
    }

    unsigned TargetObjectId(IObjRW* Target)
    {
        if (Target == nullptr)
            return 0;
        return Target->get_id();
    }

    void InvokeServerSetTarget(XSetTarget const& Info)
    {
        // DAT_0067ecd0 vt+0x10 -- IServer::SetTarget(DAT_00673344, &info), no tests.
        static_cast<IServer*>(GetLocalServerInterface())->SetTarget(GetPlayerConnectionId(), Info);
    }
} // namespace

    // LAB_005468e0 — CShip Host+4 vt+0x48 (target, sub, player). RET 0xc. Public: the Ship
    // gameobj's IObjAffect set_target (client_gameobj.cpp) forwards here.
    int CliShipSetTarget(void* AdjustorThis, IObjRW* Target, unsigned short SubId, int PlayerId)
    {
        // 005468e0..00546a04. Nothing is null-tested except the CShip narrowing's own chain,
        // whose result is used untested.
        int const LocalId = static_cast<int>(GetPlayerConnectionId()); // FUN_0054bb00
        unsigned char bLocal = 0;
        if (PlayerId == -1 || PlayerId == LocalId)
            bLocal = 1;

        FClientEquippedObj* Host =
            static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
        CEqObj* EqObj = ResolveEqObjFromHost(Host);
        CShip* Ship = nullptr;
        if (EqObj != nullptr &&
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
            Ship = static_cast<CShip*>(EqObj);

        unsigned short EffectiveSub = SubId;
        if (bLocal != 0)
        {
            // FUN_005763b0(0xcc, 0) — PUSH 0 / PUSH 0xcc → (msgId=0xcc, bSkipMods=0).
            if (Target != nullptr)
                HudPostCommand(0xccu, 0);
            IObjRW* Current = Ship->get_target();
            if (Current != Target)
                EffectiveSub = SUBOBJ_ID_NONE;
        }

        if (SinglePlayer() && bLocal == 0)
            return 0;

        // 0x0054697a..0x00546991: get_sub_target is only asked when the target is unchanged.
        if (Ship->get_target() == Target && Ship->get_sub_target() == EffectiveSub)
            return 0;

        Ship->set_target(Target);
        Ship->set_sub_target(EffectiveSub);

        if (bLocal == 0)
            return 0;

        XSetTarget Info;
        std::memset(&Info, 0, sizeof(Info));
        Info.ObjectId = HostObjectId(Host);
        Info.SubObjId = SUBOBJ_ID_NONE;
        Info.TargetObjectId = TargetObjectId(Target);
        Info.TargetSubObjId = EffectiveSub;
        InvokeServerSetTarget(Info);
        return 0;
    }

    // FUN_005351d0 — EquippedObj/CShip Host+4 vt+0x44 (hp*, target, sub, player). RET 0x10.
    int CliEquippedObjSetGunTarget(void* AdjustorThis, unsigned short const* HardpointId,
                                   IObjRW* Target, unsigned short SubId, int PlayerId)
    {
        // 005351d0..0053531b. Neither the adjustor nor the hardpoint pointer is tested; the
        // object at [this+0xc] is narrowed by class & 0x103 and FindByID runs on the result's
        // +0xe4 untested.
        int const LocalId = static_cast<int>(GetPlayerConnectionId()); // FUN_0054bb00
        unsigned char bLocal = 0;
        if (PlayerId == -1 || PlayerId == LocalId)
            bLocal = 1;

        if (SinglePlayer() && bLocal == 0)
            return 0;

        FClientEquippedObj* const Owner =
            static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
        CObject* const Obj0 = Owner->Object;
        CEqObj* const EqObj =
            (Obj0 != nullptr && (static_cast<unsigned>(Obj0->ObjectClass) & 0x103u) == 0x103u)
                ? static_cast<CEqObj*>(Obj0)
                : nullptr;

        CEquip* Equip = EqObj->EquipManager.FindByID(*HardpointId);
        if (Equip == nullptr || Equip->IsDestroyed())
            return static_cast<int>(0xfffffffe);

        CEGun* Gun = CEGun::cast(Equip);
        if (Gun == nullptr)
            return static_cast<int>(0xfffffffd);

        CSimple* TargetSimple = nullptr;
        if (Target != nullptr)
        {
            CObject const* Obj = Target->cobject();
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 3u) == 3u)
                TargetSimple = const_cast<CSimple*>(static_cast<CSimple const*>(Obj));
        }

        bool const Changed = Gun->SetTarget(TargetSimple, SubId);
        if (!Changed || bLocal == 0)
            return 0;

        FClientEquippedObj* Host =
            static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
        XSetTarget Info;
        std::memset(&Info, 0, sizeof(Info));
        Info.ObjectId = HostObjectId(Host);
        Info.SubObjId = *HardpointId;
        Info.TargetObjectId = TargetObjectId(Target);
        Info.TargetSubObjId = SubId;
        InvokeServerSetTarget(Info);
        return 0;
    }

void CliLauncherPlayMuzzleFx(FCliLauncherFx* Launcher)
{
    // FUN_0052cfd0 (thiscall, plain RET) -- listing 0x0052cfd0..0x0052d278. Callers: the fire
    // slots FUN_0052d410 (0x0052d432), FUN_0052d510 (0x0052d51b), FUN_0052dd40 (0x0052dd4f).
    CELauncher* const Launch = Launcher->LaunchEquip();

    // 0x0052cfd7..0x0052cfe9: EBP = the gun companion, [[+4]+0xc]+4, zero when the launcher or
    // its archetype is null.
    FCliGunFxCompanion* const Companion = GunCompanionFx(Launch);

    // 0x0052cfec..0x0052d00b: CELauncher::GetBarrelPosWS(0) is taken before the launcher is
    // tested; a null launcher or a destroyed one (vt+8) ends the call.
    Vector const BarrelWs = Launch->GetBarrelPosWS(0);
    if (Launch == nullptr || Launch->IsDestroyed())
        return;

    // 0x0052d011..0x0052d048: with a use-animation, Anim vt+0x2c (joint, 0, 0, 1.0, 0, 1.0,
    // 1.0, 0) and AnimDB::Add(joint).
    if (Launcher->AnimJointIndex.Value != -1)
    {
        DALib::Anim->EvalPlayback(
            static_cast<unsigned long>(Launcher->AnimJointIndex.Value), 0, 0.0f, 1.0f, nullptr,
            AsOpaqueDword(1.0f), AsOpaqueDword(1.0f), nullptr);
        AnimDB::Add(Launcher->AnimJointIndex.Value);
    }

    // 0x0052d04b..0x0052d088: from the cockpit camera (FUN_0054bc80), the player's ship
    // (FUN_0054baf0, +0xc, +4, class 0x503; zero otherwise) compared with the launcher's owner.
    if (IsFirstPersonCameraCurrent() && ResolvePlayerShipObject() == Launch->Owner)
    {
        // 0x0052d08e..0x0052d093: no light animation name -> nothing.
        if (Companion->LightAnimLen == 0)
            return;

        // 0x0052d099..0x0052d0a9, 0x0052d178: the animation looked up (FUN_00538e80).
        Light::AnimationArch* const Anim = GameLightFindAnimByName(Companion->LightAnimName);
        if (Anim == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: light animation (%s) is not defined\n",
                  kCliLauncherCpp, 0x140, Companion->LightAnimName);
            return;
        }

        // 0x0052d0af..0x0052d0f3: a white point light (four 0xff bytes) at the barrel with the
        // flash radius (FUN_00537af0).
        float Pos3[3];
        Pos3[0] = BarrelWs.x;
        Pos3[1] = BarrelWs.y;
        Pos3[2] = BarrelWs.z;
        unsigned char const Rgba[4] = {0xff, 0xff, 0xff, 0xff};
        FGameLight* const Light = GameLightCreatePoint(Pos3, Rgba, Companion->FlashRadius);
        if (Light == nullptr)
        {
            // 0x0052d140..0x0052d16b: named by the launcher archetype's nickname.
            FDUMP(0x100002, "%s(%d) : *** WARNING: Light creation for gun failed: %s\n",
                  kCliLauncherCpp, 0x13b, Launch->Arch->Nickname.get_string());
            return;
        }

        // 0x0052d0f5..0x0052d107: the host's velocity (IObjInspect vt+4 get_velocity) handed to
        // the light (FUN_005377a0).
        Vector const HostVelocity = Launcher->Host->get_velocity();
        GameLightSetFreeMotionOffset(Light, &HostVelocity.x);

        // 0x0052d10c..0x0052d12d: FUN_00537960(Anim, 1.0, 1.0, 0); false -> the light's own
        // vt+8 (disable); then FUN_00537ad0 drops the creation reference.
        if (!GameLightAttachAnim(Light, Anim, 1.0f, 1.0f, 0.0f))
        {
            typedef void(__thiscall * FDisableFn)(FGameLight*);
            Light->Disable();
        }
        GameLightRelease(Light);
        return;
    }

    // 0x0052d1a1..0x0052d1ae: no flash particle -> nothing.
    if (Companion->OneShotEffect.IsEmpty())
        return;

    // 0x0052d1b4..0x0052d1d0: the previous muzzle effect -- FUN_004f8110, its vt+4, and
    // FUN_004f7a90 -- then cleared.
    if (Launcher->MuzzleEffect != nullptr)
    {
        FParticleEffect* const Prior = static_cast<FParticleEffect*>(Launcher->MuzzleEffect);
        ParticleEffectCleanupAlchemy(Prior);
        ParticleEffectArmDeath(Prior);
        ParticleEffectRelease(Prior);
        Launcher->MuzzleEffect = nullptr;
    }

    // 0x0052d1d7..0x0052d1e8: CELauncher::GetBarrelInfo(0) (IAT 005c68b0, not tested); no
    // engine part -> done.
    CELauncher::Barrel const* const Barrel = Launch->GetBarrelInfo(0);
    if (Barrel->EngineIndex == -1)
        return;

    // 0x0052d1ee..0x0052d216: FUN_0040f080 (not tested), then EffectAlloc(0x84) + FUN_004f8060(0)
    // (null when the pool is empty) stored at +0x28.
    FEffectArch* const EffectArch = FindEffectArch(&Companion->OneShotEffect.Id);
    FParticleEffect* const Effect = ParticleEffectCreate();
    Launcher->MuzzleEffect = Effect;

    // 0x0052d219..0x0052d24d: the barrel's orientation (+4, nine dwords) and position (+0x28)
    // as the local transform, attached to the host at the barrel's part (FUN_004f7d20).
    FTransform BarrelXform;
    MatrixCopyFromCommon(&BarrelXform.Rotation, Barrel->Orient);
    BarrelXform.Translation.X = Barrel->Position.x;
    BarrelXform.Translation.Y = Barrel->Position.y;
    BarrelXform.Translation.Z = Barrel->Position.z;
    ParticleEffectAttach(Effect, Launcher->Host, Barrel->EngineIndex, &BarrelXform);

    // 0x0052d252..0x0052d26a: vt+0 start(arch); false -> FUN_004f7a90 and +0x28 cleared.
    if (ParticleEffectStart(Effect, EffectArch) == 0)
    {
        ParticleEffectRelease(Effect);
        Launcher->MuzzleEffect = nullptr;
    }
}

int CliShipActivateEquip(void* AdjustorThis, unsigned short const* HardpointId,
                         unsigned char bOn, int PlayerId)
{
    // LAB_00535330 -- EquippedObj IObjAffect vt+0x4c, this = the gameobj + 4 (RET 0xc). EAX is
    // zeroed at 0x00535459 on the one common exit, so every path returns 0.
    // 0x0053533a..0x00535350: local = PlayerId -1, or PlayerId == FUN_0054bb00 (DAT_00673344).
    unsigned char bLocal = 1;
    if (PlayerId != -1)
        bLocal = (static_cast<unsigned>(PlayerId) == GetPlayerConnectionId()) ? 1 : 0;
    // 0x00535355..0x00535365: in single player only local requests go on.
    if (SinglePlayer() && bLocal == 0)
        return 0;

    // 0x0053536b..0x00535393: the gameobj (this - 4) and its object narrowed by 0x103, null
    // when the narrow fails -- read through below with no test, as retail does.
    FClientGameobj* const Self =
        static_cast<FClientGameobj*>(static_cast<IObjAffect*>(AdjustorThis));
    CObject* const Obj = Self->Object;
    CEqObj* const EqObj =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u)
            ? static_cast<CEqObj*>(Obj)
            : nullptr;

    // 0x00535393..0x005353bc: CEquipManager::FindByID(*HardpointId) at CEqObj+0xe4; none, or
    // its vt+8 true (destroyed) -> done.
    CEquip* const Equip = EqObj->EquipManager.FindByID(*HardpointId);
    if (Equip == nullptr || Equip->IsDestroyed())
        return 0;

    // 0x005353c2..0x00535407: switching an engine (+0x14 == 0x20000) off is refused while the
    // ship strafes or has a thruster on -- the object narrowed by 0x503 (no null test).
    if (bOn == 0 && Equip->Type == static_cast<unsigned>(EquipTypeEngine))
    {
        CShip* const Ship =
            (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Obj)
                : nullptr;
        if (Ship->get_strafe_dir() != 0)
            return 0;
        if (Ship->any_thrusters_on())
            return 0;
    }

    // 0x00535409..0x00535419: vt+4 IsActive against bOn (byte compare); vt+0x30 Activate.
    unsigned char const Active = Equip->IsActive() ? 1 : 0;
    if (Active == bOn)
        return 0;
    Equip->Activate(bOn != 0);

    // 0x0053541c..0x00535453: a local request goes to the server -- XActivateEquip {the
    // gameobj's vt+0x20 get_id, *HardpointId, bOn (+7 never written; zero here)} through
    // DAT_0067ecd0 slot 1 ActivateEquip(DAT_00673344, Info), no null test.
    if (bLocal != 0)
    {
        XActivateEquip Info;
        Info.ObjectId = Self->get_id();
        Info.EquipId = *HardpointId;
        Info.bActivate = bOn != 0;
        Info.Pad_0x07 = 0;
        static_cast<IServer*>(GetLocalServerInterface())->ActivateEquip(GetPlayerConnectionId(),
                                                                         Info);
    }
    return 0;
}

unsigned CliShipFireWeapons(void* AdjustorThis, unsigned short const* HardpointIds,
                            unsigned HardpointCount, Vector const* TargetPos, int PlayerId)
{
    // FUN_00534870 (thiscall on the IObjAffect sub-object, RET 0x10) -- listing
    // 0x00534870..0x00534e59. Every launcher that fired is remembered (at most ten, retail's
    // stack array at +0x84); afterwards the ones sharing a one-shot sound play it once together.
    FClientEquippedObj* const Host =
        static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
    FEquipFxListNode* const Head = static_cast<FEquipFxListNode*>(Host->EquipFx.Head);
    // Retail's inlined CEqObj cast of the host object (class & 0x103); a null result would
    // fault at +0xe4 in CEquipManager::FindByID, and an equipped object's object always is one.
    CEqObj* const Ship = static_cast<CEqObj*>(Host->Object);

    FCliLauncherFx* Fired[10];
    unsigned FiredCount = 0;

    if (PlayerId == -1 || PlayerId == static_cast<int>(GetPlayerConnectionId()))
    {
        // 0x005348b0..0x0053490c: the shot record -- its inlined constructor (id 0, target
        // FLT_MAX x3, empty vector at [ESP+0x54]), the host id (vtable +0x20), the target
        // when one is given; the equipment ids that fire are inserted straight into the
        // record's own vector (COMMON vector<ushort>::insert(_Last, 1, id) on [ESP+0x54]).
        XFireWeaponInfo Info;
        Info.ObjectId = Host->get_id();
        if (TargetPos != nullptr)
            Info.TargetPos = *TargetPos;

        if (HardpointCount != 0)
        {
            // 0x0053491f..0x00534a2c: each requested id -> its CEquip (CEquipManager::FindByID)
            // -> the equip-FX wrapper holding it (+0xb8 list, +4 compare); a launcher with a
            // target and no force_gun_ori (munition +0x79) takes slot 3 and, when it fires,
            // adds the id to the record; otherwise slot 4. No IsActive test on this path.
            for (unsigned I = 0; I < HardpointCount; ++I)
            {
                CEquip* const Equip = Ship->EquipManager.FindByID(HardpointIds[I]);
                FEquipFxListNode* Node = Head->Next;
                while (Node != Head && Node->Wrapper->Equip != Equip)
                    Node = Node->Next;
                if (Node == Head)
                    continue;
                FCliLauncherFx* const Launcher = static_cast<FCliLauncherFx*>(Node->Wrapper);
                if (Launcher == nullptr || Launcher->Equip == nullptr)
                    continue;
                CELauncher* const Launch = CELauncher::cast(Launcher->Equip);
                unsigned char bFired = 0;
                if (TargetPos != nullptr &&
                    !static_cast<Archetype::Projectile const*>(Launch->Projectile)->bForceGunOri)
                {
                    bFired = Launcher->TryFire(TargetPos);
                    if (bFired != 0)   // 0x005349ea..0x005349f6
                        Info.Hardpoints.insert(Info.Hardpoints.end(), 1u, HardpointIds[I]);
                }
                else
                {
                    bFired = Launcher->FireBarrelAim();
                }
                if (bFired != 0 && FiredCount < 10u)
                {
                    Fired[FiredCount] = Launcher;
                    FiredCount = FiredCount + 1;
                }
            }
        }
        else
        {
            // 0x00534a37..0x00534ae1: no ids -- every wrapper whose equipment is an active
            // launcher (CELauncher::cast, vtable +4), the same slot 3 / slot 4 choice, the
            // launcher's own id (+0x08) recorded.
            for (FEquipFxListNode* Node = Head->Next; Node != Head; Node = Node->Next)
            {
                CELauncher* const Launch = CELauncher::cast(Node->Wrapper->Equip);
                if (Launch == nullptr || !Launch->IsActive())
                    continue;
                FCliLauncherFx* const Launcher = static_cast<FCliLauncherFx*>(Node->Wrapper);
                unsigned char bFired = 0;
                if (TargetPos != nullptr &&
                    !static_cast<Archetype::Projectile const*>(Launch->Projectile)->bForceGunOri)
                {
                    bFired = Launcher->TryFire(TargetPos);
                    if (bFired != 0)   // 0x00534a93..0x00534aab
                        Info.Hardpoints.insert(Info.Hardpoints.end(), 1u, Launch->GetID());
                }
                else
                {
                    bFired = Launcher->FireBarrelAim();
                }
                if (bFired != 0 && FiredCount < 10u)
                {
                    Fired[FiredCount] = Launcher;
                    FiredCount = FiredCount + 1;
                }
            }
        }

        // 0x00534ae7..0x00534b20: a non-empty record goes to DAT_0067ecd0 slot 0
        // FireWeapon(DAT_00673344, &record) when `_First != 0 && (_Last - _First) >> 1 != 0`;
        // the vector's buffer is freed either way (0x00534b20, ~XFireWeaponInfo at scope exit;
        // FUN_0052d3f0 via Unwind@005c0930 on the exception path).
        if (!Info.Hardpoints.empty())
        {
            static_cast<IServer*>(GetLocalServerInterface())
                ->FireWeapon(GetPlayerConnectionId(), Info);
        }
    }
    else
    {
        // 0x00534b2d..0x00534cfb: another player's shot. The target is copied without a test;
        // each id's wrapper (same lookup) takes slot 5.
        Vector Aim = *TargetPos;
        bool bLeadAimed = false;
        for (unsigned I = 0; I < HardpointCount; ++I)
        {
            CEquip* const Equip = Ship->EquipManager.FindByID(HardpointIds[I]);
            FEquipFxListNode* Node = Head->Next;
            while (Node != Head && Node->Wrapper->Equip != Equip)
                Node = Node->Next;
            if (Node == Head)
                continue;
            FCliLauncherFx* const Launcher = static_cast<FCliLauncherFx*>(Node->Wrapper);
            if (Launcher == nullptr || Launcher->Equip == nullptr)
                continue;
            if (SinglePlayer())
            {
                // 0x00534caf..0x00534cd1: the lead-aimed point once there is one, else the
                // target as sent.
                Launcher->FireDirect(bLeadAimed ? &Aim : TargetPos);
            }
            else
            {
                // 0x00534be9..0x00534bf6: FUN_005357f0 on the wrapper's host with the
                // equipment's id -- re-aim it at the target its sub-target map holds.
                static_cast<FClientEquippedObj*>(Launcher->Host)
                    ->ApplySubTarget(Launcher->Equip->GetID());
                if (bLeadAimed)
                {
                    Launcher->FireDirect(&Aim);
                }
                else
                {
                    // 0x00534c07..0x00534caa: an unguided gun (CEGun::cast, Seeker +0x90 == 0)
                    // turns the sent point into world space through its target's transform
                    // (CEGun::GetTarget, the 0x30 bytes at +0x08) with DAT_00675540 slot 10
                    // TransformXform -- the 3DMathEngine FUN_005b73e0 acquires on first use --
                    // and every later launcher reuses it.
                    CEGun* const Gun = CEGun::cast(Launcher->Equip);
                    if (Gun != nullptr && Gun->MunitionArch()->Seeker == 0)
                    {
                        CSimple* Target = nullptr;
                        unsigned short TargetSubObj = 0;
                        Gun->GetTarget(Target, TargetSubObj);
                        if (Target != nullptr)
                        {
                            FTransform TargetXform;
                            std::memcpy(&TargetXform, &Target->get_transform(),
                                        sizeof(TargetXform));
                            if (G_FireLeadMath == nullptr)
                                G_FireLeadMath = Acquire3DMathEngine();
                            FVector World;
                            G_FireLeadMath->TransformXform(
                                World, TargetXform, *reinterpret_cast<FVector const*>(TargetPos));
                            Aim.x = World.X;
                            Aim.y = World.Y;
                            Aim.z = World.Z;
                        }
                        bLeadAimed = true;
                        Launcher->FireDirect(&Aim);
                    }
                    else
                    {
                        Launcher->FireDirect(TargetPos);
                    }
                }
            }
            if (FiredCount < 10u)
            {
                Fired[FiredCount] = Launcher;
                FiredCount = FiredCount + 1;
            }
        }
    }

    // 0x00534d01..0x00534e39: every remembered launcher, in order, gathers the later ones that
    // share its one-shot sound (FUN_0052cf80): their projectiles-per-fire add up, their barrel-0
    // world positions add up and they drop out; the summed position times 1.0 (DAT_005c75dc) /
    // the summed count is where FUN_0052ced0 plays the sound for all of them.
    for (unsigned I = 0; I < FiredCount; ++I)
    {
        FCliLauncherFx* const Launcher = Fired[I];
        if (Launcher == nullptr)
            continue;
        CELauncher* const Launch = Launcher->LaunchEquip();
        int ShotCount = static_cast<int>(Launch->GetProjectilesPerFire());
        Vector Pos = Launch->GetBarrelPosWS(0);
        ID_String const SoundId = Launcher->OneShotSoundId();
        for (unsigned J = I + 1; J < FiredCount; ++J)
        {
            FCliLauncherFx* const Other = Fired[J];
            if (Other == nullptr || Other->OneShotSoundId().Id != SoundId.Id)
                continue;
            CELauncher* const OtherLaunch = Other->LaunchEquip();
            ShotCount = ShotCount + static_cast<int>(OtherLaunch->GetProjectilesPerFire());
            Vector const OtherPos = OtherLaunch->GetBarrelPosWS(0);
            Fired[J] = nullptr;
            Pos.x = Pos.x + OtherPos.x;
            Pos.y = Pos.y + OtherPos.y;
            Pos.z = Pos.z + OtherPos.z;
        }
        double const Scale = 1.0 / static_cast<double>(ShotCount);
        Pos.x = static_cast<float>(Pos.x * Scale);
        Pos.y = static_cast<float>(Pos.y * Scale);
        Pos.z = static_cast<float>(Pos.z * Scale);
        Launcher->PlayFireSound(&Pos, ShotCount);
    }
    return 0;
}

unsigned char EquippedObjHandleEquipMsg(FClientEquippedObj* Host,
                                        FCliEquipFirePacket const* Packet)
{
    // FUN_00534460 (thiscall, RET 4) -- listing 00534460..00534710. Neither `this` nor the packet
    // is tested at entry.
    if (Packet->Op == 0x18)
    {
        // 0x005346ba..0x005346f4: the whole +0x10 dword is tested (TEST EAX,EAX), the id pushed
        // is its low word zero-extended.
        unsigned short Hp = Packet->HardpointId;
        unsigned short const* Ids = nullptr;
        unsigned Count = 0;
        if (Packet->HardpointId != 0 || Packet->Pad12 != 0)
        {
            Ids = &Hp;
            Count = 1;
        }
        Vector Target = Packet->TargetPos;
        int const PlayerId = static_cast<int>(GetPlayerConnectionId());
        CliShipFireWeapons(static_cast<IObjAffect*>(Host), Ids, Count, &Target, PlayerId);
        return 1;
    }

    if (Packet->Op != 0x19)
        return 0;

    // ActivateEquip arm -- cases 0..4. Retail leaves local_29=0 (return 0). 0x00534483..
    // 0x005344b2: the object narrowed by class & 0x103 (else 0) and FindByID called on its +0xe4
    // manager with no test of the narrowing's result.
    CEqObj* Ship = ResolveEqObjFromHost(Host);

    CEquip* Equip = Ship->EquipManager.FindByID(Packet->HardpointId);
    if (Equip == nullptr || Equip->IsDestroyed())
        return 0;

    int const PlayerId = static_cast<int>(GetPlayerConnectionId());
    unsigned short HpId = Equip->Id;
    int const Sub = Packet->ActivateSubOp;

    switch (Sub)
    {
    case 0:
        CliShipActivateEquip(static_cast<IObjAffect*>(Host), &HpId, 0, PlayerId);
        return 0;
    case 1:
        CliShipActivateEquip(static_cast<IObjAffect*>(Host), &HpId, 1, PlayerId);
        return 0;
    case 2:
    {
        unsigned char const Active = Equip->IsActive() ? 1 : 0;
        CliShipActivateEquip(static_cast<IObjAffect*>(Host), &HpId, static_cast<unsigned char>(1 - Active),
                             PlayerId);
        return 0;
    }
    case 3:
    {
        // Mute group: activate this, deactivate others of same Type mask.
        CEquipTraverser Trav(static_cast<int>(Equip->Type));
        CEquip* Walk = Ship->EquipManager.Traverse(Trav);
        while (Walk != nullptr)
        {
            unsigned short WalkId = Walk->Id;
            if (Walk == Equip)
            {
                if (!Walk->IsActive())
                    CliShipActivateEquip(static_cast<IObjAffect*>(Host), &WalkId, 1, PlayerId);
            }
            else if (Walk->IsActive())
            {
                CliShipActivateEquip(static_cast<IObjAffect*>(Host), &WalkId, 0, PlayerId);
            }
            Walk = Ship->EquipManager.Traverse(Trav);
        }
        return 0;
    }
    case 4:
    {
        // Sync all of Type to this equip's IsActive state.
        unsigned char const Want = Equip->IsActive() ? 1 : 0;
        CEquipTraverser Trav(static_cast<int>(Equip->Type));
        CEquip* Walk = Ship->EquipManager.Traverse(Trav);
        while (Walk != nullptr)
        {
            unsigned char const Cur = Walk->IsActive() ? 1 : 0;
            if (Cur != Want)
            {
                unsigned short WalkId = Walk->Id;
                CliShipActivateEquip(static_cast<IObjAffect*>(Host), &WalkId, Want, PlayerId);
            }
            Walk = Ship->EquipManager.Traverse(Trav);
        }
        return 0;
    }
    default:
        return 0;
    }
}

void CliShipPreprocessEquipUpdate(FClientEquippedObj* Host, FCliEquipFirePacket* Packet)
{
    // FUN_00544e70 (thiscall, RET 4) -- listing 00544e70..0054503e. Ops 0x1a / 0x1b -> 0x19 + the
    // next launcher. `this`, its +0xc face and the object are tested (00544e73..00544ea2); the
    // packet is not. The Op == 1 test at 00544eaf..00544ec4 (MOVZX byte +0x25, AND 0x700, CMP
    // 0x200) can never be equal -- a zero-extended byte AND 0x700 is 0 -- and Op 1 is neither
    // 0x1a nor 0x1b, so it leaves nothing to port.
    if (Host == nullptr)
        return;

    CEqObj* Ship = nullptr;
    if (Host->Object != nullptr &&
        ((static_cast<unsigned>(Host->Object->ObjectClass) & 0x503u) == 0x503u))
        Ship = static_cast<CEqObj*>(Host->Object);
    if (Ship == nullptr)
        return;

    // 0x1fe0 = launcher family mask (gun/mine/cm droppers etc.).
    constexpr int kLauncherMask = 0x1fe0;

    if (Packet->Op == 0x1a)
    {
        Packet->Op = 0x19;
        CEquipTraverser Trav(kLauncherMask);
        CEquipManager* Mgr = &Ship->EquipManager;
        CEquip* First = Mgr->Traverse(Trav);
        while (First != nullptr)
        {
            CELauncher* AsLaunch = CELauncher::cast(First);
            if (AsLaunch != nullptr && First->IsActive())
                break;
            First = Mgr->Traverse(Trav);
        }
        CEquip* Next = Mgr->Traverse(Trav);
        if (Next == nullptr)
            Next = Mgr->Traverse(Trav);
        unsigned short Hp = SUBOBJ_ID_NONE;
        if (Next != nullptr && Next != First)
            Hp = Next->Id;
        // 0x0054502d / 0x00545034: +0x14 = 3, then the id zero-extended into the +0x10 dword.
        Packet->ActivateSubOp = 3;
        Packet->HardpointId = Hp;
        Packet->Pad12 = 0;
        return;
    }

    if (Packet->Op == 0x1b)
    {
        Packet->Op = 0x19;
        CEquipTraverser Trav(kLauncherMask);
        CEquipManager* Mgr = &Ship->EquipManager;
        CEquip* First = Mgr->ReverseTraverse(Trav);
        while (First != nullptr)
        {
            CELauncher* AsLaunch = CELauncher::cast(First);
            if (AsLaunch != nullptr && First->IsActive())
                break;
            First = Mgr->ReverseTraverse(Trav);
        }
        CEquip* Next = Mgr->ReverseTraverse(Trav);
        if (Next == nullptr)
            Next = Mgr->ReverseTraverse(Trav);
        unsigned short Hp = SUBOBJ_ID_NONE;
        if (Next != nullptr && Next != First)
            Hp = Next->Id;
        // 0x00544f6a / 0x00544f71 (0x00544f87 / 0x00544f8e): +0x14 = 3, the id zero-extended
        // into the +0x10 dword.
        Packet->ActivateSubOp = 3;
        Packet->HardpointId = Hp;
        Packet->Pad12 = 0;
    }
}

void CliShipLeadAimFire(FClientEquippedObj* Host)
{
    // FUN_00544880 (thiscall on the host, bare RET) -- listing 00544880..005449e8. Nothing is
    // null-tested except the CShip narrowing's own chain, whose result is used untested.
    IObjInspect const* const Inspect = static_cast<IObjInspect const*>(Host);

    // 0x00544886..0x005448ac: host vt+0xc (get_transform); Vector(-m02, -m12, -m22) through
    // FUN_00422b80.
    Transform const& Xform = Inspect->get_transform();
    float DirX = -Xform.Rotation.E[0][2];
    float DirY = -Xform.Rotation.E[1][2];
    float DirZ = -Xform.Rotation.E[2][2];

    // 0x005448b1..0x005448f8: 1 / sqrt((y*y + z*z) + x*x) (FDIVR 1.0, no zero test), each
    // component times it, FSTP float.
    double const InvLen =
        1.0 / std::sqrt((static_cast<double>(DirY) * DirY + static_cast<double>(DirZ) * DirZ) +
                        static_cast<double>(DirX) * DirX);
    DirX = static_cast<float>(DirX * InvLen);
    DirY = static_cast<float>(DirY * InvLen);
    DirZ = static_cast<float>(DirZ * InvLen);

    // 0x005448b5..0x0054491d: the object narrowed by class & 0x503 (CShip, else 0) and
    // get_tgt_lead_fire_pos called on it with no test.
    CObject* const Obj = Host->Object;
    CShip* const Ship = (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                            ? static_cast<CShip*>(Obj)
                            : nullptr;
    Vector Lead;
    if (Ship->get_tgt_lead_fire_pos(Lead))
    {
        // 0x00544927..0x00544977: host vt+0 (get_position); the distance to the lead point,
        // sqrt((dz*dz + dy*dy) + dx*dx) on the stack; each component times it, FSTP float.
        Vector const& Pos = Inspect->get_position();
        double const Dx = static_cast<double>(Lead.x) - Pos.x;
        double const Dy = static_cast<double>(Lead.y) - Pos.y;
        double const Dz = static_cast<double>(Lead.z) - Pos.z;
        double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);
        DirX = static_cast<float>(DirX * Dist);
        DirY = static_cast<float>(DirY * Dist);
        DirZ = static_cast<float>(DirZ * Dist);
    }
    else
    {
        // 0x0054497b..0x005449a1: each component times 1e10f (DAT_005dd55c), FSTP float.
        DirX = static_cast<float>(static_cast<double>(DirX) * static_cast<double>(1e10f));
        DirY = static_cast<float>(static_cast<double>(DirY) * static_cast<double>(1e10f));
        DirZ = static_cast<float>(static_cast<double>(DirZ) * static_cast<double>(1e10f));
    }

    // 0x005449a5..0x005449e1: host vt+0 again; aim = dir + position (FSTP float each); Host+4
    // vt+0x34 (FUN_00534870) with no hardpoints.
    Vector const& Pos = Inspect->get_position();
    Vector Aim;
    Aim.z = static_cast<float>(static_cast<double>(DirZ) + Pos.z);
    Aim.y = static_cast<float>(static_cast<double>(DirY) + Pos.y);
    Aim.x = static_cast<float>(static_cast<double>(DirX) + Pos.x);
    int const PlayerId = static_cast<int>(GetPlayerConnectionId());
    CliShipFireWeapons(static_cast<IObjAffect*>(Host), nullptr, 0, &Aim, PlayerId);
}

unsigned char CliShipHandleEquipUpdate(FClientEquippedObj* Host,
                                       FCliEquipFirePacket const* Packet)
{
    // FUN_00544d70 (thiscall, RET 4) -- listing 00544d70..00544e62. Neither `this` nor the packet
    // is tested.
    if (ShouldSkipLocalServerTick())
        return 1;

    FCliEquipFirePacket Local = *Packet;
    CliShipPreprocessEquipUpdate(Host, &Local);
    unsigned char const Handled = EquippedObjHandleEquipMsg(Host, &Local);
    if (Handled != 0)
        return 1;

    if (Local.Op == 0x1c)
    {
        // Packet +0x10 = object id (dword); +0x14 = sub id.
        unsigned const TargetId = *reinterpret_cast<unsigned const*>(
            reinterpret_cast<unsigned char const*>(&Local) + 0x10);
        unsigned short const SubId = static_cast<unsigned short>(Local.ActivateSubOp);
        FClientGameobj* TargetGo = ClientObjDb_FindObject(TargetId, 0); // 0x00544dd7
        if (TargetGo != nullptr)
        {
            IObjRW* TargetRw = static_cast<IObjRW*>(TargetGo);
            int const PlayerId = static_cast<int>(GetPlayerConnectionId());

            // Host+4 vt+0x48 = LAB_005468e0 (CShip set_target + net).
            CliShipSetTarget(static_cast<IObjAffect*>(Host), TargetRw, SubId, PlayerId);

            // 0x00544dff..0x00544e62: CEquipTraverser(0x80); each pass narrows the object by
            // class & 0x503 (CShip, else 0) and walks its +0xe4 manager with no test of that
            // result; every gun gets FUN_005351d0 (per-gun SetTarget + DAT_0067ecd0+0x10).
            CEquipTraverser Trav(0x80);
            while (true)
            {
                CObject* const Obj = Host->Object;
                CEqObj* const Ship =
                    (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CEqObj*>(Obj)
                        : nullptr;
                CEquip* const Equip = Ship->EquipManager.Traverse(Trav);
                if (Equip == nullptr)
                    break;
                unsigned short HpId = Equip->Id;
                CliEquippedObjSetGunTarget(static_cast<IObjAffect*>(Host), &HpId, TargetRw, SubId, PlayerId);
            }
        }
    }
    else if (Local.Op == 0xbb)
    {
        CliShipLeadAimFire(Host);
    }
    return 1;
}
FLauncherAnimHandle::~FLauncherAnimHandle()
{
    // FUN_0052ca50 (thiscall on +0x10): listing 0x0052ca50..0x0052ca7f. Reached in retail from
    // the unwind funclets Unwind@005c0438 / Unwind@005c0468 (ctor / dtor exception paths) and
    // inlined at 0x0052cc56 at the end of FUN_0052caa0.
    if (Value != -1)
    {
        DALib::Anim->StopPlayback(static_cast<unsigned long>(Value));        // vt+0x38
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(
            static_cast<uintptr_t>(static_cast<unsigned long>(Value))));    // vt+0x28
        Value = -1;
    }
}

FLauncherEffectCompanion::~FLauncherEffectCompanion()
{
    // FUN_00502d90 on +0x30 (Unwind@005c0443 / Unwind@005c0473, and the second pass inlined in
    // FUN_0052caa0).
    CliEffectCompanionClear(Slots);
}

FCliLauncherFx::FCliLauncherFx()
    : DryFireSoundArch(nullptr)
    , OneShotSoundArch(nullptr)
    , FireVoice(nullptr)
    , ConstEffectArch(nullptr)
    , MunitionExplosion(nullptr)
    , MuzzleEffect(nullptr)
    , DefaultEffectArch(nullptr)
{
    AnimJointIndex.Value = -1;
    Companion.Slots[0] = nullptr;
    Companion.Slots[1] = nullptr;
    Companion.Slots[2] = nullptr;
    Companion.Slots[3] = nullptr;
}

namespace
{
    // The NN voice lines the launcher family's notifies queue when the player's own equipment
    // is destroyed (static initializers LAB_0052d6c0, 0x0052d860, LAB_0052e170 / LAB_0052e190 /
    // LAB_0052e1b0).
    unsigned const G_MineDropperDestroyedId = CreateID("mine_dropper_destroyed");      // 00675444
    unsigned const G_CmDropperDestroyedId =
        CreateID("countermeasure_dropper_destroyed");                                   // 00675440
    unsigned const G_GunDestroyedId = CreateID("gun_destroyed");                       // 00675434
    unsigned const G_LauncherDestroyedId = CreateID("launcher_destroyed");             // 00675438
    unsigned const G_TurretDestroyedId = CreateID("turret_destroyed");                 // 0067543c

    // The block every family notify opens its code-0xa arm with (0x0052d5d2..0x0052d627,
    // 0x0052d77b..0x0052d7d0, 0x0052da15..0x0052da72): the host is the player's (FUN_0054baf0,
    // a pointer compare), not reaped (vtable +0x158, Slot158), and its CSimple (class 0x103, else
    // null -- get_hit_pts is called on it regardless) still has hit points above 0 (FCOMP 0,
    // TEST AH,0x41, JNZ skips on <= / NaN).
    bool IsLivePlayerHost(FClientGameobj* Host)
    {
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) != GetPlayerObjRW())
            return false;
        if (Host->Slot158())
            return false;
        CSimple* Simple = nullptr;
        if (Host != nullptr && Host->Object != nullptr &&
            (static_cast<unsigned>(Host->Object->ObjectClass) & 0x103u) == 0x103u)
        {
            Simple = static_cast<CSimple*>(Host->Object);
        }
        return Simple->get_hit_pts() > 0.0f;
    }

    // An identity FTransform (the MOV runs at 0x0052dad4..0x0052db2c / 0x0052dc78..0x0052dcd0).
    FTransform IdentityTransform()
    {
        FTransform Xform;
        std::memset(&Xform, 0, sizeof(Xform));
        Xform.Rotation.D[0][0] = 1.0f;
        Xform.Rotation.D[1][1] = 1.0f;
        Xform.Rotation.D[2][2] = 1.0f;
        return Xform;
    }

    // The companion "still running" test the launcher Update inlines twice (0x0052cd83..
    // 0x0052cdbb and 0x0052ce89..0x0052ceb9; FUN_00503330's predicate): a particle whose +8
    // bit 1 is clear, an active sound (vtable +0x14), or a light without anim keys (+0xd4).
    bool LauncherCompanionAlive(void* const* Companion)
    {
        FEffectBase const* const Effect = static_cast<FEffectBase const*>(Companion[2]);
        if (Effect != nullptr && (Effect->Flags08 & 2u) == 0)
            return true;
        ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);
        if (Sound != nullptr && Sound->IsActive() != 0)
            return true;
        FGameLight const* const Light = static_cast<FGameLight const*>(Companion[3]);
        return Light != nullptr && Light->Anim.Keys == nullptr;
    }
}

FCliLauncherFx::~FCliLauncherFx()
{
    // FUN_0052caa0. The ClientEquip part (FUN_00531af0 at 0x0052cc92: disconnect, separation
    // explosion) is ~FClientEquip, which runs after this body.
    // 0x0052cac6..0x0052cae5: the fire voice stopped (vtable +0x60), released (+0x08), cleared.
    if (FireVoice != nullptr)
    {
        FireVoice->Stop();
        FireVoice->Release();
        FireVoice = nullptr;
    }
    // 0x0052cae8..0x0052cb70: the muzzle effect re-attached to nothing (FUN_004f7d20 with 0,
    // -1 and an identity frame), killed (vtable +4), released (FUN_004f7a90), cleared.
    if (MuzzleEffect != nullptr)
    {
        FEffectBase* const Effect = static_cast<FEffectBase*>(MuzzleEffect);
        FTransform const Identity = IdentityTransform();
        EffectAttach(Effect, nullptr, -1, &Identity);
        typedef void(__thiscall * FEffectKill)(FEffectBase*);
        Effect->Kill();
        EffectRelease(Effect);
        MuzzleEffect = nullptr;
    }
    // 0x0052cb73..0x0052cba4: the munition explosion (FUN_00536170), the const effect arch
    // (FUN_0040e990), then the one-shot and dry-fire sound archetypes (slot 4, +0x10) -- each
    // only when set; none of the pointers is cleared.
    if (MunitionExplosion != nullptr)
        ExplosionArchRelease(MunitionExplosion);
    if (ConstEffectArch != nullptr)
        EffectArchRelease(ConstEffectArch);
    if (OneShotSoundArch != nullptr)
        ReleaseClientSoundArch(OneShotSoundArch);
    if (DryFireSoundArch != nullptr)
        ReleaseClientSoundArch(DryFireSoundArch);
    // 0x0052cba7..0x0052cbdc: AnimDB::Rem on the use-animation handle (also -1), then with one
    // IAnimation vtable +0x38 and +0x28 on it and the handle set to -1.
    AnimDB::Rem(AnimJointIndex.Value);
    if (AnimJointIndex.Value != -1)
    {
        DALib::Anim->StopPlayback(static_cast<unsigned long>(AnimJointIndex.Value));
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(
            static_cast<uintptr_t>(static_cast<unsigned long>(AnimJointIndex.Value))));
        AnimJointIndex.Value = -1;
    }
    // 0x0052cbe3..0x0052cbea: the multi_sparks02 arch released (FUN_0040e990).
    if (DefaultEffectArch != nullptr)
        EffectArchRelease(DefaultEffectArch);
    // 0x0052cbef..0x0052cc53: the +0x30 companion's slots released by hand (FUN_00502d90's
    // body); then the member destructors run in reverse order: the companion's
    // (~FLauncherEffectCompanion, the second pass, finds them null) and the +0x10 handle's
    // (~FLauncherAnimHandle = FUN_0052ca50, inlined at 0x0052cc56..0x0052cc81, finds -1).
    CliEffectCompanionClear(Companion.Slots);
}

void FCliLauncherFx::DropOnDestroying()
{
    // 0x0052ccbd..0x0052ccf2: AnimDB::Rem on the use-animation handle (also when it is -1),
    // then, with one, IAnimation vtable +0x38 and +0x28 on it and the handle cleared.
    AnimDB::Rem(AnimJointIndex.Value);
    if (AnimJointIndex.Value != -1)
    {
        DALib::Anim->StopPlayback(static_cast<unsigned long>(AnimJointIndex.Value));
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(
            static_cast<uintptr_t>(static_cast<unsigned long>(AnimJointIndex.Value))));
        AnimJointIndex.Value = -1;
    }
    // 0x0052ccf9..0x0052cd03: FUN_004fcef0 on the host's effect list (+0x28) with &this+0x30.
    CliEffectListRemove(&Host->EffectList, Companion.Slots);
    // 0x0052cd08..0x0052cd1c: the muzzle effect's vtable +4 (kill), FUN_004f7a90, cleared.
    if (MuzzleEffect != nullptr)
    {
        FEffectBase* const Effect = static_cast<FEffectBase*>(MuzzleEffect);
        typedef void(__thiscall * FEffectKill)(FEffectBase*);
        Effect->Kill();
        EffectRelease(Effect);
        MuzzleEffect = nullptr;
    }
}

void FCliLauncherFx::notify(Event Code, void* Param)
{
    // LAB_0052ccb0 (RET 8).
    if (Code == EquipDestroying)
        DropOnDestroying();
    // 0x0052cd23..0x0052cd2b: FUN_00531b20 with both arguments.
    FClientEquip::notify(Code, Param);
}

bool FCliLauncherFx::Update(float Dt)
{
    // FUN_0052cd40 (RET 4). Single player only (COMMON.DLL SinglePlayer, JNZ skips), with an
    // equipment.
    if (!SinglePlayer() && Equip != nullptr)
    {
        // 0x0052cd65..0x0052cd7d: vtable +0x58 (CAttachedEquip::IsConnected) and +0x10
        // (CELauncher::IsDisabled) -- both true takes the spark arm.
        CELauncher* const Launch = LaunchEquip();
        if (Launch->IsConnected() && Launch->IsDisabled())
        {
            // 0x0052cd83..0x0052cdbb: nothing while the companion still runs.
            if (!LauncherCompanionAlive(Companion.Slots))
            {
                // 0x0052cdc1..0x0052ce37: CreateID("multi_sparks02") into a local ID_String,
                // an identity frame, vtable +0x84 (CAttachedEquip::GetRootIndex) as the part.
                ID_String SparksId;
                SparksId.Id = CreateID("multi_sparks02");
                FTransform const Local = IdentityTransform();
                long const Part = static_cast<CAttachedEquip*>(Launch)->GetRootIndex();
                // 0x0052ce3d..0x0052ce6b: an empty id ends here; otherwise FUN_0040f080 and
                // FUN_00503070 on DAT_00674c60 with the host and &this+0x30.
                if (!SparksId.IsEmpty())
                {
                    CliCreateEffectOneShot(FindEffectArch(&SparksId.Id), Host,
                                           static_cast<int>(Part), &Local, Companion.Slots);
                }
            }
        }
        else
        {
            // 0x0052ce89..0x0052cec2: a companion still running leaves the host's effect list.
            if (LauncherCompanionAlive(Companion.Slots))
                CliEffectListRemove(&Host->EffectList, Companion.Slots);
        }
    }
    // 0x0052ce74..0x0052ce7b: FUN_00532000.
    return FClientEquip::Update(Dt);
}

void FCliMineDropperFx::notify(Event Code, void* Param)
{
    // FUN_0052d5c0 (RET 8): code 0xa -- the player's live ship queues "mine_dropper_destroyed"
    // (FUN_00489e80 with DAT_00675444, 0) -- then LAB_0052ccb0's body and FUN_00531b20.
    if (Code == EquipDestroying)
    {
        if (IsLivePlayerHost(Host))
            NnVoiceEnqueue(G_MineDropperDestroyedId, 0);
        DropOnDestroying();
    }
    FClientEquip::notify(Code, Param);
}

void FCliCmDropperFx::notify(Event Code, void* Param)
{
    // FUN_0052d700 (RET 8).
    if (Code == EquipDestroying)
    {
        // 0x0052d77a..0x0052d843: "countermeasure_dropper_destroyed" (DAT_00675440), then the
        // launcher body.
        if (IsLivePlayerHost(Host))
            NnVoiceEnqueue(G_CmDropperDestroyedId, 0);
        DropOnDestroying();
    }
    else if (Code == Event_0x19)
    {
        // 0x0052d716..0x0052d770: the host object's owner (class & 3: CSimple +0xb4, else
        // FUN_0054bb00 = DAT_00673344) must be this client (DAT_00673344); then the host's
        // IObjAffect vtable +0x34 fires this one equipment -- ids {Equip +8}, count 1, no
        // target, this client.
        unsigned Owner = 0;
        CObject* const Object = Host->Object;
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3u) == 3u)
            Owner = static_cast<CSimple*>(Object)->OwnerPlayer;
        else
            Owner = GetPlayerConnectionId();
        if (Owner == GetPlayerConnectionId())
        {
            unsigned short const EquipId = Equip->GetID();
            static_cast<IObjAffect*>(Host)->slot_34(&EquipId, 1, nullptr,
                                                    static_cast<int>(GetPlayerConnectionId()));
        }
    }
    FClientEquip::notify(Code, Param);
}

void FCliGunFx::notify(Event Code, void* Param)
{
    // LAB_0052da00 (RET 8).
    if (Code == EquipDestroying)
    {
        // 0x0052da15..0x0052dabc: the player's live ship queues "turret_destroyed" for an
        // auto_turret gun (arch +0xba), else "launcher_destroyed" for a seeking munition
        // (Munition +0x90), else "gun_destroyed".
        if (IsLivePlayerHost(Host))
        {
            CEGun const* const Gun = static_cast<CEGun const*>(Equip);
            unsigned MsgId = 0;
            if (Gun->GunArch()->bAutoTurret)
                MsgId = G_TurretDestroyedId;
            else if (Gun->MunitionArch()->Seeker != 0)
                MsgId = G_LauncherDestroyedId;
            else
                MsgId = G_GunDestroyedId;
            NnVoiceEnqueue(MsgId, 0);
        }

        // 0x0052dabf..0x0052db38: the turret voice stopped (vtable +0x60) and re-posed on no
        // attachment (vtable +0x94 with -1 and an identity frame).
        ISoundSource* const Voice = TurretVoice;
        if (Voice != nullptr)
        {
            Voice->Stop();
            FTransform const Identity = IdentityTransform();
            Voice->SetPose(-1, reinterpret_cast<float const*>(&Identity));
        }
        // 0x0052db3e..0x0052db73: with a voice, the gun companion's +0x84 sound archetype (not
        // empty) released through FUN_00427a40.
        if (TurretVoice != nullptr)
        {
            ID_String const* const MotionSound = reinterpret_cast<ID_String const*>(
                reinterpret_cast<unsigned char const*>(Equip->Arch->Companion) + 0x84);
            if (!MotionSound->IsEmpty())
                UnloadClientSoundArch(MotionSound);
        }
        // 0x0052db76..0x0052dbd4: the launcher body.
        DropOnDestroying();
    }
    FClientEquip::notify(Code, Param);
}

bool FCliGunFx::Update(float Dt)
{
    // FUN_0052dbf0 (RET 4). With an equipment and a turret voice (+0x40):
    if (Equip != nullptr && TurretVoice != nullptr)
    {
        CEGun const* const Gun = static_cast<CEGun const*>(Equip);
        if (Gun->IsJointInMotion())
        {
            // 0x0052dc19..0x0052dc44: the voice (tested again) starts the companion's +0x84
            // sound -- FUN_0042ae40 pops only its own argument, the pushed 0 is Start's second.
            ISoundSource* const Voice = TurretVoice;
            if (Voice != nullptr)
            {
                ID_String const* const MotionSound = reinterpret_cast<ID_String const*>(
                    reinterpret_cast<unsigned char const*>(Equip->Arch->Companion) + 0x84);
                Voice->Start(LookupClientSoundArch(MotionSound), 0);
            }
        }
        else if (!Gun->IsJointInMotion())
        {
            // 0x0052dc5b..0x0052dcdc: IsJointInMotion asked again; still at rest -> the voice
            // stopped (vtable +0x60) and re-posed on no attachment (+0x94, -1, identity).
            ISoundSource* const Voice = TurretVoice;
            if (Voice != nullptr)
            {
                Voice->Stop();
                FTransform const Identity = IdentityTransform();
                Voice->SetPose(-1, reinterpret_cast<float const*>(&Identity));
            }
        }
    }
    // 0x0052dc47..0x0052dc4e / 0x0052dce2..0x0052dce9: FUN_0052cd40 on every path.
    return FCliLauncherFx::Update(Dt);
}

namespace
{
    // DAT_00675448, seeded by the static initializer LAB_0052d550 (0x0052d55e).
    unsigned const G_MultiSparks02Id = CreateID("multi_sparks02");

    // FUN_0052c830 (thiscall, RET 8) after the object's allocation; the port's default
    // constructor has already zeroed +0x10..+0x3c and set +0x10 to -1 (0x0052c863..0x0052c88c).
    void CliLauncherFxBaseFill(FCliLauncherFx* Wrapper, CEquip* Equip, void* Host)
    {
        // 0x0052c85c: FUN_00531a30 (+4 / +8 / +0xc, connect, separation explosion).
        ClientEquipBaseFill(Wrapper, Equip, static_cast<FClientGameobj*>(Host));

        // 0x0052c88f..0x0052c8c8: a non-empty use_animation (arch +0x9c, first byte tested, no
        // null test) plays through IAnimation vtable +0x24 -- the arch's script index (+0x44),
        // the equipment's root (vtable +0x84), the name, 0, 0 -- into +0x10.
        CELauncher* const Launch = Wrapper->LaunchEquip();
        Archetype::Launcher const* const LaunchArch =
            static_cast<Archetype::Launcher const*>(Launch->EquipArch());
        char const* const UseAnimation = LaunchArch->UseAnimation.Value;
        if (UseAnimation[0] != '\0')
        {
            int const ScriptIndex = LaunchArch->AnimIndex;
            long const RootIndex = Launch->GetRootIndex();
            Wrapper->AnimJointIndex.Value = static_cast<int>(DALib::Anim->PlayScript(
                AsOpaqueDword(ScriptIndex), AsOpaqueDword(static_cast<int>(RootIndex)),
                UseAnimation, nullptr, nullptr));
        }

        // 0x0052c8cb..0x0052c91d: the launcher companion's dry_fire_sound (+0x80); non-empty ->
        // FUN_0042ae40 into +0x14, Acquired (slot 3, +0x0c) when found. Retail's companion
        // accessor null-tests Equip and its arch, but both were dereferenced at 0x0052c898.
        unsigned char const* const LauncherComp =
            reinterpret_cast<unsigned char const*>(Launch->Arch->Companion);
        ID_String const* const DryFireSound =
            reinterpret_cast<ID_String const*>(LauncherComp + 0x80);
        if (!DryFireSound->IsEmpty())
        {
            Wrapper->DryFireSoundArch = LookupClientSoundArch(DryFireSound);
            if (Wrapper->DryFireSoundArch != nullptr)
                AcquireClientSoundArch(Wrapper->DryFireSoundArch);
        }

        // 0x0052c920..0x0052c95c: the munition (CELauncher +0x4c) companion's one_shot_sound
        // (+0x5c) the same way into +0x18. The munition and its companion are not tested
        // (a null one would fault on +0x5c).
        Archetype::Equipment* const Munition = Launch->Projectile;
        unsigned char const* const MunitionComp =
            reinterpret_cast<unsigned char const*>(Munition->Companion);
        ID_String const* const OneShotSound =
            reinterpret_cast<ID_String const*>(MunitionComp + 0x5c);
        if (!OneShotSound->IsEmpty())
        {
            Wrapper->OneShotSoundArch = LookupClientSoundArch(OneShotSound);
            if (Wrapper->OneShotSoundArch != nullptr)
                AcquireClientSoundArch(Wrapper->OneShotSoundArch);
        }

        // 0x0052c95f..0x0052c9d2: its const_effect (+0x58); non-empty -> FUN_0040f080 into
        // +0x20, referenced (FUN_0040e960(1)) when found, else the FDUMP at line 0x51.
        ID_String const* const ConstEffect =
            reinterpret_cast<ID_String const*>(MunitionComp + 0x58);
        if (!ConstEffect->IsEmpty())
        {
            Wrapper->ConstEffectArch = FindEffectArch(&ConstEffect->Id);
            if (Wrapper->ConstEffectArch != nullptr)
            {
                EffectArchAddRef(Wrapper->ConstEffectArch, 1);
            }
            else
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: %s: [Effect] const_effect_name %s is missing from "
                      "effects.ini.",
                      "E:\\FL\\Scratch\\Source\\Client\\Space\\CliLauncher.cpp", 0x51,
                      Munition->Nickname.get_string(), ConstEffect->get_string());
            }
        }

        // 0x0052c9d5..0x0052ca18: the munition's explosion_arch (+0x24); non-empty ->
        // Archetype::GetExplosion into +0x24, referenced (FUN_00536100) when found.
        if (!Munition->ExplosionArch.IsEmpty())
        {
            Wrapper->MunitionExplosion = Archetype::GetExplosion(Munition->ExplosionArch);
            if (Wrapper->MunitionExplosion != nullptr)
                ExplosionArchAddRef(Wrapper->MunitionExplosion);
        }

        // 0x0052ca1b..0x0052ca33: FUN_0040f080(&DAT_00675448) into +0x2c, referenced
        // (FUN_0040e960(1)) when found.
        Wrapper->DefaultEffectArch = FindEffectArch(&G_MultiSparks02Id);
        if (Wrapper->DefaultEffectArch != nullptr)
            EffectArchAddRef(Wrapper->DefaultEffectArch, 1);
    }
}

FCliLauncherFx* CliMineEquipFxConstruct(CEquip* Equip, void* Host)
{
    // FUN_0052d570 — FUN_0052c830, then the mine vtable PTR_FUN_005dd564 (no extra fields).
    FCliLauncherFx* Wrapper = new FCliMineDropperFx();
    CliLauncherFxBaseFill(Wrapper, Equip, Host);
    return Wrapper;
}

FCliLauncherFx* CliCmEquipFxConstruct(CEquip* Equip, void* Host)
{
    // FUN_0052d6e0 — FUN_0052c830, then the CM vtable PTR_FUN_005dd598 (0x0052d6f4).
    FCliLauncherFx* Wrapper = new FCliCmDropperFx();
    CliLauncherFxBaseFill(Wrapper, Equip, Host);
    return Wrapper;
}

FCliLauncherFx* CliGunEquipFxConstruct(CEquip* Equip, void* Host)
{
    // FUN_0052d880 (thiscall, RET 8): FUN_0052c830, +0x40 = 0, the gun vtable
    // PTR_LAB_005dd5d8.
    FCliGunFx* const Wrapper = new FCliGunFx();
    CliLauncherFxBaseFill(Wrapper, Equip, Host);
    Wrapper->TurretVoice = nullptr;
    // 0x0052d8b1..0x0052d8e5: the gun companion's turret sound (+0x84, companion not tested)
    // must be non-empty and DALib::Sound live.
    ID_String const* const TurretSound = reinterpret_cast<ID_String const*>(
        reinterpret_cast<unsigned char const*>(Wrapper->Equip->Arch->Companion) + 0x84);
    if (!TurretSound->IsEmpty() && DALib::Sound != nullptr)
    {
        // 0x0052d8fe: FUN_00427a10(&sound, 1) -- look up and Acquire; the result is not read.
        PreloadClientSoundArch(TurretSound);
        // 0x0052d903..0x0052d95b: operator new(0x64) + the inlined Source3D constructor
        // (FUN_00429040(1), fields, vtable 005c9efc) into +0x40, not tested afterwards.
        Wrapper->TurretVoice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
        // 0x0052d95e..0x0052d9de: looping (vtable +0x88 with 1), then posed (vtable +0x94) on
        // the equipment's root (vtable +0x84) with an identity frame.
        Wrapper->TurretVoice->SetLooping(1);
        long const RootIndex = Wrapper->LaunchEquip()->GetRootIndex();
        FTransform const Identity = IdentityTransform();
        Wrapper->TurretVoice->SetPose(static_cast<int>(RootIndex),
                                      reinterpret_cast<float const*>(&Identity));
    }
    return Wrapper;
}

