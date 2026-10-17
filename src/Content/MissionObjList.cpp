// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU40 (06f24670-06f28f60): the [ObjList] directives (see MissionObjList.h).

#include "MissionObjList.h"

#include "BehaviorCmds.h"
#include "ContentPlayerState.h"   // FindContentPlayerStateByShip, FContentPlayerShip
#include "DockQueues.h"           // RemoveFromFormation (FUN_06f67090), SetDockEquipment
#include "Encounters.h"           // FFormation, FindFormation
#include "NpcShip.h"              // FContentNpcShip, GNpcShipEvents
#include "RefHandle.h"            // TRefHandle (LoadObjList's local directive handle)
#include "SystemInfo.h"
#include "ContentStaticIds.h"     // TU40's 16 static-init thunks 06f24670-06f24850
#include "ContentInternal.h"      // SubtractVectors (FUN_06eeaee0)
#include "common_ini.h"
#include "common_server.h"

#include <cstring>

namespace
{
    // The ops declare some fields as integers where the directives store floats (spline
    // points, Field_68, the face point, the zone sphere): the bits are stored as they are.
    template <typename T>
    void StoreFloatBits(T* Destination, float Value)
    {
        static_assert(sizeof(T) == sizeof(float), "4-byte field");
        memcpy(Destination, &Value, sizeof(float));
    }

    // The ships in SystemInfo::SpaceObjs are NPC ships (adders TU43 06f348c5 and TU53
    // 06f60b05, both after FUN_06f336a0); TU40 reads their ship fields at raw offsets.
    FContentNpcShip* AsNpcShip(FContentSpaceObj* Object)
    {
        return static_cast<FContentNpcShip*>(Object);
    }

    // The inlined `FUN_06eea860; null ? 0 : FUN_06f73ac0(Name, ship +0x170)` of every Execute.
    unsigned int FindTargetId(FContentShipBase* Ship, TString<0x30> const* Name)
    {
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            return 0;
        return System->FindObjectIdByName(Name, Ship->ControllerId);
    }

    // The inlined priority default: `op +8 == 0 -> op +8 = FUN_06f57c80(ship)`.
    void DefaultPriority(pub::AI::BaseOp* Op, FContentShipBase* Ship)
    {
        if (Op->Priority == 0)
            Op->Priority = (uint32_t)Ship->GetPriority();
    }

    // The inlined mute before a goto / guide / follow: `ship vtbl[+4]() && !FUN_06f33e80 &&
    // !FUN_06f33ff0 -> FUN_06f68630(&DAT_06fd1bd8, ship, 10.0f)` (DAT_06fb8e24).
    void MuteNpcShip(FContentShipBase* Ship)
    {
        if (!Ship->Slot04())
            return;
        FContentNpcShip* NpcShip = static_cast<FContentNpcShip*>(Ship);
        if (NpcShip->IsSpacePopShip())
            return;
        if (NpcShip->GetField1b1())
            return;
        GNpcShipEvents.StartTimer(NpcShip, 10.0f);
    }

    // The inlined SubmitState to the ship's formation mates (06f25f89-06f25fc9 and the zone
    // directives): every other object of the ship's system with the same +0x154 (skipped
    // when the ship's is -1).
    void SubmitStateToFormation(FContentShipBase* Ship, pub::AI::BaseOp* Op)
    {
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            return;
        for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
             It != System->SpaceObjs.end(); ++It)
        {
            FContentNpcShip* Other = AsNpcShip(*It);
            if (Other->SpaceObjId == Ship->SpaceObjId)
                continue;
            if (Ship->FormationId == 0xffffffffu)
                continue;
            if (Other->FormationId != Ship->FormationId)
                continue;
            pub::AI::SubmitState(Other->SpaceObjId, Op);
        }
    }

    // Value 0 of the goto keys: "goto" leaves both flags, "goto_no_cruise" sets Field_5B,
    // "goto_cruise" Field_5A (stricmp, 06f261b2-06f261f4).
    void ParseGotoKind(char const* Kind, uint8_t* NoCruise, uint8_t* Cruise)
    {
        if (_stricmp(Kind, "goto") == 0)
            return;
        if (_stricmp(Kind, "goto_no_cruise") == 0)
        {
            *NoCruise = 1;
            return;
        }
        if (_stricmp(Kind, "goto_cruise") == 0)
            *Cruise = 1;
    }

    void ClearName(TString<0x30>* Name)
    {
        Name->Length = 0;
        Name->Data[0] = '\0';
    }
}

// ---------------------------------------------------------------------------------------
// Factory and loader
// ---------------------------------------------------------------------------------------

// FUN_06f24870 (06f24870-06f24fe5). Every case: operator new(size), base part (+4 RefCount 1,
// +8 Name.Length 0, +0xc Name[0] 0, vtable 06fb8314; FUN_06f25050 in cases 3-6, 0xa-0x10,
// 0x15), the op's default ctor at +0x10c, the class's own members, its vtable. Sizes: 0 0x120,
// 1 0x11c, 2 0x118, 3 0x1e8, 4/5 0x1b4, 6 0x15c, 7 0x168, 8 0x200, 9 0x234, 0xa 0x188,
// 0xb/0xc 0x110, 0xd/0xe 0x180, 0xf 0x124, 0x10 0x120, 0x11/0x12 0x10c, 0x13 0x16c, 0x14 0x124,
// 0x15 0x124, 0x16 0x13c (FUN_06f25140), 0x19 0x120 (FUN_06f251b0); 0x17, 0x18 and > 0x19 ->
// null. *Out = the object; returns it != null.
bool __cdecl CreateObjListDirective(int Type, FObjListDirective** Out)
{
    FObjListDirective* Directive = nullptr;
    switch (Type)
    {
    case 0x0:  Directive = new FObjListAvoidance(); break;
    case 0x1:  Directive = new FObjListDelay(); break;
    case 0x2:  Directive = new FObjListIdle(); break;
    case 0x3:  Directive = new FObjListGotoShip(); break;
    case 0x4:  Directive = new FObjListGotoSpline(); break;
    case 0x5:  Directive = new FObjListGotoVec(); break;
    case 0x6:  Directive = new FObjListTrail(); break;
    case 0x7:  Directive = new FObjListFollow(); break;
    case 0x8:  Directive = new FObjListGuide(); break;
    case 0x9:  Directive = new FObjListGuideShip(); break;
    case 0xa:  Directive = new FObjListFace(); break;
    case 0xb:  Directive = new FObjListSetLifeTime(); break;
    case 0xc:  Directive = new FObjListSetPriority(); break;
    case 0xd:  Directive = new FObjListStayInRange(); break;
    case 0xe:  Directive = new FObjListStayOutOfRange(); break;
    case 0xf:  Directive = new FObjListMakeNewFormation(); break;
    case 0x10: Directive = new FObjListFollowPlayer(); break;
    case 0x11: Directive = new FObjListBreakFormation(); break;
    case 0x12: Directive = new FObjListCancelObjective(); break;
    case 0x13: Directive = new FObjListDock(); break;
    case 0x14: Directive = new FObjListInstantTradelane(); break;
    case 0x15: Directive = new FObjListLaunch(); break;
    case 0x16: Directive = new FObjListWaitForPlayerManeuver(); break;
    case 0x19: Directive = new FObjListTractorBeam(); break;
    default: break;
    }
    *Out = Directive;
    return Directive != nullptr;
}

namespace
{
    struct FObjListKey
    {
        char const* Key;
        int Type;
    };

    // The is_value chain of FUN_06f25260, in retail order.
    FObjListKey const GObjListKeys[] =
    {
        {"Avoidance", 0x0},
        {"BreakFormation", 0x11},
        {"CancelObjective", 0x12},
        {"Delay", 0x1},
        {"Idle", 0x2},
        {"Dock", 0x13},
        {"InstantTradelane", 0x14},
        {"Follow", 0x7},
        {"Guide", 0x8},
        {"GuideShip", 0x9},
        {"GotoShip", 0x3},
        {"GotoSpline", 0x4},
        {"GotoVec", 0x5},
        {"Face", 0xa},
        {"Launch", 0x15},
        {"MakeNewFormation", 0xf},
        {"FollowPlayer", 0x10},
        {"SetPriority", 0xc},
        {"SetLifeTime", 0xb},
        {"StayInRange", 0xd},
        {"StayOutOfRange", 0xe},
        {"Trail", 0x6},
        {"WaitForPlayerManeuver", 0x16},
    };
}

// FUN_06f25260 (06f25260-06f25f29). *OutNickname = CreateID("") (DAT_06fb572c), then per
// value: "nickname" -> CreateID(value); "system" -> ignored; a directive key -> factory,
// Name = the whole line (FUN_06f0dfa0 / the inlined TString copy: null -> empty, clamped to
// 0xff), vtbl[+0x10] Parse(Reader), then the list takes it: retail inserts at the end
// (FUN_06ecfc30 / FUN_06f1e390 / inline), AddRefs it for the list and releases the creation
// reference, so the list holds the one reference. Unknown keys are skipped.
void __cdecl LoadObjList(INI_Reader* Reader, std::list<FObjListDirective*>* List,
                         unsigned int* OutNickname)
{
    *OutNickname = CreateID("");
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            *OutNickname = CreateID(Reader->get_value_string());
            continue;
        }
        if (Reader->is_value("system"))
            continue;

        int Type = -1;
        for (int Index = 0; Index < static_cast<int>(sizeof(GObjListKeys) / sizeof(GObjListKeys[0]));
             ++Index)
        {
            if (Reader->is_value(GObjListKeys[Index].Key))
            {
                Type = GObjListKeys[Index].Type;
                break;
            }
        }
        if (Type < 0)
            continue;

        // The creation reference lives in a local handle (FUN_06f28e10 in the 23 unwind
        // states of 06f25260); the list takes its own reference.
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(Type, &Directive.Pointer);
        Directive->Name = Reader->get_line_ptr();
        Directive->Parse(Reader);
        List->push_back(Directive);
        Directive->AddRef();
    }
}

// ---------------------------------------------------------------------------------------
// 0 Avoidance
// ---------------------------------------------------------------------------------------

// FUN_06f25f60 (06f25f60-06f25fd8): SubmitState(ship, op) (result ignored), the same to the
// formation mates, FUN_06f57b00; true.
bool FObjListAvoidance::Execute(FContentShipBase* Ship)
{
    pub::AI::SubmitState(Ship->SpaceObjId, &Op);
    SubmitStateToFormation(Ship, &Op);
    Ship->AdvanceDirective();
    return true;
}

// FUN_06f25fe0 (06f25fe0-06f26015): FlagId = 0; bValue = stricmp(value 0, "true") == 0.
void FObjListAvoidance::Parse(INI_Reader* Reader)
{
    Op.FlagId = 0;
    Op.bValue = _stricmp(Reader->get_value_string(0), "true") == 0 ? 1 : 0;
}

// 06ea1450: XOR EAX,EAX.
int FObjListAvoidance::GetType()
{
    return 0;
}

// FUN_06f26020 (06f26020-06f26034).
void FObjListAvoidance::SetAvoidance(bool bAvoid)
{
    Op.FlagId = 0;
    Op.bValue = bAvoid ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// 1 Delay, 2 Idle
// ---------------------------------------------------------------------------------------

// FUN_06f27900 (06f27900-06f2792f): priority default, FUN_06f57c60.
bool FObjListDelay::Execute(FContentShipBase* Ship)
{
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f27950 (06f27950-06f27966): DelaySeconds = float value 0.
void FObjListDelay::Parse(INI_Reader* Reader)
{
    Op.DelaySeconds = Reader->get_value_float(0);
}

// 06ee9e10: MOV EAX,1.
int FObjListDelay::GetType()
{
    return 1;
}

// FUN_06f27940 (06f27940-06f2794a).
void FObjListDelay::SetDelay(float Seconds)
{
    Op.DelaySeconds = Seconds;
}

// FUN_06f27900 (the Delay body, ICF-folded).
bool FObjListIdle::Execute(FContentShipBase* Ship)
{
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// 06ee9660: MOV EAX,2.
int FObjListIdle::GetType()
{
    return 2;
}

// ---------------------------------------------------------------------------------------
// 3 GotoShip, 4 GotoSpline, 5 GotoVec
// ---------------------------------------------------------------------------------------

namespace
{
    // FUN_06f263a0 (06f263a0-06f26451), the Execute of GotoSpline and GotoVec: the object
    // name, when set, gives Field_5C (a miss finishes the directive: FUN_06f57b00, true);
    // priority default, mute, FUN_06f57c60.
    bool ExecuteGotoToObject(FContentShipBase* Ship, pub::AI::DirectiveGotoOp* Op,
                             TString<0x30> const* ObjectName)
    {
        if (ObjectName->Length != 0)
        {
            unsigned int Id = FindTargetId(Ship, ObjectName);
            Op->Field_5C = Id;
            if ((Id & 0x3fffffff) == 0)
            {
                Ship->AdvanceDirective();
                return true;
            }
        }
        DefaultPriority(Op, Ship);
        MuteNpcShip(Ship);
        return Ship->SubmitDirective(Op);
    }
}

// Factory case 3 (06f24bd2-06f24c2b): +0x180 / +0x1b4 names emptied.
FObjListGotoShip::FObjListGotoShip()
{
    ClearName(&TargetName);
    ClearName(&ObjectName);
}

// FUN_06f26040 (06f26040-06f2613e): TargetName -> Field_1C, ObjectName -> Field_5C (each
// only when set; a miss finishes the directive), priority default, mute, FUN_06f57c60.
bool FObjListGotoShip::Execute(FContentShipBase* Ship)
{
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.Field_1C = static_cast<int32_t>(Id);
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    if (ObjectName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &ObjectName);
        Op.Field_5C = Id;
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f26190 (06f26190-06f2638e).
void FObjListGotoShip::Parse(INI_Reader* Reader)
{
    Field_0x17c = 1;
    ParseGotoKind(Reader->get_value_string(0), &Op.Field_5B, &Op.Field_5A);
    Op.Field_0C = 0;
    TargetName = Reader->get_value_string(1);
    Op.Field_50 = Reader->get_value_float(2);
    Op.Field_58 = _stricmp(Reader->get_value_string(3), "true") == 0 ? 1 : 0;
    if (!Reader->is_value_empty(4))
        Op.Field_54 = static_cast<float>(Reader->get_value_int(4));
    if (Reader->is_value_empty(5))
        return;
    ObjectName = Reader->get_value_string(5);
    Op.Field_60 = static_cast<float>(Reader->get_value_int(6));
    Op.Field_64 = static_cast<float>(Reader->get_value_int(7));
    if (!Reader->is_value_empty(8))
        StoreFloatBits(&Op.Field_68, Reader->get_value_float(8));
    if (!Reader->is_value_empty(9))
        Field_0x17c = Reader->get_value_int(9);
}

// 06f5e540: MOV EAX,3.
int FObjListGotoShip::GetType()
{
    return 3;
}

// FUN_06f26150 (06f26150-06f2617e).
void FObjListGotoShip::SetTarget(unsigned int const* TargetId, float Range, float Param3)
{
    Op.Field_0C = 0;
    Op.Field_1C = static_cast<int32_t>(*TargetId);
    Op.Field_58 = 0;
    Op.Field_50 = Range;
    Op.Field_54 = Param3;
}

// Factory case 4 (06f24c32-06f24c7f): +0x180 name emptied.
FObjListGotoSpline::FObjListGotoSpline()
{
    ClearName(&ObjectName);
}

bool FObjListGotoSpline::Execute(FContentShipBase* Ship)
{
    return ExecuteGotoToObject(Ship, &Op, &ObjectName);
}

// FUN_06f26e60 (06f26e60-06f27141). Field_0C = 2 (a spline); the four points come in the
// value order 2, 0, 1, 3 (values 1-3 -> +0x144, 4-6 -> +0x12c, 7-9 -> +0x138, 10-12 ->
// +0x150); Field_50 is a float here.
void FObjListGotoSpline::Parse(INI_Reader* Reader)
{
    Field_0x17c = 1;
    ParseGotoKind(Reader->get_value_string(0), &Op.Field_5B, &Op.Field_5A);
    Op.Field_0C = 2;

    StoreFloatBits(&Op.Field_38, Reader->get_value_float(1));
    StoreFloatBits(&Op.Field_3C, Reader->get_value_float(2));
    StoreFloatBits(&Op.Field_40, Reader->get_value_float(3));
    StoreFloatBits(&Op.Field_20, Reader->get_value_float(4));
    StoreFloatBits(&Op.Field_24, Reader->get_value_float(5));
    StoreFloatBits(&Op.Field_28, Reader->get_value_float(6));
    StoreFloatBits(&Op.Field_2C, Reader->get_value_float(7));
    StoreFloatBits(&Op.Field_30, Reader->get_value_float(8));
    StoreFloatBits(&Op.Field_34, Reader->get_value_float(9));
    StoreFloatBits(&Op.Field_44, Reader->get_value_float(10));
    StoreFloatBits(&Op.Field_48, Reader->get_value_float(11));
    StoreFloatBits(&Op.Field_4C, Reader->get_value_float(12));

    Op.Field_50 = Reader->get_value_float(13);
    Op.Field_58 = Reader->get_value_bool(14) ? 1 : 0;
    if (!Reader->is_value_empty(15))
        Op.Field_54 = static_cast<float>(Reader->get_value_int(15));
    if (Reader->is_value_empty(16))
        return;
    ObjectName = Reader->get_value_string(16);
    Op.Field_60 = static_cast<float>(Reader->get_value_int(17));
    Op.Field_64 = static_cast<float>(Reader->get_value_int(18));
    if (!Reader->is_value_empty(19))
        StoreFloatBits(&Op.Field_68, Reader->get_value_float(19));
    if (!Reader->is_value_empty(20))
        Field_0x17c = Reader->get_value_int(20);
}

// 06f25080: MOV EAX,4.
int FObjListGotoSpline::GetType()
{
    return 4;
}

// Factory case 5 (06f24c86-06f24cd3): +0x180 name emptied.
FObjListGotoVec::FObjListGotoVec()
{
    ClearName(&ObjectName);
}

bool FObjListGotoVec::Execute(FContentShipBase* Ship)
{
    return ExecuteGotoToObject(Ship, &Op, &ObjectName);
}

// FUN_06f264d0 (06f264d0-06f2669d). Field_0C = 1 (a point); the point and Field_50 are ints.
void FObjListGotoVec::Parse(INI_Reader* Reader)
{
    Field_0x17c = 1;
    ParseGotoKind(Reader->get_value_string(0), &Op.Field_5B, &Op.Field_5A);
    Op.Field_0C = 1;
    Op.Destination.x = static_cast<float>(Reader->get_value_int(1));
    Op.Destination.y = static_cast<float>(Reader->get_value_int(2));
    Op.Destination.z = static_cast<float>(Reader->get_value_int(3));
    Op.Field_50 = static_cast<float>(Reader->get_value_int(4));
    Op.Field_58 = Reader->get_value_bool(5) ? 1 : 0;
    if (!Reader->is_value_empty(6))
        Op.Field_54 = static_cast<float>(Reader->get_value_int(6));
    if (Reader->is_value_empty(7))
        return;
    ObjectName = Reader->get_value_string(7);
    Op.Field_60 = static_cast<float>(Reader->get_value_int(8));
    Op.Field_64 = static_cast<float>(Reader->get_value_int(9));
    if (!Reader->is_value_empty(10))
        StoreFloatBits(&Op.Field_68, Reader->get_value_float(10));
    if (!Reader->is_value_empty(11))
        Field_0x17c = Reader->get_value_int(11);
}

// 06ee99a0: MOV EAX,5.
int FObjListGotoVec::GetType()
{
    return 5;
}

// FUN_06f26460 (06f26460-06f264c6).
void FObjListGotoVec::SetDestination(Vector const* Destination, float Field50, float Field54,
                                     unsigned int ObjectId, bool bNoCruise, bool bCruise)
{
    Op.Field_0C = 1;
    Op.Destination = *Destination;
    Op.Field_50 = Field50;
    Op.Field_54 = Field54;
    Op.Field_58 = 0;
    Op.Field_5A = bCruise ? 1 : 0;
    if ((ObjectId & 0x3fffffff) != 0)
        Op.Field_5C = ObjectId;
    Op.Field_5B = bNoCruise ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// 6 Trail, 7 Follow
// ---------------------------------------------------------------------------------------

// Factory case 6 (06f24f2b-06f24f74): +0x128 name emptied.
FObjListTrail::FObjListTrail()
{
    ClearName(&TargetName);
}

// LAB_06f27520 (06f27520-06f275a5): vtbl[+4] (result unused), TargetName -> TargetId (a miss
// finishes the directive), priority default, FUN_06f57c60.
bool FObjListTrail::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = Id;
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f275d0 (06f275d0-06f2764f).
void FObjListTrail::Parse(INI_Reader* Reader)
{
    TargetName = Reader->get_value_string(0);
    Op.TrailDistance = static_cast<float>(Reader->get_value_int(1));
}

// 06f250c0: MOV EAX,6.
int FObjListTrail::GetType()
{
    return 6;
}

// FUN_06f275b0 (06f275b0-06f275c6).
void FObjListTrail::SetTarget(unsigned int const* TargetId, float Distance)
{
    Op.TargetId = *TargetId;
    Op.TrailDistance = Distance;
}

// Factory case 7 (06f24aa6-06f24aff): +0x110 name emptied.
FObjListFollow::FObjListFollow()
{
    ClearName(&TargetName);
}

// LAB_06f27340 (06f27340-06f273f1): TargetName -> TargetId (a miss finishes the directive),
// priority default, mute, FUN_06f57c60.
bool FObjListFollow::Execute(FContentShipBase* Ship)
{
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = Id;
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f27400 (06f27400-06f27512). FollowDistance = int value 1; exactly -1.0 becomes
// 2500.0 (DAT_06fb8e2c; `FCOMP -1.0; TEST AH,0x44; JP` skips unless equal).
void FObjListFollow::Parse(INI_Reader* Reader)
{
    Field_0x10c = 1;
    TargetName = Reader->get_value_string(0);
    Op.FollowDistance = static_cast<float>(Reader->get_value_int(1));
    if (Op.FollowDistance == -1.0f)
        Op.FollowDistance = 2500.0f;
    if (!Reader->is_value_empty(2))
    {
        Op.Field_14 = static_cast<float>(Reader->get_value_int(2));
        Op.Field_18 = static_cast<float>(Reader->get_value_int(3));
        Op.Field_1C = static_cast<float>(Reader->get_value_int(4));
    }
    if (!Reader->is_value_empty(5))
        Op.Field_20 = static_cast<float>(Reader->get_value_int(5));
}

// 06f250d0: MOV EAX,7.
int FObjListFollow::GetType()
{
    return 7;
}

// ---------------------------------------------------------------------------------------
// 8 Guide, 9 GuideShip
// ---------------------------------------------------------------------------------------

// Factory case 8 (06f24b06-06f24b5f): +0x1cc name emptied.
FObjListGuide::FObjListGuide()
{
    ClearName(&TargetName);
}

// LAB_06f266a0 (06f266a0-06f26763): vtbl[+4] (result unused); TargetName -> GotoParams
// Field_5C (a miss finishes the directive); FollowParams.TargetId = Field_5C; priority
// default, mute, FUN_06f57c60.
bool FObjListGuide::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.GotoParams.Field_5C = Id;
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    Op.FollowParams.TargetId = Op.GotoParams.Field_5C;
    DefaultPriority(&Op, Ship);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f26810 (06f26810-06f26a8b).
void FObjListGuide::Parse(INI_Reader* Reader)
{
    Field_0x1c8 = 1;
    TargetName = Reader->get_value_string(0);
    Op.FollowParams.Field_14 = static_cast<float>(Reader->get_value_int(1));
    Op.FollowParams.Field_18 = static_cast<float>(Reader->get_value_int(2));
    Op.FollowParams.Field_1C = static_cast<float>(Reader->get_value_int(3));
    Op.GotoParams.Field_0C = 1;
    ParseGotoKind(Reader->get_value_string(4), &Op.GotoParams.Field_5B, &Op.GotoParams.Field_5A);
    Op.GotoParams.Destination.x = static_cast<float>(Reader->get_value_int(5));
    Op.GotoParams.Destination.y = static_cast<float>(Reader->get_value_int(6));
    Op.GotoParams.Destination.z = static_cast<float>(Reader->get_value_int(7));
    Op.GotoParams.Field_50 = static_cast<float>(Reader->get_value_int(8));
    Op.GotoParams.Field_58 = Reader->get_value_bool(9) ? 1 : 0;
    Op.GotoParams.Field_54 = static_cast<float>(Reader->get_value_int(10));
    Op.GotoParams.Field_60 = static_cast<float>(Reader->get_value_int(11));
    Op.GotoParams.Field_64 = static_cast<float>(Reader->get_value_int(12));
    if (!Reader->is_value_empty(13))
        Op.Field_A0 = Reader->get_value_bool(13) ? 1 : 0;
    if (!Reader->is_value_empty(14))
        Op.Field_AC = Reader->get_value_int(14);
    if (!Reader->is_value_empty(15))
        Op.Field_A4 = Reader->get_value_float(15);
    if (!Reader->is_value_empty(16))
        Op.Field_B0 = Reader->get_value_float(16);
    if (!Reader->is_value_empty(17))
        Op.Field_B4 = Reader->get_value_float(17);
    if (!Reader->is_value_empty(18))
        Op.Field_A8 = Reader->get_value_float(18);
    if (!Reader->is_value_empty(19))
        Field_0x1c8 = Reader->get_value_int(19);
}

// 06ee99a0: MOV EAX,5.
int FObjListGuide::GetType()
{
    return 5;
}

// FUN_06f26770 (06f26770-06f2680a): Field_0C = 1, the destination, Field_50, Field_58,
// Field_5C, the cruise flags, Field_60 = 200.0, Field_64 = 500.0; FollowParams target and
// offset; Field_A0.
void FObjListGuide::SetGuide(unsigned int const* TargetId, Vector const* FollowOffset,
                             Vector const* Destination, float Field50, bool bNoCruise,
                             bool bCruise, bool FieldA0, bool Field58)
{
    Op.GotoParams.Field_0C = 1;
    Op.GotoParams.Destination = *Destination;
    Op.GotoParams.Field_58 = Field58 ? 1 : 0;
    Op.GotoParams.Field_50 = Field50;
    Op.GotoParams.Field_5C = *TargetId;
    Op.GotoParams.Field_5B = bNoCruise ? 1 : 0;
    Op.GotoParams.Field_5A = bCruise ? 1 : 0;
    Op.GotoParams.Field_60 = 200.0f;
    Op.GotoParams.Field_64 = 500.0f;
    Op.FollowParams.TargetId = *TargetId;
    Op.FollowParams.Field_14 = FollowOffset->x;
    Op.FollowParams.Field_18 = FollowOffset->y;
    Op.FollowParams.Field_1C = FollowOffset->z;
    Op.Field_A0 = FieldA0 ? 1 : 0;
}

// Factory case 9 (06f24b66-06f24bcb): +0x1cc and +0x200 names emptied.
FObjListGuideShip::FObjListGuideShip()
{
    ClearName(&ObjectName);
    ClearName(&TargetName);
}

// LAB_06f26a90 (06f26a90-06f26ba9): vtbl[+4] (result unused); TargetName (+0x200) ->
// GotoParams.Field_5C, ObjectName (+0x1cc) -> GotoParams.Field_1C, no Length test (an empty
// name resolves to 0); either miss finishes the directive (true). FollowParams.TargetId =
// Field_5C; priority default; GetLocation of Field_1C into locals that nothing reads;
// mute; FUN_06f57c60.
bool FObjListGuideShip::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    unsigned int TargetId = FindTargetId(Ship, &TargetName);
    Op.GotoParams.Field_5C = TargetId;
    if ((TargetId & 0x3fffffff) == 0)
    {
        Ship->AdvanceDirective();
        return true;
    }
    unsigned int ObjectId = FindTargetId(Ship, &ObjectName);
    Op.GotoParams.Field_1C = static_cast<int32_t>(ObjectId);
    if ((ObjectId & 0x3fffffff) == 0)
    {
        Ship->AdvanceDirective();
        return true;
    }
    Op.FollowParams.TargetId = Op.GotoParams.Field_5C;
    DefaultPriority(&Op, Ship);
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(static_cast<unsigned int>(Op.GotoParams.Field_1C), Position,
                               Orientation);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f26bb0 (06f26bb0-06f26e51). Field_0C = 0 (the goto target is an object).
void FObjListGuideShip::Parse(INI_Reader* Reader)
{
    Field_0x1c8 = 1;
    TargetName = Reader->get_value_string(0);
    Op.FollowParams.Field_14 = static_cast<float>(Reader->get_value_int(1));
    Op.FollowParams.Field_18 = static_cast<float>(Reader->get_value_int(2));
    Op.FollowParams.Field_1C = static_cast<float>(Reader->get_value_int(3));
    Op.GotoParams.Field_0C = 0;
    ParseGotoKind(Reader->get_value_string(4), &Op.GotoParams.Field_5B, &Op.GotoParams.Field_5A);
    ObjectName = Reader->get_value_string(5);
    Op.GotoParams.Field_50 = static_cast<float>(Reader->get_value_int(6));
    Op.GotoParams.Field_58 = Reader->get_value_bool(7) ? 1 : 0;
    Op.GotoParams.Field_54 = static_cast<float>(Reader->get_value_int(8));
    Op.GotoParams.Field_60 = static_cast<float>(Reader->get_value_int(9));
    Op.GotoParams.Field_64 = static_cast<float>(Reader->get_value_int(10));
    if (!Reader->is_value_empty(11))
        Op.Field_A0 = Reader->get_value_bool(11) ? 1 : 0;
    if (!Reader->is_value_empty(12))
        Op.Field_AC = Reader->get_value_int(12);
    if (!Reader->is_value_empty(13))
        Op.Field_A4 = Reader->get_value_float(13);
    if (!Reader->is_value_empty(14))
        Op.Field_B0 = Reader->get_value_float(14);
    if (!Reader->is_value_empty(15))
        Op.Field_B4 = Reader->get_value_float(15);
    if (!Reader->is_value_empty(16))
        Op.Field_A8 = Reader->get_value_float(16);
    if (!Reader->is_value_empty(17))
        Field_0x1c8 = Reader->get_value_int(17);
}

// 06ee99a0: MOV EAX,5.
int FObjListGuideShip::GetType()
{
    return 5;
}

// ---------------------------------------------------------------------------------------
// 0xa Face
// ---------------------------------------------------------------------------------------

// Factory case 0xa (06f24cda-06f24d27): +0x154 name emptied.
FObjListFace::FObjListFace()
{
    ClearName(&TargetName);
}

// LAB_06f27150 (06f27150-06f271de): vtbl[+4] (result unused); an object face (Mode 1)
// with a name resolves TargetId (a miss finishes the directive); priority default,
// FUN_06f57c60.
bool FObjListFace::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    if (Op.Mode == 1 && TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = Id;
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f271f0 (06f271f0-06f2733b): value 0 "OBJECT" -> Mode 1, "POINT" -> 2 (stricmp,
// otherwise unchanged); OBJECT: name = value 1, Field_44 = bool value 2 when present;
// POINT: FacePoint = float values 1-3, Field_44 = bool value 4 when present.
void FObjListFace::Parse(INI_Reader* Reader)
{
    if (_stricmp(Reader->get_value_string(0), "OBJECT") == 0)
        Op.Mode = 1;
    else if (_stricmp(Reader->get_value_string(0), "POINT") == 0)
        Op.Mode = 2;

    if (Op.Mode == 1)
    {
        TargetName = Reader->get_value_string(1);
        if (!Reader->is_value_empty(2))
            Op.Field_44 = Reader->get_value_bool(2) ? 1 : 0;
        return;
    }
    if (Op.Mode != 2)
        return;
    Op.FacePoint.x = Reader->get_value_float(1);
    Op.FacePoint.y = Reader->get_value_float(2);
    Op.FacePoint.z = Reader->get_value_float(3);
    if (!Reader->is_value_empty(4))
        Op.Field_44 = Reader->get_value_bool(4) ? 1 : 0;
}

// 06f25070: MOV EAX,0xa.
int FObjListFace::GetType()
{
    return 0xa;
}

// ---------------------------------------------------------------------------------------
// 0xb SetLifeTime, 0xc SetPriority
// ---------------------------------------------------------------------------------------

// LAB_06f282e0 (06f282e0-06f2837f): vtbl[+4] twice (the first result unused); an NPC ship
// gets +0x1bc = +0x1c8 = (float)LifeTime, and so do its formation mates; FUN_06f57b00; true.
bool FObjListSetLifeTime::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    if (Ship->Slot04())
    {
        FContentNpcShip* NpcShip = static_cast<FContentNpcShip*>(Ship);
        NpcShip->Field_0x1bc = static_cast<float>(LifeTime);
        NpcShip->Field_0x1c8 = static_cast<float>(LifeTime);
    }
    FSystemInfo* System = Ship->GetSystem();
    if (System != nullptr)
    {
        for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
             It != System->SpaceObjs.end(); ++It)
        {
            FContentNpcShip* Other = AsNpcShip(*It);
            if (Ship->SpaceObjId == Other->SpaceObjId)
                continue;
            if (Ship->FormationId == 0xffffffffu)
                continue;
            if (Other->FormationId != Ship->FormationId)
                continue;
            Other->Field_0x1bc = static_cast<float>(LifeTime);
            Other->Field_0x1c8 = static_cast<float>(LifeTime);
        }
    }
    Ship->AdvanceDirective();
    return true;
}

// FUN_06f28390 (06f28390-06f283a6).
void FObjListSetLifeTime::Parse(INI_Reader* Reader)
{
    LifeTime = Reader->get_value_int(0);
}

// 06f250e0: MOV EAX,0xb.
int FObjListSetLifeTime::GetType()
{
    return 0xb;
}

// FUN_06f28260 (06f28260-06f2827d): FUN_06f57c90(&Priority), FUN_06f57b00, true.
bool FObjListSetPriority::Execute(FContentShipBase* Ship)
{
    Ship->SetPriority(&Priority);
    Ship->AdvanceDirective();
    return true;
}

// FUN_06f28280 (06f28280-06f282db): "NORMAL" -> 1, "ALWAYS_EXECUTE" -> 0, else unchanged.
void FObjListSetPriority::Parse(INI_Reader* Reader)
{
    if (_stricmp(Reader->get_value_string(0), "NORMAL") == 0)
    {
        Priority = 1;
        return;
    }
    if (_stricmp(Reader->get_value_string(0), "ALWAYS_EXECUTE") == 0)
        Priority = 0;
}

// 06f250b0: MOV EAX,0xc.
int FObjListSetPriority::GetType()
{
    return 0xc;
}

// ---------------------------------------------------------------------------------------
// 0xd StayInRange, 0xe StayOutOfRange
// ---------------------------------------------------------------------------------------

namespace
{
    // LAB_06f27cb0 / LAB_06f27fa0 (identical bodies). Mode == 1 calls FUN_06eeaee0
    // (06f27cdb / 06f27fcb) on the op's two box corners (BoxMax minus BoxMin, i.e.
    // directive +0x13c - +0x130) into a local nothing reads. The object name, when set, gives
    // TargetId; a miss finishes the directive. Priority default; SubmitState on the ship and
    // its formation mates; FUN_06f57b00; true.
    bool ExecuteZone(FContentShipBase* Ship, pub::AI::SetZoneBehaviorParams* Op,
                     TString<0x30> const* ObjectName)
    {
        if (Op->Mode == 1)
        {
            Vector const Unused = SubtractVectors(Op->BoxMax, Op->BoxMin);
            (void)Unused;
        }
        if (ObjectName->Length != 0)
        {
            unsigned int Id = FindTargetId(Ship, ObjectName);
            Op->TargetId = Id;
            if ((Id & 0x3fffffff) == 0)
            {
                Ship->AdvanceDirective();
                return true;
            }
        }
        DefaultPriority(Op, Ship);
        pub::AI::SubmitState(Ship->SpaceObjId, Op);
        SubmitStateToFormation(Ship, Op);
        Ship->AdvanceDirective();
        return true;
    }

    // LAB_06f27df0 / LAB_06f280a0 after their first store (InvertMode = 0 / 1). Six values
    // (value 5 present): Mode = 1 and six floats at op +0x24..+0x38 (BoxMin, BoxMax). Then
    // value 3 present: Mode = 0, three ints -> TargetPoint and one -> Radius;
    // absent: Mode = 2, a name -> ObjectName and an int -> Radius. The value after
    // them, when present and false, sets InvertMode = 2.
    void ParseZone(INI_Reader* Reader, pub::AI::SetZoneBehaviorParams* Op,
                   TString<0x30>* ObjectName)
    {
        unsigned int Index = 0;
        if (!Reader->is_value_empty(5))
        {
            Op->Mode = 1;
            Op->BoxMin.x = Reader->get_value_float(0);
            Op->BoxMin.y = Reader->get_value_float(1);
            Op->BoxMin.z = Reader->get_value_float(2);
            Op->BoxMax.x = Reader->get_value_float(3);
            Op->BoxMax.y = Reader->get_value_float(4);
            Op->BoxMax.z = Reader->get_value_float(5);
            Index = 6;
        }
        if (!Reader->is_value_empty(3))
        {
            Op->Mode = 0;
            Op->TargetPoint.x = (float)Reader->get_value_int(Index);
            Op->TargetPoint.y = (float)Reader->get_value_int(Index + 1);
            Op->TargetPoint.z = (float)Reader->get_value_int(Index + 2);
            Op->Radius = (float)Reader->get_value_int(Index + 3);
            Index += 4;
        }
        else
        {
            Op->Mode = 2;
            *ObjectName = Reader->get_value_string(Index);
            Op->Radius = (float)Reader->get_value_int(Index + 1);
            Index += 2;
        }
        if (!Reader->is_value_empty(Index) && !Reader->get_value_bool(Index))
            Op->InvertMode = 2;
    }
}

// Factory case 0xd (06f24e83-06f24ed0): +0x14c name emptied.
FObjListStayInRange::FObjListStayInRange()
{
    ClearName(&ObjectName);
}

bool FObjListStayInRange::Execute(FContentShipBase* Ship)
{
    return ExecuteZone(Ship, &Op, &ObjectName);
}

void FObjListStayInRange::Parse(INI_Reader* Reader)
{
    Op.InvertMode = 0;
    ParseZone(Reader, &Op, &ObjectName);
}

// 06f250f0: MOV EAX,0xd.
int FObjListStayInRange::GetType()
{
    return 0xd;
}

// FUN_06f27db0 (06f27db0-06f27de4).
void FObjListStayInRange::SetSphere(Vector const* Center, float Radius)
{
    Op.TargetPoint.x = Center->x;
    Op.TargetPoint.y = Center->y;
    Op.TargetPoint.z = Center->z;
    Op.Radius = Radius;
    Op.Mode = 0;
    Op.InvertMode = 0;
}

// Factory case 0xe (06f24ed7-06f24f24): +0x14c name emptied.
FObjListStayOutOfRange::FObjListStayOutOfRange()
{
    ClearName(&ObjectName);
}

bool FObjListStayOutOfRange::Execute(FContentShipBase* Ship)
{
    return ExecuteZone(Ship, &Op, &ObjectName);
}

void FObjListStayOutOfRange::Parse(INI_Reader* Reader)
{
    Op.InvertMode = 1;
    ParseZone(Reader, &Op, &ObjectName);
}

// 06f25100: MOV EAX,0xe.
int FObjListStayOutOfRange::GetType()
{
    return 0xe;
}

// ---------------------------------------------------------------------------------------
// 0xf MakeNewFormation, 0x10 FollowPlayer, 0x11 BreakFormation, 0x12 CancelObjective
// ---------------------------------------------------------------------------------------

// LAB_06f283b0 (06f283b0-06f28555): vtbl[+4] (result unused); a local list of ships; with
// a system: the ship, then every named object found by nickname (FUN_06f739c0) that is not
// the ship, after FUN_06f57740 on it; FUN_06f66ec0(&list, Formation, bAddFollowers).
// FUN_06f57b00 either way; true.
bool FObjListMakeNewFormation::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    std::list<FContentShipBase*> Members;
    FSystemInfo* System = Ship->GetSystem();
    if (System != nullptr)
    {
        Members.push_back(Ship);
        for (std::vector<TString<0x30>>::iterator It = MemberNames.begin();
             It != MemberNames.end(); ++It)
        {
            FContentSpaceObj* Object = System->FindSpaceObjByNickname(It->Data);
            if (Object == nullptr || Ship == nullptr)
                continue;
            if (Object->SpaceObjId == Ship->SpaceObjId)
                continue;
            FContentNpcShip* Member = AsNpcShip(Object);
            Member->CancelDirectives();
            Members.push_back(Member);
        }
        FormFormation(&Members, Formation, bAddFollowers);
    }
    Ship->AdvanceDirective();
    return true;
}

// LAB_06f28560 (06f28560-06f28831): the formation by CreateID(value 0) (FUN_06efd1a0); none
// -> stop. FUN_06efcaf0 / FUN_06efcac0 are called and their counts dropped. Then from value 1
// until an empty value: "ADD_FOLLOWERS" sets the flag, anything else (also "Player") is
// appended to the names.
void FObjListMakeNewFormation::Parse(INI_Reader* Reader)
{
    bAddFollowers = false;
    unsigned int FormationId = CreateID(Reader->get_value_string(0));
    Formation = FindFormation(&FormationId);
    if (Formation == nullptr)
        return;
    (void)Formation->CountShipSlots();
    (void)Formation->CountPlayerSlots();
    for (unsigned int Index = 1; !Reader->is_value_empty(Index); ++Index)
    {
        if (_stricmp(Reader->get_value_string(Index), "Player") != 0 &&
            _stricmp(Reader->get_value_string(Index), "ADD_FOLLOWERS") == 0)
        {
            bAddFollowers = true;
            continue;
        }
        TString<0x30> Name;
        Name = Reader->get_value_string(Index);
        MemberNames.push_back(Name);
    }
}

// 06f25110: MOV EAX,0xf.
int FObjListMakeNewFormation::GetType()
{
    return 0xf;
}

// FUN_06f28840 (06f28840-06f28ad2). vtbl[+4] (result unused); retail also builds a local list
// (head 06f2887f) holding only the ship (FUN_06f1e390 at 06f288b6) that nothing reads, freed
// after FUN_06f57b00 (06f28a57-06f28ab4; unwind state 0, funclet 06fae450): the port holds it
// as the same local (RE rule 13). The leader slot is the last
// slot when FUN_06efccd0 says it is the player's (no range check: an empty formation reads
// slot -1), else -1. Each named object (FUN_06f739c0 on the ship's system, not null-checked)
// that is not the ship: out of its old formation first (slot +0x14c != 0 and group +0x154
// != -1 -> FUN_06f67090); a DirectiveFollowOp on the ship at 1000.0 with the offset
// slot(n) - slot(leader) (FUN_06efcb20; n counts the members found); FUN_06f57740, priority
// default, FUN_06f57c60 -> the result. FUN_06f57b00; returns the last SubmitDirective result
// (true without members).
bool FObjListFollowPlayer::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    bool bResult = true;
    std::list<FContentShipBase*> UnusedShips;                           // 06f2887f
    FSystemInfo* System = Ship->GetSystem();
    UnusedShips.push_back(Ship);                                        // 06f288b6

    int SlotCount = static_cast<int>(Formation->Slots.size());
    int LeaderSlot = -1;
    if (Formation->IsPlayerSlot(SlotCount - 1))
        LeaderSlot = SlotCount - 1;

    unsigned int MemberSlot = 0;
    for (std::vector<TString<0x30>>::iterator It = MemberNames.begin(); It != MemberNames.end();
         ++It)
    {
        FContentSpaceObj* Object = System->FindSpaceObjByNickname(It->Data);
        if (Object == nullptr || Ship == nullptr)
            continue;
        if (Object->SpaceObjId == Ship->SpaceObjId)
            continue;
        FContentNpcShip* Member = AsNpcShip(Object);
        if (Member->FormationSlot != 0 && Member->FormationId != 0xffffffffu)
            RemoveFromFormation(Member);

        pub::AI::DirectiveFollowOp FollowOp;
        FollowOp.TargetId = Ship->SpaceObjId;
        FollowOp.FollowDistance = 1000.0f;
        FVector LeaderPosition = Formation->GetSlotPosition(static_cast<unsigned int>(LeaderSlot));
        FVector MemberPosition = Formation->GetSlotPosition(MemberSlot);
        ++MemberSlot;
        FollowOp.Field_14 = MemberPosition.X - LeaderPosition.X;
        FollowOp.Field_18 = MemberPosition.Y - LeaderPosition.Y;
        FollowOp.Field_1C = MemberPosition.Z - LeaderPosition.Z;

        Member->CancelDirectives();
        DefaultPriority(&FollowOp, Member);
        bResult = Member->SubmitDirective(&FollowOp);
    }
    Ship->AdvanceDirective();
    return bResult;
}

// LAB_06f28ae0 (06f28ae0-06f28d68): the formation by CreateID(value 0); none, or no slots ->
// stop. Values 1 .. slot count + 1 that are present are appended to the names.
void FObjListFollowPlayer::Parse(INI_Reader* Reader)
{
    unsigned int FormationId = CreateID(Reader->get_value_string(0));
    Formation = FindFormation(&FormationId);
    if (Formation == nullptr || Formation->Slots.empty())
        return;
    int Last = static_cast<int>(Formation->Slots.size()) + 1;
    for (int Index = 1; Index <= Last; ++Index)
    {
        if (Reader->is_value_empty(static_cast<unsigned int>(Index)))
            continue;
        TString<0x30> Name;
        Name = Reader->get_value_string(static_cast<unsigned int>(Index));
        MemberNames.push_back(Name);
    }
}

// 06f25110: MOV EAX,0xf.
int FObjListFollowPlayer::GetType()
{
    return 0xf;
}

// FUN_06f28d70 (06f28d70-06f28d88): FUN_06f67090(ship), FUN_06f57b00, true.
bool FObjListBreakFormation::Execute(FContentShipBase* Ship)
{
    RemoveFromFormation(Ship);
    Ship->AdvanceDirective();
    return true;
}

// 06f25120: MOV EAX,0x11.
int FObjListBreakFormation::GetType()
{
    return 0x11;
}

// FUN_06f28d90 (06f28d90-06f28dc5): outside a formation (+0x154 == -1) FUN_06f57740, else
// FUN_06f67090(ship); FUN_06f57b00; true.
bool FObjListCancelObjective::Execute(FContentShipBase* Ship)
{
    if (Ship->FormationId == 0xffffffffu)
        Ship->CancelDirectives();
    else
        RemoveFromFormation(Ship);
    Ship->AdvanceDirective();
    return true;
}

// 06f25130: MOV EAX,0x12.
int FObjListCancelObjective::GetType()
{
    return 0x12;
}

// ---------------------------------------------------------------------------------------
// 0x13 Dock, 0x14 InstantTradelane, 0x15 Launch
// ---------------------------------------------------------------------------------------

// Factory case 0x13 (06f249f2-06f24a4b): +0x138 name emptied.
FObjListDock::FObjListDock()
{
    ClearName(&ObjectName);
}

// FUN_06f27660 (06f27660-06f276de): ObjectName -> Field_1C (a miss finishes the directive);
// priority default; FUN_06f57c60.
bool FObjListDock::Execute(FContentShipBase* Ship)
{
    if (ObjectName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &ObjectName);
        Op.Field_1C = static_cast<int32_t>(Id);
        if ((Id & 0x3fffffff) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f27710 (06f27710-06f27823): DockObjectId = GetNicknameId(value 0); no value 1 ->
// Field_10 = 0; "NONE" -> 0 (the stricmp result), else GetNicknameId(value 1); no value 2 ->
// stop; ObjectName = value 2, Field_20 / Field_24 = int values 3 / 4.
void FObjListDock::Parse(INI_Reader* Reader)
{
    Op.DockObjectId = pub::GetNicknameId(Reader->get_value_string(0));
    if (Reader->is_value_empty(1))
    {
        Op.Field_10 = 0;
        return;
    }
    if (_stricmp(Reader->get_value_string(1), "NONE") == 0)
        Op.Field_10 = 0;
    else
        Op.Field_10 = static_cast<int32_t>(pub::GetNicknameId(Reader->get_value_string(1)));
    if (Reader->is_value_empty(2))
        return;
    ObjectName = Reader->get_value_string(2);
    Op.Field_20 = static_cast<float>(Reader->get_value_int(3));
    Op.Field_24 = static_cast<float>(Reader->get_value_int(4));
}

// 06f250a0: MOV EAX,0x13.
int FObjListDock::GetType()
{
    return 0x13;
}

// FUN_06f276f0 (06f276f0-06f27708).
void FObjListDock::SetTarget(unsigned int const* DockObjectId, unsigned int const* Field10)
{
    Op.DockObjectId = *DockObjectId;
    Op.Field_10 = static_cast<int32_t>(*Field10);
}

// LAB_06f27830 (06f27830-06f27866): vtbl[+4] (result unused), priority default,
// FUN_06f57c60.
bool FObjListInstantTradelane::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f278a0 (06f278a0-06f278fc).
void FObjListInstantTradelane::Parse(INI_Reader* Reader)
{
    Op.TradeLaneId = pub::GetNicknameId(Reader->get_value_string(0));
    if (Reader->is_value_empty(1))
    {
        Op.Field_10 = 0;
        return;
    }
    Op.Field_10 = static_cast<int32_t>(pub::GetNicknameId(Reader->get_value_string(1)));
}

// 06f250a0: MOV EAX,0x13.
int FObjListInstantTradelane::GetType()
{
    return 0x13;
}

// FUN_06f27870 (06f27870-06f27892).
void FObjListInstantTradelane::SetLane(unsigned int const* TradeLaneId, unsigned int Field14,
                                       unsigned int const* Field10)
{
    Op.TradeLaneId = *TradeLaneId;
    Op.Field_10 = static_cast<int32_t>(*Field10);
    Op.Field_14 = static_cast<int32_t>(Field14);
}

// LAB_06f27970 (06f27970-06f279ff): vtbl[+4] (result unused); without a launch object or
// with Field_10 == -1, FUN_06f6eca0(&ship id, &LaunchObjectId, &Field_10, &Field_14) picks
// one (false -> return false); priority default; FUN_06f6dff0(ship id, 0, 0, 0);
// FUN_06f57c60.
bool FObjListLaunch::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    if (Op.Field_10 == -1 || Op.LaunchObjectId == 0)
    {
        unsigned int ShipId = Ship->SpaceObjId;
        unsigned int TerminalType = static_cast<unsigned int>(Op.Field_14);
        if (!GetShipDockingTarget(&ShipId, &Op.LaunchObjectId, &Op.Field_10, &TerminalType))
            return false;
        Op.Field_14 = static_cast<int32_t>(TerminalType);
    }
    DefaultPriority(&Op, Ship);
    SetDockEquipment(Ship->SpaceObjId, false, false, nullptr);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f27a40 (06f27a40-06f27b74): LaunchObjectId = GetNicknameId(value 0), Field_10 = int
// value 1, Field_14 from value 2: NONE 0, BERTH 1, PAD 2, MOOR_SMALL 3, MOOR_MEDIUM 4,
// MOOR_LARGE 5, RING 6, JUMP_GATE 7, else unchanged.
void FObjListLaunch::Parse(INI_Reader* Reader)
{
    Op.LaunchObjectId = pub::GetNicknameId(Reader->get_value_string(0));
    Op.Field_10 = Reader->get_value_int(1);
    char const* Kind = Reader->get_value_string(2);
    if (_stricmp(Kind, "NONE") == 0)
        Op.Field_14 = 0;
    else if (_stricmp(Kind, "BERTH") == 0)
        Op.Field_14 = 1;
    else if (_stricmp(Kind, "PAD") == 0)
        Op.Field_14 = 2;
    else if (_stricmp(Kind, "MOOR_SMALL") == 0)
        Op.Field_14 = 3;
    else if (_stricmp(Kind, "MOOR_MEDIUM") == 0)
        Op.Field_14 = 4;
    else if (_stricmp(Kind, "MOOR_LARGE") == 0)
        Op.Field_14 = 5;
    else if (_stricmp(Kind, "RING") == 0)
        Op.Field_14 = 6;
    else if (_stricmp(Kind, "JUMP_GATE") == 0)
        Op.Field_14 = 7;
}

// 06f25090: MOV EAX,0x15.
int FObjListLaunch::GetType()
{
    return 0x15;
}

// FUN_06f27a10 (06f27a10-06f27a30).
void FObjListLaunch::SetLaunch(unsigned int const* LaunchObjectId, int Field10, int Field14)
{
    Op.LaunchObjectId = *LaunchObjectId;
    Op.Field_10 = Field10;
    Op.Field_14 = Field14;
}

// ---------------------------------------------------------------------------------------
// 0x16 WaitForPlayerManeuver, 0x19 TractorBeam
// ---------------------------------------------------------------------------------------

// FUN_06f25140 (06f25140-06f25199).
FObjListWaitForPlayerManeuver::FObjListWaitForPlayerManeuver()
{
    BehaviorName[0] = '\0';
}

// LAB_06f27b80 (06f27b80-06f27c7d): vtbl[+4] (result unused); the id of "Player" (a local
// TString, FUN_06f73ac0 on the ship's system, 0 without one); 0 -> true (the directive stays
// current). A player in behavior 10 (pub::AI::get_behavior_id) whose ship (FUN_06f49bc0 ->
// +0x10, the state is not null-checked) shares the ship's +0x154 finishes the directive
// (FUN_06f57b00, true). Else priority default and FUN_06f57c60.
bool FObjListWaitForPlayerManeuver::Execute(FContentShipBase* Ship)
{
    Ship->Slot04();
    TString<0x30> PlayerName;
    PlayerName = "Player";
    unsigned int PlayerId = FindTargetId(Ship, &PlayerName);
    if (PlayerId == 0)
        return true;
    if (pub::AI::get_behavior_id(PlayerId) == 10)
    {
        FContentPlayerShip* PlayerShip = FindContentPlayerStateByShip(&PlayerId)->ShipObject;
        if (PlayerShip != nullptr && Ship->FormationId == PlayerShip->FormationId)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f27c80 (06f27c80-06f27caa): strcpy(+0x11c, value 0) (unbounded, as retail), then
// the op's name pointer at it.
void FObjListWaitForPlayerManeuver::Parse(INI_Reader* Reader)
{
    strcpy(BehaviorName, Reader->get_value_string(0));
    Op.BehaviorName = BehaviorName;
}

// 06f251a0: MOV EAX,0x16.
int FObjListWaitForPlayerManeuver::GetType()
{
    return 0x16;
}

// LAB_06f28dd0 (06f28dd0-06f28de0): FUN_06f57c60 without the priority default.
bool FObjListTractorBeam::Execute(FContentShipBase* Ship)
{
    return Ship->SubmitDirective(&Op);
}

// 06eea870: MOV EAX,0x19.
int FObjListTractorBeam::GetType()
{
    return 0x19;
}

// FUN_06f28df0 (06f28df0-06f28e04).
void FObjListTractorBeam::SetTarget(unsigned int TargetId, bool Field10)
{
    Op.TargetId = TargetId;
    Op.Field_10 = Field10 ? 1 : 0;
}
