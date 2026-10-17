// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll fuse database and fuse-action classes -- retail .text:06cfc650-06cffc97.
// See src/include/server_fuse.h for what is and is not ported.

#include "../include/server_fuse.h"

#include "../include/common_ini.h"
#include "../include/common_cequip.h"      // CEquipTraverser, CExternalEquip (DestroyHardpoint)
#include "../include/common_goods.h"       // FindHardpoint_PS / HardpointToWorld (ImpulseAction)
#include "../include/server_gameobject.h"  // EqObj / GameObject slots every Trigger calls
#include "../include/server_sysdb.h"       // StarSystem::find_object (FUN_06d0c8d0)
#include "../include/server_createinfo.h"  // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/x86math.h"            // I3DMathEngine::Rotate (TumbleAction)

#include <cstdlib> // rand (retail: MSVCRT.DLL::rand)
#include <cstring> // _stricmp (retail: DACOM.DLL::stricmp)

namespace
{
// .data:DAT_06d8d8d0 -- Fuse.cpp's own lazily acquired "3DMathEngine" (FUN_06d5ff70), read by
// TumbleAction::Trigger (.text:06cff814) and ImpulseAction::Trigger (.text:06cff340). Retail
// never tests the engine for null.
I3DMathEngine* GFuseMathEngine = nullptr;

// The DamageList every damage Trigger builds on its stack. Retail inlines the DamageList
// constructor (e.g. LAB_06cfe180 .text:06cfe1b1-06cfe1d0: list head, Count = 0,
// bDestroyed = 0, InflictorOwner = 0) and then fills it in the same order everywhere
// (.text:06cfe1d4-06cfe233):
//   Cause = 0x15, InflictorId = SourceId;
//   if (Owner != 0 && Owner->OwnerSystem (+0x28) != 0)
//       if (Source = FUN_06d0c8d0(OwnerSystem, SourceId, 0x7f0000))
//           InflictorOwner = Source's CSimple (+0x10 under the `& 3 == 3` guard)->+0xb4;
// The CSimple read has no null test: a source whose object is not a CSimple faults in retail
// (`XOR EAX,EAX` then `MOV reg,[EAX+0xb4]`), and here.
void InitFuseDamageList(DamageList& Damage, EqObj* Owner, unsigned SourceId)
{
    unsigned int InflictorOwner = 0;
    Damage.Cause = static_cast<DamageCause>(0x15);
    Damage.InflictorId = SourceId;
    if (Owner != nullptr && Owner->OwnerSystem != nullptr)
    {
        GameObject* Source = Owner->OwnerSystem->find_object(SourceId, 0x7f0000u);
        if (Source != nullptr)
            InflictorOwner = Source->csimple()->OwnerPlayer;
    }
    Damage.InflictorOwner = InflictorOwner;
}

// TumbleAction's random interpolation, four times inline (.text:06cff788-06cff7b4 and its
// copies): FILD rand(); FMUL [.rdata:06d656b8] (0x38000000 = 2^-15); FLD High; FSUB Low;
// FMULP; FADD Low; FSTP float. Evaluated in double, the x87 precision MSVC runs with.
float RandomBetween(int Roll, float Low, float High)
{
    double const Scaled = static_cast<double>(Roll) * (1.0 / 32768.0);
    return static_cast<float>(Scaled * (static_cast<double>(High) - static_cast<double>(Low)) +
                              static_cast<double>(Low));
}
} // namespace

// ---------------------------------------------------------------------------
// ServerFuse
// ---------------------------------------------------------------------------

ServerFuse::ServerFuse(ID_String const& Nickname, float Lifetime)
    : Fuse(Nickname, Lifetime)
    , Owner(nullptr)
    , bHasDestroyRoot(false)
{
}

ServerFuse::ServerFuse(ServerFuse const& Other)
    : Fuse(Other)
    , Owner(Other.Owner)
    , bHasDestroyRoot(Other.bHasDestroyRoot)
{
}

ServerFuse::~ServerFuse()
{
    // .text:06cec7c0: Fuse::~Fuse, then operator delete when bit 0 of the flag is set.
}

bool ServerFuse::Read(INI_Reader& Reader)
{
    // FUN_06cfc650.
    if (!ReadFuseValues(Reader))
        return false; // .text:06cfc67a -> LAB_06cfcb16
    if (!Reader.read_header())
        return true;  // .text:06cfc68d -> LAB_06cfcaec

    for (;;)
    {
        // The headers are tested in this order (.text:06cfc6a0 .. 06cfcb0a).
        ServerFuseAction* Action = nullptr;
        if (Reader.is_header("ignite_fuse"))
        {
            Action = new IgniteFuseAction();
        }
        else if (Reader.is_header("destroy_root"))
        {
            // .text:06cfc711 `MOV byte ptr [EAX + 0x38],0x1` -- before the allocation.
            bHasDestroyRoot = true;
            Action = new DestroyRootAction();
        }
        else if (Reader.is_header("damage_root"))
        {
            Action = new DamageRootAction();
        }
        else if (Reader.is_header("destroy_group"))
        {
            Action = new DestroyGroupAction();
        }
        else if (Reader.is_header("damage_group"))
        {
            Action = new DamageGroupAction();
        }
        else if (Reader.is_header("destroy_hp_attachment"))
        {
            Action = new DestroyHardpointAttachmentAction();
        }
        else if (Reader.is_header("damage_hp_attachment"))
        {
            Action = new DamageHardpointAttachmentAction();
        }
        else if (Reader.is_header("impulse"))
        {
            Action = new ImpulseAction();
        }
        else if (Reader.is_header("tumble"))
        {
            Action = new TumbleAction();
        }
        else if (Reader.is_header("make_invincible"))
        {
            Action = new MakeInvincibleAction();
        }
        else if (Reader.is_header("dump_cargo"))
        {
            Action = new DumpCargoAction();
        }
        else if (Reader.is_header("fuse"))
        {
            // LAB_06cfcb03: the next fuse starts here; the caller reads it.
            return true;
        }

        if (Action != nullptr)
        {
            // .text:06cfcaa1 vt+8. Unlike the exe reader (FUN_00529e10), a failed read does NOT
            // end the fuse: .text:06cfcaa6 `JZ LAB_06cfcad8` skips the insert and goes on to the
            // next header, leaking the action (retail).
            if (Action->Slot08(Reader))
            {
                // .text:06cfcaa8-06cfcac4: FLD [action+4] -> key, FUN_06cfd7e0 multimap insert.
                Actions.insert(std::multimap<float, FuseAction*>::value_type(
                    Action->GetTriggerTime(), Action));
                // .text:06cfcacd vt+0x24.
                if (Action->ShouldRandomize())
                    bNeedsRandomize = true;
            }
        }

        // LAB_06cfcadc.
        if (!Reader.read_header())
            return true;
    }
}

void ServerFuse::SetOwner(EqObj* NewOwner)
{
    // FUN_06cfd1e0: +0x34 = owner, then every action's +0x24 = owner (walks the action tree
    // with FUN_06d160d0, the _Tree iterator increment).
    Owner = NewOwner;
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end(); ++It)
        static_cast<ServerFuseAction*>(It->second)->Owner = NewOwner;
}

// ---------------------------------------------------------------------------
// ServerFuseAction
// ---------------------------------------------------------------------------

ServerFuseAction::ServerFuseAction()
    : Owner(nullptr)
{
}

ServerFuseAction::~ServerFuseAction()
{
    // FUN_06cfd1b0 is ~FuseAction inlined; the base destructor does the same here.
}

// ---------------------------------------------------------------------------
// IgniteFuseAction
// ---------------------------------------------------------------------------

IgniteFuseAction::IgniteFuseAction()
    : FuseT(0.0f)
{
    // .text:06cfc6de-06cfc6ec / 06cfcf01-06cfcf0f: ID_String::clear on +0x28, +0x2c = 0.
    FuseId.clear();
}

FuseAction* IgniteFuseAction::Slot04() const
{
    // FUN_06cfcea0: new 0x30 object, then its slot 12 copies *this.
    IgniteFuseAction* Copy = new IgniteFuseAction();
    Copy->Slot30(*this);
    return Copy;
}

bool IgniteFuseAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfefd0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("fuse"))
            FuseId.Id = CreateID(Reader.get_value_string());
        else if (Reader.is_value("fuse_t"))
            FuseT = (float)Reader.get_value_float(0);
    }
    return true;
}

void IgniteFuseAction::Slot30(IgniteFuseAction const& Other)
{
    // FUN_06cff150 (shared by DestroyGroupAction and DestroyHardpointAttachmentAction):
    // CopyArchProperties, then the dwords at +0x28 and +0x2c.
    FuseAction::CopyArchProperties(Other);
    FuseId = Other.FuseId;
    FuseT = Other.FuseT;
}

void IgniteFuseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // FUN_06cff050, RET 8.
    //   .text:06cff056  CALL [EAX + 0x18]        ; IsTriggered() (slot 6)
    //   .text:06cff069  PUSH 0xbf800000          ; Lifetime = -1.0f
    //   .text:06cff06e  PUSH [ESI + 0x2c]        ; FuseT
    //   .text:06cff06f  PUSH EDI                 ; SubObjId
    //   .text:06cff073  PUSH &[ESI + 0x28]       ; FuseId
    //   .text:06cff078  PUSH SourceId
    //   .text:06cff079  CALL [EDX + 0x1e4]       ; Owner->light_fuse, result unused
    if (!IsTriggered())
        Owner->light_fuse(SourceId, FuseId, SubObjId, FuseT, -1.0f);
    // .text:06cff07f-06cff0d0: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

void IgniteFuseAction::UnTrigger(unsigned short SubObjId)
{
    // LAB_06cff0e0, RET 4.
    //   .text:06cff0e0-06cff11f  find SubObjId in [+0x18, +0x1c) and erase it (shift the tail
    //                            down, _Last -= 2) -- FuseAction::UnTrigger (.text:062bd790)
    //                            inlined; nothing happens when it is absent.
    //   .text:06cff129           bTriggered = 0 -- the base body leaves the flag alone.
    //   .text:06cff12f-06cff137  Owner->unlight_fuse(&FuseId, SubObjId, FuseT), CALL
    //                            [EDX + 0x1e8], result unused -- on both paths.
    FuseAction::UnTrigger(SubObjId);
    bTriggered = false;
    Owner->unlight_fuse(FuseId, SubObjId, FuseT);
}

// ---------------------------------------------------------------------------
// DestroyRootAction
// ---------------------------------------------------------------------------

DestroyRootAction::DestroyRootAction()
{
    // .text:06cfc733-06cfc73e: FuseAction(), +0x24 = 0, vftable. No fields of its own.
}

FuseAction* DestroyRootAction::Slot04() const
{
    // LAB_06cfcb30: new 0x28 object (FuseAction constructor inlined), then vt+0x2c
    // (CopyArchProperties, slot 11) on it with *this.
    DestroyRootAction* Copy = new DestroyRootAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

bool DestroyRootAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfe040: every value goes to ReadFuseActionValue; its result is not tested.
    while (Reader.read_value())
        ReadFuseActionValue(Reader);
    return true;
}

void DestroyRootAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // FUN_06cfe070, RET 8.
    //   .text:06cfe075  CALL [EAX + 0x18]        ; IsTriggered() (slot 6)
    //   .text:06cfe088  CALL [EDX + 0x158]       ; Owner->destroy(1, SourceId)
    if (!IsTriggered())
        Owner->destroy(1, SourceId);
    // .text:06cfe08e-06cfe0e1 is FuseAction::Trigger (.text:062bd730) inlined: bTriggered = 1,
    // append SubObjId through vector::insert(end, 1, id) unless present, then a side-effect-free
    // second find.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DamageRootAction
// ---------------------------------------------------------------------------

DamageRootAction::DamageRootAction()
    : Hitpoints(0)
    , bCanKill(true)
{
    // .text:06cfc782-06cfc788 / 06cfcbcb-06cfcbce: +0x28 = 0, +0x2c = 1.
}

FuseAction* DamageRootAction::Slot04() const
{
    // LAB_06cfcb90.
    DamageRootAction* Copy = new DamageRootAction();
    Copy->Slot30(*this);
    return Copy;
}

bool DamageRootAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfe0f0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("damage_type"))
        {
            // .text:06cfe12c-06cfe14a: DACOM stricmp against "absolute".
            if (_stricmp(Reader.get_value_string(), "absolute") == 0)
                bCanKill = false;
            else
                bCanKill = true;
        }
        else if (Reader.is_value("hitpoints"))
        {
            Hitpoints = Reader.get_value_int(0);
        }
    }
    return true;
}

void DamageRootAction::Slot30(DamageRootAction const& Other)
{
    // FUN_06cfe330: CopyArchProperties, dword +0x28, byte +0x2c.
    FuseAction::CopyArchProperties(Other);
    Hitpoints = Other.Hitpoints;
    bCanKill = Other.bCanKill;
}

void DamageRootAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cfe180, RET 8.
    //   .text:06cfe19e  CALL [EAX + 0x18]        ; IsTriggered() (slot 6)
    //   .text:06cfe1b1-06cfe233                  ; the stack DamageList (InitFuseDamageList)
    //   .text:06cfe226  FILD [ESI + 0x28]        ; (float)Hitpoints
    //   .text:06cfe230  MOV DL,[ESI + 0x2c]      ; bCanKill
    //   .text:06cfe23e  CALL [EAX + 0x1c0]       ; Owner->inflict_hull_damage(hp, bCanKill, DL)
    //   .text:06cfe24e  CALL [EAX + 0x188]       ; Owner->apply_damage_list(DL)
    //   .text:06cfe254-06cfe28a                  ; ~DamageList inlined
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->inflict_hull_damage(static_cast<float>(Hitpoints), bCanKill, Damage);
        Owner->apply_damage_list(Damage);
    }
    // .text:06cfe28f-06cfe316: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DestroyGroupAction
// ---------------------------------------------------------------------------

DestroyGroupAction::DestroyGroupAction()
    : Fate(0)
{
    // .text:06cfc7d9-06cfc7e3 / 06cfcc61-06cfcc6f: CacheString::clear on +0x28, +0x2c = 0.
    GroupName.clear();
}

FuseAction* DestroyGroupAction::Slot04() const
{
    // FUN_06cfcc00.
    DestroyGroupAction* Copy = new DestroyGroupAction();
    Copy->Slot30(*this);
    return Copy;
}

bool DestroyGroupAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfe370.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("group_name"))
        {
            GroupName.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("fate"))
        {
            // .text:06cfe3e1-06cfe462: the value is copied into a stack TString<32> (null ->
            // empty, else clamped to 31 characters) and compared byte-exactly (REPE CMPSB over
            // the literal and its terminator). Any other string leaves Fate unchanged.
            TString<32> FateName;
            FateName = Reader.get_value_string();
            if (FateName == "debris")
                Fate = 2;
            else if (FateName == "disappear")
                Fate = 3;
        }
    }
    return true;
}

void DestroyGroupAction::Slot30(DestroyGroupAction const& Other)
{
    // FUN_06cff150 (ICF with IgniteFuseAction's): CopyArchProperties, dwords +0x28 and +0x2c.
    FuseAction::CopyArchProperties(Other);
    GroupName = Other.GroupName;
    Fate = Other.Fate;
}

void DestroyGroupAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cfe490, RET 8.
    if (!IsTriggered()) // .text:06cfe4af CALL [EAX + 0x18]
    {
        // .text:06cfe4ba-06cfe4d4: stricmp(GroupName, DAT_06d8d8ac "random").
        if (_stricmp(GroupName.Value, GRandomGroupName) == 0)
        {
            // .text:06cfe4dd FUN_06ce6870 (ceqobj, no null test), .text:06cfe4e2 LEA EBP,[+0x144].
            CArchGroupManager& Groups = Owner->ceqobj()->ArchGroups;
            int const Living = Groups.NumLiving(); // .text:06cfe4ea
            if (Living > 0)
            {
                // .text:06cfe4f6-06cfe517: rand() * Living, signed divide by 0x8000.
                int const Target = (rand() * Living) / 0x8000;
                // .text:06cfe51a / 06cfe522: counter = 0, traverser "at start" = 1.
                int Counter = 0;
                CArchGrpTraverser Walk;
                CArchGroup* Group = Groups.Traverse(Walk);
                while (Group != nullptr)
                {
                    // .text:06cfe530-06cfe548: dead groups (State 2 / 3) are skipped; a living
                    // one bumps the counter BEFORE the compare, so the first living group is
                    // number 1 and a Target of 0 never matches -- the name then stays "random".
                    if (!Group->IsDead())
                    {
                        ++Counter;
                        if (Counter == Target)
                        {
                            // .text:06cfe559-06cfe563: CArchGroup::Name, raw pointer copy into
                            // +0x28 (CacheString has no assignment operator in retail).
                            GroupName = Group->Name();
                            break;
                        }
                    }
                    Group = Groups.Traverse(Walk);
                }
            }
            else
            {
                GroupName.clear(); // .text:06cfe568 CacheString::clear on +0x28
            }
        }

        // .text:06cfe570-06cfe577: an empty name skips the damage.
        if (GroupName.Value[0] != '\0')
        {
            // .text:06cfe57d-06cfe5f7: the stack DamageList (InitFuseDamageList).
            DamageList Damage;
            InitFuseDamageList(Damage, Owner, SourceId);
            // .text:06cfe607 CALL [EDX + 0x1c8]: destroy_group_by_name(name, Fate, DL), result
            // unused; .text:06cfe617 CALL [EAX + 0x188]: apply_damage_list(DL).
            Owner->destroy_group_by_name(GroupName.Value,
                                         static_cast<DamageEntry::SubObjFate>(Fate), Damage);
            Owner->apply_damage_list(Damage);
        }
    }
    // .text:06cfe656-06cfe6ac: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DamageGroupAction
// ---------------------------------------------------------------------------

DamageGroupAction::DamageGroupAction()
    : Hitpoints(0)
    , bCanKill(true)
{
    // .text:06cfc833-06cfc840 / 06cfcd01-06cfcd12: CacheString::clear on +0x28, +0x2c = 0,
    // +0x30 = 1.
    GroupName.clear();
}

FuseAction* DamageGroupAction::Slot04() const
{
    // FUN_06cfcca0.
    DamageGroupAction* Copy = new DamageGroupAction();
    Copy->Slot30(*this);
    return Copy;
}

bool DamageGroupAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfe6c0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("group_name"))
        {
            GroupName.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("damage_type"))
        {
            // .text:06cfe72d-06cfe796: TString<32> + byte-exact compare (NOT the stricmp
            // DamageRootAction uses).
            TString<32> DamageType;
            DamageType = Reader.get_value_string();
            if (DamageType == "absolute")
                bCanKill = false;
            else
                bCanKill = true;
        }
        else if (Reader.is_value("hitpoints"))
        {
            Hitpoints = Reader.get_value_int(0);
        }
    }
    return true;
}

void DamageGroupAction::Slot30(DamageGroupAction const& Other)
{
    // FUN_06cfe950: CopyArchProperties, dwords +0x28 / +0x2c, byte +0x30.
    FuseAction::CopyArchProperties(Other);
    GroupName = Other.GroupName;
    Hitpoints = Other.Hitpoints;
    bCanKill = Other.bCanKill;
}

void DamageGroupAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cfe7d0, RET 8. No "random" pick here -- the name goes out as read.
    //   .text:06cfe7ed  CALL [EAX + 0x18]        ; IsTriggered() (slot 6)
    //   .text:06cfe7f8-06cfe87d                  ; the stack DamageList (InitFuseDamageList)
    //   .text:06cfe872  FILD [ESI + 0x2c]        ; (float)Hitpoints
    //   .text:06cfe87a  MOV BL,[ESI + 0x30]      ; bCanKill
    //   .text:06cfe88e  CALL [EDX + 0x1c4]       ; inflict_group_damage(name, hp, bCanKill, DL)
    //   .text:06cfe89e  CALL [EAX + 0x188]       ; apply_damage_list(DL)
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->inflict_group_damage(GroupName.Value, static_cast<float>(Hitpoints), bCanKill,
                                    Damage);
        Owner->apply_damage_list(Damage);
    }
    // .text:06cfe8df-06cfe932: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DestroyHardpointAttachmentAction
// ---------------------------------------------------------------------------

DestroyHardpointAttachmentAction::DestroyHardpointAttachmentAction()
    : Fate(0)
{
    // .text:06cfc891-06cfc89b / 06cfcdb1-06cfcdbf: CacheString::clear on +0x28, +0x2c = 0.
    Hardpoint.clear();
}

FuseAction* DestroyHardpointAttachmentAction::Slot04() const
{
    // FUN_06cfcd50.
    DestroyHardpointAttachmentAction* Copy = new DestroyHardpointAttachmentAction();
    Copy->Slot30(*this);
    return Copy;
}

bool DestroyHardpointAttachmentAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfe9a0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("fate"))
        {
            // .text:06cfea0f-06cfeaaf: TString<32> + byte-exact compares; "loot" is
            // .rdata:06d66f48.
            TString<32> FateName;
            FateName = Reader.get_value_string();
            if (FateName == "debris")
                Fate = 2;
            else if (FateName == "disappear")
                Fate = 3;
            else if (FateName == "loot")
                Fate = 5;
        }
    }
    return true;
}

void DestroyHardpointAttachmentAction::Slot30(DestroyHardpointAttachmentAction const& Other)
{
    // FUN_06cff150 (ICF): CopyArchProperties, dwords +0x28 and +0x2c.
    FuseAction::CopyArchProperties(Other);
    Hardpoint = Other.Hardpoint;
    Fate = Other.Fate;
}

void DestroyHardpointAttachmentAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cfeae0, RET 8.
    if (!IsTriggered()) // .text:06cfeaff CALL [EAX + 0x18]
    {
        // .text:06cfeb0a-06cfeb21: stricmp(Hardpoint, DAT_06d8d8a8 "random").
        if (_stricmp(Hardpoint.Value, GRandomHardpointName) == 0)
        {
            // .text:06cfeb2a FUN_06ce6870 (ceqobj, no null test); .text:06cfeb52 ADD ESI,0xe4.
            CEquipManager& Equips = Owner->ceqobj()->EquipManager;
            // .text:06cfeb2f-06cfeb42: counter = 0, CEquipTraverser(0x1fe0) -- attached equips.
            int Count = 0;
            CEquipTraverser Walk(0x1fe0);
            // .text:06cfeb5b-06cfeb7f, first pass: count the equips whose slot 2 (+0x08,
            // IsDestroyed) says false.
            CEquip* Equip = Equips.Traverse(Walk);
            while (Equip != nullptr)
            {
                if (!Equip->IsDestroyed())
                    ++Count;
                Equip = Equips.Traverse(Walk);
            }

            if (Count != 0) // .text:06cfeb85 TEST EBX,EBX
            {
                // .text:06cfeb89-06cfeba6: rand() * Count, signed divide by 0x8000.
                int const Target = (rand() * Count) / 0x8000;
                // .text:06cfeba9 counter = 0; .text:06cfebad traverser +0x08 = 0 (Restart
                // inlined).
                Count = 0;
                Walk.Restart();
                // .text:06cfebb1-06cfebcf, second pass: EVERY equip bumps the counter -- the
                // destroyed ones the first pass skipped included -- before the compare, so a
                // Target of 0 never matches and the name then stays "random".
                Equip = Equips.Traverse(Walk);
                while (Equip != nullptr)
                {
                    ++Count;
                    if (Count == Target)
                    {
                        // .text:06cfebd3-06cfebe1: slot +0x60 (CExternalEquip::GetParentHPName,
                        // CacheString returned through a hidden pointer), raw pointer copy into
                        // +0x28. The 0x1fe0 traverser only yields attached equips.
                        Hardpoint = static_cast<CExternalEquip*>(Equip)->GetParentHPName();
                        break;
                    }
                    Equip = Equips.Traverse(Walk);
                }
            }
            else
            {
                Hardpoint.clear(); // .text:06cfebe6 CacheString::clear on +0x28
            }
        }

        // .text:06cfebef-06cfebf6: an empty name skips the damage.
        if (Hardpoint.Value[0] != '\0')
        {
            // .text:06cfebfc-06cfec76: the stack DamageList (InitFuseDamageList).
            DamageList Damage;
            InitFuseDamageList(Damage, Owner, SourceId);
            // .text:06cfec86 CALL [EDX + 0x1d0]: destroy_equip_by_hardpoint(hp, Fate, DL), result
            // unused; .text:06cfec96 CALL [EAX + 0x188]: apply_damage_list(DL).
            Owner->destroy_equip_by_hardpoint(Hardpoint.Value,
                                              static_cast<DamageEntry::SubObjFate>(Fate), Damage);
            Owner->apply_damage_list(Damage);
        }
    }
    // .text:06cfecd5-06cfed2c: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DamageHardpointAttachmentAction
// ---------------------------------------------------------------------------

DamageHardpointAttachmentAction::DamageHardpointAttachmentAction()
    : Hitpoints(0)
    , bCanKill(true)
{
    // .text:06cfc8eb-06cfc8f8 / 06cfce51-06cfce62: CacheString::clear on +0x28, +0x2c = 0,
    // +0x30 = 1.
    Hardpoint.clear();
}

FuseAction* DamageHardpointAttachmentAction::Slot04() const
{
    // FUN_06cfcdf0.
    DamageHardpointAttachmentAction* Copy = new DamageHardpointAttachmentAction();
    Copy->Slot30(*this);
    return Copy;
}

bool DamageHardpointAttachmentAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cfed40.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("damage_type"))
        {
            // .text:06cfedad-06cfee16: TString<32> + byte-exact compare.
            TString<32> DamageType;
            DamageType = Reader.get_value_string();
            if (DamageType == "absolute")
                bCanKill = false;
            else
                bCanKill = true;
        }
        else if (Reader.is_value("hitpoints"))
        {
            Hitpoints = Reader.get_value_int(0);
        }
    }
    return true;
}

void DamageHardpointAttachmentAction::Slot30(DamageHardpointAttachmentAction const& Other)
{
    // FUN_06cfe950 (ICF with DamageGroupAction's): CopyArchProperties, +0x28, +0x2c, +0x30.
    FuseAction::CopyArchProperties(Other);
    Hardpoint = Other.Hardpoint;
    Hitpoints = Other.Hitpoints;
    bCanKill = Other.bCanKill;
}

void DamageHardpointAttachmentAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cfee50, RET 8 -- byte-for-byte DamageGroupAction's body (LAB_06cfe7d0) except the
    // owner slot: +0x1cc instead of +0x1c4 (.text:06cfef0e). No "random" pick.
    //   .text:06cfeef2  FILD [ESI + 0x2c]        ; (float)Hitpoints
    //   .text:06cfeefa  MOV BL,[ESI + 0x30]      ; bCanKill
    //   .text:06cfef0e  CALL [EDX + 0x1cc]       ; inflict_equip_damage(hp name, hp, bCanKill, DL)
    //   .text:06cfef1e  CALL [EAX + 0x188]       ; apply_damage_list(DL)
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->inflict_equip_damage(Hardpoint.Value, static_cast<float>(Hitpoints), bCanKill,
                                    Damage);
        Owner->apply_damage_list(Damage);
    }
    // .text:06cfef5f-06cfefb2: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// ImpulseAction
// ---------------------------------------------------------------------------

ImpulseAction::ImpulseAction()
    : Force(0.0f)
    , Radius(0.0f)
    , Damage(0.0f)
{
    // .text:06cfc949-06cfc962 / 06cfcfc1-06cfcfde: CacheString::clear on +0x28, +0x2c/+0x30/
    // +0x34 = 0, Vector(0, 0, 0) at +0x38 (FUN_06ce7c10 in the reader, three stores in the
    // clone).
    Hardpoint.clear();
    PosOffset.x = 0.0f;
    PosOffset.y = 0.0f;
    PosOffset.z = 0.0f;
}

FuseAction* ImpulseAction::Slot04() const
{
    // FUN_06cfcf60.
    ImpulseAction* Copy = new ImpulseAction();
    Copy->Slot30(*this);
    return Copy;
}

bool ImpulseAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cff170.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("force"))
        {
            Force = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("radius"))
        {
            Radius = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("damage"))
        {
            Damage = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("pos_offset"))
        {
            // .text:06cff228-06cff266: values read 0, 1, 2 into temporaries, then stored.
            float const X = (float)Reader.get_value_float(0);
            float const Y = (float)Reader.get_value_float(1);
            float const Z = (float)Reader.get_value_float(2);
            PosOffset.x = X;
            PosOffset.y = Y;
            PosOffset.z = Z;
        }
    }
    return true;
}

void ImpulseAction::Slot30(ImpulseAction const& Other)
{
    // FUN_06cff550: CopyArchProperties, then +0x28 .. +0x40 -- the hardpoint IS copied here
    // (unlike the exe-side impulse action class).
    FuseAction::CopyArchProperties(Other);
    Hardpoint = Other.Hardpoint;
    Force = Other.Force;
    Radius = Other.Radius;
    Damage = Other.Damage;
    PosOffset = Other.PosOffset;
}

void ImpulseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cff290 (vftable .rdata:06d66c78 +0x1c, RET 8). Every early-out jumps to the inlined
    // base tail LAB_06cff4b8. SourceId is not read by the body -- the blast is the owner's own.
    if (!IsTriggered()) // .text:06cff2b2 CALL [EAX + 0x18]
    {
        // .text:06cff2bf-06cff2f0: FUN_06d006c0 on the owner's id (IObjInspect slot 8); the
        // lookup failing or the StarSystem out-param being null bails. The object out-param
        // ([ESP+0x4c]) is never read.
        unsigned int OwnerId = Owner->get_id();
        GameObject* Found = nullptr;
        StarSystem* System = nullptr;
        if (Server_FindObject(OwnerId, Found, System) && System != nullptr)
        {
            // .text:06cff2f6-06cff323: FindHardpoint_PS(owner get_index (slot 7), the Hardpoint
            // string, handle out preset to -1, info out); false bails.
            long HpHandle = -1;
            HardpointInfo HpInfo;
            if (FindHardpoint_PS(Owner->get_index(), Hardpoint.Value, HpHandle, HpInfo))
            {
                // .text:06cff329-06cff33a: HardpointToWorld(get_index() again, info).
                HardpointToWorld(Owner->get_index(), HpInfo);

                // .text:06cff340-06cff36a: DAT_06d8d8d0, lazily FUN_06d5ff70, then
                // I3DMathEngine +0x38 (engine, &Offset, &HpInfo.Orient (+0x10), &PosOffset).
                if (GFuseMathEngine == nullptr)
                    GFuseMathEngine = Server_Acquire3DMathEngine();
                FVector Offset;
                GFuseMathEngine->TransposeTransform(Offset,
                                                    reinterpret_cast<FMatrix const&>(HpInfo.Orient),
                                                    reinterpret_cast<FVector const&>(PosOffset));

                // .text:06cff36d-06cff39b: Offset + HpInfo.Position (+0x04), component-wise
                // (z, y, x evaluated; stored x, y, z). A float sum rounded from the 53-bit x87
                // register equals the plain float sum.
                Vector BlastPos;
                BlastPos.x = static_cast<float>(static_cast<double>(Offset.X) + HpInfo.Position.x);
                BlastPos.y = static_cast<float>(static_cast<double>(Offset.Y) + HpInfo.Position.y);
                BlastPos.z = static_cast<float>(static_cast<double>(Offset.Z) + HpInfo.Position.z);

                // .text:06cff378-06cff3cf: Archetype::Explosion(ID_String{CreateID("FuseImpulse")})
                // on the stack, then Radius (+0x04) = +0x30, Impulse (+0x08) = Force (+0x2c),
                // HullDamage (+0x0c) = Damage (+0x34).
                Archetype::Explosion Blast(ID_String{CreateID("FuseImpulse")});
                Blast.Radius = Radius;
                Blast.Impulse = Force;
                Blast.HullDamage = Damage;

                // .text:06cff3d3-06cff42a: the event at [ESP+0x2c]. Retail zeroes both ids,
                // stores cause 4 and zeroes +0x18/+0x1c, calls get_id (slot 8) once more, then
                // writes both ids, the position, cause 0x15 (over the 4) and the explosion.
                ExplosionDamageEvent Event;
                Event.ExplodingObjId = 0;
                Event.InflictorId = 0;
                Event.Cause = static_cast<DamageCause>(4);
                Event.Explosion = nullptr;
                Event.SkipCollisionGroup = nullptr;
                unsigned int const BlastOwnerId = Owner->get_id();
                Event.ExplodingObjId = BlastOwnerId;
                Event.InflictorId = BlastOwnerId;
                Event.Position = BlastPos;
                Event.Cause = static_cast<DamageCause>(0x15);
                Event.Explosion = &Blast;

                // .text:06cff42e: FUN_06d0b260 on the StarSystem the lookup returned.
                System->explode(Event);
                // .text:06cff433-06cff4b3: ~Explosion inlined (the two vectors and the effect
                // list, FUN_06cecb50) -- the scope exit of Blast.
            }
        }
    }
    // .text:06cff4b8-06cff52e: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// TumbleAction
// ---------------------------------------------------------------------------

TumbleAction::TumbleAction()
    : AngDragScale(1.0f)
    , ThrottleLow(0.0f)
    , ThrottleHigh(0.0f)
    , bEngineKill(false)
{
    // .text:06cfc9b4-06cfc9ee / 06cfd049-06cfd06b: +0x28 = 1.0f, Vector(0, 0, 0) at +0x2c,
    // Vector(1, 1, 1) at +0x38, +0x4c = 0, +0x48 = 0, +0x44 = 0.
    TurnThrottleLow.x = 0.0f;
    TurnThrottleLow.y = 0.0f;
    TurnThrottleLow.z = 0.0f;
    TurnThrottleHigh.x = 1.0f;
    TurnThrottleHigh.y = 1.0f;
    TurnThrottleHigh.z = 1.0f;
}

FuseAction* TumbleAction::Slot04() const
{
    // LAB_06cfd010.
    TumbleAction* Copy = new TumbleAction();
    Copy->Slot30(*this);
    return Copy;
}

bool TumbleAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cff5a0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("ang_drag_scale"))
        {
            AngDragScale = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("turn_throttle_x"))
        {
            TurnThrottleLow.x = (float)Reader.get_value_float(0);
            TurnThrottleHigh.x = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("turn_throttle_y"))
        {
            TurnThrottleLow.y = (float)Reader.get_value_float(0);
            TurnThrottleHigh.y = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("turn_throttle_z"))
        {
            TurnThrottleLow.z = (float)Reader.get_value_float(0);
            TurnThrottleHigh.z = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("throttle"))
        {
            ThrottleLow = (float)Reader.get_value_float(0);
            ThrottleHigh = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("engine_kill"))
        {
            bEngineKill = Reader.get_value_bool(0);
        }
    }
    return true;
}

void TumbleAction::Slot30(TumbleAction const& Other)
{
    // FUN_06cff940: CopyArchProperties, +0x28, +0x44, +0x48 and +0x4c ONLY. The two turn
    // throttle vectors (+0x2c .. +0x40) are NOT copied, so a clone keeps the constructor's
    // (0,0,0) / (1,1,1) -- retail.
    FuseAction::CopyArchProperties(Other);
    AngDragScale = Other.AngDragScale;
    ThrottleLow = Other.ThrottleLow;
    ThrottleHigh = Other.ThrottleHigh;
    bEngineKill = Other.bEngineKill;
}

void TumbleAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cff6c0, RET 8. Every early-out below jumps to the same inlined base tail.
    // The `TEST ECX,ECX` / `LEA EAX,[ECX+0xc]; TEST EAX,EAX` pairs at .text:06cff6f9-06cff706 and
    // .text:06cff744-06cff74d are the compiler's null-preserving conversion of Owner to its CBase
    // sub-object; Owner has already been dereferenced at .text:06cff6d7 by then, so they are not
    // guards and have no counterpart here.
    if (!IsTriggered()) // .text:06cff6c9 CALL [EAX + 0x18]
    {
        // .text:06cff6d4-06cff6f3: the owner's CSimple (+0x10 under the `& 3 == 3` guard, no null
        // test on the result) must have no owner player (+0xb4) -- players never tumble.
        if (Owner->csimple()->OwnerPlayer == 0)
        {
            // .text:06cff70c-06cff724: the object must be a CShip ((ObjectClass & 0x503) ==
            // 0x503). cobj()'s `& 3 == 3` guard is implied by that mask.
            CObject* Object = Owner->cobj();
            if (Object != nullptr &&
                (static_cast<unsigned>(Object->ObjectClass) & 0x503u) == 0x503u)
            {
                // .text:06cff72a FUN_06ce6870 (ceqobj); .text:06cff733 CEqObj::
                // get_behavior_interface -- a ship driven by a behaviour manager is left alone.
                CEqObj* Cq = Owner->ceqobj();
                if (Cq->get_behavior_interface() == nullptr)
                {
                    // .text:06cff741-06cff76d: the same CShip cast again, then
                    // CShip+0x2c0 = AngDragScale.
                    CShip* Ship = static_cast<CShip*>(Owner->cobj());
                    Ship->AngularDragFactor = AngDragScale;

                    // .text:06cff773-06cff809: three random turn throttles in x, y, z order.
                    FVector Axis;
                    Axis.X = RandomBetween(rand(), TurnThrottleLow.x, TurnThrottleHigh.x);
                    Axis.Y = RandomBetween(rand(), TurnThrottleLow.y, TurnThrottleHigh.y);
                    Axis.Z = RandomBetween(rand(), TurnThrottleLow.z, TurnThrottleHigh.z);

                    // .text:06cff80f CALL [EDX + 0x10]: IObjInspect::get_orientation.
                    Matrix const& Orientation = Owner->get_orientation();
                    // .text:06cff814-06cff835: DAT_06d8d8d0, lazily FUN_06d5ff70, then
                    // I3DMathEngine +0x30 Rotate(engine, &Rotated, orientation, &Axis).
                    if (GFuseMathEngine == nullptr)
                        GFuseMathEngine = Server_Acquire3DMathEngine();
                    FVector Rotated;
                    GFuseMathEngine->Rotate(Rotated,
                                            reinterpret_cast<FTransform const&>(Orientation),
                                            Axis);

                    // .text:06cff838-06cff85d: copied into a Vector, IObjAffect (Owner+4) slot
                    // 1, set_axis_throttle. Result unused.
                    Vector AxisThrottle;
                    AxisThrottle.x = Rotated.X;
                    AxisThrottle.y = Rotated.Y;
                    AxisThrottle.z = Rotated.Z;
                    static_cast<IObjAffect*>(Owner)->set_axis_throttle(AxisThrottle);

                    // .text:06cff860-06cff899: a random throttle, IObjAffect slot 0,
                    // set_throttle. Result unused.
                    float const Throttle = RandomBetween(rand(), ThrottleLow, ThrottleHigh);
                    static_cast<IObjAffect*>(Owner)->set_throttle(Throttle);

                    // .text:06cff89b-06cff8cf: engine_kill -- the first engine (type mask
                    // 0x20000) of the CEqObj, no null test; its sub-object id (word +0x08)
                    // zero-extended into a dword local and passed by address to IObjAffect
                    // slot 19 (+0x4c) with (false, 0): deactivate. Result unused.
                    if (bEngineKill)
                    {
                        unsigned int EngineId = Cq->EquipManager.FindFirst(0x20000u)->Id;
                        unsigned short const* EngineIdPtr =
                            reinterpret_cast<unsigned short const*>(&EngineId);
                        static_cast<IObjAffect*>(Owner)->slot_4c(EngineIdPtr, false, 0);
                    }
                }
            }
        }
    }
    // .text:06cff8d2-06cff92c: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// MakeInvincibleAction
// ---------------------------------------------------------------------------

MakeInvincibleAction::MakeInvincibleAction()
    : bTurnOn(true)
{
    // .text:06cfca2e / 06cfd0db: +0x28 = 1.
}

FuseAction* MakeInvincibleAction::Slot04() const
{
    // LAB_06cfd0a0.
    MakeInvincibleAction* Copy = new MakeInvincibleAction();
    Copy->Slot30(*this);
    return Copy;
}

bool MakeInvincibleAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cff970.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("turn_on"))
            bTurnOn = Reader.get_value_bool(0);
    }
    return true;
}

void MakeInvincibleAction::Slot30(MakeInvincibleAction const& Other)
{
    // FUN_06cffa80: CopyArchProperties, byte +0x28.
    FuseAction::CopyArchProperties(Other);
    bTurnOn = Other.bTurnOn;
}

void MakeInvincibleAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cff9d0, RET 8.
    //   .text:06cff9d8  CALL [EAX + 0x18]        ; IsTriggered() (slot 6)
    //   .text:06cff9e4  [ESP+4]  = 4             ; FInvincibility::Type (the list key)
    //   .text:06cff9ec  [ESP+8]  = bTurnOn       ; bProtect
    //   .text:06cff9f0  [ESP+9]  = bTurnOn       ; bFromPlayers
    //   .text:06cff9f4  [ESP+0xc] = 0.0f, 1.0f when !bTurnOn (.text:06cff9fe) ; MaxHpLoss
    //   .text:06cffa10  CALL [EDX + 0x194]       ; Owner->set_invincible(&block)
    // Bytes +0x0a/+0x0b of the block are never written by retail (stack residue); zeroed here.
    if (!IsTriggered())
    {
        FInvincibility Command;
        Command.Type = 4;
        Command.bProtect = bTurnOn;
        Command.bFromPlayers = bTurnOn;
        Command.Pad06[0] = 0;
        Command.Pad06[1] = 0;
        Command.MaxHpLoss = bTurnOn ? 0.0f : 1.0f;
        Owner->set_invincible(Command);
    }
    // .text:06cffa16-06cffa6f: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DumpCargoAction
// ---------------------------------------------------------------------------

DumpCargoAction::DumpCargoAction()
{
    // .text:06cfca7c-06cfca84 / 06cfd170-06cfd178: CacheString::clear on +0x28.
    OriginHardpoint.clear();
}

FuseAction* DumpCargoAction::Slot04() const
{
    // FUN_06cfd110.
    DumpCargoAction* Copy = new DumpCargoAction();
    Copy->Slot30(*this);
    return Copy;
}

bool DumpCargoAction::Slot08(INI_Reader& Reader)
{
    // FUN_06cffaa0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("origin_hardpoint"))
            OriginHardpoint.Value = StringAlloc(Reader.get_value_string(0), false);
    }
    return true;
}

void DumpCargoAction::Slot30(DumpCargoAction const& Other)
{
    // FUN_06cffc80: CopyArchProperties, dword +0x28.
    FuseAction::CopyArchProperties(Other);
    OriginHardpoint = Other.OriginHardpoint;
}

void DumpCargoAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_06cffb10, RET 8.
    //   .text:06cffb2e  CALL [EAX + 0x18]        ; IsTriggered() (slot 6)
    //   .text:06cffb39-06cffbb5                  ; the stack DamageList (InitFuseDamageList)
    //   .text:06cffbc1  CALL [EDX + 0x1d4]       ; dump_cargo(OriginHardpoint, DL), result unused
    //   .text:06cffbd1  CALL [EAX + 0x188]       ; apply_damage_list(DL)
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->dump_cargo(OriginHardpoint.Value, Damage);
        Owner->apply_damage_list(Damage);
    }
    // .text:06cffc16-06cffc6c: FuseAction::Trigger (.text:062bd730) inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// "random" -- LAB_06cfe350 / LAB_06cfe980, CRT dynamic initializers:
// `PUSH 0; PUSH "random"; CALL StringAlloc; MOV [DAT], EAX`.
// ---------------------------------------------------------------------------

char const* GRandomGroupName = StringAlloc("random", false);     // DAT_06d8d8ac
char const* GRandomHardpointName = StringAlloc("random", false); // DAT_06d8d8a8

// ---------------------------------------------------------------------------
// ServerFuseDB
// ---------------------------------------------------------------------------

// DAT_06d8d870. Its dynamic initializer (LAB_06cfd220 -> FUN_06cfd320 tree ctor, atexit
// FUN_06cfd240 tree dtor guarded by bit 0 of DAT_06d8d86c) is the std::map's own construction
// and destruction; neither deletes the ServerFuse values.
std::map<unsigned, ServerFuse*> ServerFuseDB::Fuses;

std::map<unsigned, ServerFuse*>& ServerFuseDB::Get()
{
    // FUN_06cfd390: `MOV EAX,DAT_06d8d870`.
    return Fuses;
}

void ServerFuseDB::Clear()
{
    // FUN_06cfd3a0: virtual-delete (vt[0](1)) every non-null prototype in key order, then erase
    // all nodes (the whole-tree fast path when begin() is the leftmost node, node by node
    // otherwise -- same result).
    for (std::map<unsigned, ServerFuse*>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Fuses.clear();
}

void ServerFuseDB::LoadAll()
{
    // FUN_06cfd4b0: one FUN_06cfd4e0 per path in FuseDB::m_FuseINIFiles (node+0xc = the
    // TString's characters).
    for (std::list<TString<260>>::iterator It = m_FuseINIFiles.begin(); It != m_FuseINIFiles.end(); ++It)
        LoadFile(It->Data);
}

void ServerFuseDB::LoadFile(char const* Path)
{
    // FUN_06cfd4e0.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return;
    Reader.read_header(); // result unused (.text:06cfd537)
    while (!Reader.is_end())
    {
        if (Reader.is_header("fuse"))
        {
            ServerFuse* NewFuse = new ServerFuse(ID_String{CreateID("")}, -1.0f);
            // .text:06cfd5c3. A failed Read, or a nickname already in the map (FUN_06d5c810
            // lower_bound + key compare, FUN_06d5c600 hinted insert), leaks NewFuse (retail).
            if (NewFuse->Read(Reader))
                Fuses.insert(std::map<unsigned, ServerFuse*>::value_type(NewFuse->Name().Id, NewFuse));
        }
        else
        {
            Reader.read_header(); // LAB_06cfd7af
        }
    }
}
