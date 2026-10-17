// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Client/Space/tradelane.cpp -- the client trade lane ring runtime, 0x005135b0..0x00516ae3,
// plus the IClient entry points that drive it (LAB_0053c760, LAB_0053c830, FUN_0053d940) and the
// solar activate hook FUN_005484c0. Rewritten against the listing 2026-09-26 (claude-code:exe-w3a).

#include "tradelane_fx.h"

#include "cli_projectile_fx.h"
#include "client_equip_fx.h"
#include "client_objdb.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_iface.h"
#include "common_server_types.h"
#include "common_misc.h" // SinglePlayer
#include "dacom.h"
#include "hud_target.h"
#include "nn_arrival_text.h"
#include "particle_effect.h"
#include "player_behavior.h"
#include "vis_effect_arch.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    // .rdata 005db798 / 005dec60 -- the source paths the FDUMP lines carry.
    char const kTradeLaneCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\tradelane.cpp";
    char const kIClientSCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\IClientS.cpp";

    // {head, tail, count} -- the live nodes DAT_006751c0/c4/c8 and the free list
    // DAT_006751b4/b8/bc.
    struct FTlRingList
    {
        FTradeLaneRingRuntime* Head;
        FTradeLaneRingRuntime* Tail;
        int Count;
    };
    FTlRingList G_TlLiveRings = {nullptr, nullptr, 0}; // DAT_006751c0
    FTlRingList G_TlFreeRings = {nullptr, nullptr, 0}; // DAT_006751b4

    // FUN_00514110's statics: DAT_006751fc (cached once), DAT_006751dc/e0/e4 (HpRightLane) and
    // DAT_006751d0/d4/d8 (HpLeftLane). DAT_006751e8 only guards the atexit registration of the two
    // vectors' (empty) destructors, LAB_00514250 / LAB_00514240 -- nothing to register here.
    bool G_TlHpCached = false;
    FVector G_TlHpRight = {};
    FVector G_TlHpLeft = {};

    // DAT_00675200 -- .bss, read by FUN_00513b40 / FUN_005163e0 and never written anywhere in the
    // listing, so the travel effect is only ever started by FUN_005142e0 / FUN_00515890.
    unsigned char G_TlStartTravelOnEnter = 0;

    // DAT_006751f8 -- tradelane.cpp's 3DMathEngine, FUN_005b73e0 on first use (0x00514b25).
    I3DMathEngine* G_TlMath = nullptr;

    I3DMathEngine* TlMath()
    {
        if (G_TlMath == nullptr)
            G_TlMath = Acquire3DMathEngine();
        return G_TlMath;
    }

    // CliEQTradeLane (client_cliobj.cpp FCliTradeLane) -- the fields this file reads.
    enum : unsigned
    {
        kTlPlayerEnter = 0x54,
        kTlPlayerTravel = 0x58,
        kTlPlayerSplash = 0x5c,
        kTlPlayerExit = 0x60,
        kTlPlayerDisrupt = 0x64,
        kTlShipEnter = 0x68,
        kTlShipTravel = 0x6c,
        kTlShipSplash = 0x70,
        kTlShipExit = 0x74,
        kTlShipDisrupt = 0x78,
        kTlRingActive = 0x7c,
        kTlSecsBeforeEnter = 0x94,
        kTlSecsBeforeExit = 0x98,
        kTlSecsBeforeSplash = 0x9c,
        kTlActivationStart = 0xa0,
        kTlActivationEnd = 0xa4,
    };

    ID_String const* TlCompanionId(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return reinterpret_cast<ID_String const*>(reinterpret_cast<unsigned char*>(Companion) +
                                                  Offset);
    }

    float TlCompanionFloat(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return *reinterpret_cast<float const*>(reinterpret_cast<unsigned char*>(Companion) +
                                               Offset);
    }

    // FUN_0042a7e0 list node -- {Next, Prev, ship id}, 0xc bytes.
    struct FTlShipListNode
    {
        FTlShipListNode* Next;
        FTlShipListNode* Prev;
        unsigned ShipId;
    };
    static_assert(sizeof(FTlShipListNode) == 0xc, "ship list node 0xc");

    FTlShipListNode* TlShipListHead(FTradeLaneRingRuntime* Ring)
    {
        return static_cast<FTlShipListNode*>(Ring->ShipListSentinel);
    }

    // FUN_0042a7e0 (thiscall on the list, RET 8): operator_new(0xc), Next / Prev as given, a null
    // one pointing back at the node itself.
    FTlShipListNode* TlNewShipListNode(FTlShipListNode* Next, FTlShipListNode* Prev)
    {
        FTlShipListNode* const Node = static_cast<FTlShipListNode*>(operator new(sizeof(FTlShipListNode)));
        Node->Next = (Next != nullptr) ? Next : Node;
        Node->Prev = (Prev != nullptr) ? Prev : Node;
        return Node;
    }

    // push_back, inline at 0x00514517..0x0051455b and 0x00515b11..0x00515b4b.
    void TlShipListPushBack(FTradeLaneRingRuntime* Ring, unsigned ShipId)
    {
        FTlShipListNode* const Head = TlShipListHead(Ring);
        FTlShipListNode* const Node = TlNewShipListNode(Head, Head->Prev);
        Head->Prev = Node;
        Node->Prev->Next = Node;
        Node->ShipId = ShipId; // FUN_005999b0 (copy-construct the value)
        ++Ring->ShipListCount;
    }

    // FUN_00516f70 (erase) -- unlink, free (FUN_005b7e1d), size - 1.
    void TlShipListErase(FTradeLaneRingRuntime* Ring, FTlShipListNode* Node)
    {
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        operator delete(Node);
        --Ring->ShipListCount;
    }

    // The find loop every leave path inlines (0x0051361e, 0x005144a1, 0x00515974, 0x00515b7f,
    // 0x00515e3f, 0x005160a7, 0x005162d2): the node holding ShipId, or null.
    FTlShipListNode* TlShipListFind(FTradeLaneRingRuntime* Ring, unsigned ShipId)
    {
        FTlShipListNode* const Head = TlShipListHead(Ring);
        for (FTlShipListNode* Node = Head->Next; Node != Head; Node = Node->Next)
        {
            if (Node->ShipId == ShipId)
                return Node;
        }
        return nullptr;
    }

    // FUN_00404d50 (thiscall on the list, RET 8) -- insert Node before Before (null: at the tail).
    void TlRingListInsert(FTlRingList* List, FTradeLaneRingRuntime* Node,
                          FTradeLaneRingRuntime* Before)
    {
        FTradeLaneRingRuntime* Prev = nullptr;
        if (Before == nullptr)
        {
            Prev = List->Tail;
            List->Tail = Node;
        }
        else
        {
            Prev = Before->LivePrev;
            Before->LivePrev = Node;
        }
        if (Prev == nullptr)
            List->Head = Node;
        else
            Prev->LiveNext = Node;
        Node->LiveNext = Before;
        Node->LivePrev = Prev;
        ++List->Count;
    }

    // FUN_00404d90 (thiscall on the list, RET 4) -- unlink Node; its own links are left as they are.
    void TlRingListRemove(FTlRingList* List, FTradeLaneRingRuntime* Node)
    {
        if (Node->LivePrev == nullptr)
            List->Head = Node->LiveNext;
        else
            Node->LivePrev->LiveNext = Node->LiveNext;
        if (Node->LiveNext == nullptr)
            List->Tail = Node->LivePrev;
        else
            Node->LiveNext->LivePrev = Node->LivePrev;
        --List->Count;
    }

    // The watched ring gameobj: RingWatcher's Watchable minus 8, or null.
    FClientGameobj* TlWatchedGameobj(FTradeLaneRingRuntime const* Ring)
    {
        Watchable const* const Watched = Ring->RingWatcher.target();
        if (Watched == nullptr)
            return nullptr;
        return reinterpret_cast<FClientGameobj*>(
            reinterpret_cast<unsigned char*>(const_cast<Watchable*>(Watched)) - 8);
    }

    // The CSolar narrowing every function here inlines: null gameobj, null object or a class
    // that is not a solar ((class & 0x303) != 0x303) gives null.
    CSolar* TlNarrowSolar(FClientGameobj* Gameobj)
    {
        if (Gameobj == nullptr)
            return nullptr;
        CObject* const Obj = Gameobj->GetObject();
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) != 0x303u)
            return nullptr;
        return static_cast<CSolar*>(Obj);
    }

    // FUN_005140c0 (cdecl) -- the ring solar's TradeLane equipment (0x800000) companion: the
    // CliEQTradeLane. FindFirst runs on the narrowed solar with no null test (0x005140ee).
    Archetype::ICliObj* TlTradeLaneCompanion(FClientGameobj* RingGameobj)
    {
        CSolar* const Solar = TlNarrowSolar(RingGameobj);
        CEquip const* const Equip = Solar->EquipManager.FindFirst(0x800000u);
        if (Equip == nullptr)
            return nullptr;
        Archetype::Equipment const* const Arch = Equip->EquipArch();
        if (Arch == nullptr)
            return nullptr;
        return Arch->Companion;
    }

    FTradeLaneRingRuntime*& TlShipRingPtr(FClientGameobj* Ship)
    {
        return *reinterpret_cast<FTradeLaneRingRuntime**>(
            reinterpret_cast<unsigned char*>(Ship) + kTlShipRingRuntime);
    }

    unsigned char& TlShipFlags184(FClientGameobj* Ship)
    {
        return *(reinterpret_cast<unsigned char*>(Ship) + kTlShipFlags184);
    }

    unsigned char& TlShipFlags185(FClientGameobj* Ship)
    {
        return *(reinterpret_cast<unsigned char*>(Ship) + kTlShipFlags185);
    }

    // The +0x184 dword store (0x00513b23, 0x005138b8, ...).
    void TlShipClearFlags(FClientGameobj* Ship)
    {
        *reinterpret_cast<unsigned*>(reinterpret_cast<unsigned char*>(Ship) + kTlShipFlags184) = 0;
    }

    void* TlShipCompanion(FClientGameobj* Ship, unsigned Offset)
    {
        return reinterpret_cast<unsigned char*>(Ship) + Offset;
    }

    // FUN_0054baf0 -- the player's gameobj (the watchable at DAT_00679744 minus eight) or null.
    FClientGameobj* TlPlayerGameobj()
    {
        return static_cast<FClientGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
    }

    // FUN_00515640 (thiscall on a companion) -- the stop sequence (particle vt+4, sound pose and
    // Stop, light detach and vt+8), then the release FUN_00502d90 inlined. Also inlined at
    // 0x005136b3, 0x0051380f, 0x005139e9, 0x00514ccb, 0x00514fbd.
    void TlCompanionStopAndClear(void* Companion)
    {
        CliEffectCompanionArmStop(Companion);
        CliEffectCompanionClear(Companion);
    }

    // FUN_00514070 (thiscall on the factory, RET 0x14) -- an empty id gives false; else
    // FUN_00503070 on the same factory with FindEffectArch(Id).
    unsigned char TlCreateEffectFromId(FCliEffectFactory const* Factory, ID_String const* Id,
                                       void* ListOwner, int EngineIndex, FTransform const* Xform,
                                       void* CompanionOut)
    {
        if (Id->IsEmpty())
            return 0;
        return CliCreateEffectOneShotOn(Factory, FindEffectArch(&Id->Id), ListOwner, EngineIndex,
                                        Xform, CompanionOut);
    }

    // FUN_00515dd0 (thiscall on the factory, RET 0x10) -- the world-space twin of FUN_00514070:
    // an empty id gives false, else FUN_00503f40 with FindEffectArch(Id).
    unsigned char TlCreateWorldEffectFromId(FCliEffectFactory const* Factory, ID_String const* Id,
                                            FTransform const* Xform, float const* Pos3,
                                            void* CompanionOut)
    {
        if (Id->IsEmpty())
            return 0;
        return CliCreateEffectWorldOneShotOn(Factory, FindEffectArch(&Id->Id), Xform, Pos3,
                                             CompanionOut);
    }

    void TlIdentityXform(FTransform* Out)
    {
        // FUN_004b5150 / FUN_004215b0 + zero translation.
        std::memset(Out, 0, sizeof(FTransform));
        Out->Rotation.D[0][0] = 1.0f;
        Out->Rotation.D[1][1] = 1.0f;
        Out->Rotation.D[2][2] = 1.0f;
    }

    // The camera-space player one-shot of FUN_00514960 / 00514bf0 / 00514f20 / 00515b60 /
    // 00515e20: identity orientation at (0, 0, -radius) (get_physical_radius, vt+0x2c), a zero
    // velocity, on the CameraParticles factory DAT_00674c5c with no companion out.
    void TlPlayerCameraOneShot(FClientGameobj* Ship, ID_String const* Id)
    {
        float Radius = 0.0f;
        Vector Center = {};
        Ship->get_physical_radius(Radius, Center);
        FTransform Xform;
        TlIdentityXform(&Xform);
        Xform.Translation.Z = -Radius;
        float const Velocity[3] = {0.0f, 0.0f, 0.0f};
        TlCreateWorldEffectFromId(&G_CliEffectFactoryCameraParticles, Id, &Xform, Velocity,
                                  nullptr);
    }

    // FUN_00513fb0 (thiscall on a vector) -- |V| through the 0x5f3759df inverse square root:
    // (1.5 - y*y*s*0.5) * y * s with s = V.V (DAT_005c75e0 = 0.5, DAT_005ca220 = 1.5). s stays on
    // the FPU stack (only its float copy feeds the bit trick, 0x00513fd4) and so does the
    // result: callers that FSTP it get a float (the distance), the speed stays wide -- hence
    // double here.
    double TlVecLength(FVector const* V)
    {
        double const Sqr = static_cast<double>(V->X) * V->X + static_cast<double>(V->Y) * V->Y +
                           static_cast<double>(V->Z) * V->Z;
        float const SqrFloat = static_cast<float>(Sqr);
        int Bits = 0;
        std::memcpy(&Bits, &SqrFloat, sizeof(Bits));
        int const Guess = 0x5f3759df - (Bits >> 1);
        float Y = 0.0f;
        std::memcpy(&Y, &Guess, sizeof(Y));
        double const YD = Y;
        return (1.5 - YD * YD * Sqr * 0.5) * YD * Sqr;
    }

    // FUN_004fcbe0 (thiscall on ring + 0x20): the current glow to 1.0f.
    void TlGlowCtor(FTradeLaneRingRuntime* Ring)
    {
        Ring->GlowScale = 1.0f;
    }

    // FUN_004fcbf0 (thiscall on ring + 0x20, RET 0xc): max / min of A and B (A > B keeps A as
    // the max, anything else -- NaN included -- B), the rate Param when above 0, else 0.
    void TlGlowSetRange(FTradeLaneRingRuntime* Ring, float Param, float A, float B)
    {
        Ring->GlowMax = B;
        Ring->GlowMin = A;
        if (A > B)
        {
            Ring->GlowMin = B;
            Ring->GlowMax = A;
        }
        if (Param > 0.0f)
            Ring->GlowParam = Param;
        else
            Ring->GlowParam = 0.0f;
    }

    // FUN_004fcc40 (thiscall on ring + 0x20, RET 0xc) -- the glow moves towards its target at
    // GlowParam per second: 0 with no ship probe or the probe outside GlowMax, 1 inside GlowMin,
    // a linear ramp in between.
    float TlGlowUpdate(FTradeLaneRingRuntime* Ring, float Dt, FVector const* ProbeA,
                       FVector const* ProbeB)
    {
        float* const Block = &Ring->GlowMax;
        if (ProbeB == nullptr)
        {
            if (Block[2] >= 0.0f)
            {
                if (Block[2] > 0.0f)
                {
                    float const Next = Block[2] - Dt * Block[3];
                    Block[2] = Next;
                    if (Next < 0.0f)
                        Block[2] = 0.0f;
                }
            }
            else
            {
                float const Next = Dt * Block[3] + Block[2];
                Block[2] = Next;
                if (Next > 0.0f)
                    Block[2] = 0.0f;
            }
            return Block[2];
        }
        float const Dx = ProbeA->X - ProbeB->X;
        float const Dy = ProbeA->Y - ProbeB->Y;
        float const Dz = ProbeA->Z - ProbeB->Z;
        float const DistSqr = Dx * Dx + Dy * Dy + Dz * Dz;
        // The squared radii (0x004fcce3, 0x004fcd6d) and the ramp target (0x004fcdf8..0x004fce08,
        // FSUBR double 1.0 at DAT_005c89b8) stay on the FPU stack: double here.
        double const MaxR = Block[0];
        if (static_cast<double>(DistSqr) < MaxR * MaxR)
        {
            double const MinR = Block[1];
            if (static_cast<double>(DistSqr) > MinR * MinR)
            {
                double const Dist = std::sqrt(static_cast<double>(DistSqr));
                double const Target = 1.0 - (Dist - MinR) / (MaxR - MinR);
                if (Target <= Block[2])
                {
                    if (Target < Block[2])
                    {
                        float const Next = Block[2] - Dt * Block[3];
                        Block[2] = Next;
                        if (Next < Target)
                            Block[2] = static_cast<float>(Target);
                    }
                }
                else
                {
                    float const Next = Dt * Block[3] + Block[2];
                    Block[2] = Next;
                    if (Target < Next)
                        Block[2] = static_cast<float>(Target);
                }
            }
            else if (Block[2] >= 1.0f)
            {
                if (Block[2] > 1.0f)
                {
                    float const Next = Block[2] - Dt * Block[3];
                    Block[2] = Next;
                    if (Next < 1.0f)
                        Block[2] = 1.0f;
                }
            }
            else
            {
                float const Next = Dt * Block[3] + Block[2];
                Block[2] = Next;
                if (Next > 1.0f)
                    Block[2] = 1.0f;
            }
        }
        else if (Block[2] >= 0.0f)
        {
            if (Block[2] > 0.0f)
            {
                float const Next = Block[2] - Dt * Block[3];
                Block[2] = Next;
                if (Next < 0.0f)
                    Block[2] = 0.0f;
            }
        }
        else
        {
            float const Next = Dt * Block[3] + Block[2];
            Block[2] = Next;
            if (Next > 0.0f)
                Block[2] = 0.0f;
        }
        return Block[2];
    }

    // FUN_00514050 (thiscall on ring + 0x30, RET 4): the companion's particle (+8), when there
    // is one, gets its own vtable slot +0x0c with the glow -- a tail JMP.
    void TlNotifyGlowScale(FTradeLaneRingRuntime* Ring, float Glow)
    {
        FEffectBase* const Effect = (FEffectBase*)Ring->EffectSlot2;
        if (Effect == nullptr)
            return;
        typedef void(__thiscall * FSetScaleSlot)(void*, float);
        Effect->SetIntensity(Glow);
    }

    // FUN_00514260 (thiscall on the node) -- the next ring of the lane in the node's direction
    // (traverse_rings vt+0x130 with the side key) while the current ring's health is not 0.0f
    // (vt+0x40; TEST AH,0x44 / JNP: only equal returns null, NaN goes on), from ObjDB bucket 2.
    FClientGameobj* TlNextRingGameobj(FTradeLaneRingRuntime* Ring)
    {
        unsigned NextId = 0;
        if (TlWatchedGameobj(Ring)->traverse_rings(NextId, Ring->SideKey != 0) != 0)
            return nullptr;
        float Health = 1.0f;
        TlWatchedGameobj(Ring)->get_status(Health);
        if (Health == 0.0f)
            return nullptr;
        return ClientObjDb_FindObject(NextId, 2);
    }

    // FUN_005139d0 (thiscall on the node -- never read -- RET 4): the player's tunnel companion
    // stopped and cleared (0x40 dropped), a live lane companion taken off the ship's effect list
    // (FUN_004fcef0), then flags and the node pointer zeroed.
    void TlShipLeaveRingEffects(FTradeLaneRingRuntime* /*Ring*/, FClientGameobj* Ship)
    {
        if (Ship == TlPlayerGameobj())
        {
            TlCompanionStopAndClear(TlShipCompanion(Ship, kTlShipFxBinder174));
            TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) & 0xbfu);
        }
        void* const Lane = TlShipCompanion(Ship, kTlShipFxBinder164);
        if (CreateEffectCompanionAlive(Lane) != 0)
            CliEffectListRemove(reinterpret_cast<unsigned char*>(Ship) + 0x28, Lane);
        TlShipClearFlags(Ship);
        TlShipRingPtr(Ship) = nullptr;
    }

    // FUN_005145a0 (thiscall on the node, RET 4) -- the enter effect, once (+0x185 bit 1; bits
    // 0..1 set): tl_player_enter for the player when it names one, else tl_ship_enter; identity
    // at the ship's engine index, factory DAT_00674c60, no companion out.
    void TlOneshot145a0(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if ((TlShipFlags185(Ship) & 2u) != 0)
            return;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 3u);
        ID_String const* Id = TlCompanionId(Companion, kTlShipEnter);
        if (bPlayer && !TlCompanionId(Companion, kTlPlayerEnter)->IsEmpty())
            Id = TlCompanionId(Companion, kTlPlayerEnter);
        FTransform Xform;
        TlIdentityXform(&Xform);
        long const Index = Ship->get_index();
        if (Id->IsEmpty())
            return;
        CliCreateEffectOneShotOn(&G_CliEffectFactoryC60, FindEffectArch(&Id->Id), Ship,
                                 static_cast<int>(Index), &Xform, nullptr);
    }

    // FUN_00514750 (thiscall on the node, RET 4) -- the travel effect, once (+0x184 bit 0x20).
    // Player: tl_player_travel on the tunnel factory DAT_006751cc, oriented like the ring (its
    // third column negated unless +0x184 bit 0 is set), at the ship's position, engine index -1,
    // into the tunnel companion +0x174. Anyone else: tl_ship_travel on DAT_00674c60, identity at
    // the ship's engine index, into the lane companion +0x164. FUN_005142e0 inlines this whole
    // body through FUN_00514070 (0x00514318..0x0051448c).
    void TlOneshot14750(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if ((TlShipFlags184(Ship) & 0x20u) != 0)
            return;
        TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) | 0x20u);
        if (bPlayer)
        {
            Transform const& RingXform = TlWatchedGameobj(Ring)->get_transform();
            FTransform Xform;
            std::memcpy(&Xform.Rotation, &RingXform.Rotation, sizeof(Xform.Rotation));
            if ((TlShipFlags184(Ship) & 1u) == 0)
            {
                Xform.Rotation.D[0][2] = -Xform.Rotation.D[0][2];
                Xform.Rotation.D[1][2] = -Xform.Rotation.D[1][2];
                Xform.Rotation.D[2][2] = -Xform.Rotation.D[2][2];
            }
            Vector const& Pos = Ship->get_position();
            Xform.Translation.X = Pos.x;
            Xform.Translation.Y = Pos.y;
            Xform.Translation.Z = Pos.z;
            TlCreateEffectFromId(&G_CliEffectFactoryTunnel, TlCompanionId(Companion, kTlPlayerTravel),
                                 Ship, -1, &Xform, TlShipCompanion(Ship, kTlShipFxBinder174));
            return;
        }
        FTransform Xform;
        TlIdentityXform(&Xform);
        long const Index = Ship->get_index();
        TlCreateEffectFromId(&G_CliEffectFactoryC60, TlCompanionId(Companion, kTlShipTravel), Ship,
                             static_cast<int>(Index), &Xform,
                             TlShipCompanion(Ship, kTlShipFxBinder164));
    }

    // FUN_00514960 (thiscall on the node, RET 4) -- the splash ahead of the next ring, once
    // (+0x185 bit 0). The player gets tl_player_splash in camera space when it names one; else
    // tl_ship_splash in world space at the ring's lane hardpoint for the side in +0x184 bit 0
    // (FUN_00514110, then DAT_006751f8's TransformXform with the ring's transform), factory
    // DAT_00674c60 through FUN_00503f40, no companion out.
    void TlOneshot14960(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        if ((TlShipFlags185(Ship) & 1u) != 0)
            return;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 1u);
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if (bPlayer && !TlCompanionId(Companion, kTlPlayerSplash)->IsEmpty())
        {
            TlPlayerCameraOneShot(Ship, TlCompanionId(Companion, kTlPlayerSplash));
            return;
        }
        FClientGameobj* const RingGo = TlWatchedGameobj(Ring);
        FVector HpLocal;
        TradeLaneRingLaneHardpoint(&HpLocal, RingGo,
                                   static_cast<unsigned char>(TlShipFlags184(Ship) & 1u));
        FTransform const* const RingXform =
            reinterpret_cast<FTransform const*>(&RingGo->get_transform());
        FVector HpWorld;
        TlMath()->TransformXform(HpWorld, *RingXform, HpLocal);
        FTransform Xform;
        TlIdentityXform(&Xform);
        Xform.Translation = HpWorld;
        float const Velocity[3] = {0.0f, 0.0f, 0.0f};
        ID_String const* const Id = TlCompanionId(Companion, kTlShipSplash);
        if (Id->IsEmpty())
            return;
        CliCreateEffectWorldOneShotOn(&G_CliEffectFactoryC60, FindEffectArch(&Id->Id), &Xform,
                                      Velocity, nullptr);
    }

    // FUN_00514bf0 (thiscall on the node, RET 8) -- the exit, once (+0x185 bit 2; bits 0 and 2
    // set). The player: with bAnnounce, the lane's space name (CSolar::get_tradelane_space_name)
    // through NN_ArrivalText (FUN_0045b340); the tunnel companion stopped and cleared (0x40
    // dropped). A live lane companion leaves the ship's effect list. Then tl_player_exit in camera
    // space for the player when it names one, else tl_ship_exit at the ship on DAT_00674c60.
    void TlOneshot14bf0(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship, bool bAnnounce)
    {
        if ((TlShipFlags185(Ship) & 4u) != 0)
            return;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 5u);
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if (bPlayer)
        {
            if (bAnnounce)
            {
                CSolar* const Solar = TlNarrowSolar(TlWatchedGameobj(Ring));
                if (Solar != nullptr)
                {
                    unsigned const NameIds = Solar->get_tradelane_space_name();
                    if (NameIds != 0)
                        NnArrivalTextShowNamed(NameIds);
                }
            }
            TlCompanionStopAndClear(TlShipCompanion(Ship, kTlShipFxBinder174));
            TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) & 0xbfu);
        }
        void* const Lane = TlShipCompanion(Ship, kTlShipFxBinder164);
        if (CreateEffectCompanionAlive(Lane) != 0)
            CliEffectListRemove(reinterpret_cast<unsigned char*>(Ship) + 0x28, Lane);
        if (bPlayer && !TlCompanionId(Companion, kTlPlayerExit)->IsEmpty())
        {
            TlPlayerCameraOneShot(Ship, TlCompanionId(Companion, kTlPlayerExit));
            return;
        }
        FTransform Xform;
        TlIdentityXform(&Xform);
        long const Index = Ship->get_index();
        ID_String const* const Id = TlCompanionId(Companion, kTlShipExit);
        if (Id->IsEmpty())
            return;
        CliCreateEffectOneShotOn(&G_CliEffectFactoryC60, FindEffectArch(&Id->Id), Ship,
                                 static_cast<int>(Index), &Xform, nullptr);
    }

    // FUN_00514f20 (thiscall on the node, RET 4) -- leaving a lane early, unless the exit already
    // ran (+0x185 bit 2). The player's tunnel companion stopped and cleared (0x40 dropped first),
    // a live lane companion off the effect list. When the ring gameobj of the ship's node has
    // +0x1f0 set (no ship is using the lane, FUN_00516910) it is a normal exit: FUN_00514bf0 with
    // no announcement. Otherwise the disrupt effect: tl_player_disrupt in camera space for the
    // player when it names one, else tl_ship_disrupt at the ship. +0x185 |= 5 either way.
    void TlLeaveOneshot14f20(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        if ((TlShipFlags185(Ship) & 4u) != 0)
            return;
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if (bPlayer)
        {
            TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) & 0xbfu);
            TlCompanionStopAndClear(TlShipCompanion(Ship, kTlShipFxBinder174));
        }
        void* const Lane = TlShipCompanion(Ship, kTlShipFxBinder164);
        if (CreateEffectCompanionAlive(Lane) != 0)
            CliEffectListRemove(reinterpret_cast<unsigned char*>(Ship) + 0x28, Lane);
        FClientSolarGameobj const* const ShipRingGo =
            static_cast<FClientSolarGameobj const*>(TlWatchedGameobj(TlShipRingPtr(Ship)));
        if (ShipRingGo->Field_0x1f0 != 0)
        {
            TlOneshot14bf0(Ring, Ship, false);
        }
        else if (Ship == TlPlayerGameobj() &&
                 !TlCompanionId(Companion, kTlPlayerDisrupt)->IsEmpty())
        {
            TlPlayerCameraOneShot(Ship, TlCompanionId(Companion, kTlPlayerDisrupt));
        }
        else
        {
            FTransform Xform;
            TlIdentityXform(&Xform);
            long const Index = Ship->get_index();
            ID_String const* const Id = TlCompanionId(Companion, kTlShipDisrupt);
            if (!Id->IsEmpty())
                CliCreateEffectOneShotOn(&G_CliEffectFactoryC60, FindEffectArch(&Id->Id), Ship,
                                         static_cast<int>(Index), &Xform, nullptr);
        }
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 5u);
    }

    void TlBadThings()
    {
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kTradeLaneCpp, 0x367, "bad things (tm)");
    }

    // FUN_005142e0 (thiscall on the node, RET 4) -- put Ship on this node. Already on it: only the
    // travel effect when riding (the FUN_00514750 body inlined). On another node: off that one
    // (not listed there: FDUMP "bad things (tm)"; listed: FUN_00514f20 when riding, FUN_005139d0,
    // erase). Then +0x160 = this, +0x185 bits 0 and 2 cleared, the travel effect when riding, and
    // the ship id appended to the list.
    void TlAssignShipToRing(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        FTradeLaneRingRuntime* const Old = TlShipRingPtr(Ship);
        if (Old != nullptr)
        {
            if (Old == Ring)
            {
                if ((TlShipFlags184(Ship) & 0x80u) != 0)
                    TlOneshot14750(Ring, Ship);
                return;
            }
            FTlShipListNode* const Node = TlShipListFind(Old, Ship->get_id());
            if (Node == nullptr)
            {
                TlBadThings();
            }
            else
            {
                if ((TlShipFlags184(Ship) & 0x80u) != 0)
                    TlLeaveOneshot14f20(Old, Ship);
                TlShipLeaveRingEffects(Old, Ship);
                TlShipListErase(Old, Node);
            }
        }
        TlShipRingPtr(Ship) = Ring;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) & 0xfau);
        if ((TlShipFlags184(Ship) & 0x80u) != 0)
            TlOneshot14750(Ring, Ship);
        TlShipListPushBack(Ring, Ship->get_id());
    }

    // FUN_005163e0 (thiscall on the node) -- one pass over the node's ships; returns the nearest
    // ship not riding the lane (1e7 to start), or null. Gone ships and ships on another node are
    // erased. Each ship's time to the ring (distance over speed; 2500 while riding, 100 when the
    // speed is not above 1) triggers the enter effect (FUN_005145a0, plus FUN_00514750 under
    // DAT_00675200) below SecsBeforeEnter; while riding, the splash (next ring exists) below
    // SecsBeforeSplash or the exit (last ring) below SecsBeforeExit, and passing the ring
    // (get_ring_side flips) moves the ship to the next ring's node or, at the end, clears it.
    // FUN_00513b40 runs the same loop inline (0x00513b90..0x00513dbe, calling 005145a0 / 00514750
    // where this inlines them).
    FClientGameobj* TlRingUpdateShips(FTradeLaneRingRuntime* Ring)
    {
        FClientGameobj* Nearest = nullptr;
        float NearestDist = 1.0e7f; // 0x4b189680
        FTlShipListNode* const Head = TlShipListHead(Ring);
        FTlShipListNode* Node = Head->Next;
        while (Node != Head)
        {
            FTlShipListNode* const Next = Node->Next;
            FClientGameobj* const Ship = ClientObjDb_FindObject(Node->ShipId, 1);
            if (Ship == nullptr)
            {
                TlShipListErase(Ring, Node);
                Node = Next;
                continue;
            }
            Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
            if (TlShipRingPtr(Ship) != Ring)
            {
                TlShipListErase(Ring, Node);
                Node = Next;
                continue;
            }

            Vector const& RingPos = TlWatchedGameobj(Ring)->get_position();
            Vector const& ShipPos = Ship->get_position();
            FVector Delta;
            Delta.X = ShipPos.x - RingPos.x;
            Delta.Y = ShipPos.y - RingPos.y;
            Delta.Z = ShipPos.z - RingPos.z;
            float const Dist = static_cast<float>(TlVecLength(&Delta));

            double Speed = 2500.0; // DAT_005db7d8
            if ((TlShipFlags184(Ship) & 0x80u) == 0)
            {
                Vector const Velocity = Ship->get_velocity();
                FVector V;
                V.X = Velocity.x;
                V.Y = Velocity.y;
                V.Z = Velocity.z;
                Speed = TlVecLength(&V);
            }
            // FCOM 1.0f / TEST AH,0x41 / JNZ: only a speed above 1 divides.
            float Ratio = 100.0f;
            if (Speed > 1.0)
                Ratio = static_cast<float>(Dist / Speed);

            // TEST AH,5 / JP: below.
            if (Ratio < TlCompanionFloat(Companion, kTlSecsBeforeEnter))
            {
                TlOneshot145a0(Ring, Ship);
                if (G_TlStartTravelOnEnter != 0)
                    TlOneshot14750(Ring, Ship);
            }

            if ((TlShipFlags184(Ship) & 0x80u) == 0)
            {
                if (Dist < NearestDist)
                {
                    NearestDist = Dist;
                    Nearest = Ship;
                }
                Node = Next;
                continue;
            }

            FClientGameobj* const NextRing = TlNextRingGameobj(Ring);
            if (NextRing != nullptr)
            {
                if (Ratio < TlCompanionFloat(Companion, kTlSecsBeforeSplash))
                    TlOneshot14960(Ring, Ship);
            }
            else if (Ratio < TlCompanionFloat(Companion, kTlSecsBeforeExit))
            {
                TlOneshot14bf0(Ring, Ship, true);
            }

            bool bSide = true;
            FClientGameobj* const RingGo = TlWatchedGameobj(Ring);
            RingGo->get_ring_side(Ship->get_transform(), &bSide);
            unsigned char const WasSide =
                static_cast<unsigned char>((TlShipFlags184(Ship) >> 1) & 1u);
            if (WasSide != static_cast<unsigned char>(bSide ? 1u : 0u))
            {
                TlShipListErase(Ring, Node);
                TlShipRingPtr(Ship) = nullptr;
                if (NextRing != nullptr)
                {
                    FTradeLaneRingRuntime* const NewRing =
                        TradeLaneRingFindOrCreate(NextRing, Ring->SideKey, true);
                    if ((TlShipFlags184(Ship) & 0x20u) != 0 && Ship == TlPlayerGameobj())
                        TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) | 0x40u);
                    TlAssignShipToRing(NewRing, Ship);
                }
                else
                {
                    TlShipClearFlags(Ship);
                }
            }
            Node = Next;
        }
        return Nearest;
    }

    // The tail FUN_00513b40 (0x00513dc8..0x00513fab) and FUN_00516060's leave path
    // (0x00516136..0x0051627d) share: the glow probe at the ring's lane hardpoint in world space
    // (FUN_00514110, FUN_004e7090); with a nearest ship, the ring's tl_ring_active effect restarted
    // at the lane hardpoint (FUN_00514070 on DAT_00674c60 into ring + 0x30) when its companion is
    // no longer alive and the node is active; the glow updated against the nearest ship's
    // position; false when the node has no ship and the glow is at or below 0.001 (double
    // DAT_005c75f8; TEST AH,0x41 / JZ keeps only a greater one), else the glow handed to the
    // effect (FUN_00514050) and true.
    bool TlRingGlowTail(FTradeLaneRingRuntime* Ring, FClientGameobj* Nearest, float Dt)
    {
        FClientGameobj* const RingGo = TlWatchedGameobj(Ring);
        FVector HpLocal;
        TradeLaneRingLaneHardpoint(&HpLocal, RingGo, Ring->SideKey);
        FVector HpWorld;
        HudTransformPoint(reinterpret_cast<FTransform const*>(&RingGo->get_transform()), &HpWorld,
                          &HpLocal);
        FTransform Probe;
        TradeLaneRingMakeTranslationXform(&Probe, &HpWorld);

        FVector const* ProbeShip = nullptr;
        if (Nearest != nullptr)
        {
            ProbeShip = reinterpret_cast<FVector const*>(&Nearest->get_position());
            if (CreateEffectCompanionAlive(&Ring->EffectSlot0) == 0 && Ring->bActive != 0)
            {
                Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
                FVector Hp;
                TradeLaneRingLaneHardpoint(&Hp, TlWatchedGameobj(Ring), Ring->SideKey);
                FTransform Xform;
                TradeLaneRingMakeTranslationXform(&Xform, &Hp);
                FClientGameobj* const Owner = TlWatchedGameobj(Ring);
                long const Index = TlWatchedGameobj(Ring)->get_index();
                TlCreateEffectFromId(&G_CliEffectFactoryC60, TlCompanionId(Companion, kTlRingActive),
                                     Owner, static_cast<int>(Index), &Xform, &Ring->EffectSlot0);
            }
        }

        float const Glow = TlGlowUpdate(Ring, Dt, &Probe.Translation, ProbeShip);
        if (Ring->ShipListCount == 0 && !(static_cast<double>(Glow) > 0.001))
            return false;
        TlNotifyGlowScale(Ring, Glow);
        return true;
    }

    // FUN_00513b40 (thiscall on the node, RET 4) -- false when the ring gameobj is gone; else the
    // ship pass (the FUN_005163e0 loop, inline in retail) and the glow tail.
    bool TlRingKeepAlive(FTradeLaneRingRuntime* Ring, float Dt)
    {
        if (TlWatchedGameobj(Ring) == nullptr)
            return false;
        FClientGameobj* const Nearest = TlRingUpdateShips(Ring);
        return TlRingGlowTail(Ring, Nearest, Dt);
    }

    // FUN_005137c0 (thiscall on the node) -- a node going to the free list: every listed ship
    // still in ObjDB gets the FUN_005139d0 body (inline here), every entry is erased, and the
    // ring's own companion is stopped and cleared. The watcher and the rest stay as they are.
    void TlDestroyRingRuntime(FTradeLaneRingRuntime* Ring)
    {
        if (Ring->ShipListCount != 0)
        {
            FTlShipListNode* const Head = TlShipListHead(Ring);
            FTlShipListNode* Node = Head->Next;
            while (Node != Head)
            {
                FClientGameobj* const Ship = ClientObjDb_FindObject(Node->ShipId, 1);
                if (Ship != nullptr)
                    TlShipLeaveRingEffects(Ring, Ship);
                TlShipListErase(Ring, Node);
                Node = Head->Next;
            }
        }
        TlCompanionStopAndClear(&Ring->EffectSlot0);
    }

    // FUN_00515730 (thiscall on the node) -- the node's destructor: the FUN_005137c0 ship pass
    // (with FUN_00515640 for the player's tunnel companion), the ring companion stopped and
    // cleared, then the members torn down in reverse order (companion release, watcher, list).
    void TlRingRuntimeDestruct(FTradeLaneRingRuntime* Ring)
    {
        if (Ring->ShipListCount != 0)
        {
            FTlShipListNode* const Head = TlShipListHead(Ring);
            FTlShipListNode* Node = Head->Next;
            while (Node != Head)
            {
                FClientGameobj* const Ship = ClientObjDb_FindObject(Node->ShipId, 1);
                if (Ship != nullptr)
                    TlShipLeaveRingEffects(Ring, Ship);
                TlShipListErase(Ring, Node);
                Node = Head->Next;
            }
        }
        TlCompanionStopAndClear(&Ring->EffectSlot0);
        CliEffectCompanionClear(&Ring->EffectSlot0); // the member's own release (0x005157f1)
        Ring->RingWatcher.set(nullptr);              // 0x00515824: set_pointer(0) when watching
        FTlShipListNode* const Head = TlShipListHead(Ring);
        FTlShipListNode* Node = Head->Next;
        while (Node != Head)
        {
            FTlShipListNode* const Next = Node->Next;
            TlShipListErase(Ring, Node);
            Node = Next;
        }
        operator delete(Head);
        Ring->ShipListSentinel = nullptr;
        Ring->ShipListCount = 0;
    }

    // FUN_00515b60 (cdecl) -- the ship leaves its node: no node, nothing; not listed there, FDUMP
    // "bad things (tm)"; listed, the FUN_00514f20 body when riding and the exit has not run (inline
    // 0x00515bdd..0x00515d50), the FUN_005139d0 body (inline 0x00515d57..0x00515d94), erase.
    void TlShipLeaveCurrentRing(FClientGameobj* Ship)
    {
        FTradeLaneRingRuntime* const Ring = TlShipRingPtr(Ship);
        if (Ring == nullptr)
            return;
        FTlShipListNode* const Node = TlShipListFind(Ring, Ship->get_id());
        if (Node == nullptr)
        {
            TlBadThings();
            return;
        }
        if ((TlShipFlags184(Ship) & 0x80u) != 0 && (TlShipFlags185(Ship) & 4u) == 0)
            TlLeaveOneshot14f20(Ring, Ship);
        TlShipLeaveRingEffects(Ring, Ship);
        TlShipListErase(Ring, Node);
    }

    // IBehaviorManager event 4 (a trade lane) to the player's CEqObj behavior manager, when the
    // player has one (0x0053c765..0x0053c7e2 / 0x0053c836..0x0053c8be). The player's object is
    // narrowed to a CEqObj ((class & 0x103) == 0x103) with no null test on the result.
    void TlNotifyPlayerBehavior(FBehaviorTradelaneEvent* Block)
    {
        FClientGameobj* const Player = TlPlayerGameobj();
        if (Player == nullptr)
            return;
        CObject* const PlayerObj = Player->GetObject();
        CEqObj* const PlayerEq =
            (PlayerObj != nullptr && (static_cast<unsigned>(PlayerObj->ObjectClass) & 0x103u) == 0x103u)
                ? static_cast<CEqObj*>(PlayerObj)
                : nullptr;
        IBehaviorManager* const Behavior = PlayerEq->get_behavior_interface();
        if (Behavior == nullptr)
            return;
        FBehaviorManagerEvent Event;
        Event.Type = 4;
        Event.Data = Block;
        Behavior->Slot00(&Event);
    }

    // The CShip narrowing of the IClient handlers ((class & 0x503) == 0x503).
    CShip* TlNarrowShip(FClientGameobj* Gameobj)
    {
        CObject* const Obj = Gameobj->GetObject();
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return static_cast<CShip*>(Obj);
    }
}

FTradeLaneRingRuntime* TradeLaneRingLiveHead()
{
    return G_TlLiveRings.Head;
}

int TradeLaneRingLiveCount()
{
    return G_TlLiveRings.Count;
}

void TradeLaneRingMakeTranslationXform(FTransform* Out, FVector const* Translation)
{
    // FUN_00514010 -- listing 00514010..0051404c.
    Out->Rotation.D[0][0] = 1.0f;
    Out->Rotation.D[0][1] = 0.0f;
    Out->Rotation.D[0][2] = 0.0f;
    Out->Rotation.D[1][0] = 0.0f;
    Out->Rotation.D[1][1] = 1.0f;
    Out->Rotation.D[1][2] = 0.0f;
    Out->Rotation.D[2][0] = 0.0f;
    Out->Rotation.D[2][1] = 0.0f;
    Out->Rotation.D[2][2] = 1.0f;
    Out->Translation = *Translation;
}

void TradeLaneRingLaneHardpoint(FVector* Out, FClientGameobj* RingGameobj, unsigned char Side)
{
    // FUN_00514110 -- listing 00514110..00514234. The hardpoints are looked up once for the
    // process, on the ring's engine index (vt+0x1c); FindHardpoint_PS's result is not tested.
    if (!G_TlHpCached)
    {
        G_TlHpCached = true;
        long HpHandle = 0;
        HardpointInfo HpInfo = {};
        FindHardpoint_PS(RingGameobj->get_index(), "HpRightLane", HpHandle, HpInfo);
        G_TlHpRight.X = HpInfo.Position.x;
        G_TlHpRight.Y = HpInfo.Position.y;
        G_TlHpRight.Z = HpInfo.Position.z;
        FindHardpoint_PS(RingGameobj->get_index(), "HpLeftLane", HpHandle, HpInfo);
        G_TlHpLeft.X = HpInfo.Position.x;
        G_TlHpLeft.Y = HpInfo.Position.y;
        G_TlHpLeft.Z = HpInfo.Position.z;
    }
    if (Side != 0)
        *Out = G_TlHpRight;
    else
        *Out = G_TlHpLeft;
}

FTradeLaneRingRuntime* TradeLaneRingFindOrCreate(FClientGameobj* RingGameobj,
                                                  unsigned char Side,
                                                  bool bCreate)
{
    // FUN_00515200 -- listing 00515200..00515496. TypeBits bit 7 of the narrowed solar, read with
    // no null test (0x00515247).
    if (!TlNarrowSolar(RingGameobj)->is_lane_ring())
        return nullptr;

    for (FTradeLaneRingRuntime* Node = G_TlLiveRings.Head; Node != nullptr; Node = Node->LiveNext)
    {
        if (TlWatchedGameobj(Node) == RingGameobj && Node->SideKey == Side)
            return Node;
    }
    if (!bCreate)
        return nullptr;

    FTradeLaneRingRuntime* Ring = nullptr;
    if (G_TlFreeRings.Count == 0)
    {
        // operator_new(0x40) and the inlined constructor (0x005152cc..0x0051531e).
        Ring = new FTradeLaneRingRuntime;
        Ring->LiveNext = nullptr;
        Ring->LivePrev = nullptr;
        Ring->Side = Side;
        Ring->ShipListSentinel = TlNewShipListNode(nullptr, nullptr);
        Ring->ShipListCount = 0;
        Ring->RingWatcher.set(nullptr);
        Ring->bActive = 1;
        TlGlowCtor(Ring);
        CliEffectCompanionInit(&Ring->EffectSlot0, nullptr, nullptr, nullptr, nullptr);
    }
    else
    {
        // The free list's head, unlinked inline (0x00515334..0x00515365).
        Ring = G_TlFreeRings.Head;
        TlRingListRemove(&G_TlFreeRings, Ring);
    }

    Ring->bActive = 1;
    Watchable const* WatchTarget = nullptr;
    if (RingGameobj != nullptr)
        WatchTarget = reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char*>(RingGameobj) + 8);
    Ring->RingWatcher.set(WatchTarget);
    Ring->SideKey = Side;

    Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));

    // 0x005153e2..0x0051542d: the lane hardpoint in world space made into a transform nothing
    // reads (FUN_00514110 fills its cache as a side effect).
    FVector HpLocal;
    TradeLaneRingLaneHardpoint(&HpLocal, TlWatchedGameobj(Ring), Ring->SideKey);
    FVector HpWorld;
    HudTransformPoint(reinterpret_cast<FTransform const*>(&TlWatchedGameobj(Ring)->get_transform()),
                      &HpWorld, &HpLocal);
    FTransform Unused;
    TradeLaneRingMakeTranslationXform(&Unused, &HpWorld);

    Ring->GlowScale = 0.0f;
    TlGlowSetRange(Ring, 0.0f, TlCompanionFloat(Companion, kTlActivationEnd),
                   TlCompanionFloat(Companion, kTlActivationStart));

    // Pushed in front of the live head (FUN_00404d50's body inlined, 0x0051544c..0x0051547c).
    TlRingListInsert(&G_TlLiveRings, Ring, G_TlLiveRings.Head);
    return Ring;
}

bool TradeLaneStartCommand(FClientGameobj* EntranceRing, FClientGameobj* ExitRing,
                           FClientGameobj* Ship, unsigned char SideByte)
{
    // FUN_00515890 -- listing 00515890..00515b57.
    unsigned NextId = 0;
    unsigned char LaneSide = 1;
    if (EntranceRing->traverse_rings(NextId, true) < 0 || NextId != ExitRing->get_id())
    {
        LaneSide = 0;
        if (EntranceRing->traverse_rings(NextId, false) < 0 || NextId != ExitRing->get_id())
        {
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kTradeLaneCpp, 0x3bc,
                  "server passed non-contigious ring information.... start tradlane command ignored");
            return false;
        }
    }

    FClientGameobj* const Anchor = (SideByte != 0) ? ExitRing : EntranceRing;
    FTradeLaneRingRuntime* const NewRing = TradeLaneRingFindOrCreate(Anchor, LaneSide, true);

    FTradeLaneRingRuntime* Old = TlShipRingPtr(Ship);
    if (Old != nullptr && Old != NewRing)
    {
        TradeLaneRingFindOrCreate(Anchor, LaneSide, true); // 0x0051595b, result unused
        Old = TlShipRingPtr(Ship);
        if (Old != nullptr)
        {
            FTlShipListNode* const Node = TlShipListFind(Old, Ship->get_id());
            if (Node == nullptr)
            {
                TlBadThings();
            }
            else
            {
                // 0x00515a4b..0x00515adc: FUN_00514f20 when riding, the FUN_005139d0 body
                // inline, erase.
                if ((TlShipFlags184(Ship) & 0x80u) != 0)
                    TlLeaveOneshot14f20(Old, Ship);
                TlShipLeaveRingEffects(Old, Ship);
                TlShipListErase(Old, Node);
            }
        }
    }

    // 0x005159c1..0x00515a0e: the ring side against the exit ring, riding, the node's side key.
    bool bRingSide = false;
    ExitRing->get_ring_side(Ship->get_transform(), &bRingSide);
    unsigned char Flags = TlShipFlags184(Ship);
    unsigned char const SideBit = static_cast<unsigned char>((bRingSide ? 1u : 0u) << 1);
    Flags = static_cast<unsigned char>((((SideBit ^ Flags) & 2u) ^ Flags) | 0x80u);
    Flags = static_cast<unsigned char>(((NewRing->SideKey ^ Flags) & 1u) ^ Flags);
    TlShipFlags184(Ship) = Flags;
    if (SideByte != 0)
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 2u);

    FTradeLaneRingRuntime* const Current = TlShipRingPtr(Ship);
    if (Current != nullptr)
    {
        if (Current == NewRing)
        {
            if ((TlShipFlags184(Ship) & 0x80u) != 0)
                TlOneshot14750(NewRing, Ship);
            return true;
        }
        TlShipLeaveCurrentRing(Ship);
    }

    TlShipRingPtr(Ship) = NewRing;
    TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) & 0xfau);
    if ((TlShipFlags184(Ship) & 0x80u) != 0)
        TlOneshot14750(NewRing, Ship);
    TlShipListPushBack(NewRing, Ship->get_id());
    return true;
}

void TradeLaneShipStop(FClientGameobj* Ship)
{
    // FUN_00515e20 -- listing 00515e20..0051605f: the FUN_00515b60 body again (no node: nothing;
    // not listed: FDUMP; listed: the FUN_00514f20 body when riding and the exit has not run, the
    // FUN_005139d0 body, erase).
    TlShipLeaveCurrentRing(Ship);
}

bool TradeLaneShipGoHandler(FClientGameobj* Ship, FClientGameobj* RingA,
                            FClientGameobj* RingB)
{
    // LAB_00546e90 tail -- listing 00546f2b..00546f59.
    return TradeLaneStartCommand(RingA, RingB, Ship, 0);
}

void TradeLaneDetachTick(float Dt)
{
    // FUN_005135b0 -- listing 005135b0..005137ba.
    FClientGameobj* const Player = TlPlayerGameobj();
    if (Player != nullptr)
    {
        CShip* const PlayerShip = TlNarrowShip(Player);
        if (PlayerShip != nullptr && !PlayerShip->is_using_tradelane() &&
            (TlShipFlags184(Player) & 0x80u) != 0)
        {
            FTradeLaneRingRuntime* const Ring = TlShipRingPtr(Player);
            if (Ring != nullptr)
            {
                FTlShipListNode* const Node = TlShipListFind(Ring, Player->get_id());
                if (Node == nullptr)
                {
                    TlBadThings();
                }
                else
                {
                    if ((TlShipFlags184(Player) & 0x80u) != 0)
                        TlLeaveOneshot14f20(Ring, Player);
                    TlShipLeaveRingEffects(Ring, Player);
                    TlShipListErase(Ring, Node);
                }
            }
            TlCompanionStopAndClear(TlShipCompanion(Player, kTlShipFxBinder164));
            TlCompanionStopAndClear(TlShipCompanion(Player, kTlShipFxBinder174));
        }
    }

    // 0x00513740..0x005137b2. A node FUN_00513b40 rejects leaves the live list for the tail of
    // the free list and is emptied (FUN_005137c0); the walk then goes on from the node *after* the
    // rejected one's successor, and restarts at the head when the rejected node was the last --
    // retail's cursor handling, kept as is.
    FTradeLaneRingRuntime* Cursor = nullptr;
    for (;;)
    {
        FTradeLaneRingRuntime* const Node = (Cursor == nullptr) ? G_TlLiveRings.Head : Cursor->LiveNext;
        if (Node == nullptr)
            break;
        if (TlRingKeepAlive(Node, Dt))
        {
            Cursor = Node;
            continue;
        }
        Cursor = Node->LiveNext;
        TlRingListRemove(&G_TlLiveRings, Node);
        TlRingListInsert(&G_TlFreeRings, Node, nullptr);
        TlDestroyRingRuntime(Node);
    }
}

void TradeLaneShutdown()
{
    // FUN_00515500 -- listing 00515500..00515631. Both walks keep retail's cursor: after a node
    // is handled the next one looked at is the successor of the handled node's successor (the
    // head again once that successor is null), so they stop as soon as that lookup is null.
    FTradeLaneRingRuntime* After = nullptr;
    for (;;)
    {
        FTradeLaneRingRuntime* const Node = (After == nullptr) ? G_TlLiveRings.Head : After->LiveNext;
        if (Node == nullptr)
            break;
        After = Node->LiveNext;
        TlRingListRemove(&G_TlLiveRings, Node);
        TlRingListInsert(&G_TlFreeRings, Node, nullptr);
        if (Node->ShipListCount != 0)
        {
            FTlShipListNode* const Head = TlShipListHead(Node);
            FTlShipListNode* Entry = Head->Next;
            while (Entry != Head)
            {
                FClientGameobj* const Ship = ClientObjDb_FindObject(Entry->ShipId, 1);
                if (Ship != nullptr)
                    TlShipLeaveRingEffects(Node, Ship);
                TlShipListErase(Node, Entry);
                Entry = Head->Next;
            }
        }
        TlCompanionStopAndClear(&Node->EffectSlot0);
    }

    FTradeLaneRingRuntime* FreeAfter = nullptr;
    for (;;)
    {
        FTradeLaneRingRuntime* const Node =
            (FreeAfter == nullptr) ? G_TlFreeRings.Head : FreeAfter->LiveNext;
        if (Node == nullptr)
            break;
        FreeAfter = Node->LiveNext;
        TlRingListRemove(&G_TlFreeRings, Node);
        TlRingRuntimeDestruct(Node);
        delete Node; // FUN_005b7e1d
    }
}

void TradeLaneRingSideCommand(FClientGameobj* RingGameobj, unsigned ShipId, bool bActivate)
{
    // FUN_00516060 -- listing 00516060..005163dc.
    FClientGameobj* const Ship = ClientObjDb_FindObject(ShipId, 1);
    if (Ship == nullptr)
        return;

    if (!bActivate)
    {
        FTradeLaneRingRuntime* const Ring = TlShipRingPtr(Ship);
        if (Ring == nullptr)
            return;
        FTlShipListNode* const Node = TlShipListFind(Ring, Ship->get_id());
        if (Node == nullptr)
        {
            TlBadThings();
            return;
        }
        if ((TlShipFlags184(Ship) & 0x80u) != 0)
            TlLeaveOneshot14f20(Ring, Ship);
        TlShipLeaveRingEffects(Ring, Ship);
        TlShipListErase(Ring, Node);
        if (TlWatchedGameobj(Ring) == nullptr)
            return;
        FClientGameobj* const Nearest = TlRingUpdateShips(Ring);
        TlRingGlowTail(Ring, Nearest, 0.0f);
        return;
    }

    if (TlShipRingPtr(Ship) != nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kTradeLaneCpp, 0x424,
              "tradelane activate: server activating a tradelane twice for the same ship. ignoring. ");
        FTradeLaneRingRuntime* const Old = TlShipRingPtr(Ship);
        if (Old != nullptr)
        {
            FTlShipListNode* const Node = TlShipListFind(Old, Ship->get_id());
            if (Node == nullptr)
            {
                TlBadThings();
            }
            else
            {
                if ((TlShipFlags184(Ship) & 0x80u) != 0)
                    TlLeaveOneshot14f20(Old, Ship);
                TlShipLeaveRingEffects(Old, Ship);
                TlShipListErase(Old, Node);
                TlRingKeepAlive(Old, 0.0f); // 0x00516360, the answer is not used
            }
        }
    }

    bool bDirection = false;
    if (RingGameobj->get_lane_direction(Ship->get_transform(), &bDirection) != 0)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kTradeLaneCpp, 0x42d,
              " couldnt find lane direction for activation effect. ignoring. ");
        return;
    }
    FTradeLaneRingRuntime* const NewRing =
        TradeLaneRingFindOrCreate(RingGameobj, static_cast<unsigned char>(bDirection ? 1u : 0u),
                                  true);
    if (NewRing != nullptr)
        TlAssignShipToRing(NewRing, Ship);
}

void TradeLaneClientSolarActivate(FClientGameobj* SolarGo, bool bActivate, unsigned ShipId)
{
    // FUN_005484c0 -- listing 005484c0..00548511. The object is narrowed with a null test only on
    // the pointer; TypeBits (+0xe0) is then read with no test.
    CObject* const Obj = SolarGo->GetObject();
    CSolar* const Solar =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
            ? static_cast<CSolar*>(Obj)
            : nullptr;
    if (Solar->is_lane_ring())
        TradeLaneRingSideCommand(SolarGo, ShipId, bActivate);
    if (!SinglePlayer())
        Solar->activate(bActivate, ShipId);
}

unsigned char TradeLaneSolarLaneSwitch(FClientGameobj* SolarGo, unsigned char bShipLeft)
{
    // FUN_00516910 (cdecl) -- listing 00516910..00516ae3.
    // 0x00516919..0x0051694a: the first equip-FX wrapper (EquippedObj +0xb8 list) whose
    // equipment is a CETradeLaneEquip. No wrapper leaves a null that retail then reads through
    // (0x00516952), exactly as here.
    FEquipFxListNode* const Head =
        static_cast<FEquipFxListNode*>(static_cast<FClientEquippedObj*>(SolarGo)->EquipFx.Head);
    FClientEquip* Wrapper = nullptr;
    for (FEquipFxListNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (CETradeLaneEquip::cast(Node->Wrapper->Equip) != nullptr)
        {
            Wrapper = Node->Wrapper;
            break;
        }
    }

    // 0x0051694c..0x00516962: the count of ships using the lane, -1 when the byte is set, +1
    // otherwise.
    int* const ShipCount = CliTradeLaneEquipFxShipCount(Wrapper);
    if (bShipLeft != 0)
        *ShipCount = *ShipCount - 1;
    else
        *ShipCount = *ShipCount + 1;

    // 0x00516965..0x00516978: ships still on it -> the host's effect list cleared of the effects
    // whose arch holds 0x28 (FUN_004fd0d0(0x28)); none left -> the lane one-shots again
    // (FUN_00516d10).
    if (*ShipCount != 0)
        CliEffectListClear(reinterpret_cast<unsigned char*>(Wrapper->Host) + 0x28,
                           reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x28)));
    else
        CliTradeLaneEquipFxAttachLaneEffects(Wrapper);

    // 0x0051697d..0x00516ada: for the side-1 node, then the side-0 node, of this ring (looked up,
    // never created): +0x1d = "no ship is using the lane"; while ships use it, the node's
    // tl_ring_active effect is stopped and released when still alive (the FUN_00503330 test
    // inline, then the FUN_00515640 body inline).
    unsigned char const bNoShips = (*ShipCount == 0) ? 1u : 0u;
    unsigned char const Sides[2] = {1, 0};
    for (int Index = 0; Index < 2; ++Index)
    {
        FTradeLaneRingRuntime* const Ring = TradeLaneRingFindOrCreate(SolarGo, Sides[Index], false);
        if (Ring == nullptr)
            continue;
        Ring->bActive = bNoShips;
        if (bNoShips != 0)
            continue;
        if (CreateEffectCompanionAlive(&Ring->EffectSlot0) != 0)
            TlCompanionStopAndClear(&Ring->EffectSlot0);
    }
    return bNoShips;
}

void TradeLaneSPObjActivate(unsigned ObjectId, bool bActivate, unsigned ShipId)
{
    // FUN_0053d940 -- listing 0053d940..0053d9c6.
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kIClientSCpp, 0x5f0,
              "SPObjActivate failed... probably jump-gate in old system closing?\n");
        return;
    }
    if (Go->ManagerKind == 2)
    {
        TradeLaneClientSolarActivate(Go, bActivate, ShipId);
        return;
    }
    if (Go->ManagerKind == 1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kIClientSCpp, 0x5e8,
              "Combat mode no longer supported! (Ignorable)");
    }
}

void TradeLaneIClientGoTradelane(XGoTradelane const* Info)
{
    // LAB_0053c830 (IClient slot 6, RET 8) -- listing 0053c830..0053c936.
    FBehaviorTradelaneEvent Block;
    Block.bEnteringLane = 1;
    Block.bExitRequested = 0;
    Block.bFastExit = static_cast<unsigned char>(Info->Field_0x0c);
    Block.Pad03 = 0;
    Block.Speed = 0.0f;
    Block.Field_0x08 = 0;
    Block.ShipId = Info->ObjectId;
    Block.EntranceId = Info->TradelaneRing1;
    Block.ExitId = Info->TradelaneRing2;
    TlNotifyPlayerBehavior(&Block);

    FClientGameobj* const Ship = ClientObjDb_FindObject(Info->ObjectId, 1);
    if (Ship != nullptr)
    {
        CShip* const ShipObj = TlNarrowShip(Ship);
        if (ShipObj != nullptr)
            ShipObj->use_tradelane(true);
    }
    FClientGameobj* const Ring1 = ClientObjDb_FindObject(Info->TradelaneRing1, 2);
    FClientGameobj* const Ring2 = ClientObjDb_FindObject(Info->TradelaneRing2, 2);
    if (Ship != nullptr && Ring1 != nullptr && Ring2 != nullptr)
        TradeLaneStartCommand(Ring1, Ring2, Ship, static_cast<unsigned char>(Info->Field_0x0c));
}

void TradeLaneIClientStopTradelane(unsigned ShipId, unsigned EntranceId)
{
    // LAB_0053c760 (IClient slot 7, RET 0x10) -- listing 0053c760..0053c829.
    FBehaviorTradelaneEvent Block;
    Block.bEnteringLane = 0;
    Block.bExitRequested = 0;
    Block.bFastExit = 0;
    Block.Pad03 = 0;
    Block.Speed = 0.0f;
    Block.Field_0x08 = 0;
    Block.ShipId = ShipId;
    Block.EntranceId = EntranceId;
    Block.ExitId = 0;
    TlNotifyPlayerBehavior(&Block);

    FClientGameobj* const Ship = ClientObjDb_FindObject(ShipId, 1);
    if (Ship == nullptr)
        return;
    CShip* const ShipObj = TlNarrowShip(Ship);
    if (ShipObj != nullptr)
        ShipObj->use_tradelane(false);
    TradeLaneShipStop(Ship);
}
