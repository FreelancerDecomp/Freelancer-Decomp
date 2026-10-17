// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype::DamageObjInfo(List), ShieldLink, CollisionGroup, EqObj, Solar, Ship -- split out of
// common_archetype.cpp (retail Archetype.cpp, one TU) in CMN-SPLIT phase B.

#include "common_archetype.h"
#include "common_archetype_internal.h"

#include "common_cobject.h" // CollisionGroupDesc (get_undamaged_collision_group_list)
#include "common_fuses.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_phyarch.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <cstddef>
#include <list>
#include <map>
#include <math.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace Archetype
{
    namespace
    {
        // FUN_062fbb90 (item 1955) + listing PTR_s_berth_063eca00 -- 8-entry name/value table
        // consulted by EqObj::read's "docking_sphere" case (value index 0). No match -> Type=0
        // (unlike LookupHpType's miss-defaults-to-4; docking_sphere has no such fallback in
        // retail -- FUN_062f9700-equivalent EqObj::read masks the found value with the bool
        // result of FUN_062fbb90, so a miss yields 0, not the last/uninitialized table slot).
        struct FDockTypeEntry
        {
            char const* Text;
            unsigned Value;
        };

        FDockTypeEntry const* DockTypeTable(unsigned* OutCount)
        {
            static FDockTypeEntry const kTable[] = {
                {"berth", 1},
                {"moor_small", 3},
                {"moor_medium", 4},
                {"moor_large", 5},
                {"ring", 6},
                {"pad", 6},
                {"jump", 7},
                {"airlock", 8},
            };
            if (OutCount != nullptr)
                *OutCount = sizeof(kTable) / sizeof(kTable[0]);
            return kTable;
        }

        // FUN_062fbb90 (thiscall on the table, RET 0xc: Name, &Out, Count): a null entry name is
        // skipped, otherwise DACOM.DLL stricmp(Name, entry) == 0 stores the value and returns
        // true; no match -> false with Out untouched. No guard on Name or Out.
        bool TryLookupDockType(char const* Name, unsigned* OutValue)
        {
            unsigned Count = 0;
            FDockTypeEntry const* Table = DockTypeTable(&Count);
            for (unsigned Index = 0; Index < Count; ++Index)
            {
                if (Table[Index].Text == nullptr)
                    continue;
                if (CallDacomStricmp(Name, Table[Index].Text) == 0)
                {
                    *OutValue = Table[Index].Value;
                    return true;
                }
            }
            return false;
        }
    }

    namespace
    {
        const char kArchetypeSource[] = "E:\\FL\\Scratch\\Source\\Common\\Archetype.cpp";

        struct FNamedType
        {
            char const* Name;
            unsigned Type;
        };

        // PTR_DAT_063ec968, 19 entries, stride 8, uVar15 < 0x98.
        const FNamedType kCollisionGroupTypes[] = {
            {"Fin", 2},
            {"Top_Fin", 4},
            {"Port_Fin", 8},
            {"Starboard_Fin", 0x10},
            {"Port_Wing", 0x20},
            {"Starboard_Wing", 0x40},
            {"Middle_Wing", 0x80},
            {"Engine", 0x100},
            {"Port_Engine", 0x200},
            {"Middle_Engine", 0x400},
            {"Starboard_Engine", 0x800},
            {"Top_Engine", 0x1000},
            {"Bottom_Engine", 0x2000},
            {"Spoiler", 0x4000},
            {"Tail", 0x8000},
            {"Port_Side_Panel", 0x10000},
            {"Starboard_Side_Panel", 0x20000},
            {"Port_Arm", 0x40000},
            {"Starboard_Arm", 0x80000}
        };

        // PTR_s_scanner_063ec8f8, 14 entries, uVar15 < 0x70. Values are EEquipType bits.
        const FNamedType kLinkedEquipTypes[] = {
            {"scanner", 0x80000},
            {"engine", 0x20000},
            {"weapon", 0x80},
            {"shield", 0x100},
            {"thruster", 0x400},
            {"power", 0x40000},
            {"light", 1},
            {"tractor", 0x100000},
            {"attached_fx", 2},
            {"internal_fx", 0x400000},
            {"tradelane", 0x800000},
            {"repairdroid", 0x200000},
            {"cloaking_device", 0x1000},
            {"armor", 0x1000000}
        };

        // FUN_062f9b10's two table walks (062f9bc0..062f9bfa "type", 062f9e62..062f9ece
        // "linked_equip"): a null table name is skipped, otherwise DACOM stricmp(Name, entry) == 0
        // returns the entry's value; no match -> the default.
        unsigned LookupNamedType(char const* Name, FNamedType const* Table, unsigned Count,
                                 unsigned DefaultValue)
        {
            unsigned Index = 0;
            for (; Index < Count; ++Index)
            {
                if (Table[Index].Name != nullptr && CallDacomStricmp(Name, Table[Index].Name) == 0)
                    return Table[Index].Type;
            }
            return DefaultValue;
        }

        // The per-entry test of DamageObjInfoList::IsValid (0626f8d0..0626f8db) and of
        // FUN_062f9b10's two post-loop walks (062f9eb2..062f9ec1, 062f9f20..062f9f2f): the
        // hardpoint string's first byte is 0 (CMP byte [Value],0, no null test), or the id's
        // low 30 bits are 0.
        bool DamageObjUnmatched(DamageObjInfo const& Info)
        {
            if (Info.Hardpoint.Value[0] == '\0')
                return true;
            if ((Info.Arch.Id & 0x3fffffffu) == 0)
                return true;
            return false;
        }

        // FUN_062f9700's two shapes. *_hp (062f971c..062f986f): a non-empty list whose last
        // entry's hardpoint string is "" (CMP byte [Value],0 -- no null test) gets the new value
        // there; otherwise a new entry {hp, cleared id} is appended. *_obj (062f9752..062f97cc,
        // 062f9887..062f9924): the last entry's ID_String::IsEmpty (Id == 0, 06333e50) decides.
        // Retail reads the value after the test; reading it first changes nothing (one call
        // either way).
        void FillOrPushHp(std::vector<DamageObjInfo>& List, char const* Hp)
        {
            if (!List.empty() && List.back().Hardpoint.Value[0] == '\0')
            {
                List.back().Hardpoint.Value = Hp;
                return;
            }
            DamageObjInfo Info;
            Info.Hardpoint.Value = Hp;
            Info.Arch.clear();
            List.push_back(Info);
        }

        void FillOrPushObj(std::vector<DamageObjInfo>& List, unsigned ArchId)
        {
            if (!List.empty() && List.back().Arch.IsEmpty())
            {
                List.back().Arch.Id = ArchId;
                return;
            }
            DamageObjInfo Info;
            Info.Hardpoint.clear();
            Info.Arch.Id = ArchId;
            List.push_back(Info);
        }

        // FUN_062f9700 (cdecl: reader, dmg list, group-dmg list). Every value goes through the
        // no-argument get_value_string (06310b70, the whole value). Returns true for the four
        // keys, false otherwise. The retail list keeps its own size at +0x08; the port mirrors it
        // in DmgCount / GroupDmgCount.
        bool ReadDamagePair(INI_Reader& Reader, CollisionGroup* Group)
        {
            if (Reader.is_value("dmg_hp"))
            {
                FillOrPushHp(*Group->DmgObjs.Items, StringAlloc(Reader.get_value_string(), false));
                Group->DmgObjs.Count = (unsigned)Group->DmgObjs.Items->size();
                return true;
            }
            if (Reader.is_value("dmg_obj"))
            {
                FillOrPushObj(*Group->DmgObjs.Items, CreateID(Reader.get_value_string()));
                Group->DmgObjs.Count = (unsigned)Group->DmgObjs.Items->size();
                return true;
            }
            if (Reader.is_value("group_dmg_hp"))
            {
                FillOrPushHp(*Group->GroupDmgObjs.Items, StringAlloc(Reader.get_value_string(), false));
                Group->GroupDmgObjs.Count = (unsigned)Group->GroupDmgObjs.Items->size();
                return true;
            }
            if (Reader.is_value("group_dmg_obj"))
            {
                FillOrPushObj(*Group->GroupDmgObjs.Items, CreateID(Reader.get_value_string()));
                Group->GroupDmgObjs.Count = (unsigned)Group->GroupDmgObjs.Items->size();
                return true;
            }
            return false;
        }

        void WarnUnmatchedDamage(CollisionGroup* Group, std::vector<DamageObjInfo>* List,
                                 char const* Fmt, int Line)
        {
            unsigned Index = 0;
            for (; Index < List->size(); ++Index)
            {
                if (!DamageObjUnmatched((*List)[Index]))
                    continue;
                FDUMP(0x100002, Fmt, kArchetypeSource, Line, Group->ObjName);
                List->clear();
                return;
            }
        }
    }

    // Common.dll.txt 0xf8a0 (??0DamageObjInfo@Archetype@@QAE@XZ): CacheString::clear on the
    // hardpoint at +0 (stores the StringCache pool's shared empty string), ID_String::clear on +4.
    DamageObjInfo::DamageObjInfo()
    {
        Hardpoint.clear();
        Arch.clear();
    }

    // Common.dll.txt 0x20ee0 (??0DamageObjInfo@Archetype@@QAE@ABU01@@Z, the same bytes also
    // serve ??4): copies the dword at +0 and the dword at +4.
    DamageObjInfo::DamageObjInfo(DamageObjInfo const& Other)
        : Hardpoint(Other.Hardpoint)
        , Arch(Other.Arch)
    {
    }

    // Common.dll.txt 0xc6c0 (??0ShieldLink@Archetype@@QAE@XZ): ShieldArch.clear(), EquipId =
    // SUBOBJ_ID_NONE, CacheString::clear on the hardpoint, empty vector. Pad06 is not written
    // by retail.
    ShieldLink::ShieldLink()
    {
        ShieldArch.clear();
        EquipId = SUBOBJ_ID_NONE;
        Hardpoint.clear();
    }

    // Common.dll.c 23240-23245 (0xf840): unsigned compare, Hardpoint not considered.
    bool DamageObjInfo::operator<(DamageObjInfo const& Other) const
    {
        return Arch.Id < Other.Arch.Id;
    }

    // Common.dll.c 23228-23233 (0xf820): Arch id equality, Hardpoint not considered.
    bool DamageObjInfo::operator==(DamageObjInfo const& Other) const
    {
        return Arch.Id == Other.Arch.Id;
    }

    // Common.dll.c 23252-23257 (0xf860): Arch id inequality, Hardpoint not considered.
    bool DamageObjInfo::operator!=(DamageObjInfo const& Other) const
    {
        return Arch.Id != Other.Arch.Id;
    }

    // Common.dll.c 23264-23273 (0xf880): unsigned greater-than on Arch id, Hardpoint not considered.
    bool DamageObjInfo::operator>(DamageObjInfo const& Other) const
    {
        return Other.Arch.Id < Arch.Id;
    }

    // Common.dll.c 23292-23307 (0xf8c0): every entry must have a non-empty Hardpoint string
    // and a nonzero Arch id (masked to its low 30 bits, matching retail's raw `& 0x3fffffff`
    // node-flags check); an empty list is trivially valid. Reuses DamageObjUnmatched (above,
    // this file's anonymous namespace) which already encodes the exact same per-entry test
    // for the "prune stale entries" callers.
    // Common.dll.txt 0xf8f0 (??0DamageObjInfoList@Archetype@@QAE@XZ): stores the allocator
    // byte (uninitialised stack byte in retail), allocates the empty head node at +0x04 and
    // zeroes the size at +0x08.
    DamageObjInfoList::DamageObjInfoList()
        : Items(new std::vector<DamageObjInfo>())
        , Count(0)
    {
    }

    // Common.dll.txt 0xf920 (??0DamageObjInfoList@Archetype@@QAE@ABV01@@Z, the same bytes as
    // list<CollisionGroupDesc>'s copy ctor): copies the allocator byte, allocates a fresh head,
    // then appends a copy of each of Other's elements in order, incrementing the size per node.
    DamageObjInfoList::DamageObjInfoList(DamageObjInfoList const& Other)
        : Items(new std::vector<DamageObjInfo>(*Other.Items))
        , Count((unsigned)Other.Items->size())
    {
        Pad0[0] = Other.Pad0[0];
    }

    // Common.dll.txt 0x468e0 (??1DamageObjInfoList@Archetype@@QAE@XZ): unlinks and frees every
    // node, frees the head, zeroes head (+0x04) and size (+0x08).
    DamageObjInfoList::~DamageObjInfoList()
    {
        delete Items;
        Items = nullptr;
        Count = 0;
    }

    // Common.dll.txt 0xf9b0 (??4DamageObjInfoList@Archetype@@QAEAAV01@ABV01@@Z): returns at once
    // on self-assignment; otherwise overwrites this list's nodes with Other's elements in order,
    // erases this list's surplus nodes (size decremented per node) or appends Other's remaining
    // elements (size incremented per node). The vector assignment is the same element-wise
    // result over the port's representation.
    DamageObjInfoList& DamageObjInfoList::operator=(DamageObjInfoList const& Other)
    {
        if (this != &Other)
        {
            *Items = *Other.Items;
            Count = (unsigned)Items->size();
        }
        return *this;
    }

    bool DamageObjInfoList::IsValid()
    {
        for (DamageObjInfo const& Info : *Items)
        {
            if (DamageObjUnmatched(Info))
                return false;
        }
        return true;
    }

    // FuseIgnitionList's four members are the same list code inlined in CollisionGroup's ctor,
    // copy ctor and operator= (see the class comment).
    FuseIgnitionList::FuseIgnitionList()
        : Items(new std::vector<FGroupFuse>())
        , Count(0)
    {
    }

    FuseIgnitionList::FuseIgnitionList(FuseIgnitionList const& Other)
        : Items(new std::vector<FGroupFuse>(*Other.Items))
        , Count((unsigned)Other.Items->size())
    {
        Pad0[0] = Other.Pad0[0];
    }

    FuseIgnitionList::~FuseIgnitionList()
    {
        delete Items;
        Items = nullptr;
        Count = 0;
    }

    FuseIgnitionList& FuseIgnitionList::operator=(FuseIgnitionList const& Other)
    {
        if (this != &Other)
        {
            *Items = *Other.Items;
            Count = (unsigned)Items->size();
        }
        return *this;
    }

    CollisionGroup::CollisionGroup()
        : Next(nullptr)
        , Id(0)
        , ObjName("")
        , Type(1)
        , HitPts(0)
        , bSeparable(false)
        , bDestroyParent(false)
        , bRootHealthProxy(false)
        , ParentImpulse(0.0f)
        , ChildImpulse(0.0f)
        , Mass(1.0f)
        , bHasRotationInertia(false)
        , RotationInertiaX(0.0f)
        , RotationInertiaY(0.0f)
        , RotationInertiaZ(0.0f)
        , ExplosionResistance(1.0f)
        , LinkedEquipType(0)
        , LinkedEquipHealth(0.0f)
    {
        // 062f9950 / 062f9961: Id = SUBOBJ_ID_NONE, and +0x08 goes through CacheString::clear,
        // i.e. the StringCache pool's shared empty string, not a literal. The three lists are
        // built empty by their own ctors.
        Id = SUBOBJ_ID_NONE;
        CacheString EmptyName;
        EmptyName.clear();
        ObjName = EmptyName.Value;
        SeparationExplosion.clear();
        DebrisType.clear();
    }

    CollisionGroup::CollisionGroup(CollisionGroup const& Other)
        // Common.dll.c 23433-23492 (0xfa70): allocates three fresh empty VC6 std::list<>
        // sentinel nodes (Next+0x44/+0x50/+0x5c) then copies Other's list contents into
        // them -- same net effect as constructing empty vectors and assigning, which is
        // what the port's owned-vector members already give us via their own copy.
        : Next(Other.Next)
        , Id(Other.Id)
        , ObjName(Other.ObjName)
        , Type(Other.Type)
        , HitPts(Other.HitPts)
        , bSeparable(Other.bSeparable)
        , bDestroyParent(Other.bDestroyParent)
        , bRootHealthProxy(Other.bRootHealthProxy)
        , ParentImpulse(Other.ParentImpulse)
        , ChildImpulse(Other.ChildImpulse)
        , Mass(Other.Mass)
        , bHasRotationInertia(Other.bHasRotationInertia)
        , RotationInertiaX(Other.RotationInertiaX)
        , RotationInertiaY(Other.RotationInertiaY)
        , RotationInertiaZ(Other.RotationInertiaZ)
        , SeparationExplosion(Other.SeparationExplosion)
        , DebrisType(Other.DebrisType)
        , ExplosionResistance(Other.ExplosionResistance)
        // 0626fb00..0626fb94: each list copies Other's allocator byte, gets a fresh head, and
        // the insert of Other's [first, head) counts its size up from 0 -- the size is the
        // number of elements copied, not Other's size field. The lists are built in order and
        // retail's unwind funclet (LAB_06392756) destroys the ones already built if a later
        // allocation throws, as the member destructors do here.
        , DmgObjs(Other.DmgObjs)
        , GroupDmgObjs(Other.GroupDmgObjs)
        , Fuses(Other.Fuses)
        , LinkedEquipType(Other.LinkedEquipType)
        , LinkedEquipHealth(Other.LinkedEquipHealth)
    {
    }

    CollisionGroup::~CollisionGroup()
    {
        // The three lists free themselves (DamageObjInfoList::~ 0x468e0 for the first two).
    }

    CollisionGroup& CollisionGroup::operator=(CollisionGroup const& Other)
    {
        // Common.dll.c 23500-23579 (0xfbc0): retail reuses existing VC6 std::list<> nodes
        // in place (walking both lists together, overwriting node payloads, then
        // erase()/insert()-ing the size difference) for the three list members
        // (DmgObjs@+0x40, GroupDmgObjs@+0x4c, Fuses@+0x58) -- net observable effect is
        // simply "this list's contents become a copy of Other's list's contents", which
        // std::vector's own operator= already gives us. Also note retail copies the
        // intrusive Next(+0x00) pointer too -- reproduced faithfully even though copying
        // list-linkage during an element assignment looks unusual; CollisionGroup::operator=
        // has no traced call site in this codebase (folded, XREF-orphaned like most of the
        // "inherit=" cluster) so this fidelity choice is unverified but not disprovable.
        // No top-level self test: the scalars are copied unconditionally and each list has
        // its own address test (0626fc43 / 0626fc9c / 0626fd0e). The overwrite / erase /
        // insert leaves each list's size field at Other's element count.
        Next = Other.Next;
        Id = Other.Id;
        ObjName = Other.ObjName;
        Type = Other.Type;
        HitPts = Other.HitPts;
        bSeparable = Other.bSeparable;
        bDestroyParent = Other.bDestroyParent;
        bRootHealthProxy = Other.bRootHealthProxy;
        ParentImpulse = Other.ParentImpulse;
        ChildImpulse = Other.ChildImpulse;
        Mass = Other.Mass;
        bHasRotationInertia = Other.bHasRotationInertia;
        RotationInertiaX = Other.RotationInertiaX;
        RotationInertiaY = Other.RotationInertiaY;
        RotationInertiaZ = Other.RotationInertiaZ;
        SeparationExplosion = Other.SeparationExplosion;
        DebrisType = Other.DebrisType;
        ExplosionResistance = Other.ExplosionResistance;
        DmgObjs = Other.DmgObjs;            // each list operator= has its own address test
        GroupDmgObjs = Other.GroupDmgObjs;
        Fuses = Other.Fuses;
        LinkedEquipType = Other.LinkedEquipType;
        LinkedEquipHealth = Other.LinkedEquipHealth;
        return *this;
    }

    // FUN_062f9b10 (062f9b10..062f9fb9), cdecl(reader, owner, id). Both callers (LoadShips
    // 062f30c4, LoadSolar 062f3b07) pass a non-null owner. "obj", "type", "separation_explosion"
    // and "debris_type" read the no-argument get_value_string (06310b70, the whole value);
    // "fuse" and "linked_equip" the indexed one.
    void ReadCollisionGroup(INI_Reader& Reader, EqObj* Owner, unsigned short GroupId)
    {
        CollisionGroup* Group = new CollisionGroup();
        Group->Id = GroupId;

        while (Reader.read_value())
        {
            if (Reader.is_value("obj"))
            {
                Group->ObjName = StringAlloc(Reader.get_value_string(), false);
            }
            else if (Reader.is_value("type"))
            {
                Group->Type = LookupNamedType(Reader.get_value_string(), kCollisionGroupTypes,
                                              sizeof(kCollisionGroupTypes) / sizeof(kCollisionGroupTypes[0]),
                                              1);
            }
            else if (Reader.is_value("hit_pts"))
            {
                Group->HitPts = Reader.get_value_int(0);
            }
            else if (Reader.is_value("separable"))
            {
                Group->bSeparable = true;
            }
            else if (Reader.is_value("destroy_parent"))
            {
                if (Reader.is_value_empty(0))
                    Group->bDestroyParent = true;
                else
                    Group->bDestroyParent = Reader.get_value_bool(0);
            }
            else if (Reader.is_value("root_health_proxy"))
            {
                Group->bRootHealthProxy = Reader.get_value_bool(0);
            }
            else if (Reader.is_value("parent_impulse"))
            {
                Group->ParentImpulse = Reader.get_value_float(0);
            }
            else if (Reader.is_value("child_impulse"))
            {
                Group->ChildImpulse = Reader.get_value_float(0);
            }
            else if (Reader.is_value("mass"))
            {
                Group->Mass = Reader.get_value_float(0);
            }
            else if (Reader.is_value("rotation_inertia"))
            {
                Group->RotationInertiaX = Reader.get_value_float(0);
                Group->RotationInertiaY = Reader.get_value_float(1);
                Group->RotationInertiaZ = Reader.get_value_float(2);
                Group->bHasRotationInertia = true;
            }
            else if (Reader.is_value("separation_explosion"))
            {
                Group->SeparationExplosion.Id = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("debris_type"))
            {
                Group->DebrisType.Id = CreateID(Reader.get_value_string());
            }
            else if (ReadDamagePair(Reader, Group))
            {
            }
            else if (Reader.is_value("explosion_resistance"))
            {
                Group->ExplosionResistance = Reader.get_value_float(0);
            }
            else if (Reader.is_value("fuse"))
            {
                FGroupFuse Fuse;
                Fuse.Fuse.Id = CreateID(Reader.get_value_string(0));
                Fuse.Value1 = Reader.get_value_float(1);
                Fuse.Value2 = Reader.get_value_float(2);
                Group->Fuses.Items->push_back(Fuse);
                Group->Fuses.Count = (unsigned)Group->Fuses.Items->size();
            }
            else if (Reader.is_value("linked_equip"))
            {
                Group->LinkedEquipType =
                    LookupNamedType(Reader.get_value_string(0), kLinkedEquipTypes,
                                    sizeof(kLinkedEquipTypes) / sizeof(kLinkedEquipTypes[0]), 0);
                Group->LinkedEquipHealth = Reader.get_value_float(1);
            }
        }

        WarnUnmatchedDamage(Group, Group->DmgObjs.Items,
                            "%s(%d) : *** WARNING: Unmatched dmg_obj or dmg_hp values for Group[%s].  No damage objects will be attached.",
                            0x11de);
        Group->DmgObjs.Count = (unsigned)Group->DmgObjs.Items->size();
        WarnUnmatchedDamage(Group, Group->GroupDmgObjs.Items,
                            "%s(%d) : *** WARNING: Unmatched group_dmg_obj or group_dmg_hp values for Group[%s].  No damage objects will be attached.",
                            0x11e3);
        Group->GroupDmgObjs.Count = (unsigned)Group->GroupDmgObjs.Items->size();

        if (Owner->CollisionGroups == nullptr)
        {
            Owner->CollisionGroups = Group;
            return;
        }
        CollisionGroup* Tail = Owner->CollisionGroups;
        while (Tail->Next != nullptr)
            Tail = Tail->Next;
        Tail->Next = Group;
    }

    EqObj::EqObj(ICliObj* InCompanion)
        : Root(InCompanion)
        , CollisionGroups(nullptr)
        , ShieldIdNext(0xfff0)
        , ShieldLinks(nullptr)
        , Fuses(nullptr)
        , bNomad(false)
        , bDockingCamera(true)
    {
        memset(Pad62, 0, sizeof(Pad62));
        memset(Pad6c, 0, sizeof(Pad6c));
        memset(Pad78, 0, sizeof(Pad78));
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
        memset(DockingSpheresPad, 0, sizeof(DockingSpheresPad));
#endif

        ShieldLinks = new std::vector<ShieldLink>();
        Fuses = new std::vector<FGroupFuse>();
    }

    // Common.dll.txt 0x91460-0x91665 (??0EqObj@Archetype@@QAE@ABU01@@Z). Retail runs
    // Root(0), the ShieldIdMaker ctor at +0x60, builds the empty shield-link vector (+0x64..
    // +0x70), the empty fuse list (fresh 0x14-byte sentinel at +0x78) and the empty docking-
    // sphere vector (+0x84), stores the EqObj vptr, then performs EqObj::operator= inline:
    // Root::operator=, the docking-sphere vector assignment and the CollisionGroup chain clone
    // (+0x5c is only ever written by that clone). Retail UB: bNomad (+0x80) and bDockingCamera
    // (+0x81) are never written, so they keep whatever the allocation held. The port keeps the
    // copy deterministic and gives them the default ctor's values (false / true).
    EqObj::EqObj(EqObj const& Other)
        : Root(nullptr)
        , CollisionGroups(nullptr)
        , ShieldIdNext(0xfff0)
        , ShieldLinks(new std::vector<ShieldLink>())
        , Fuses(new std::vector<FGroupFuse>())
        , bNomad(false)
        , bDockingCamera(true)
    {
        memset(Pad62, 0, sizeof(Pad62));
        memset(Pad6c, 0, sizeof(Pad6c));
        memset(Pad78, 0, sizeof(Pad78));
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
        memset(DockingSpheresPad, 0, sizeof(DockingSpheresPad));
#endif
        operator=(Other);
    }

    EqObj::~EqObj()
    {
        CollisionGroup* Group = CollisionGroups;
        while (Group != nullptr)
        {
            CollisionGroup* Next = Group->Next;
            delete Group;
            Group = Next;
        }
        CollisionGroups = nullptr;
        delete ShieldLinks;
        ShieldLinks = nullptr;
        delete Fuses;
        Fuses = nullptr;
    }

    // Common.dll.txt:261602-261819. CORRECTED (D51, 2026-09-09): real retail logic backing
    // Ship/Solar "inherit=" (Archetype::LoadShips 0x062f2d43 / Archetype::LoadSolar
    // 0x062f383d) -- see tracker item 71899's notes for the full XREF trail that overturned
    // the prior "confirmed dead" verdict.
    //
    // Traced field-by-field against the raw disasm:
    //   - calls Root::operator= first for the base-class part (Common.dll.txt:261628).
    //   - DockingSpheres (+0x84): retail inlines a std::vector<DockingSphere>::operator=
    //     (FUN_06316ac0/FUN_062fba30/FUN_062fd2b0 growth/copy helpers) -- fully subsumed by
    //     this port's own std::vector<DockingSphere>'s assignment operator (same precedent as
    //     tracker item 1980's finding for the sibling FUN_062fd2b0 caller).
    //   - CollisionGroups (+0x5c): retail heap-allocates and copy-constructs a fresh node per
    //     donor node (operator_new(0x6c) + CollisionGroup::CollisionGroup(*donor_node)) and
    //     relinks them into a new chain (Common.dll.txt:261777-261819) -- ported the same way
    //     using CollisionGroup's existing copy ctor. Like retail, this does NOT free/merge into
    //     a pre-existing destination chain first (both known callers -- LoadShips/LoadSolar --
    //     assign into a freshly-constructed, still-empty EqObj, so this never leaks in practice;
    //     documented here rather than silently relied upon).
    //   - ShieldIdNext (+0x60) / ShieldLinks (+0x68) / Fuses (+0x74) / bNomad (+0x80) /
    //     bDockingCamera (+0x81): confirmed by direct disasm read that retail's real function
    //     body never references these offsets at all -- this is retail's own actual (partial)
    //     copy behavior, not a missed field. Left untouched here to match exactly.
    //
    // Returns EqObj const&, matching retail's mangled return type.
    EqObj const& EqObj::operator=(EqObj const& Other)
    {
        Root::operator=(Other);

        DockingSpheres = Other.DockingSpheres;

        // Clone the donor's singly-linked CollisionGroup chain into a fresh chain of our own.
        // Snapshot the donor's head into a local before writing anything to *this so a
        // (never-exercised-in-practice, but still handled) self-assignment doesn't destroy the
        // source list before it's been walked.
        CollisionGroup* SrcHead = Other.CollisionGroups;
        CollisionGroup* NewHead = nullptr;
        CollisionGroup** Tail = &NewHead;
        for (CollisionGroup const* Cur = SrcHead; Cur != nullptr; Cur = Cur->Next)
        {
            CollisionGroup* NewNode = new CollisionGroup(*Cur);
            NewNode->Next = nullptr;
            *Tail = NewNode;
            Tail = &NewNode->Next;
        }
        CollisionGroups = NewHead;

        return *this;
    }

    // Common.dll.txt 0x062f1c60: `JMP Archetype::Root::redefine` -- EqObj adds nothing.
    void EqObj::redefine(Root const& Other)
    {
        Root::redefine(Other);
    }

    bool EqObj::read(INI_Reader& Reader)
    {
        if (Root::read(Reader))
            return true;

        if (Reader.is_value("docking_camera"))
        {
            bDockingCamera = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("docking_sphere"))
        {
            // .text:062f1898..062f1938 (table lookup FUN_062fbb90 is item 1955, push helper
            // FUN_062fc710). value(0) -> Type via the retail 8-entry berth/moor/ring/pad/
            // jump/airlock table (no match -> 0), value(1) -> HardpointName (record +0x04,
            // StringAlloc'd, then _strlwr'd in place), value(2) -> Radius, optional value(3)
            // -> AnimName (record +0x08, StringAlloc'd).
            DockingSphere Sphere;

            char const* TypeName = Reader.get_value_string(0);
            unsigned FoundType = 0;
            Sphere.Type = TryLookupDockType(TypeName, &FoundType) ? FoundType : 0;

            char* HardpointAlloc = StringAlloc(Reader.get_value_string(1), false);
            _strlwr(HardpointAlloc);
            Sphere.HardpointName = HardpointAlloc;

            Sphere.Radius = Reader.get_value_float(2);

            if (!Reader.is_value_empty(3))
                Sphere.AnimName = StringAlloc(Reader.get_value_string(3), false);

            DockingSpheres.push_back(Sphere);
            return true;
        }

        if (Reader.is_value("fuse"))
        {
            // Common.dll.c 126077-126091 (EqObj::read's own body). Same element shape and
            // same per-value parsing as CollisionGroup::Fuses (ReadCollisionGroup's own
            // "fuse" case above): value(0) -> CreateID'd Fuse.Fuse.Id, value(1)/value(2) ->
            // Value1/Value2. Stored in the heap Fuses vector at retail's own +0x74 slot
            // (see EqObj's declaration, common_archetype.h).
            FGroupFuse Fuse;
            Fuse.Fuse.Id = CreateID(Reader.get_value_string(0));
            Fuse.Value1 = Reader.get_value_float(1);
            Fuse.Value2 = Reader.get_value_float(2);
            Fuses->push_back(Fuse);
            return true;
        }

        if (Reader.is_value("shield_link"))
        {
            ShieldLink Link;
            Link.ShieldArch.clear();
            Link.EquipId = 0;
            Link.Pad06 = 0;
            Link.Hardpoint.clear();
            char const* ArchName = Reader.get_value_string(0);
            Link.ShieldArch.Id = CreateID(ArchName);
            ++ShieldIdNext;
            Link.EquipId = ShieldIdNext;
            // .text:062f1a66 / 062f1a9b: StringAlloc(value, false) into the CacheStrings.
            Link.Hardpoint.Value = StringAlloc(Reader.get_value_string(1), false);
            unsigned ValueIndex = 2;
            while (!Reader.is_value_empty(ValueIndex))
            {
                CacheString HpName;
                HpName.Value = StringAlloc(Reader.get_value_string(ValueIndex), false);
                Link.GeneratorHps.push_back(HpName);
                ValueIndex += 1;
            }
            ShieldLinks->push_back(Link);
            return true;
        }

        if (Reader.is_value("nomad"))
        {
            bNomad = Reader.get_value_bool(0);
            return true;
        }

        return false;
    }

    AClassType EqObj::get_class_type() const
    {
        return AClassEqObj;
    }

    CollisionGroup const* EqObj::get_group_by_id(unsigned short GroupId) const
    {
        CollisionGroup* Group = CollisionGroups;
        while (Group != nullptr)
        {
            if (Group->Id == GroupId)
                return Group;
            Group = Group->Next;
        }
        return nullptr;
    }

    CollisionGroup const* EqObj::get_group_by_name(CacheString const& Name) const
    {
        CollisionGroup* Group = CollisionGroups;
        while (Group != nullptr)
        {
            // .text:062f1ee6: interned StringCache pointers are compared, not their text.
            if (Group->ObjName == Name.Value)
                return Group;
            Group = Group->Next;
        }
        return nullptr;
    }

    bool EqObj::traverse_groups(CollisionGroup const*& Cursor) const
    {
        if (Cursor == nullptr)
        {
            Cursor = CollisionGroups;
            return Cursor != nullptr;
        }
        Cursor = Cursor->Next;
        return Cursor != nullptr;
    }

    // Common.dll.c 0x91f00. See header comment: archetype-level list, every group
    // reported at full (1.0f) health since the archetype carries no per-instance damage.
    bool EqObj::get_undamaged_collision_group_list(std::list<CollisionGroupDesc>& OutDescs) const
    {
        OutDescs.clear();
        for (CollisionGroup const* Group = CollisionGroups; Group != nullptr; Group = Group->Next)
        {
            CollisionGroupDesc Desc;
            Desc.Id = Group->Id;
            Desc.Pad02 = 0;
            Desc.Health = 1.0f;
            OutDescs.push_back(Desc);
        }
        return true;
    }

    bool EqObj::dock_terminal_valid(int Index) const
    {
        // Common.dll.c 0xc950: retail treats DockingSpheres as a raw MSVC vector
        // (begin +0x84, end +0x88, cap +0x8c) and checks
        // Index != -1 && end != null && (unsigned)Index < (end-begin)/sizeof(element).
        // Our std::vector<DockingSphere> gives the same containment check via size().
        // EqObj::read() now populates DockingSpheres from "docking_sphere" INI values
        // (D43, 2026-09-09), so this returns real containment, not an always-false stub.
        if (Index == -1 || DockingSpheres.empty())
            return false;
        return (size_t)Index < DockingSpheres.size();
    }

    Solar::Solar(ICliObj* InCompanion)
        : EqObj(InCompanion)
        , bDestructible(false)
        , SolarRadius(500.0f)
        , Toughness(0)
    {
        OpenAnim.clear();
        JumpOutHp.clear();
        Loadout.clear();
    }

    Solar::~Solar()
    {
        // Retail 0626d230 is `JMP EqObj::~EqObj`: Solar's own members are all scalars.
    }

    bool Solar::read(INI_Reader& Reader)
    {
        // Retail 062f3470..062f357d. No "inherit" case: LoadSolar handles that key before calling
        // read() (062f3705). The three string keys use the no-argument get_value_string
        // (06310b70, the whole value), not the indexed one.
        if (EqObj::read(Reader))
            return true;

        if (Reader.is_value("toughness"))
        {
            Toughness = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("destructible"))
        {
            bDestructible = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("open_anim"))
        {
            OpenAnim.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("loadout"))
        {
            Loadout.Id = CreateID(Reader.get_value_string());
            return true;
        }

        if (Reader.is_value("jump_out_hp"))
        {
            JumpOutHp.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("solar_radius"))
        {
            SolarRadius = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    void Solar::redefine(Root const& Other)
    {
        Root::redefine(Other);
        Solar const* Donor = (Solar const*)&Other;
        if (Donor->Toughness != Toughness)
            Toughness = Donor->Toughness;
    }

    AClassType Solar::get_class_type() const
    {
        // Common.dll.c 0x71740 (Archetype::Solar::get_class_type): return 2.
        return AClassSolar;
    }

    // .rdata:063a08ac (exported ?MAX_EXHAUST_NOZZLES@Ship@Archetype@@2HB): int 9.
    int const Ship::MAX_EXHAUST_NOZZLES = 9;

    Ship::Ship(ICliObj* InCompanion)
        : EqObj(InCompanion)
        , CommReferrable()
        , IdsInfo1(0)
        , IdsInfo2(0)
        , IdsInfo3(0)
        , ShipClass(0)
        , NumExhaustNozzles(9)
        , HoldSize(0.0f)
        , LinearDrag(100.0f)
        , AngularDragX(0.75f)
        , AngularDragY(0.75f)
        , AngularDragZ(0.75f)
        , SteeringTorqueX(2000.0f)
        , SteeringTorqueY(2000.0f)
        , SteeringTorqueZ(2000.0f)
        , NudgeForce(0.0f)
        , StrafeForce(0.0f)
        , StrafePowerUsage(0.0f)
        , MaxBankAngle(0.0f)
        , LegalHps()                        // retail zeroes the vector header at +0x130
        , NanobotLimit(-1)
        , ShieldBatteryLimit(-1)
    {
        BayDoorAnim.clear();
        HpBaySurface.clear();
        HpBayExternal.clear();
        // Common.dll.c 126545-126546: defaults to "HpHeadlight", not empty.
        HpTractorSource.Value = StringAlloc("HpHeadlight", false);
    }

    Ship::~Ship()
    {
        // Common.dll.c 0xd000: retail's +0x130 is a VC6 std::vector whose 0x14-byte elements each
        // own an inner VC6 vector<CacheString> (its _First at element+8). Retail frees each inner
        // buffer, nulls that element's three pointers, then frees the outer buffer. The port holds
        // the same data in LegalHps, so ~vector does all of that; the CacheString values themselves
        // are StringAlloc'd pool strings, which retail does not free here either.
        //
        // Retail additionally appears to run `MetaList<>::operator=(element+4, elem)` over each
        // inner range before freeing it -- an earlier note called that "not understood". It sits
        // exactly where VC6 inlines `vector<CacheString>::_Destroy(first,last)`, and CacheString is
        // a single pointer with no destructor, so there is no observable effect to reproduce. This
        // is an inference from the shape, not a byte-level reading of that call.
        LegalHps.clear();
    }

    bool Ship::read(INI_Reader& Reader)
    {
        // Retail 062f21b0..062f27e4. No "inherit" case: LoadShips handles that key before calling
        // read() (062f2ae0). The four CacheString keys use the no-argument get_value_string
        // (06310b70, the whole value); hp_type uses the indexed one (06310a30).
        if (EqObj::read(Reader))
            return true;

        if (Reader.is_value("bay_door_anim"))
        {
            BayDoorAnim.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("HP_bay_surface"))
        {
            HpBaySurface.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("HP_bay_external"))
        {
            HpBayExternal.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("HP_tractor_source"))
        {
            HpTractorSource.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("num_exhaust_nozzles"))
        {
            NumExhaustNozzles = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("hold_size"))
        {
            HoldSize = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("linear_drag"))
        {
            LinearDrag = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("angular_drag"))
        {
            AngularDragX = Reader.get_value_float(0);
            AngularDragY = Reader.get_value_float(1);
            AngularDragZ = Reader.get_value_float(2);
            return true;
        }

        if (Reader.is_value("steering_torque"))
        {
            SteeringTorqueX = Reader.get_value_float(0);
            SteeringTorqueY = Reader.get_value_float(1);
            SteeringTorqueZ = Reader.get_value_float(2);
            return true;
        }

        if (Reader.is_value("nudge_force"))
        {
            NudgeForce = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("strafe_force"))
        {
            StrafeForce = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("strafe_power_usage"))
        {
            StrafePowerUsage = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("ids_info1"))
        {
            IdsInfo1 = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ids_info2"))
        {
            IdsInfo2 = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ids_info3"))
        {
            IdsInfo3 = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ship_class"))
        {
            ShipClass = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("nanobot_limit"))
        {
            NanobotLimit = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("shield_battery_limit"))
        {
            ShieldBatteryLimit = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("max_bank_angle"))
        {
            // 062f2504..062f2526: get_value_float's result stays on the x87 stack unrounded
            // (value_num is the same body); FCOM [DAT_063a15b4] = 180.0f, TEST AH,0x41, JNZ keeps
            // less / equal / unordered, the fall-through (greater) loads 180.0f; FMUL float
            // [DAT_0639d210] = 0x3c8efa35; FSTP float. The product is formed in double and
            // rounded once to float; retail forms it at the x87 precision-control width.
            double Degrees = Reader.value_num(0);
            if (Degrees > 180.0)
                Degrees = 180.0;
            MaxBankAngle = (float)(Degrees * (double)0.017453292f);
            return true;
        }

        // .text:062f2531: `LEA ECX,[ESI+0x94]; CALL [EAX+8]` -- the CommReferrable subobject's
        // slot 2, a virtual call.
        if (read_comm_ini_data(Reader))
            return true;

        // Common.dll .text:062f2549 .. .text:062f27cc -- the "hp_type" directive.
        //   hp_type = <class name>, <hardpoint>, <hardpoint>, ...
        // .text:062f256a walks the 63-entry {name, value} table at .data:063eca40 (0x1f8 bytes,
        // stride 8, stricmp through DACOM.DLL) looking for the class name; a name that is not in
        // the table is swallowed and the line ignored (.text:062f2598 returns AL=1), and so is a
        // table value of 0 (.text:062f25a6). On a hit retail push_backs one default element onto
        // the vector at this+0x130 (.text:062f25d5), writes the class into element+0x00
        // (.text:062f2600) and then appends StringAlloc'd hardpoint names from INI index 1 upward
        // until is_value_empty (.text:062f2612 .. .text:062f27c6).
        if (Reader.is_value("hp_type"))
        {
            unsigned Value = 0;
            if (!TryLookupHpType(Reader.get_value_string(0), &Value) || Value == 0)
                return true;
            HpAttachmentType const Type = (HpAttachmentType)Value;

            LegalHps.push_back(LegalHpTypes());
            LegalHpTypes& Entry = LegalHps.back();
            Entry.Type = Type;

            for (int Index = 1; !Reader.is_value_empty(Index); ++Index)
            {
                CacheString Hp;
                Hp.Value = StringAlloc(Reader.get_value_string(Index), false);
                Entry.Hardpoints.push_back(Hp);
            }
            return true;
        }

        return false;
    }

    // 0x929a0 (.text:062f29a0). Retail walks [this+0x134, this+0x138) with a 0x14 stride, compares
    // element+0x00 against Type and returns element+0x04 -- the inner vector -- or null. Note it
    // returns the vector even when that vector is empty; only "no element of this class at all"
    // yields null. Server.dll's reason-3 cheat check depends on exactly that distinction.
    std::vector<CacheString> const* Ship::get_legal_hps(HpAttachmentType Type) const
    {
        for (unsigned int Index = 0; Index < LegalHps.size(); ++Index)
        {
            if (LegalHps[Index].Type == Type)
                return &LegalHps[Index].Hardpoints;
        }
        return nullptr;
    }

    // Common.dll.c 0x927f0. Only copies HpBaySurface/HpBayExternal/HpTractorSource and the
    // physics scalars (hold_size .. max_bank_angle); BayDoorAnim/IdsInfo1-3/ShipClass/
    // NumExhaustNozzles/StrafePowerUsage are NOT touched by retail's redefine either.
    void Ship::redefine(Root const& Other)
    {
        Root::redefine(Other);
        Ship const* Donor = (Ship const*)&Other;
        if (Donor->HpBaySurface.Value != HpBaySurface.Value)
            HpBaySurface.Value = Donor->HpBaySurface.Value;
        if (Donor->HpBayExternal.Value != HpBayExternal.Value)
            HpBayExternal.Value = Donor->HpBayExternal.Value;
        if (Donor->HpTractorSource.Value != HpTractorSource.Value)
            HpTractorSource.Value = Donor->HpTractorSource.Value;
        if (Donor->HoldSize != HoldSize)
            HoldSize = Donor->HoldSize;
        if (Donor->LinearDrag != LinearDrag)
            LinearDrag = Donor->LinearDrag;
        if (Donor->AngularDragX != AngularDragX || Donor->AngularDragY != AngularDragY
            || Donor->AngularDragZ != AngularDragZ)
        {
            AngularDragX = Donor->AngularDragX;
            AngularDragY = Donor->AngularDragY;
            AngularDragZ = Donor->AngularDragZ;
        }
        if (Donor->SteeringTorqueX != SteeringTorqueX || Donor->SteeringTorqueY != SteeringTorqueY
            || Donor->SteeringTorqueZ != SteeringTorqueZ)
        {
            SteeringTorqueX = Donor->SteeringTorqueX;
            SteeringTorqueY = Donor->SteeringTorqueY;
            SteeringTorqueZ = Donor->SteeringTorqueZ;
        }
        if (Donor->NudgeForce != NudgeForce)
            NudgeForce = Donor->NudgeForce;
        if (Donor->StrafeForce != StrafeForce)
            StrafeForce = Donor->StrafeForce;
        if (Donor->MaxBankAngle != MaxBankAngle)
            MaxBankAngle = Donor->MaxBankAngle;
    }

    AClassType Ship::get_class_type() const
    {
        // Common.dll.c 0xc9a0 (Archetype::Ship::get_class_type): return 3.
        return AClassShip;
    }
}

// ===========================================================================
// Explicit STL instantiation exported like retail's. Retail Common.dll exports all 55
// members of its VC6 std::list<CollisionGroupDesc>; FreeLancer.exe, Server.dll and
// rpclocal.dll import them by name. The port exports the modern instantiation; the
// importing declaration is in common_archetype.h.
// ===========================================================================
template class COMMON_API std::list<CollisionGroupDesc>;
