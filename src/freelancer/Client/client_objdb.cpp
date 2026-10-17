// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_objdb.h"

#include "client_gameobj.h"
#include "client_tick_profile.h" // Port-only owner-requested collision profiling
#include "common_archetype.h"
#include "common_cequip.h"    // CEShield (FUN_0052fe60)
#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_misc.h"      // ICamera, Geometry::Sphere
#include "common_physys.h"    // GenerateCollisions / GetCollisions (FUN_0052fc20)
#include "common_server.h"
#include "common_universe.h"
#include "menu_sound_arch.h"  // GetClientSoundListenerObject (FUN_0040f130)
#include "player_spawn.h"     // GetPlayerConnectionId (DAT_00673344)
#include "server_connect.h"   // GetLocalServerInterface (DAT_0067ecd0)
#include "star_sys.h"
#include "x86math.h"          // I3DMathEngine::MatrixToQuaternion

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

// ============================================================================
// Client IObjDB — retail DAT_00678a2c (PTR_DAT_00613ed0), vtable .rdata:005df8ac.
//
// Type-bucket heads (FUN_00540fc0 First pointers → Count at First-4):
//   kind 1 ships  DAT_00678aa8   mask walk 0x7f0000
//   kind 2 solars DAT_00678ab4   mask walk 0xfdff
//   kind 3..10    DAT_00678a90 … (see FUN_00540fc0)
// Link/unlink: FUN_00542350 / FUN_00542390. List advance: FUN_00542330.
// GetBucket (+0x0c): DAT_00678ae0 — Common ISpatialPartition, freed by FUN_00540e90 and
// (re)built with NewSpatial("BSP") by FUN_00540eb0 around every system INI load.
// GetZoneDb (+0x08): DAT_00679954 = Universe::ISystem* (EnterStarSystem).
// ============================================================================

namespace
{
    struct FGameobjList
    {
        int Count;
        FClientGameobj* First;
        FClientGameobj* Last;
    };

    struct FObjDbIterState
    {
        FClientGameobj* Cursor;
        FGameobjList* ListHead;
        unsigned Mask;
    };

    struct FIObjDB : public IObjDB
    {
        // Lists[1..10] are the ManagerKind buckets (FUN_00540fc0). Lists[0] is DAT_00678a54,
        // the eleventh list: no FUN_00540c30 case links into it, but its static initialiser
        // (LAB_00540b00), FUN_00541210, FUN_00541380 (0x005414d3) and FUN_00541510
        // (0x00541556) all walk it, so it is kept.
        FGameobjList Lists[11];
        ISpatialPartition* Spatial; // DAT_00678ae0

        IObjRW* __thiscall GetNext(void* IterStateRaw) override;
        IObjRW* __thiscall FindById(unsigned const* ObjectId) override;
        Universe::ISystem* __thiscall GetZoneDb() override;
        void* __thiscall GetBucket(unsigned TypeMask) override;
    };

    unsigned TotalCount(FIObjDB const* Db)
    {
        unsigned Sum = 0;
        for (int Kind = 1; Kind <= 10; ++Kind)
            Sum += static_cast<unsigned>(Db->Lists[Kind].Count);
        return Sum;
    }

    // FUN_00542350 (ECX = list, RET 4): append at the tail. A null Gameobj is a no-op.
    void LinkGameobj(FGameobjList* List, FClientGameobj* Go)
    {
        if (Go == nullptr)
            return;

        Go->ListPrev = List->Last;
        if (List->Last != nullptr)
        {
            List->Last->ListNext = Go;
            List->Last = Go;
        }
        else
        {
            List->First = Go;
            List->Last = Go;
        }
        Go->ListNext = nullptr;
        ++List->Count;
    }

    // FUN_00542390 (ECX = list, RET 4). The three shapes are retail's own branches
    // (0x005423a1 no prev, 0x005423d8 prev but no next, 0x005423f4 both). A null Gameobj skips
    // the unlink but still decrements the count (0x00542399 -> 0x00542407); no caller passes
    // one.
    void UnlinkGameobj(FGameobjList* List, FClientGameobj* Go)
    {
        if (Go != nullptr)
        {
            FClientGameobj* const Prev = Go->ListPrev;
            FClientGameobj* const Next = Go->ListNext;
            if (Prev == nullptr)
            {
                List->First = Next;
                if (Next != nullptr)
                    Next->ListPrev = nullptr;
                else
                    List->Last = nullptr;
            }
            else if (Next == nullptr)
            {
                List->Last = Prev;
                Prev->ListNext = nullptr;
            }
            else
            {
                Prev->ListNext = Next;
                Next->ListPrev = Prev;
            }
            Go->ListPrev = nullptr;
            Go->ListNext = nullptr;
        }
        --List->Count;
    }

    // FUN_00542330 (ECX = list, RET 4): a null cursor starts at First (list+4), otherwise the
    // cursor's ListNext (+0x14); the result is stored back and returned. No null tests.
    FClientGameobj* AdvanceListCursor(FGameobjList* List, FClientGameobj** CursorInOut)
    {
        if (*CursorInOut == nullptr)
        {
            *CursorInOut = List->First;
            return List->First;
        }
        *CursorInOut = (*CursorInOut)->ListNext;
        return *CursorInOut;
    }

    // One arm of LAB_00541d60, and the whole of FUN_005420a0 (its out-of-line copy, used for the
    // last three arms; RET 0x10: Mask, List, &Iter->Cursor, &Iter->ListHead -- the ECX it is
    // handed is never read). A walk already parked in another list yields nothing for this arm
    // (0x00541d83..0x00541d8a / 0x005420b2..0x005420b6). Otherwise the saved cursor advances
    // until a gameobj whose CObject, narrowed to a CSimple (class & 3 == 3, else null), has a
    // TypeBits (+0xe0) bit in Mask: the cursor and this list are saved and the gameobj
    // returned. The narrowed pointer is dereferenced without a test (0x00541db1..0x00541dc2,
    // 0x005420df..0x005420f4). The list's end clears both and yields nothing.
    FClientGameobj* WalkListArm(FObjDbIterState* Iter, FGameobjList* List, unsigned Mask)
    {
        if (Iter->ListHead != nullptr && Iter->ListHead != List)
            return nullptr;

        FClientGameobj* CursorScratch = Iter->Cursor;
        for (;;)
        {
            FClientGameobj* const Go = AdvanceListCursor(List, &CursorScratch);
            if (Go == nullptr)
            {
                Iter->Cursor = nullptr;
                Iter->ListHead = nullptr;
                return nullptr;
            }
            CObject* const Obj = const_cast<CObject*>(Go->cobject());       // vt+0x150
            CSimple* const Simple = (Obj != nullptr && (Obj->ObjectClass & 3u) == 3u)
                                        ? static_cast<CSimple*>(Obj)
                                        : nullptr;
            if ((Simple->TypeBits & Mask) == 0u)
                continue;
            Iter->Cursor = CursorScratch;
            Iter->ListHead = List;
            return Go;
        }
    }

    IObjRW* __thiscall FIObjDB::GetNext(void* IterStateRaw)
    {
        // LAB_00541d60 (IObjDB slot 0, RET 4). The iterator is {Cursor, ListHead, Mask}; the
        // mask is re-read before every arm, and an arm runs only while nothing has been found.
        // Arm order and lists (kind -> list as KindList):
        //   0x7f0000 kind 1 DAT_00678aa8, 0xfdff kind 2 ab4, 0x10000000 kind 7 a60,
        //   0x20000000 kind 8 a48, 0x40000000 kind 9 a3c, 0x1000000 kind 4 a9c -- inline;
        //   0x4000000 kind 5 a84, 0x8000000 kind 6 a78, 0x2000000 kind 3 a90 -- FUN_005420a0
        //   (0x00542027..0x00542088). No null test on the iterator.
        FObjDbIterState* const Iter = static_cast<FObjDbIterState*>(IterStateRaw);
        FClientGameobj* Hit = nullptr;

        if ((Iter->Mask & 0x7f0000u) != 0u)
            Hit = WalkListArm(Iter, &Lists[1], Iter->Mask);
        if ((Iter->Mask & 0xfdffu) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[2], Iter->Mask);
        if ((Iter->Mask & 0x10000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[7], Iter->Mask);
        if ((Iter->Mask & 0x20000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[8], Iter->Mask);
        if ((Iter->Mask & 0x40000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[9], Iter->Mask);
        if ((Iter->Mask & 0x1000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[4], Iter->Mask);
        if ((Iter->Mask & 0x4000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[5], Iter->Mask);
        if ((Iter->Mask & 0x8000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[6], Iter->Mask);
        if ((Iter->Mask & 0x2000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[3], Iter->Mask);

        return Hit;
    }

    IObjRW* __thiscall FIObjDB::FindById(unsigned const* ObjectId)
    {
        // LAB_00542130 (RET 4): FUN_005416c0(*ObjectId, 0) inlined -- same 0x3fffffff gate,
        // same 1/4/5/6/7/2 bucket order. The id is dereferenced unconditionally (0x00542135).
        return ClientObjDb_FindObject(*ObjectId, 0);
    }

    Universe::ISystem* __thiscall FIObjDB::GetZoneDb()
    {
        // LAB_005422c0 → FUN_005556a0: return DAT_00679954 (owned by star_sys.cpp).
        return const_cast<Universe::ISystem*>(StarSysGetSystem());
    }

    void* __thiscall FIObjDB::GetBucket(unsigned TypeMask)
    {
        // LAB_005422d0: (TypeMask & 0xfdff) ? DAT_00678ae0 : nullptr. The partition is only
        // created by FUN_00540eb0 (ClientObjDb_RebuildSpatial) at the end of a system load.
        if ((TypeMask & 0xfdffu) == 0u)
            return nullptr;
        return Spatial;
    }

    // DAT_00678ae4 -- incremented by FUN_00540eb0 on every non-empty rebuild (00540ee3);
    // retail never reads it.
    unsigned G_ClientSpatialRebuildCount = 0;

    // DAT_00613ec4 = 200.0f (0x43480000): a solar whose radius is above it is a "large" solar.
    constexpr float kLargeSolarRadiusThreshold = 200.0f;
    // DAT_00613ecc = 10000.0f (0x461c4000): the radius of FUN_00541380's update sphere around
    // the listener camera.
    constexpr float kSpatialUpdateRadius = 10000.0f;

    // Large solars -- DAT_00678ac0 is a std::vector<Gameobj*> (first +0xc4, last +0xc8). A
    // kind-2 Gameobj goes in when its radius is above 200 and bit 7 of its get_type is clear
    // (FUN_00540c30, 0x00540d18..0x00540d5f, push through FUN_00537260 = _Insert_n at end).
    // FUN_00541380 updates them every frame no matter where the camera is, and skips them in
    // its sphere query; FUN_00541040 erases them.
    std::vector<FClientGameobj*> G_LargeSolars;

    // The 0x2f8-byte block FUN_00540c30 allocates for a remote-physics Gameobj
    // (0x00540dbd..0x00540e12): the CRemotePhysicsSimulation is constructed at +0x10 first,
    // then +0x00 and +0x04 are zeroed and +0x08 gets the Gameobj's CObject narrowed to a CShip
    // (ObjectClass & 0x503, else null). FUN_00541200 is its destructor (the simulation's) and
    // FUN_00541040 frees it with operator delete.
    struct FRemotePhysBlock
    {
        // +0x00: both dwords zeroed at 0x00540e0d / 0x00540e0f (= 0.0); FUN_00541580 adds Dt to it
        // as a double every multiplayer frame and sends it as SSPObjUpdateInfo::Timestamp.
        double ElapsedTime;                    // +0x00
        CShip* Ship;                           // +0x08  FUN_00541040 matches blocks on it
        unsigned Field_0x0c;                   // +0x0c  never written by FUN_00540c30
        CRemotePhysicsSimulation Simulation;   // +0x10
    };

    // DAT_00678a30: a VC6 std::list<block*> -- head node at DAT_00678a34 (built by the static
    // initialiser LAB_00540b30), size at DAT_00678a38. FUN_00540c30 push_backs a 0xc node
    // {next, prev, block*}; FUN_00541040 erases the first node whose block's Ship matches.
    std::list<FRemotePhysBlock*> G_RemotePhysBlocks;

    // The CObject a Gameobj wraps, narrowed the way FUN_00540c30 / FUN_00541040 do it: the
    // CBase sub-object at Gameobj+0xc, its CObject at +0x10 (Gameobj+0x10), and the class test.
    // The null tests on the Gameobj pointer itself in retail are only the compiler's
    // this-adjustment; both functions dereference Gameobj+0x1c unconditionally afterwards.
    CSimple* NarrowSimple(FClientGameobj* Go)
    {
        CObject* const Obj = Go->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 3u) != 3u)
            return nullptr;
        return static_cast<CSimple*>(Obj);
    }

    CShip* NarrowShip(FClientGameobj* Go)
    {
        CObject* const Obj = Go->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return static_cast<CShip*>(Obj);
    }

    // Retail builds each of the eleven lists with FUN_00542320 (Count, Last, First = 0) from its
    // own XC entry (LAB_00540a80 .. LAB_00540b20) and leaves DAT_00678ae0 zero in .bss; the
    // value-initialized static object starts in exactly that state, so nothing is re-zeroed
    // on first use.
    static FIObjDB G_ClientObjDb = {};

    // The list FUN_00540c30 / FUN_00541040 switch Gameobj+0x1c into (kind - 1 above 9 -> none):
    // 1 DAT_00678aa8, 2 ab4, 3 a90, 4 a9c, 5 a84, 6 a78, 7 a60, 8 a48, 9 a3c, 10 a6c.
    FGameobjList* KindList(int Kind)
    {
        if (static_cast<unsigned>(Kind - 1) > 9u)
            return nullptr;
        return &G_ClientObjDb.Lists[Kind];
    }

    // FUN_005424b0 (ECX = list, RET 4): every Gameobj's update (vt+0x174, slot 93) in link
    // order; the next link is read after the call.
    void UpdateList(FGameobjList* List, float Dt)
    {
        FClientGameobj* Go = List->First;
        while (Go != nullptr)
        {
            Go->update(Dt);
            Go = Go->ListNext;
        }
    }

    // FUN_00542410 (ECX = list, RET 8): unlink every Gameobj that is not in Keep (a null Keep
    // keeps nothing) and, with bDelete, delete it through the scalar deleting destructor of its
    // CBase sub-object at +0xc (vt[0](1), 0x00542493..0x0054249a). The next link is read before
    // the Gameobj is touched. Unlike FUN_005424e0 it does not go through FUN_00541040: the
    // remote-physics blocks and the large-solar vector are cleared by the caller.
    void ClearListExcept(FGameobjList* List, std::list<FClientGameobj*> const* Keep, bool bDelete)
    {
        FClientGameobj* Go = List->First;
        while (Go != nullptr)
        {
            FClientGameobj* const Next = Go->ListNext;
            bool bKeep = false;
            if (Keep != nullptr)
            {
                // 0x0054242e..0x00542444: the keep list's values compared with the Gameobj.
                std::list<FClientGameobj*>::const_iterator It = Keep->begin();
                for (; It != Keep->end(); ++It)
                {
                    if (*It == Go)
                    {
                        bKeep = true;
                        break;
                    }
                }
            }
            if (!bKeep)
            {
                // 0x00542446..0x0054248f: the inlined FUN_00542390 unlink (the three shapes),
                // both links cleared, --Count.
                UnlinkGameobj(List, Go);
                if (bDelete)
                    delete Go;
            }
            Go = Next;
        }
    }

    // FUN_005424e0 (ECX = list): the next link is read first; a Gameobj whose slot 86 (+0x158,
    // Flags bit 0) says it is finished is unregistered (FUN_00541040) and deleted through the
    // scalar deleting destructor of its CBase sub-object at +0xc (vt[0](1), 0x00542507).
    void ReapList(FGameobjList* List)
    {
        FClientGameobj* Go = List->First;
        while (Go != nullptr)
        {
            FClientGameobj* const Next = Go->ListNext;
            if (Go->Slot158())
            {
                ClientObjDb_Unregister(Go);
                delete Go;
            }
            Go = Next;
        }
    }
}

IObjDB* GetClientObjDb()
{
    return &G_ClientObjDb;
}

void ClientObjDb_RegisterGameobj(FClientGameobj* Go, bool bRemotePhysics)
{
    // FUN_00540c30 (cdecl, two arguments; eleven callers 0x004f4752 .. 0x0053d316).
    // 0x00540c50..0x00540c74: EDI = the CObject as a CSimple (class & 3), else null.
    CSimple* const Simple = NarrowSimple(Go);

    int const Kind = Go->ManagerKind;
    if (Kind == 2)
    {
        // 0x00540d18..0x00540d5f: cobject (vt+0x150) radius above 200 and bit 7 of get_type
        // (vt+0x88, result not tested; the out value is written on every path) clear ->
        // the large-solar vector.
        if (Go->cobject()->Radius > kLargeSolarRadiusThreshold)
        {
            uint Type = 0;
            Go->get_type(Type);
            if ((Type & 0x80u) == 0u)
                G_LargeSolars.push_back(Go);
        }

        // 0x00540d64.
        LinkGameobj(&G_ClientObjDb.Lists[2], Go);

        // 0x00540d6f..0x00540dae: with the partition up and TypeBits (+0xe0) & 0xfdff, AddObject
        // (+0x10) the entry {Gameobj, CSimple+0x2c position, CSimple+0x38 radius}. Nothing is
        // created here: during a system load the partition was freed by FUN_00540e90 and
        // FUN_00540eb0 bulk-adds every solar afterwards. Simple is not null-tested (0x00540d79).
        if (G_ClientObjDb.Spatial != nullptr && (Simple->TypeBits & 0xfdffu) != 0u)
        {
            FSpatialEntry Entry;
            Entry.Object = static_cast<IObjRW*>(Go);
            Entry.Position = Simple->WorldPosition;
            Entry.Radius = Simple->Radius;
            SpatialAddObject(G_ClientObjDb.Spatial, &Entry);
        }
    }
    else
    {
        FGameobjList* const List = KindList(Kind);
        if (List != nullptr)
            LinkGameobj(List, Go);
    }

    // 0x00540db1..0x00540e49: the remote-physics block, pushed at the back of DAT_00678a30.
    if (bRemotePhysics)
    {
        FRemotePhysBlock* const Block = new FRemotePhysBlock;
        Block->ElapsedTime = 0.0;
        Block->Ship = NarrowShip(Go);
        G_RemotePhysBlocks.push_back(Block);
    }
}

void ClientObjDb_Unregister(FClientGameobj* Go)
{
    // FUN_00541040 (cdecl; callers 0x0053bd1c and FUN_005424e0). It only unlinks: both
    // callers delete the Gameobj themselves afterwards.

    // 0x00541041..0x005410c2: the first remote-physics block whose Ship equals this Gameobj's
    // CShip (null for a non-ship, which then matches a block whose Ship is null) is destroyed
    // (FUN_00541200, operator delete) and its node erased.
    CShip* const Ship = NarrowShip(Go);
    std::list<FRemotePhysBlock*>::iterator It = G_RemotePhysBlocks.begin();
    for (; It != G_RemotePhysBlocks.end(); ++It)
    {
        if ((*It)->Ship == Ship)
        {
            delete *It;
            G_RemotePhysBlocks.erase(It);
            break;
        }
    }

    int const Kind = Go->ManagerKind;
    if (Kind == 2)
    {
        // 0x0054111d..0x00541165: unlink, then erase the first matching large-solar slot.
        UnlinkGameobj(&G_ClientObjDb.Lists[2], Go);
        std::vector<FClientGameobj*>::iterator Large = G_LargeSolars.begin();
        for (; Large != G_LargeSolars.end(); ++Large)
        {
            if (*Large == Go)
            {
                G_LargeSolars.erase(Large);
                break;
            }
        }

        // 0x0054116b..0x00541191: get_type (vt+0x88) succeeded and & 0xfdff -> RemoveObject
        // (+0x0c) from the partition. Retail does not test DAT_00678ae0 for null here.
        uint Type = 0;
        if (Go->get_type(Type) == 0 && (Type & 0xfdffu) != 0u)
        {
            G_ClientObjDb.Spatial->Remove(static_cast<IObjRW*>(Go));
        }
        return;
    }

    FGameobjList* const List = KindList(Kind);
    if (List != nullptr)
        UnlinkGameobj(List, Go);
}

void ClientObjDb_UpdateAll(float Dt)
{
    // FUN_00541380 (cdecl; caller 0x0054b03b in the space frame).
    UpdateList(&G_ClientObjDb.Lists[1], Dt);
    UpdateList(&G_ClientObjDb.Lists[3], Dt);
    UpdateList(&G_ClientObjDb.Lists[4], Dt);
    UpdateList(&G_ClientObjDb.Lists[5], Dt);
    UpdateList(&G_ClientObjDb.Lists[6], Dt);

    // 0x005413cb..0x005414c5: solars are only updated while the partition exists.
    ISpatialPartition* const Spatial = G_ClientObjDb.Spatial;
    if (Spatial != nullptr)
    {
        // A 10000-radius sphere around the listener camera (FUN_0040f130, ICamera slot 3
        // GetPosition, stdcall), queried with Fill (+0x1c) into the usual {0x800, 0, items}.
        ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());
        float const* const CameraPos = Camera->GetPosition();
        Geometry::Sphere Query;
        Query.Center.x = CameraPos[0];
        Query.Center.y = CameraPos[1];
        Query.Center.z = CameraPos[2];
        Query.Radius = kSpatialUpdateRadius;
        // The result lives on the stack, as retail's 0x201c-byte frame (_chkstk at 0x00541385).
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        SpatialFill(Spatial, &Query, &Result);

        for (unsigned Index = 0; Index < Result.Count; ++Index)
        {
            IObjRW* const Item = static_cast<IObjRW*>(Result.Items[Index]);
            // 0x00541436..0x00541467: a large solar (radius above 200, get_type bit 7 clear)
            // is left to the vector loop below.
            if (Item->cobject()->Radius > kLargeSolarRadiusThreshold)
            {
                uint Type = 0;
                Item->get_type(Type);
                if ((Type & 0x80u) == 0u)
                    continue;
            }
            // 0x00541469..0x00541489: __RTDynamicCast IObjRW -> GameObject, then update.
            FClientGameobj* const Go = dynamic_cast<FClientGameobj*>(Item);
            if (Go != nullptr)
                Go->update(Dt);
        }

        // 0x0054149c..0x005414c5: every large solar; the end is re-read after each call.
        for (size_t Index = 0; Index < G_LargeSolars.size(); ++Index)
            G_LargeSolars[Index]->update(Dt);
    }

    UpdateList(&G_ClientObjDb.Lists[10], Dt);
    UpdateList(&G_ClientObjDb.Lists[0], Dt);
    UpdateList(&G_ClientObjDb.Lists[7], Dt);
    UpdateList(&G_ClientObjDb.Lists[8], Dt);
    UpdateList(&G_ClientObjDb.Lists[9], Dt);
}

void ClientObjDb_Reap()
{
    // FUN_00541510 (caller 0x0054b15e): FUN_005424e0 on every list, in this order.
    ReapList(&G_ClientObjDb.Lists[1]);
    ReapList(&G_ClientObjDb.Lists[3]);
    ReapList(&G_ClientObjDb.Lists[4]);
    ReapList(&G_ClientObjDb.Lists[5]);
    ReapList(&G_ClientObjDb.Lists[6]);
    ReapList(&G_ClientObjDb.Lists[2]);
    ReapList(&G_ClientObjDb.Lists[10]);
    ReapList(&G_ClientObjDb.Lists[0]);
    ReapList(&G_ClientObjDb.Lists[7]);
    ReapList(&G_ClientObjDb.Lists[8]);
    ReapList(&G_ClientObjDb.Lists[9]);
}

FClientGameobj* ClientObjDb_FindByCObject(CObject const* Obj)
{
    // FUN_005419a0 (cdecl; caller 0x0052fe88). ObjectClass (+0x4c) picks the bucket -- the
    // byte table at 0x00541ca0 for classes 1..0xa7, compares above it -- and the bucket is
    // walked for the Gameobj whose cobject (vt+0x150) is Obj. Any other class is no Gameobj.
    int Kind = 0;
    switch (static_cast<int>(Obj->ObjectClass))
    {
    case CObject::ClassShip: Kind = 1; break;
    case CObject::ClassSolar: Kind = 2; break;
    case CObject::ClassBeam: Kind = 3; break;
    case CObject::ClassGuided: Kind = 4; break;
    case CObject::ClassMine: Kind = 5; break;
    case CObject::ClassCounterMeasure: Kind = 6; break;
    case CObject::ClassLoot: Kind = 7; break;
    case CObject::ClassAsteroid: Kind = 8; break;
    case CObject::ClassDynamicAsteroid: Kind = 9; break;
    case CObject::ClassObject: Kind = 10; break;
    default: return nullptr;
    }

    FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
    for (; Go != nullptr; Go = Go->ListNext)
    {
        if (Go->cobject() == Obj)
            return Go;
    }
    return nullptr;
}

unsigned ClientObjDb_GetRegisteredCount()
{
    return TotalCount(&G_ClientObjDb);
}

unsigned ClientObjDb_GetSize()
{
    return ClientObjDb_GetRegisteredCount();
}

bool ClientObjDb_TryGetWorldPosById(unsigned NicknameId, float OutPos[3])
{
    if (OutPos == nullptr || NicknameId == 0u)
        return false;

    for (int Kind = 1; Kind <= 10; ++Kind)
    {
        for (FClientGameobj* Go = G_ClientObjDb.Lists[Kind].First; Go != nullptr;
             Go = Go->ListNext)
        {
            if (Go->Object == nullptr)
                continue;
            if (Go->get_id() != NicknameId)
                continue;
            Vector const& Pos = Go->get_position();
            OutPos[0] = Pos.x;
            OutPos[1] = Pos.y;
            OutPos[2] = Pos.z;
            return true;
        }
    }
    return false;
}

bool ClientObjDb_TryGetWorldPosByArchetypeId(unsigned ArchetypeId, float OutPos[3])
{
    if (OutPos == nullptr || ArchetypeId == 0u)
        return false;

    for (int Kind = 1; Kind <= 10; ++Kind)
    {
        for (FClientGameobj* Go = G_ClientObjDb.Lists[Kind].First; Go != nullptr;
             Go = Go->ListNext)
        {
            if (Go->Object == nullptr)
                continue;
            Archetype::Root* const Arch = Go->Object->get_archetype();
            if (Arch == nullptr || Arch->Nickname.Id != ArchetypeId)
                continue;
            Vector const& Pos = Go->get_position();
            OutPos[0] = Pos.x;
            OutPos[1] = Pos.y;
            OutPos[2] = Pos.z;
            return true;
        }
    }
    return false;
}

FClientGameobj* ClientObjDb_GetKindHead(int const* Kind)
{
    // FUN_00540fc0: switch on *Kind - 1 (JA past 9 -> null). Each case returns one bucket's
    // head dword (kind 1 DAT_00678aac, 2 ab8, 3 a94, 4 aa0, 5 a88, 6 a7c, 7 a64, 8 a4c,
    // 9 a40, 10 a70); the port keeps the ten buckets as Lists[1..10]. G_ClientObjDb is a
    // zero-initialised static, as retail's .data lists are, so no init call precedes the read.
    int const Index = *Kind;
    if (Index < 1 || Index > 10)
        return nullptr;
    return G_ClientObjDb.Lists[Index].First;
}

FClientGameobj* ClientObjDb_LargeSolarFirst(size_t* It)
{
    // FUN_00540a00 (thiscall, ECX = the caller's iterator): *It = first (DAT_00678ac4); the
    // entry there, or null when first == last (DAT_00678ac8).
    *It = 0;
    if (*It == G_LargeSolars.size())
        return nullptr;
    return G_LargeSolars[*It];
}

FClientGameobj* ClientObjDb_LargeSolarNext(size_t* It)
{
    // FUN_00540a20 (thiscall, ECX = the iterator): advance by one slot; the entry there, or null
    // at last.
    *It = *It + 1;
    if (*It == G_LargeSolars.size())
        return nullptr;
    return G_LargeSolars[*It];
}

bool ClientObjDb_IsLargeSolar(FClientGameobj const* Go)
{
    // FUN_00540a40 (cdecl): walk first..last; a null entry ends the walk with false
    // (0x00540a53 / 0x00540a6d), an entry equal to Go returns true (0x00540a60).
    for (size_t Index = 0; Index < G_LargeSolars.size(); ++Index)
    {
        FClientGameobj const* const Entry = G_LargeSolars[Index];
        if (Entry == nullptr)
            return false;
        if (Entry == Go)
            return true;
    }
    return false;
}

void ClientObjDb_MoveBeams(float Dt)
{
    // FUN_00540bd0 (cdecl; caller 0x0054b058 in the space frame): every Gameobj of bucket 3
    // (FUN_00540fc0 with a local 3), its CObject narrowed to a CBeam (class & 0x67 == 0x67, else
    // null -- the test of the Gameobj is only the CBase this-adjustment), CBeam::move(Dt) with
    // no further test; next through +0x14.
    int Kind = 3;
    for (FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind); Go != nullptr; Go = Go->ListNext)
    {
        CObject* const Obj = Go->Object;
        CBeam* Beam = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x67u) == 0x67u)
            Beam = static_cast<CBeam*>(Obj);
        Beam->move(Dt);
    }
}

namespace
{
    // DAT_006754bc -- the number of PhySys collision events FUN_0052fc20 has walked, bumped
    // once per event (0x0052fcf7..0x0052fd00). Its only reader is that increment.
    int GClientCollisionEventCount = 0;

    // The 0x24-byte record FUN_0052fc20 builds on its stack at [ESP+0x3c] for slot 101's first
    // argument: the event's +0x10 point, then its +0x1c and +0x28 vectors (negated for side A,
    // as they are for side B).
    struct FClientCollisionRecord
    {
        Vector Point;      // +0x00  event +0x10
        Vector Field_0x0c; // +0x0c  event +0x1c
        Vector Field_0x18; // +0x18  event +0x28
    };
    static_assert(sizeof(FClientCollisionRecord) == 0x24, "collision record 0x24");

    // FUN_0052fe60 (cdecl): one side of a PhySys collision -> the Gameobj of the part's root
    // (FUN_005419a0) and the sub-object it was hit on.
    void ResolveCollisionSide(CObject* Part, unsigned PartId, FClientGameobj** OutGo,
                              unsigned short* OutSubObjId)
    {
        *OutGo = nullptr;                                                      // 0x0052fe6a
        *OutSubObjId = SUBOBJ_ID_NONE;                                         // 0x0052fe7e
        FClientGameobj* const Go = ClientObjDb_FindByCObject(GetRoot(Part));   // 0x0052fe88
        *OutGo = Go;
        if (Go == nullptr)
            return;                                                            // 0x0052fe94
        // 0x0052fe9a..0x0052febf: the Gameobj's own CObject (+0x10) under the CEqObj mask;
        // anything else was hit on its root (0x0052ff4b..0x0052ff54).
        CObject* const Obj = Go->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
        {
            *OutSubObjId = SUBOBJ_ID_ROOT;
            return;
        }
        // 0x0052fec5..0x0052fef5: CObject::part_to_inst inlined on the PART -- with no part map
        // (+0x5c == 0) its own engine instance (+0x04); else the first {PartId, Inst, ...}
        // entry (stride 0xc) of [+0x68, +0x6c) keyed by PartId. No entry, or an entry of -1,
        // falls back to +0x04.
        long Inst = Part->EngineInstanceId;
        if (Part->SurfaceManager != nullptr)
        {
            for (std::vector<FPartMapEntry>::const_iterator Entry = Part->PartMap.begin();
                 Entry != Part->PartMap.end(); ++Entry)
            {
                if (Entry->PartId == PartId)
                {
                    Inst = Entry->EngineInstanceId;
                    break;
                }
            }
            if (Inst == -1)
                Inst = Part->EngineInstanceId;
        }
        // 0x0052fef8..0x0052ff03: CEqObj::inst_to_subobj_id (vt+0xf4).
        CEqObj* const Eq = static_cast<CEqObj*>(Obj);
        unsigned short const SubObjId = Eq->inst_to_subobj_id(Inst);
        *OutSubObjId = SubObjId;
        // 0x0052ff06..0x0052ff12: already a shield -- done.
        if (SubObjectID::IsShieldEquipID(SubObjId))
            return;
        // 0x0052ff14..0x0052ff46: the first shield (type 0x100) of the equipment manager
        // (+0xe4), when it is functioning (vt+0xc), takes the hit: its id (+0x08).
        CEShield* const Shield = CEShield::cast(Eq->EquipManager.FindFirst(0x100u));
        if (Shield == nullptr)
            return;
        if (!Shield->IsFunctioning())
            return;
        *OutSubObjId = Shield->Id;
    }

    void const* SubObjArg(unsigned short SubObjId)
    {
        // Slot 101 takes the sub-object ids as dwords (the pushes at 0x0052fdb8 / 0x0052fde4).
        return reinterpret_cast<void const*>(static_cast<uintptr_t>(SubObjId));
    }
}

void ClientObjDb_ProcessCollisions()
{
    FClientTickScope BeamProfile(EClientTickSection::BeamHits);
    // FUN_0052fc20 (cdecl, plain RET; caller 0x0054b05d in the space frame).
    // 0x0052fc26..0x0052fcb2: bucket 3 (FUN_00540fc0 with a local 3). Each Gameobj's CObject is
    // narrowed to a CBeam (class & 0x67 == 0x67, else null) and its +0xe8 life read with no
    // null test. FCOMP 0.0 (DAT_005c7474) / FNSTSW / TEST AH,1 / JNZ skips when C0 is set --
    // a life below 0 or NaN; a life >= 0 falls through. Then the Gameobj's slot 86 (+0x158):
    // true skips. Otherwise the beam is counted and handed to PhySys::GenerateCollisions
    // (narrowed again, null passed through).
    int Count = 0;
    int Kind = 3;
    for (FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind); Go != nullptr; Go = Go->ListNext)
    {
        CObject* const Obj = Go->Object;
        CBeam* Beam = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x67u) == 0x67u)
            Beam = static_cast<CBeam*>(Obj);
        if (!(Beam->Lifetime >= 0.0f))
            continue;
        if (Go->Slot158())
            continue;
        ++Count;
        PhySys::GenerateCollisions(Beam);                                      // 0x0052fca6
    }

    BeamProfile.Stop();
    // 0x0052fcb6..0x0052fcc5: LogStream::LogEvent("Timing_cu.collide.num_bullets", (float)
    // Count, 4).
    LogStream::LogEvent("Timing_cu.collide.num_bullets", static_cast<float>(Count), 4);
    FClientTickScope HitProfile(EClientTickSection::HitDispatch);

    // 0x0052fcce..0x0052fcde: PhySys::GetCollisions(FUN_00554e90's system id, Begin, End).
    PhySys::CollisionEvent const* Begin = nullptr;
    PhySys::CollisionEvent const* End = nullptr;
    PhySys::GetCollisions(StarSysGetActiveSystemId(), Begin, End);

    // 0x0052fcef..0x0052fe46: the next element is taken (+0x34) before the dispatch, the end
    // compared after it.
    for (PhySys::CollisionEvent const* Event = Begin; Event != End; ++Event)
    {
        ++GClientCollisionEventCount;                                          // 0x0052fcf7

        FClientGameobj* GoA = nullptr;
        unsigned short SubA = 0;
        ResolveCollisionSide(Event->ObjectA, Event->PartIdA, &GoA, &SubA);     // 0x0052fd1e
        if (GoA == nullptr)
            continue;                                                          // 0x0052fd2c
        FClientGameobj* GoB = nullptr;
        unsigned short SubB = 0;
        ResolveCollisionSide(Event->ObjectB, Event->PartIdB, &GoB, &SubB);     // 0x0052fd44
        if (GoB == nullptr)
            continue;                                                          // 0x0052fd52

        // 0x0052fd58..0x0052fdf0: side A's record -- the shared point, both vectors negated
        // (FLD / FCHS / FSTP per component) -- then GoA->Slot194(&Rec, SubA, GoB, SubB).
        FClientCollisionRecord Rec;
        Rec.Point = Event->Point;
        Rec.Field_0x0c.x = -Event->SurfaceNormal.x;
        Rec.Field_0x0c.y = -Event->SurfaceNormal.y;
        Rec.Field_0x0c.z = -Event->SurfaceNormal.z;
        Rec.Field_0x18.x = -Event->RelativeVelocity.x;
        Rec.Field_0x18.y = -Event->RelativeVelocity.y;
        Rec.Field_0x18.z = -Event->RelativeVelocity.z;
        GoA->Slot194(&Rec, SubObjArg(SubA), GoB, SubObjArg(SubB));

        // 0x0052fdf6..0x0052fe38: the same record with both vectors copied as they are, then
        // GoB->Slot194(&Rec, SubB, GoA, SubA).
        Rec.Field_0x0c = Event->SurfaceNormal;
        Rec.Field_0x18 = Event->RelativeVelocity;
        GoB->Slot194(&Rec, SubObjArg(SubB), GoA, SubObjArg(SubA));
    }
}

namespace
{
    // DAT_00678adc -- this file's lazily acquired 3DMathEngine (FUN_005b73e0 on first use at
    // 0x005415dd); FUN_00541580 is its only user.
    I3DMathEngine* G_RemotePhysMathEngine = nullptr;
}

void ClientObjDb_UpdateRemotePhysics(float Dt)
{
    // FUN_00541580 (cdecl, plain RET; caller 0x0054b16d in the space frame).
    // 0x0054158c..0x00541594: single player -> nothing.
    if (SinglePlayer())
        return;

    // 0x0054159a..0x005416ab: every node of DAT_00678a30, head DAT_00678a34.
    std::list<FRemotePhysBlock*>::iterator It = G_RemotePhysBlocks.begin();
    for (; It != G_RemotePhysBlocks.end(); ++It)
    {
        FRemotePhysBlock* const Block = *It;
        // 0x005415b3..0x005415c1: FLD float Dt / FADD double [block] / FSTP double -- the sum
        // is formed in double.
        Block->ElapsedTime = static_cast<double>(Dt) + Block->ElapsedTime;
        CShip* const Ship = Block->Ship;
        // 0x005415c5..0x005415cf: CRemotePhysicsSimulation::Doit((double)Dt) on the block's
        // simulation (+0x10).
        Block->Simulation.Doit(static_cast<double>(Dt));

        // 0x005415d4..0x005415f3: the ship's orientation (+0x08) to a quaternion through
        // DAT_00678adc's MatrixToQuaternion (vt+0x50). No null test on the ship anywhere below.
        if (G_RemotePhysMathEngine == nullptr)
            G_RemotePhysMathEngine = Acquire3DMathEngine();
        Quaternion Orientation;
        G_RemotePhysMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orientation),
                                                   reinterpret_cast<FMatrix const&>(
                                                       Ship->Orientation));

        // 0x005415f6..0x00541609: CheckForSync(position, position, orientation) -- the ship's
        // +0x2c position passed twice. False -> next node.
        if (!Block->Simulation.CheckForSync(Ship->WorldPosition, Ship->WorldPosition,
                                            Orientation))
            continue;

        // 0x0054160f..0x0054167c: the SSPObjUpdateInfo on the stack at [ESP+0x28] -- the ship's
        // id (+0xb0), the quaternion, the position, the block's elapsed time, and the low byte
        // of the ship's +0x1a0 -- sent through DAT_0067ecd0 slot 52 (+0xd0)
        // SPObjUpdate(Info, DAT_00673344). +0x28 and the three bytes after +0x2c are never
        // written by retail (stack contents); the port stores 0.
        SSPObjUpdateInfo Info;
        Info.ObjectId = Ship->Id;
        Info.Orientation = Orientation;
        Info.Position = Ship->WorldPosition;
        Info.Timestamp = Block->ElapsedTime;
        Info.Field_0x28 = 0;
        Info.Field_0x2c = static_cast<unsigned char>(Ship->BehaviorIndex);
        Info.Pad_0x2d[0] = 0;
        Info.Pad_0x2d[1] = 0;
        Info.Pad_0x2d[2] = 0;
        static_cast<IServer*>(GetLocalServerInterface())->SPObjUpdate(Info,
                                                                       GetPlayerConnectionId());

        // 0x00541682..0x00541698: the simulation's vt+0x4 RemoteUpdate(position, orientation,
        // the Timestamp just sent -- re-read from the info as a double).
        Block->Simulation.RemoteUpdate(Ship->WorldPosition, Orientation, Info.Timestamp);
    }
}

namespace
{
    // The loop FUN_005416c0 repeats once per bucket: walk the bucket's Gameobj chain through
    // ListNext (+0x14) until get_id (vt+0x20) equals ObjectId.
    FClientGameobj* FindObjectInKind(int Kind, unsigned ObjectId)
    {
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
        for (; Go != nullptr; Go = Go->ListNext)
        {
            if (Go->get_id() == ObjectId)
                return Go;
        }
        return nullptr;
    }

    // The loop FUN_00541890 runs over buckets 1 and 2: cobject (vt+0x150), narrowed to a
    // CEqObj when (ObjectClass +0x4c & 0x103) == 0x103, else null, and CEqObj+0x104 compared
    // against VibeId. Retail compares through the null too (0x005418ef XOR EAX,EAX falls into
    // CMP [EAX+0x104]); buckets 1 and 2 only ever hold ships and solars, both CEqObj.
    FClientGameobj* FindEqObjInKind(int Kind, unsigned VibeId)
    {
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
        for (; Go != nullptr; Go = Go->ListNext)
        {
            CObject const* const Obj = Go->cobject();
            CEqObj const* EqObj = nullptr;
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u)
                EqObj = static_cast<CEqObj const*>(Obj);
            if (EqObj->VibeId == VibeId)
                return Go;
        }
        return nullptr;
    }
}

FClientGameobj* ClientObjDb_FindObject(unsigned ObjectId, int Kind)
{
    // FUN_005416c0 (cdecl, two args). 0x005416c8: an id with no bits under 0x3fffffff is no
    // object. Kind != 0 (0x005416dd -> 0x0054184d): walk that one bucket. Kind == 0: buckets
    // 1, 4, 5, 6, 7, 2 in that order (0x005416e8 / 00541723 / 00541763 / 005417a3 / 005417e3
    // / 0054181f), first hit wins.
    if ((ObjectId & 0x3fffffffu) == 0u)
        return nullptr;
    if (Kind != 0)
        return FindObjectInKind(Kind, ObjectId);

    int const kOrder[6] = {1, 4, 5, 6, 7, 2};
    for (int OrderIndex = 0; OrderIndex < 6; ++OrderIndex)
    {
        FClientGameobj* const Found = FindObjectInKind(kOrder[OrderIndex], ObjectId);
        if (Found != nullptr)
            return Found;
    }
    return nullptr;
}

FClientGameobj* ClientObjDb_FindEqObjByVibeId(unsigned VibeId, int Kind)
{
    // FUN_00541890 (cdecl, two args; callers 0x004c5140 / 0x004c5320). Same id gate as
    // FUN_005416c0. Kind != 0 (0x005418ad -> 0x0054195d) walks that bucket by get_id -- not
    // by the vibe field. Kind == 0 walks bucket 1 (0x005418b8) then bucket 2 (0x00541911)
    // comparing CEqObj+0x104.
    if ((VibeId & 0x3fffffffu) == 0u)
        return nullptr;
    if (Kind != 0)
        return FindObjectInKind(Kind, VibeId);

    FClientGameobj* const Ship = FindEqObjInKind(1, VibeId);
    if (Ship != nullptr)
        return Ship;
    return FindEqObjInKind(2, VibeId);
}

unsigned ClientObjDb_GetSolarCount()
{
    if (G_ClientObjDb.Lists[2].Count <= 0)
        return 0u;
    return static_cast<unsigned>(G_ClientObjDb.Lists[2].Count);
}

void ClientObjDb_ClearExcept(std::list<FClientGameobj*> const* Keep)
{
    // FUN_00541210 (cdecl; caller 0x00554bb5, the star-system unload).
    // 0x00541210..0x00541277 / 0x00541333..0x00541375: every remote-physics block whose Ship is
    // not the narrowed CShip of some keep-list value (a null value, or a non-ship, narrows to
    // null and so keeps a block whose Ship is null) is destroyed (FUN_00541200 + operator delete)
    // and its node erased. A null Keep erases every block.
    std::list<FRemotePhysBlock*>::iterator It = G_RemotePhysBlocks.begin();
    while (It != G_RemotePhysBlocks.end())
    {
        bool bKeep = false;
        if (Keep != nullptr)
        {
            std::list<FClientGameobj*>::const_iterator KeepIt = Keep->begin();
            for (; KeepIt != Keep->end(); ++KeepIt)
            {
                FClientGameobj* const KeepGo = *KeepIt;
                CShip* const KeepShip = KeepGo != nullptr ? NarrowShip(KeepGo) : nullptr;
                if (KeepShip == (*It)->Ship)
                {
                    bKeep = true;
                    break;
                }
            }
        }
        if (bKeep)
        {
            ++It;
        }
        else
        {
            delete *It;
            It = G_RemotePhysBlocks.erase(It);
        }
    }

    // 0x0054127a..0x0054130e: FUN_00542410(Keep, 1) on every list in this order -- kinds 1, 3,
    // 4, 5, 6, then (after the large-solar vector is emptied, DAT_00678ac8 = DAT_00678ac4) kinds
    // 2, 10, the eleventh list, 7, 8, 9.
    ClearListExcept(&G_ClientObjDb.Lists[1], Keep, true);  // DAT_00678aa8
    ClearListExcept(&G_ClientObjDb.Lists[3], Keep, true);  // DAT_00678a90
    ClearListExcept(&G_ClientObjDb.Lists[4], Keep, true);  // DAT_00678a9c
    ClearListExcept(&G_ClientObjDb.Lists[5], Keep, true);  // DAT_00678a84
    ClearListExcept(&G_ClientObjDb.Lists[6], Keep, true);  // DAT_00678a78
    G_LargeSolars.clear();                                  // 0x005412bb..0x005412c8
    ClearListExcept(&G_ClientObjDb.Lists[2], Keep, true);  // DAT_00678ab4
    ClearListExcept(&G_ClientObjDb.Lists[10], Keep, true); // DAT_00678a6c
    ClearListExcept(&G_ClientObjDb.Lists[0], Keep, true);  // DAT_00678a54
    ClearListExcept(&G_ClientObjDb.Lists[7], Keep, true);  // DAT_00678a60
    ClearListExcept(&G_ClientObjDb.Lists[8], Keep, true);  // DAT_00678a48
    ClearListExcept(&G_ClientObjDb.Lists[9], Keep, true);  // DAT_00678a3c

    // 0x00541313..0x00541328: the spatial partition freed and cleared (FUN_00540e90's body).
    ClientObjDb_FreeSpatial();
}

void ClientObjDb_FreeSpatial()
{
    // FUN_00540e90 (StarSysLoadIni entry, 00553763).
    if (G_ClientObjDb.Spatial != nullptr)
    {
        FreeSpatial(G_ClientObjDb.Spatial);
        G_ClientObjDb.Spatial = nullptr;
    }
}

void ClientObjDb_RebuildSpatial()
{
    // FUN_00540eb0 (StarSysLoadIni exit, 00554280).
    if (G_ClientObjDb.Spatial != nullptr)
        G_ClientObjDb.Spatial->Clear(); // +0x08

    // 00540ecc: DAT_00678ab4 is the solar list; its first dword is the count (JBE -> unsigned).
    unsigned const SolarCount = static_cast<unsigned>(G_ClientObjDb.Lists[2].Count);
    if (SolarCount == 0u)
        return;

    ++G_ClientSpatialRebuildCount;
    if (G_ClientObjDb.Spatial == nullptr)
    {
        G_ClientObjDb.Spatial = NewSpatial("BSP"); // PTR_LAB_005df8a4 = "BSP"
        if (G_ClientObjDb.Spatial == nullptr)
            return;
    }

    FSpatialEntry* const Entries =
        static_cast<FSpatialEntry*>(operator new(SolarCount * sizeof(FSpatialEntry)));
    unsigned Added = 0;
    FClientGameobj* Cursor = nullptr;
    // FUN_00542330 walk over the solar list; get_type (+0x88) must succeed and match 0xfdff.
    while (AdvanceListCursor(&G_ClientObjDb.Lists[2], &Cursor) != nullptr)
    {
        uint Type = 0;
        if (Cursor->get_type(Type) != 0)
            continue;
        if ((Type & 0xfdffu) == 0u)
            continue;
        Entries[Added].Object = static_cast<IObjRW*>(Cursor);
        Entries[Added].Position = Cursor->cobject()->WorldPosition; // cobject (+0x150) +0x2c
        Entries[Added].Radius = Cursor->cobject()->Radius;          // cobject (+0x150) +0x38
        ++Added;
    }
    G_ClientObjDb.Spatial->AddArray(Entries, Added); // +0x14
    operator delete(Entries); // FUN_005b7e1d
}

bool ClientObjDb_TryGetSolarWorldPosByIndex(unsigned Index, float OutPos[3])
{
    if (OutPos == nullptr)
        return false;

    unsigned Cursor = 0;
    for (FClientGameobj* Go = G_ClientObjDb.Lists[2].First; Go != nullptr; Go = Go->ListNext)
    {
        if (Go->Object == nullptr)
            continue;
        if (Cursor == Index)
        {
            Vector const& Pos = Go->get_position();
            OutPos[0] = Pos.x;
            OutPos[1] = Pos.y;
            OutPos[2] = Pos.z;
            return true;
        }
        ++Cursor;
    }
    return false;
}

// ===========================================================================
// OwnerList<pub::SpaceObj::CargoDesc> -- template defined in src/include/
// common_server.h. Retail FreeLancer.exe exports its own copy of all six members
// (instruction-identical to Common.dll's and Server.dll's): ??0 (ord 2,
// .text:004c9770), copy ??0 (ord 1, .text:004c9790), ??1 (ord 14, .text:004ced60),
// ??4 (ord 15, .text:0041c1a0), ??_7 (ord 25, .rdata:005d711c) and free (ord 58,
// .text:004cecf0). This explicit instantiation definition, with SHARED_INLINE_API
// = dllexport under FREELANCER_EXPORTS, is what emits and exports them here.
// ===========================================================================
// Member-level export (common_server.h) cannot emit ??_7, so the vftable (retail ord 25,
// .rdata:005d711c; export directory 0x0060d510 lists 69 names) is exported by name. The
// explicit instantiation of the constructors above references the vftable, so this TU always
// defines the symbol.
OWNERLIST_CARGODESC_EXPORTS
#pragma comment(linker, "/EXPORT:??_7?$OwnerList@UCargoDesc@SpaceObj@pub@@@@6B@,DATA")
