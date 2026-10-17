// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IClientImpl slots of retail IClientS.cpp (.text:0053aaa0..0053e4a0; the FDUMP path string is
// .rdata:005dec60) -- the space side of the server's IClient calls. See freelancer_client.h for
// the split with client_iclient.cpp. Every slot's retail body is quoted by address.
//
// The gameobj a slot works on is FUN_005416c0 (ClientObjDb_FindObject); its IObjAffect is the
// sub-object at +4 (`LEA ECX,[EAX+4]; CALL [vt+N]`), i.e. a plain call through FClientGameobj.
// "Narrowed to M" is retail's inline `(Object->ObjectClass & M) == M ? Object : 0`, used with no
// null test after it wherever retail has none.

#include "freelancer_client.h"

#include "character_select_equip.h"   // player equip / collision / health setters (0x004c4xxx)
#include "cli_projectile_fx.h"        // FCliLauncherFx::FireDirect (the dropper tails, slots 49..51)
#include "client_cliobj.h"            // Archetype::LoadMountedEquipmentCompanions (FUN_004252d0)
#include "client_equip_fx.h"          // EquippedObjFindEquipFx / CliTractorEquipFxDropLoot (slot 64)
#include "client_gameobj.h"           // FClientGameobj family
#include "client_objdb.h"             // ClientObjDb_FindObject (FUN_005416c0)
#include "common_archetype.h"
#include "common_cequip.h"            // CEquip, CECargo
#include "common_cobject.h"           // CObject / CSimple / CEqObj / CShip / CLoot
#include "common_csys.h"              // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"             // EquipDescList / EquipDescVector / Loadout
#include "common_iface.h"             // IBehaviorManager, IDirectiveInfo, FBehaviorManagerEvent
#include "common_misc.h"              // DamageList, SUBOBJ_ID_NONE / SUBOBJ_ID_ROOT
#include "common_physys.h"            // PhySys::AnyActive / RCSet / RCEnd
#include "common_pub.h"               // pub::AI::Directive*Op (slot 100)
#include "common_reputation.h"        // Reputation::set / Vibe::SetAffiliation / SetRank
#include "common_server_types.h"      // XFireWeaponInfo .. XJettisonCargo
#include "common_text.h"              // FmtStr (slot 47)
#include "common_tstring.h"           // TString<16> (slot 82)
#include "dacom.h"                    // FDUMP
#include "game_options.h"             // AssignGameSessionFlags (FUN_004c8190)
#include "hud_comm.h"                 // ClientSendComm / HudCommPreloadCostume
#include "hud_waypoint.h"             // EqObjCreateParmsDefaults (FUN_004f4930, slot 46)
#include "hud_weapon_groups.h"        // G_WeaponGroups (DAT_00674c0c)
#include "misc_util.h"                // FmtStrSprintf (FUN_0041c970, slot 47)
#include "menu_sound_arch.h"          // LookupClientSoundArch / PlayClientSoundArch
#include "menu_ui_widgets.h"          // RetailU32QueueClear / PushBack (slot 120)
#include "chat_init.h"                // ChatSetServerMotd (slot 115), player info (117..119)
#include "nn_inventory.h"             // ReqPlayerHullStatusRetail (FUN_004c4f50)
#include "gf_script_object.h"         // FGfScriptSequence::OnMissionOfferAnswered (FUN_004557d0)
#include "nn_messaging.h"             // NnVoiceEnqueue (FUN_00489e80)
#include "nn_mission_message.h"       // NnMissionMessagePopUp (slot 95), ShowScanNotice (109)
#include "nn_navmap.h"                // G_bNavMapNoBestPath (DAT_006720a8), FUN_0048d8a0
#include "nn_player_info.h"           // NnPlayerInfoOnPlayerStats (FUN_004a8e40, slot 94)
#include "nn_story_star.h"            // StoryStarLoadHistory (slot 97), StoryStarSetRankUp (117)
#include "player_behavior.h"          // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"             // IsPlayerShipId, player ids, waypoints
#include "rpc_iclient_args.h"         // the slot argument structs shared with Server / rpclocal
#include "server_createinfo.h"        // CEqObjCreateInfo (slots 46 / 47)
#include "server_sysdb.h"             // FMineExplosion (SendServerExplodeAsteroidMine)
#include "space_camera.h"             // SetupSpaceScriptCamera / RequestSpaceCameraMode
#include "space_state.h"              // EnterBaseFromDock
#include "space_app.h"                // SpaceAppArmDeathCamera / SetStaticCameraTransform
#include "star_sys.h"                 // StarSysSetMusicOverride / StarSysClearMusicOverride
#include "tradelane_fx.h"             // the trade-lane receivers (slots 6, 7, 56)
#include "x86math.h"                  // I3DMathEngine

#include <list>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace
{
    // .rdata 0x005dec60 -- IClientS.cpp's path string.
    char const kIClientSCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\IClientS.cpp";

    // DAT_006789f8 -- IClientS.cpp's 3DMathEngine, FUN_005b73e0 on first use. Raw scan: 12
    // test+fill pairs -- 0x0053ae6b / 0x0053b47e (the unported multiplayer CSolar / CShip builds),
    // 0x0053ba99 (loot), 0x0053cbb5 / 0x0053cec1 (mine / counter, multiplayer arm), 0x0053d1ca /
    // 0x0053d371 (guided), 0x0053d6a8 / 0x0053d6e2 (system switch-in, one per arm), 0x0053d816 /
    // 0x0053d88c (launch), 0x0053e100 (set camera).
    I3DMathEngine* G_IClientSMath = nullptr;

    I3DMathEngine* IClientSMath()
    {
        if (G_IClientSMath == nullptr)
        {
            G_IClientSMath = Acquire3DMathEngine();
        }
        return G_IClientSMath;
    }

    // The inline narrowing every handler repeats on the gameobj's object (+0x0c CBase, +0x10
    // Object): the object when (ObjectClass & Mask) == Mask, else null.
    CObject* NarrowObject(CObject* Obj, unsigned Mask)
    {
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
        {
            return nullptr;
        }
        return Obj;
    }

    CObject* NarrowGameobj(FClientGameobj const* Go, unsigned Mask)
    {
        return NarrowObject(Go->GetObject(), Mask);
    }

    bool IsPlayerGameobj(FClientGameobj const* Go)
    {
        // The FUN_0054baf0 compare the handlers make against a gameobj pointer.
        return static_cast<void const*>(static_cast<IObjRW const*>(Go)) == GetPlayerObjRW();
    }

    // IDirectiveInfo as slot 100 writes it: three dwords {First, 0, Type}.
    IDirectiveInfo MakeDirectiveInfo(unsigned First, unsigned Type)
    {
        IDirectiveInfo Info;
        unsigned const Dwords[3] = {First, 0u, Type};
        memcpy(Info.Data, Dwords, sizeof(Dwords));
        return Info;
    }

    // The local equipment vector CREATESOLAR / CREATESHIP build (0x0053ac68..0x0053acd1,
    // 0x0053b280..0x0053b2ec): the loadout's items when Field_0x6c names one (the VC6 vector
    // operator= onto the empty local), then every item of the create info appended (vector
    // insert at end, one element each). EquipDescVector::add_equipment_item(_, false) is the
    // plain append.
    void BuildCreateEquipment(CEqObjCreateInfo const* Info, EquipDescVector& Out)
    {
        if (Info->Field_0x6c != 0)
        {
            Loadout::Map const* const Map = Loadout::Get(Info->Field_0x6c);
            for (std::vector<EquipDesc>::const_iterator It = Map->Items.begin();
                 It != Map->Items.end(); ++It)
            {
                Out.add_equipment_item(*It, false);
            }
        }
        for (EquipDesc const* It = Info->Equipment.Begin; It != Info->Equipment.End; ++It)
        {
            Out.add_equipment_item(*It, false);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Shared stubs
// ---------------------------------------------------------------------------------------------

// LAB_0053aaa0 -- `RET 8` (PushToClient, SendServerSetCargo, SendServerRepairObject,
// SendServerSetStoryCue; also LocationController's GFDESTROYCHARACTER target).
void IClientImpl::PushToClient(unsigned int /*ClientId*/, CDAPacket* /*Packet*/)
{
}

void IClientImpl::SendServerSetCargo(unsigned int /*ClientId*/, const EquipDescVector& /*Cargo*/)
{
}

void IClientImpl::SendServerRepairObject(unsigned int /*ClientId*/, const FServerRepairObjectInfo& /*Info*/)
{
}

void IClientImpl::SendServerSetStoryCue(const unsigned int& /*ClientId*/, const unsigned int& /*CueId*/)
{
}

// LAB_0053e180: `XOR EAX,EAX; RET 4` (GetSendQSize and GetSendQBytes).
unsigned int IClientImpl::GetSendQSize(unsigned int /*ClientId*/)
{
    return 0;
}

unsigned int IClientImpl::GetSendQBytes(unsigned int /*ClientId*/)
{
    return 0;
}

// LAB_0053e190: `FLD double [DAT_005c8ba8]; RET 4` -- the constant is 0.0.
double IClientImpl::GetLinkSaturation(unsigned int /*ClientId*/)
{
    return 0.0;
}

// FUN_0053d4f0: `RET 0x10` (also Projectile's slot 104).
void IClientImpl::SendServerSetGuidedTarget(unsigned int /*ClientId*/, unsigned int /*GuidedId*/,
                                            unsigned int /*TargetId*/, unsigned short /*TargetSubObjId*/)
{
}

// LAB_0053e4a0: `RET 4` (SendServerPlayerListClear and DumpPacketStats).
void IClientImpl::SendServerPlayerListClear(unsigned int /*ClientId*/)
{
}

void IClientImpl::DumpPacketStats(const char* /*Name*/)
{
}

// ---------------------------------------------------------------------------------------------
// Equipment traffic (slots 0..8)
// ---------------------------------------------------------------------------------------------

// FUN_0053c2c0 (RET 8) -- ch1 FIREWEAPON: the gameobj (any bucket); when found, its IObjAffect
// slot 13 (+0x34) with the hardpoint array, its count ((last - first) >> 1, 0 for a null
// first), &TargetPos and 0.
void IClientImpl::SendCommonFireWeapon(unsigned int /*ClientId*/, const XFireWeaponInfo& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    Go->slot_34(Info.Hardpoints.data(), static_cast<unsigned>(Info.Hardpoints.size()),
                &Info.TargetPos, 0);
}

// FUN_0053c730 (RET 8) -- ch1 ACTIVATEEQUIP: IObjAffect slot 19 (+0x4c)(&EquipId, bActivate, 0).
void IClientImpl::SendCommonActivateEquip(unsigned int /*ClientId*/, const XActivateEquip& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    Go->slot_4c(&Info.EquipId, Info.bActivate, 0);
}

// FUN_0053c940 (RET 8) -- ch1 ACTIVATECRUISE: the ship (bucket 1), IObjAffect slot 20 (+0x50)
// with the two bytes +4 / +5 and 0.
void IClientImpl::SendCommonActivateCruise(unsigned int /*ClientId*/, const XActivateCruise& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 1);
    if (Go == nullptr)
    {
        return;
    }
    Go->slot_50(Info.bActivate, Info.Field_0x05 != 0, 0);
}

// FUN_0053c980 (RET 8) -- ch1 ACTIVATETHRUSTERS: the ship (bucket 1), IObjAffect slot 21
// (+0x54)(bActivate, 0).
void IClientImpl::SendCommonActivateThrusters(unsigned int /*ClientId*/, const XActivateThrusters& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 1);
    if (Go == nullptr)
    {
        return;
    }
    Go->slot_54(Info.bActivate, 0);
}

// FUN_0053c9b0 (RET 8) -- ch1 SETTARGET. With the gameobj (any bucket): outside single player
// its slot 106 (+0x1a8)(SubObjId, TargetObjectId, TargetSubObjId) first; then the target
// (FUN_005416c0, any bucket, not tested) goes to IObjAffect set_target (+0x48)(target,
// TargetSubObjId, 0) when SubObjId is SUBOBJ_ID_NONE, else to slot 17 (+0x44)(&SubObjId,
// target, TargetSubObjId, 0).
void IClientImpl::SendCommonSetTarget(unsigned int /*ClientId*/, const XSetTarget& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    if (!SinglePlayer())
    {
        Go->Slot1a8(Info.SubObjId, Info.TargetObjectId, Info.TargetSubObjId);
    }
    FClientGameobj* const TargetGo = ClientObjDb_FindObject(Info.TargetObjectId, 0);
    IObjRW* const Target = TargetGo;
    if (Info.SubObjId == SUBOBJ_ID_NONE)
    {
        Go->set_target(Target, Info.TargetSubObjId, 0);
        return;
    }
    Go->slot_44(&Info.SubObjId, Target, Info.TargetSubObjId, 0);
}

// FUN_0053c470 (RET 8) -- ch1 TRACTOROBJECTS: the gameobj (any bucket); every target id that
// resolves (FUN_005416c0, any bucket) is appended to a local std::vector<IObjInspect*> (Common's
// vector insert); a non-empty vector goes to IObjAffect slot 15 (+0x3c)(&TractorEquipId, vec,
// 0); the buffer is freed. The count is taken once, before the loop.
void IClientImpl::SendCommonTractorObjects(unsigned int /*ClientId*/, const XTractorObjects& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    std::vector<IObjRW*> Targets;
    unsigned int const Count = static_cast<unsigned int>(Info.TargetIds.size());
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        FClientGameobj* const Found = ClientObjDb_FindObject(Info.TargetIds[Index], 0);
        if (Found != nullptr)
        {
            Targets.push_back(Found);
        }
    }
    if (!Targets.empty())
    {
        Go->slot_3c(&Info.TractorEquipId, Targets, 0);
    }
}

// LAB_0053c830 (RET 8) -- ch2 GOTRADELANE. The client id is not read; the body is
// TradeLaneIClientGoTradelane (tradelane_fx.cpp).
void IClientImpl::SendCommonGoTradeLane(unsigned int /*ClientId*/, const XGoTradelane& Info)
{
    TradeLaneIClientGoTradelane(&Info);
}

// LAB_0053c760 (RET 0x10) -- ch2 STOPTRADELANE: (client id, ship id, entrance ring id, unused);
// the body is TradeLaneIClientStopTradelane (tradelane_fx.cpp).
void IClientImpl::SendCommonStopTradeLane(unsigned int /*ClientId*/, unsigned int ShipId,
                                          unsigned int TradelaneRing1, unsigned int /*TradelaneRing2*/)
{
    TradeLaneIClientStopTradelane(ShipId, TradelaneRing1);
}

// FUN_0053c570 (RET 8) -- ch1 JETTISONCARGO: when the gameobj (any bucket) exists, IObjAffect
// slot 14 (+0x38)(ItemId, Count, 0); then, for the player's ship (FUN_00548eb0) whether or not
// it was found, FUN_004c4320(0, ItemId, Count).
void IClientImpl::SendCommonJettisonCargo(unsigned int /*ClientId*/, const XJettisonCargo& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go != nullptr)
    {
        Go->slot_38(Info.ItemId, static_cast<int>(Info.Count), 0);
    }
    if (IsPlayerShipId(Info.ObjectId))
    {
        RemovePlayerEquipRetail(0, Info.ItemId, static_cast<int>(Info.Count));
    }
}

// ---------------------------------------------------------------------------------------------
// Object creation (slots 46..51)
// ---------------------------------------------------------------------------------------------

// FUN_0053ac10 (RET 8) -- ch2 CREATESOLAR, Info the CSolarCreateInfo. Nothing unless PhySys has a
// system (PhySys::AnyActive). A Solar gameobj (operator new(0x208) + FUN_005475b0) and the local
// equipment vector (BuildCreateEquipment). Single player: attach to the server's CObject
// (slot 91 +0x16c find(&ObjId)); a miss warns (IClientS.cpp line 0x67), deletes the gameobj
// (its CBase vt0(1)) and frees the vector. Multiplayer (0x0053ad44..0x0053b034): Archetype::
// GetSolar, create, the CSolar::CreateParms build and CSolar::init (MultiplayerCreateSolar).
// Then the tail 0x0053b039..0x0053b0b3: the object narrowed to 0x303 (no test), its archetype's
// companion (+4) vt+0x20 Load(1), FUN_004252d0(&vector, 1), FUN_00425480(object +4), slot 92
// (+0x170), FUN_00547550(DAT_00673344, bMarkedBy), FUN_00540c30(gameobj, 0) and
// CObject::enable_collisions_r(true). Then 0x0053b0b9..0x0053b1f4 (multiplayer, with a player
// ship): a solar that appears over the player's ship lifts the ship clear of it.
namespace
{
    // 0x0053ad44..0x0053b034 -- the multiplayer arm of CREATESOLAR. The parms are the stack
    // record at [ESP+0x54]; they are destroyed at the end of this function, which is retail's
    // inlined anim-list teardown (0x0053afd3..0x0053b02c: FUN_0053e430 begin, FUN_004a2f80
    // erase, the head freed) followed by FUN_004f4a10 (the +0xac list, then the +0x9c vector).
    // Unwind states 2..4 of LAB_005c0b8b are the same destructors on the exception path.
    void MultiplayerCreateSolar(FClientSolarGameobj* Go, CSolarCreateInfo const* Info,
                                EquipDescVector const& Equipment)
    {
        // 0x0053ad44..0x0053ad67: Archetype::GetSolar(+0x08), Root vt+0x0c load, slot 90
        // (+0x168) create, Archetype::Root::Release. Nothing is null-tested.
        Archetype::Solar* const Arch = Archetype::GetSolar(Info->ArchetypeNickname);
        Arch->load();
        Go->create(Arch);
        Arch->Release();

        // 0x0053ad6d..0x0053ad94: the object narrowed to 0x303 and kept at [ESP+0x10]; init
        // below is called on it with no test.
        CSolar* const Solar = static_cast<CSolar*>(NarrowGameobj(Go, 0x303));

        // 0x0053ad94..0x0053ae2c: FUN_004f4930 (the CEqObj part), then the CSolar tail --
        // ID_String::clear on +0xb8 / +0xc0 / +0xcc, byte +0xbc, bytes +0xc4 / +0xc5, dwords
        // +0xc8 and +0xd0..+0xe0 zero -- and the anim list's head (FUN_0042a7e0(0, 0), size 0).
        CSolar::CreateParms Parms;
        EqObjCreateParmsDefaults(Parms);
        Parms.NicknameSrc.clear();
        Parms.VisitValue = 0;
        reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();
        Parms.bDestructible = 0;
        Parms.bDynamic = 0;
        Parms.DestSystem = 0;
        reinterpret_cast<ID_String&>(Parms.DestGateId).clear();
        Parms.PrevRing = 0;
        Parms.NextRing = 0;
        Parms.TradelaneSpaceNameId = 0;
        Parms.AtmosphereRange = 0.0f;
        Parms.DockWithBaseId = 0;

        // 0x0053ae33..0x0053ae86: HitPts is the record's integer +0x68 (FILD, FSTP float); Id
        // +0x04; the client ObjDB (PTR_DAT_00613ed0); +0x0c into both DockingCount (+0x5c) and
        // DockWithBaseId (+0xe0); Pos +0x4c; OwnerPlayer 0.
        Parms.HitPts = static_cast<float>(Info->HitPts);
        Parms.Id = Info->ObjId;
        Parms.ObjDb = GetClientObjDb();
        Parms.DockingCount = Info->DockingBaseId;
        Parms.DockWithBaseId = Info->DockingBaseId;
        Parms.Pos = Info->Position;
        Parms.OwnerPlayer = 0;

        // 0x0053ae6b..0x0053aeba: the orientation +0x58 through DAT_006789f8 vt+0x54 into a
        // stack matrix, copied into +0x1c (REP MOVSD, nine dwords).
        IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                           reinterpret_cast<FQuaternion const&>(Info->Orientation));

        // 0x0053aea4..0x0053af27: +0x10 -> +0x60, +0x14 -> +0x64, +0x48 -> +0x98, +0x18 ->
        // +0x68, +0x1c -> +0x6c, +0x20 -> +0x70, the count +0x44 -> +0x94, and that many dwords
        // from +0x24 into +0x74 (JLE / JL, the count re-read from +0x94 every pass; no clamp).
        Parms.VibeId = static_cast<unsigned>(Info->VibeGroupId);
        Parms.SpaceCostume.PartIds[0] = Info->CostumeHead;
        Parms.VoiceId = Info->VoiceId;
        Parms.SpaceCostume.PartIds[1] = Info->CostumeBody;
        Parms.SpaceCostume.PartIds[2] = Info->CostumeLeftHand;
        Parms.SpaceCostume.PartIds[3] = Info->CostumeRightHand;
        Parms.SpaceCostume.AccessoryCount = Info->CostumeAccessoryCount;
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        {
            Parms.SpaceCostume.AccessoryIds[Index] = Info->CostumeAccessory[Index];
        }

        // 0x0053af29..0x0053af35: the local equipment vector (loadout + record items) assigned
        // into +0x9c (Common's vector operator=).
        Parms.Equipment = Equipment;
        // 0x0053af3b..0x0053af9e: the record's collision groups (+0x80) assigned into +0xac --
        // VC6's list operator= inlined (the two lists are never the same object).
        Parms.ArchGroupDescs = Info->CollisionGroups;

        // 0x0053afa4..0x0053afc6: byte +0xc4 = the record's +0xad, byte +0xc5 = 1, and the
        // nickname +0xb8 = the object id just stored in +0x00.
        Parms.bDestructible = Info->Field_0xad;
        Parms.bDynamic = 1;
        Parms.NicknameSrc.Id = Parms.Id;

        // 0x0053afcd: CSolar::init (the Common export, not virtual).
        Solar->init(Parms);
    }
}

void IClientImpl::SendServerCreateSolar(unsigned int /*ClientId*/, const CSolarCreateInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FClientSolarGameobj* const Go = new FClientSolarGameobj();
    EquipDescVector Equipment;
    BuildCreateEquipment(&Info, Equipment);

    if (SinglePlayer())
    {
        // 0x0053acdb..0x0053ad3f: slot 91 (+0x16c) find(&ObjId).
        if (!Go->find(Info.ObjId))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: SolarCreate - CObject %d not found.",
                  kIClientSCpp, 0x67, Info.ObjId);
            delete Go;
            return;
        }
    }
    else
    {
        MultiplayerCreateSolar(Go, &Info, Equipment);
    }

    // 0x0053b039..0x0053b0b3.
    CObject* const Obj = NarrowGameobj(Go, 0x303);
    Obj->get_archetype()->Companion->Load(1);
    Archetype::LoadMountedEquipmentCompanions(&Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Obj->EngineInstanceId);
    Go->Slot170();
    Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), Info.bMarkedBy);
    ClientObjDb_RegisterGameobj(Go, false);
    Obj->enable_collisions_r(true);

    // 0x0053b0b9..0x0053b0d0: the player's gameobj (FUN_0054baf0) is fetched first; nothing
    // more in single player or without one.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (SinglePlayer() || Player == nullptr)
    {
        return;
    }

    // 0x0053b0d6..0x0053b139: both physical spheres (IObjInspect slot 11), the centre seeded
    // with the position (slot 0) and the radius with 0; the return values are not read.
    Vector SolarCenter = Go->get_position();
    float SolarRadius = 0.0f;
    Go->get_physical_radius(SolarRadius, SolarCenter);
    Vector PlayerCenter = Player->get_position();
    float PlayerRadius = 0.0f;
    Player->get_physical_radius(PlayerRadius, PlayerCenter);

    // 0x0053b13c..0x0053b183: the differences and the summed radius stay on the FPU (double);
    // the sum is also stored as a float ([ESP+0x18]). The squared distance adds dz*dz + dy*dy
    // first, then dx*dx. FCOMPP compares Reach*Reach (ST0) with it; TEST AH,0x41 / JNZ leaves
    // when Reach*Reach is not greater (or unordered) -- the fall-through is "the spheres
    // overlap".
    double const Dx = static_cast<double>(SolarCenter.x) - PlayerCenter.x;
    double const Dy = static_cast<double>(SolarCenter.y) - PlayerCenter.y;
    double const Dz = static_cast<double>(SolarCenter.z) - PlayerCenter.z;
    double const Reach = static_cast<double>(PlayerRadius) + SolarRadius;
    float const ReachStored = static_cast<float>(Reach);
    double const DistanceSq = Dz * Dz + Dy * Dy + Dx * Dx;
    if (!(Reach * Reach > DistanceSq))
    {
        return;
    }

    // 0x0053b185..0x0053b1c0: the new position is the solar's centre with only y changed. FCOMP
    // of the centre's y against the player's; TEST AH,0x5 / JP is taken when C0 and C2 agree --
    // not less (or unordered) -- and then y = centre.y - the stored float sum. The fall-through
    // (centre below the player) is centre.y + player radius + solar radius on the FPU.
    Vector Clear;
    Clear.x = SolarCenter.x;
    Clear.z = SolarCenter.z;
    if (SolarCenter.y < PlayerCenter.y)
    {
        Clear.y = static_cast<float>(static_cast<double>(SolarCenter.y) + PlayerRadius + SolarRadius);
    }
    else
    {
        Clear.y = static_cast<float>(static_cast<double>(SolarCenter.y) - ReachStored);
    }

    // 0x0053b1be..0x0053b1f4: the player's CObject (slot 84 +0x150), a CShip ((class & 0x503)
    // == 0x503) or nothing, beamed there (CObject vt+0x7c) with the player's own transform
    // (IObjInspect slot 3, its rotation) and bAttach false -- the 0 is pushed before slot 3 is
    // called.
    CObject* const PlayerShip = NarrowObject(const_cast<CObject*>(Player->cobject()), 0x503);
    if (PlayerShip == nullptr)
    {
        return;
    }
    PlayerShip->beam_object(Clear, Player->get_transform().Rotation, false);
}

// FUN_0053b220 (RET 8) -- ch2 CREATESHIP, Info the CShipCreateInfo. As slot 46 with a Ship gameobj
// (operator new(0x190) + FUN_00543380); the single-player miss warns at line 0xff; a single-
// player hit with bMarkedBy first joins the watch list (FUN_00547550(DAT_00673344, 1)). The
// multiplayer build (0x0053b389..0x0053b6b0) is MultiplayerCreateShip. Tail 0x0053b6b5..
// 0x0053b729: narrowed to 0x503, companion Load(1), FUN_004252d0, FUN_00425480, slot 92,
// FUN_00547550(DAT_00673344, bMarkedBy), FUN_00540c30(gameobj, 0). Then, outside single player,
// the reputation name block 0x0053b73a..0x0053b839.
namespace
{
    // 0x0053b389..0x0053b6b0 -- the multiplayer arm of CREATESHIP. The CShip::CreateParms are
    // the stack record at [ESP+0x70]; their list (+0xac, state 4) and vector (+0x9c, state 1)
    // are destroyed at 0x0053b687..0x0053b6ab, which is the end of this function.
    void MultiplayerCreateShip(FClientShipGameobj* Go, CShipCreateInfo const* Info,
                               EquipDescVector const& Equipment)
    {
        // 0x0053b389..0x0053b3ac: Archetype::GetShip(+0x08), Root vt+0x0c load, slot 90
        // (+0x168) create, Archetype::Root::Release. Nothing is null-tested.
        Archetype::Ship* const Arch = Archetype::GetShip(Info->ArchetypeNickname);
        Arch->load();
        Go->create(Arch);
        Arch->Release();

        // 0x0053b3b2..0x0053b3d9: the object narrowed to 0x503, kept at [ESP+0x1c]; init and
        // the +0x1b4 store below use it with no test.
        CShip* const Ship = static_cast<CShip*>(NarrowGameobj(Go, 0x503));

        // 0x0053b3d9..0x0053b443: FUN_0052e0a0 on the CSimple head (+0x00..+0x58), +0x5c = 0,
        // +0x60 = -1, FUN_004cdc10 on +0x64 (+0x64..+0x70 and the count +0x94), +0x98 = 0, the
        // EquipDescVector (+0x9c) and the collision-group list (+0xac) constructed, +0xe8 /
        // +0xec / +0xf0 = 0, byte +0xf4 = 0. Retail leaves +0x74..+0x90 and +0xb8..+0xe7 as
        // they lie; value-initialisation zeroes them, and CShip::init reads neither.
        CShip::CreateParms Parms{};
        InitCreateParmsScratchRetail(&Parms);
        Parms.DockingCount = 0;
        Parms.VibeId = 0xffffffffu;
        Parms.SpaceCostume.PartIds[0] = 0;
        Parms.SpaceCostume.PartIds[1] = 0;
        Parms.SpaceCostume.PartIds[2] = 0;
        Parms.SpaceCostume.PartIds[3] = 0;
        Parms.SpaceCostume.AccessoryCount = 0;
        Parms.VoiceId = 0;
        Parms.FieldE8 = 0;
        Parms.GroupNameId = 0;
        Parms.Rank = 0;
        Parms.bUsingTradelane = 0;

        // 0x0053b44a..0x0053b494: HitPts the record's integer +0x68 (FILD, FSTP float), Id
        // +0x04, the client ObjDB (PTR_DAT_00613ed0), OwnerPlayer +0xac, Pos +0x4c.
        Parms.HitPts = static_cast<float>(Info->HitPts);
        Parms.Id = Info->ObjId;
        Parms.ObjDb = GetClientObjDb();
        Parms.OwnerPlayer = Info->OwnerPlayer;
        Parms.Pos = Info->Position;

        // 0x0053b47e..0x0053b4ce: the orientation +0x58 through DAT_006789f8 vt+0x54 into a
        // stack matrix, copied into +0x1c.
        IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                           reinterpret_cast<FQuaternion const&>(Info->Orientation));

        // 0x0053b4b6..0x0053b592: Velocity +0xb8 -> +0x40, AngularVelocity +0xc4 -> +0x4c,
        // +0x10 -> +0x60, +0x48 -> +0x98, +0xb4 -> +0xf0, +0x14..+0x20 -> +0x64..+0x70, the count
        // +0x44 -> +0x94 and that many dwords from +0x24 into +0x74 (re-read every pass, no clamp).
        Parms.Velocity = Info->Velocity;
        Parms.AngularVelocity = Info->AngularVelocity;
        Parms.VibeId = static_cast<unsigned>(Info->VibeGroupId);
        Parms.VoiceId = Info->VoiceId;
        Parms.Rank = Info->ReputationRank;
        Parms.SpaceCostume.PartIds[0] = Info->CostumeHead;
        Parms.SpaceCostume.PartIds[1] = Info->CostumeBody;
        Parms.SpaceCostume.PartIds[2] = Info->CostumeLeftHand;
        Parms.SpaceCostume.PartIds[3] = Info->CostumeRightHand;
        Parms.SpaceCostume.AccessoryCount = Info->CostumeAccessoryCount;
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        {
            Parms.SpaceCostume.AccessoryIds[Index] = Info->CostumeAccessory[Index];
        }

        // 0x0053b594..0x0053b60a: the local equipment vector into +0x9c (vector operator=), the
        // record's collision groups (+0x80) into +0xac (VC6 list operator= inlined).
        Parms.Equipment = Equipment;
        Parms.ArchGroupDescs = Info->CollisionGroups;

        // 0x0053b610..0x0053b645: +0xb0 -> +0xec, +0x0c -> +0x5c; a marked record puts this
        // connection's id (DAT_00673344) into +0x58; the byte +0xdc -> +0xf4.
        Parms.GroupNameId = Info->Field_0xb0;
        Parms.DockingCount = Info->DockingBaseId;
        if (Info->bMarkedBy != 0)
        {
            Parms.Pad58 = GetPlayerConnectionId();
        }
        Parms.bUsingTradelane = Info->bUsingTradelane;

        // 0x0053b64c..0x0053b655: CShip vt+0x10c init.
        Ship->init(Parms);

        // 0x0053b65b..0x0053b684: CShip+0x1b4 = the record's +0xd8, then
        // PhySys::RCBegin(ship, +0x4c, +0xb8, +0x58, +0xc4).
        Ship->GroupId = Info->Field_0xd8;
        PhySys::RCBegin(Ship, Info->Position, Info->Velocity, Info->Orientation,
                        Info->AngularVelocity);
    }
}

void IClientImpl::SendServerCreateShip(unsigned int /*ClientId*/, const CShipCreateInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FClientShipGameobj* const Go = new FClientShipGameobj();
    EquipDescVector Equipment;
    BuildCreateEquipment(&Info, Equipment);

    if (SinglePlayer())
    {
        // 0x0053b2fa..0x0053b384: slot 91 (+0x16c) find(&ObjId).
        if (!Go->find(Info.ObjId))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: ShipCreate - CObject %d not found.",
                  kIClientSCpp, 0xff, Info.ObjId);
            delete Go;
            return;
        }
        if (Info.bMarkedBy != 0)
        {
            Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), 1);
        }
    }
    else
    {
        MultiplayerCreateShip(Go, &Info, Equipment);
    }

    // 0x0053b6b5..0x0053b729.
    CObject* const Obj = NarrowGameobj(Go, 0x503);
    Obj->get_archetype()->Companion->Load(1);
    Archetype::LoadMountedEquipmentCompanions(&Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Obj->EngineInstanceId);
    Go->Slot170();
    Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), Info.bMarkedBy);
    ClientObjDb_RegisterGameobj(Go, false);

    // 0x0053b72c..0x0053b839: outside single player, the record's reputation group (+0x10)
    // re-gets its names -- the first FmtStr at [ESP+0x48] (constructed first, unwind state 6),
    // the second at [ESP+0x30], the display-name pointer zeroed -- and, when the second name
    // carries a mad-lib id (+0x04), sets them back with that name printed into a 0x18-char
    // stack buffer (FUN_0041c970). The return values are not read; the two FmtStrs are
    // destroyed second-first (state 5, then 1).
    if (SinglePlayer())
    {
        return;
    }
    FmtStr Name1;
    FmtStr Name2;
    wchar_t const* DisplayName = nullptr;
    Reputation::Vibe::GetName(Info.VibeGroupId, Name1, Name2, DisplayName);
    if (Name2.MadLibId != 0)
    {
        wchar_t Printed[0x18];
        FmtStrSprintf(&Name2, Printed, 0x18);
        Reputation::Vibe::SetName(Info.VibeGroupId, Name1, Name2, Printed);
    }
}

namespace
{
    // DAT_005dd55c -- 1e10, the "far away" scale the dropper tails aim with.
    constexpr float kIClientFarScale = 1.0e10f;

    // The launcher tail CREATEMINE / CREATECOUNTER (0x0053ccab..0x0053ccef, 0x0053cfb7..
    // 0x0053cffb) and CREATEGUIDED (0x0053d31e..0x0053d36b) share, on the single-player miss
    // path too: an owner id that is set and not the player's ship (FUN_00548eb0), its gameobj in
    // any bucket (FUN_005416c0(Id, 0)), the equipment wrapper of SubObjId on it (FUN_00533100)
    // with an equipment (+4). Null when any step fails. Retail then calls the wrapper's slot 5
    // (+0x14) -- FireDirect of the launcher family, the only wrappers a munition's launcher id
    // can name.
    FCliLauncherFx* FindRemoteLauncher(unsigned OwnerId, unsigned short SubObjId)
    {
        if (OwnerId == 0)
        {
            return nullptr;
        }
        if (IsPlayerShipId(OwnerId))
        {
            return nullptr;
        }
        FClientGameobj* const Owner = ClientObjDb_FindObject(OwnerId, 0);
        if (Owner == nullptr)
        {
            return nullptr;
        }
        FClientEquip* const Wrapper = EquippedObjFindEquipFx(Owner, SubObjId);
        if (Wrapper == nullptr || Wrapper->Equip == nullptr)
        {
            return nullptr;
        }
        return static_cast<FCliLauncherFx*>(Wrapper);
    }

    // CREATEMINE / CREATECOUNTER (FUN_0053ca40 / FUN_0053cd70, one body apart from the gameobj
    // type and the class mask 0xf / 0x17). The multiplayer CreateParms: the Mine build zeroes
    // Id..Pos and stores HitPts -1 first, the CounterMeasure build only the HitPts -- both then
    // overwrite every one of those, so the default CProjectile::CreateParms is the same record.
    void CreateDroppedProjectile(FClientProjectileGameobj* Go, unsigned Mask,
                                 FServerCreateProjectileInfo const* Info)
    {
        if (SinglePlayer())
        {
            // 0x0053cad3..0x0053caf1: find(&ObjId); a miss deletes the gameobj (CBase vt0(1),
            // no warning) and goes straight to the launcher tail.
            if (!Go->find(Info->ObjId))
            {
                delete Go;
                Go = nullptr;
            }
        }
        else
        {
            // 0x0053caf6..0x0053cb1a: Archetype::GetEquipment(+0x04), Root vt+0x0c load, slot 90
            // create, Archetype::Root::Release.
            Archetype::Equipment* const Arch = Archetype::GetEquipment(Info->ArchetypeNickname);
            Arch->load();
            Go->create(Arch);
            Arch->Release();
            // 0x0053cb20..0x0053cc3b: the object narrowed by Mask (no null test), the parms, its
            // vt+0xa0 init.
            CProjectile* const Shot = static_cast<CProjectile*>(NarrowGameobj(Go, Mask));
            CProjectile::CreateParms Parms;
            Parms.Id = Info->ObjId;
            Parms.ObjDb = GetClientObjDb();
            Parms.OwnerPlayer = Info->Field_0x44;
            Parms.Pos = Info->Position;
            // 0x0053cbb5 (mine) / 0x0053cec1 (counter): DAT_006789f8 tested and filled only here,
            // on the multiplayer arm.
            IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                               reinterpret_cast<FQuaternion const&>(Info->Orientation));
            Parms.Velocity = Info->Velocity;
            Parms.AngularVelocity = Info->Field_0x38;
            Parms.OwnerId = Info->OwnerId;
            Shot->init(Parms);
            // 0x0053cc41..0x0053cc4c: PhySys::RCBegin(object, +0x10, +0x2c, +0x1c, +0x38).
            PhySys::RCBegin(static_cast<CSimple*>(Shot), Info->Position, Info->Velocity,
                            Info->Orientation, Info->Field_0x38);
        }
        if (Go != nullptr)
        {
            // 0x0053cc5b..0x0053cca8: narrowed again (no null test) -- the archetype's companion
            // Load(1), FUN_00425480 on the object's engine instance (+4), slot 92,
            // FUN_00540c30(gameobj, 0).
            CObject* const Obj = NarrowGameobj(Go, Mask);
            Obj->get_archetype()->Companion->Load(1);
            Archetype::HookUpInstanceTreeMaterials(Obj->EngineInstanceId);
            Go->Slot170();
            ClientObjDb_RegisterGameobj(Go, false);
        }

        // 0x0053ccab..0x0053cd4e: the dropper that let it go fires at +0x10 + 1e10 * +0x2c. The
        // x and y products stay on the FPU; z's is stored to a float before the add.
        FCliLauncherFx* const Launcher = FindRemoteLauncher(Info->OwnerId, Info->SubObjId);
        if (Launcher == nullptr)
        {
            return;
        }
        Vector Target;
        Target.x = static_cast<float>(static_cast<double>(Info->Velocity.x) * kIClientFarScale +
                                      Info->Position.x);
        Target.y = static_cast<float>(static_cast<double>(Info->Velocity.y) * kIClientFarScale +
                                      Info->Position.y);
        float const FarZ = static_cast<float>(static_cast<double>(Info->Velocity.z) * kIClientFarScale);
        Target.z = FarZ + Info->Position.z;
        Launcher->FireDirect(&Target);
    }
}

// FUN_0053b910 (RET 8) -- ch2 CREATELOOT, Info the FServerCreateLootRequest: one Loot gameobj per
// item (count byte +0x10, FServerLootDesc stride 0x48 at +0x0c); then, for a kind-1 request,
// the owner's gameobj plays the cargo-jettisoned sound (FUN_00543f50).
void IClientImpl::SendServerCreateLoot(unsigned int /*ClientId*/, const FServerCreateLootRequest& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FServerLootDesc const* Desc = Info.Items;
    for (unsigned Left = Info.Count; Left != 0; --Left, ++Desc)
    {
        // 0x0053b960..0x0053b989: operator new(0xb0) + FUN_0053e470.
        FClientLootGameobj* const Go = new FClientLootGameobj();
        if (SinglePlayer())
        {
            // 0x0053b9a0..0x0053b9e1: find(&LootId); a miss warns (line 0x17c) and deletes the
            // gameobj (CBase vt0(1)), and the next item follows.
            if (!Go->find(Desc->LootId))
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kIClientSCpp, 0x17c,
                      "LootCreate - CObject not found.");
                delete Go;
                continue;
            }
        }
        else
        {
            // 0x0053b9e6..0x0053ba09: Archetype::GetEquipment(the container, +0x10), load,
            // slot 90 create, Archetype::Root::Release.
            Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc->ContainerNickname);
            Arch->load();
            Go->create(Arch);
            Arch->Release();
            // 0x0053ba0f..0x0053bb85: the CLoot (class & 0x803, no null test) and its vt+0xa0
            // init: Matrix identity (FUN_004215b0), Velocity / AngularVelocity / Pad58 zero,
            // OwnerPlayer 0, bCanAiTractor 1; Id, ObjDb, HitPts (+0x14), Pos (+0x1c), the
            // orientation (+0x28, vt+0x54), Velocity (+0x38), the request's OwnerId, NameId
            // (+0x18), ContentsArch (GetEquipment(+0x04)), bTemporary (+0x45), Units (+0x0c) and
            // ContentsHitPts (+0x08).
            CLoot* const Loot = static_cast<CLoot*>(NarrowGameobj(Go, 0x803));
            CLoot::CreateParms Parms;
            Parms.Id = Desc->LootId;
            Parms.ObjDb = GetClientObjDb();
            Parms.OwnerPlayer = 0;
            Parms.HitPts = Desc->HitPts;
            Parms.Pos = Desc->Position;
            IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                               reinterpret_cast<FQuaternion const&>(Desc->Orientation));
            Parms.Velocity = Desc->Velocity;
            Parms.AngularVelocity.x = 0.0f;
            Parms.AngularVelocity.y = 0.0f;
            Parms.AngularVelocity.z = 0.0f;
            Parms.Pad58 = 0;
            Parms.OwnerId = Info.OwnerId;
            Parms.NameId = static_cast<int>(Desc->NameOverride);
            Parms.ContentsArch = Archetype::GetEquipment(Desc->ContentsNickname);
            Parms.Units = Desc->Units;
            Parms.ContentsHitPts = Desc->ContentsHitPts;
            Parms.bCanAiTractor = true;
            Parms.bTemporary = (Desc->bTemporary != 0);
            Loot->init(Parms);
            // 0x0053bb8b..0x0053bbb2: PhySys::RCBegin(CLoot, +0x1c, +0x38, +0x28, a zero
            // angular velocity).
            Vector NoSpin;
            NoSpin.x = 0.0f;
            NoSpin.y = 0.0f;
            NoSpin.z = 0.0f;
            PhySys::RCBegin(Loot, Desc->Position, Desc->Velocity, Desc->Orientation, NoSpin);
        }

        // 0x0053bbbd..0x0053bc24: narrowed to 0x803 (no null test) -- the archetype's companion
        // Load(1), FUN_00425480 on get_index, slot 92, FUN_00547550(DAT_00673344, bMarkedBy
        // +0x46), FUN_00540c30(gameobj, 0).
        CObject* const Obj = NarrowGameobj(Go, 0x803);
        Obj->get_archetype()->Companion->Load(1);
        Archetype::HookUpInstanceTreeMaterials(Go->get_index());
        Go->Slot170();
        Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), Desc->bMarkedBy);
        ClientObjDb_RegisterGameobj(Go, false);
    }

    // 0x0053bc46..0x0053bc5f: a kind-1 request -- the owner's gameobj (any bucket) plays
    // FUN_00543f50. Retail calls it on whatever FUN_005416c0 returns; its body narrows to the
    // CShip itself.
    if (Info.Kind == 1)
    {
        FClientGameobj* const Owner = ClientObjDb_FindObject(Info.OwnerId, 0);
        if (Owner != nullptr)
        {
            static_cast<FClientShipGameobj*>(Owner)->PlayCargoJettisonSound();
        }
    }
}

// FUN_0053ca40 (RET 8) -- ch2 CREATEMINE, Info the FServerCreateProjectileInfo: operator
// new(0xc0) + FUN_00542550(5) + Mine's vtables (0x0053ca6d..0x0053caa4).
void IClientImpl::SendServerCreateMine(unsigned int /*ClientId*/, const FServerCreateProjectileInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    CreateDroppedProjectile(new FClientMineGameobj(), 0xf, &Info);
}

// FUN_0053d080 (RET 8) -- ch2 CREATEGUIDED, Info the FServerCreateGuidedInfo: operator new(0xc0)
// + FUN_00542550(4) + Guided's vtables (0x0053d0ad..0x0053d0e4). ClientId is not read.
void IClientImpl::SendServerCreateGuided(const unsigned int& /*ClientId*/, const FServerCreateGuidedInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FClientGuidedGameobj* Go = new FClientGuidedGameobj();
    if (SinglePlayer())
    {
        // 0x0053d10b..0x0053d129: find(&ObjId); a miss deletes the gameobj and goes to the
        // launcher tail.
        if (!Go->find(Info.ObjId))
        {
            delete Go;
            Go = nullptr;
        }
    }
    else
    {
        // 0x0053d12e..0x0053d152: Archetype::GetEquipment(+0x28), load, create, Release.
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Info.ArchetypeNickname);
        Arch->load();
        Go->create(Arch);
        Arch->Release();
        // 0x0053d158..0x0053d2a2: the CGuided (class & 0xa7, no null test) and CGuided::init
        // (the Common export, not virtual): FUN_0052e0a0's head, Id, ObjDb, OwnerPlayer +0x2c,
        // Pos +0x04, the orientation +0x10 (vt+0x54), zero velocities, OwnerId +0x30, Target =
        // FUN_005416c0(+0x20, 0), SubTarget +0x24.
        CGuided* const Guided = static_cast<CGuided*>(NarrowGameobj(Go, 0xa7));
        CGuided::CreateParms Parms;
        Parms.SubTarget = SUBOBJ_ID_NONE;
        Parms.OwnerPlayer = Info.Field_0x2c;
        Parms.Id = Info.ObjId;
        Parms.ObjDb = GetClientObjDb();
        Parms.Pos = Info.Position;
        IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                           reinterpret_cast<FQuaternion const&>(Info.Orientation));
        Parms.OwnerId = Info.OwnerId;
        FClientGameobj* const TargetGo = ClientObjDb_FindObject(Info.TargetId, 0);
        Parms.SubTarget = Info.TargetSubObjId;
        Parms.Target = static_cast<IObjRW*>(TargetGo);
        Guided->init(Parms);
        // 0x0053d2a8..0x0053d2bb: PhySys::RCBegin(CGuided, +0x04, the parms' zero velocity,
        // +0x10, the parms' zero angular velocity).
        PhySys::RCBegin(Guided, Info.Position, Parms.Velocity, Info.Orientation,
                        Parms.AngularVelocity);
    }
    if (Go != nullptr)
    {
        // 0x0053d2c6..0x0053d31b: narrowed again -- companion Load(1), FUN_00425480 on
        // get_index, slot 92, FUN_00540c30(gameobj, 0).
        CObject* const Obj = NarrowGameobj(Go, 0xa7);
        Obj->get_archetype()->Companion->Load(1);
        Archetype::HookUpInstanceTreeMaterials(Go->get_index());
        Go->Slot170();
        ClientObjDb_RegisterGameobj(Go, false);
    }

    // 0x0053d31e..0x0053d405: the launcher that fired it (owner +0x30, sub-object +0x34) fires
    // at +0x04 minus 1e10 times the orientation's third column (+0x10 through vt+0x54 again).
    // The x and y products are stored to floats before the add; z's stays on the FPU.
    FCliLauncherFx* const Launcher = FindRemoteLauncher(Info.OwnerId, Info.SubObjId);
    if (Launcher == nullptr)
    {
        return;
    }
    Matrix Orient;
    IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Orient),
                                       reinterpret_cast<FQuaternion const&>(Info.Orientation));
    float const FarX = static_cast<float>(-static_cast<double>(Orient.E[0][2]) * kIClientFarScale);
    float const FarY = static_cast<float>(-static_cast<double>(Orient.E[1][2]) * kIClientFarScale);
    Vector Target;
    Target.x = FarX + Info.Position.x;
    Target.y = FarY + Info.Position.y;
    Target.z = static_cast<float>(-static_cast<double>(Orient.E[2][2]) * kIClientFarScale +
                                  Info.Position.z);
    Launcher->FireDirect(&Target);
}

// FUN_0053cd70 (RET 8) -- ch2 CREATECOUNTER, Info the FServerCreateProjectileInfo: operator
// new(0xc0) + FUN_00542550(6) + CounterMeasure's vtables (0x0053cd9d..0x0053cdd4). ClientId is
// not read.
void IClientImpl::SendServerCreateCounter(const unsigned int& /*ClientId*/,
                                          const FServerCreateProjectileInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    CreateDroppedProjectile(new FClientCounterMeasureGameobj(), 0x17, &Info);
}

// ---------------------------------------------------------------------------------------------
// Object state (slots 52..63)
// ---------------------------------------------------------------------------------------------

// FUN_0053d480 (RET 8) -- ch2 EXPLODEASTEROIDMINE, Info = {mine id, offset}: the gameobj of
// bucket 8, its object narrowed to class bit 0 (not tested); world position + offset to slot
// 108 (+0x1b0).
void IClientImpl::SendServerExplodeAsteroidMine(unsigned int /*ClientId*/, const FMineExplosion& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.MineId, 8);
    if (Go == nullptr)
    {
        return;
    }
    CObject* const Obj = NarrowGameobj(Go, 1);
    Vector const& Offset = Info.Offset;
    Vector Pos;
    Pos.x = Offset.x + Obj->WorldPosition.x;
    Pos.y = Obj->WorldPosition.y + Offset.y;
    Pos.z = Obj->WorldPosition.z + Offset.z;
    Go->Slot1b0(&Pos);
}

// LAB_0053bd40 (RET 8) -- ch1 UPDATEOBJECT (FCommonUpdateObjectInfo): the gameobj (any bucket);
// PhySys::RCSet on its object narrowed to (class & 3) == 3 (null when not), with the position
// (+0x14), orientation (+0x04) and the timestamp (+0x20, widened to double); then, when the
// object is a CShip (0x503), CShip::set_throttle(+0x24) and CShip+0x1a0 = the byte +0x28.
void IClientImpl::SendCommonUpdateObject(unsigned int /*ClientId*/, const FCommonUpdateObjectInfo& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    CSimple* const Simple = static_cast<CSimple*>(NarrowGameobj(Go, 3));
    PhySys::RCSet(Simple, Info.Position, Info.Orientation, static_cast<double>(Info.Field_0x20));
    CShip* const Ship = static_cast<CShip*>(NarrowGameobj(Go, 0x503));
    if (Ship == nullptr)
    {
        return;
    }
    Ship->set_throttle(Info.Field_0x24);
    Ship->BehaviorIndex = static_cast<int>(Info.Field_0x28);
}

// FUN_0053bc80 (RET 8) -- ch2 DESTROYOBJECT, Info = {id, destroy type}. With the gameobj (any
// bucket): the player's ship asks for a zero hull status on type 1 (FUN_004c4f50(0)) and
// captures the camera view (FUN_0054b900); any other gameobj is owned by this connection
// (FUN_005397f0) and its object narrowed to (class & 3) == 3 (not tested) re-connects to the
// client ObjDB (CSimple vt+0x90 connect(PTR_DAT_00613ed0)). Then slot 103 (+0x19c)(type) and,
// outside single player for type 0, FUN_00541040 and the gameobj deleted (CBase vt0(1)).
void IClientImpl::SendServerDestroyObject(unsigned int /*ClientId*/, const FServerDestroyObjectInfo& Info)
{
    // Retail reads the destroy type as the whole dword at +0x04 (0x0053bcac CMP dword ptr
    // [EDI+0x4],1), i.e. Field_0x04 together with the three pad bytes after it.
    unsigned int DestroyType = 0;
    memcpy(&DestroyType, reinterpret_cast<unsigned char const*>(&Info) + 4, sizeof(DestroyType));
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    if (IsPlayerShipId(Info.ObjectId))
    {
        if (DestroyType == 1)
        {
            ReqPlayerHullStatusRetail(0.0f);
        }
        SpaceAppArmDeathCamera(); // 0x0053bcbc FUN_0054b900
    }
    else
    {
        Go->SetOwnerPlayer(GetPlayerConnectionId());
        CSimple* const Simple = static_cast<CSimple*>(NarrowGameobj(Go, 3));
        Simple->connect(GetClientObjDb());
    }
    Go->Slot19c(static_cast<int>(DestroyType));
    if (SinglePlayer() || DestroyType != 0)
    {
        return;
    }
    ClientObjDb_Unregister(Go);
    delete Go;
}

// FUN_0053d940 (RET 8) -- ch2 ACTIVATEOBJECT; the body is TradeLaneSPObjActivate
// (tradelane_fx.cpp). Retail reads the argument with no null test.
void IClientImpl::SendServerActivateObject(unsigned int /*ClientId*/, const FPayloadServerActivateObject& Info)
{
    TradeLaneSPObjActivate(Info.ObjectId, Info.bActivate != 0, Info.Field_0x08);
}

// LAB_0053d500 (RET 8) -- ch2 SYSTEM_SWITCH_OUT, Info = {ship id, gate id}.
void IClientImpl::SendServerSystemSwitchOut(unsigned int /*ClientId*/, const FServerSystemSwitchOutInfo& Info)
{
    // 0x0053d504..0x0053d51b: the ship (bucket 1); none returns.
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.ShipId, 1));
    if (Ship == nullptr)
    {
        return;
    }

    // 0x0053d521..0x0053d557: the gate (bucket 2); none is an ERROR, and the handler goes on
    // (retail reads [EDI+0x1c] next with no test).
    FClientGameobj* const Gate = ClientObjDb_FindObject(Info.GateId, 2);
    if (Gate == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kIClientSCpp, 0x54e,
              "No facilitator found for SystemSwitchOut");
    }
    // 0x0053d55a..0x0053d581: a gate gameobj that is not a solar (ManagerKind 2) warns.
    if (Gate->ManagerKind != 2)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kIClientSCpp, 0x550,
              "SystemSwitchOut, obj is not a Solar?");
    }

    // 0x0053d585..0x0053d5fa: the player's CEqObj (0x103, no null test) behavior manager, when
    // it has one, hears event 3 with {gate id, gate type, ship id}.
    FClientGameobj* const Player = static_cast<FClientGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Player != nullptr)
    {
        CEqObj* const PlayerEq = static_cast<CEqObj*>(NarrowGameobj(Player, 0x103));
        IBehaviorManager* const Behavior = PlayerEq->get_behavior_interface();
        if (Behavior != nullptr)
        {
            unsigned int Block[3];
            Block[0] = Info.GateId;
            Block[2] = Info.ShipId;
            unsigned GateType = 0;
            Gate->get_type(GateType);
            Block[1] = GateType;
            FBehaviorManagerEvent Event;
            Event.Type = 3;
            Event.Data = Block;
            Behavior->Slot00(&Event);
        }
    }

    // 0x0053d5fc..0x0053d638: any ship but the player's: owned by this connection (FUN_0054bb00
    // = DAT_00673344, FUN_005397f0) and control exclusion 1 on its CEqObj (0x103, no null test).
    if (!IsPlayerGameobj(Ship))
    {
        Ship->SetOwnerPlayer(GetPlayerConnectionId());
        CEqObj* const ShipEq = static_cast<CEqObj*>(NarrowGameobj(Ship, 0x103));
        ShipEq->set_control_exclusion(1);
    }

    // 0x0053d63e..0x0053d66e: the gate's type (vt+0x88): bit 0x40 or 0x800 -> the jump out
    // through it (FUN_00543530); otherwise the system switch straight away (FUN_00543750).
    unsigned Type = 0;
    Gate->get_type(Type);
    if ((Type & 0x40u) != 0 || (Type & 0x800u) != 0)
    {
        Ship->BeginJumpOut(Gate);
    }
    else
    {
        Ship->RequestSystemSwitch(Gate);
    }
}

// LAB_0053d680 (RET 8) -- ch2 SYSTEM_SWITCH_IN.
void IClientImpl::SendServerSystemSwitchIn(unsigned int /*ClientId*/, const FPayloadServerSystemSwitchIn& Info)
{
    // 0x0053d684..0x0053d69a: the object (any bucket); none returns.
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.Field_0x00, 0));
    if (Ship == nullptr)
    {
        return;
    }

    // 0x0053d6a8..0x0053d6c7 / 0x0053d6e2..0x0053d701: the orientation (+0x14) to a matrix
    // (vt+0x54) on both paths.
    Matrix Orient;
    IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Orient),
                                       reinterpret_cast<FQuaternion const&>(Info.Orientation));

    // 0x0053d69c..0x0053d6a6: jump type bits 0x40 or 0x800 -> the jump in (FUN_00543630);
    // otherwise the airlock (FUN_005437a0), both at +0x08.
    if ((Info.Field_0x04 & 0x40u) != 0 || (Info.Field_0x04 & 0x800u) != 0)
    {
        Ship->BeginJumpIn(&Info.Position, &Orient);
    }
    else
    {
        Ship->AirlockIn(Info.Position, Orient);
    }
}

// FUN_0053d720 (RET 8) -- ch2 LAND, Info = {ship id, dock id, base id}.
void IClientImpl::SendServerLand(unsigned int /*ClientId*/, const FServerLandInfo& Info)
{
    // 0x0053d725..0x0053d737: the ship (any bucket); none returns.
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.ShipId, 0));
    if (Ship == nullptr)
    {
        return;
    }

    // 0x0053d73d..0x0053d797: the dock (any bucket). When there is one: the player's ship enters
    // the dock's base at once (FUN_0054bc60 with the dock CEqObj's +0x164, 0x303 narrowing with
    // no test); any other ship is owned by this connection (FUN_005397f0 with FUN_0054bb00).
    FClientGameobj* const Dock = ClientObjDb_FindObject(Info.DockId, 0);
    if (Dock != nullptr)
    {
        if (IsPlayerGameobj(Ship))
        {
            CEqObj* const DockEq = static_cast<CEqObj*>(NarrowGameobj(Dock, 0x303));
            EnterBaseFromDock(&DockEq->DockingPointBaseId);
        }
        else
        {
            Ship->SetOwnerPlayer(GetPlayerConnectionId());
        }
    }

    // 0x0053d799..0x0053d7c5: outside single player the ship's remote-control session ends --
    // PhySys::RCEnd on its cobject (vt+0x150) narrowed by (class & 3) == 3.
    if (!SinglePlayer())
    {
        CObject* const Obj = const_cast<CObject*>(Ship->cobject());
        PhySys::RCEnd(static_cast<CSimple*>(NarrowObject(Obj, 3)));
    }

    // 0x0053d7c8..0x0053d7cf: FUN_00543b40(dock, &base id).
    Ship->RequestLand(Dock, &Info.BaseId);
}

// LAB_0053d7e0 (RET 8) -- ch2 LAUNCH (FServerLaunchInfo).
void IClientImpl::SendServerLaunch(unsigned int /*ClientId*/, const FServerLaunchInfo& Info)
{
    // 0x0053d7e5..0x0053d7fa / 0x0053d8e8..0x0053d906: the ship (bucket 1); none warns (line
    // 0x5c4) and returns.
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.Field_0x00, 1));
    if (Ship == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Launch (): launching object %d not found.",
              kIClientSCpp, 0x5c4, Info.Field_0x00);
        return;
    }

    // The orientation (+0x0c) to a matrix (vt+0x54), and the ship's object (class bit 0, no null
    // test) beamed to +0x1c, not attached (vt+0x7c) -- outside single player when launching
    // from a dock, always when not.
    if (Info.Field_0x04 != 0)
    {
        // 0x0053d80c..0x0053d85f.
        if (!SinglePlayer())
        {
            Matrix Orient;
            IClientSMath()->QuaternionToMatrix(
                reinterpret_cast<FMatrix&>(Orient),
                reinterpret_cast<FQuaternion const&>(Info.Orientation));
            NarrowGameobj(Ship, 1)->beam_object(Info.Position, Orient, false);
        }
        // 0x0053d862..0x0053d877: the dock (any bucket) and the launch point to FUN_00543d50.
        Ship->LaunchFromDock(ClientObjDb_FindObject(Info.Field_0x04, 0),
                             static_cast<int>(Info.Field_0x08));
    }
    else
    {
        // 0x0053d88c..0x0053d8d5.
        Matrix Orient;
        IClientSMath()->QuaternionToMatrix(
            reinterpret_cast<FMatrix&>(Orient),
            reinterpret_cast<FQuaternion const&>(Info.Orientation));
        NarrowGameobj(Ship, 1)->beam_object(Info.Position, Orient, false);
    }

    // 0x0053d87e / 0x0053d8da: DAT_00678b96 = 1.
    SetPlayerLaunchAcknowledged(true);
}

// LAB_0053d920 (RET 0xc) -- ch2 REQUESTCREATESHIPRESP: DAT_00678b95 = 1, DAT_00678b94 =
// (byte)bSuccess, DAT_00673378 = ShipId.
void IClientImpl::SendServerRequestCreateShipResp(unsigned int /*ClientId*/, bool bSuccess, unsigned int ShipId)
{
    SetPlayerCreateShipResponse(static_cast<unsigned char>(bSuccess), ShipId);
}

// LAB_0053d9d0 (RET 8) -- ch2 REQUEST_SPACE_SCRIPT. The first argument is never read.
void IClientImpl::SendServerRequestSpaceScript(unsigned int /*ClientId*/, const FServerRequestSpaceScriptInfo& Info)
{
    // 0x0053d9d8..0x0053d9e7: FUN_005416c0(+0x000, 0); no object -> nothing.
    FClientGameobj* const Object = ClientObjDb_FindObject(Info.Field_0x000, 0);
    if (Object == nullptr)
    {
        return;
    }

    // 0x0053d9ed..0x0053d9ff: mode 1 -- FUN_0054bb10(object, 0, name): the script camera keeps
    // the transform it has.
    if (Info.Field_0x114 == 1u)
    {
        SetupSpaceScriptCamera(Object, nullptr, Info.Name);
        return;
    }

    // 0x0053da0e..0x0053da7f: mode 2 -- twelve floats on the stack, an identity orientation
    // (1.0f at +0x00 / +0x10 / +0x20, zero elsewhere) then the position from +0x108..+0x110, and
    // FUN_0054bb10(object, &transform, name). Any other mode does nothing.
    if (Info.Field_0x114 != 2u)
    {
        return;
    }
    float Transform[12];
    Transform[0] = 1.0f;
    Transform[1] = 0.0f;
    Transform[2] = 0.0f;
    Transform[3] = 0.0f;
    Transform[4] = 1.0f;
    Transform[5] = 0.0f;
    Transform[6] = 0.0f;
    Transform[7] = 0.0f;
    Transform[8] = 1.0f;
    memcpy(&Transform[9], &Info.Field_0x108, sizeof(float));
    memcpy(&Transform[10], &Info.Field_0x10c, sizeof(float));
    memcpy(&Transform[11], &Info.Field_0x110, sizeof(float));
    SetupSpaceScriptCamera(Object, Transform, Info.Name);
}

// LAB_0053bdd0 (RET 0xc) -- ch2 DAMAGEOBJECT, Damage the DamageList. The gameobj (any bucket) takes
// the damage (slot 102 +0x198); for the player's ship (FUN_00548eb0) every entry then updates
// the player's own records, through its CEqObj (0x103, no null test):
//   an equipment id (SubObjectID::IsEquipID) with fate 0: the CEquip (FindByID) re-read into a
//     fresh EquipDesc (CEquip vt+0x28) and, when that succeeds, FUN_004c4590 -- the argument
//     shuffle into FUN_004c43d0 -- with {Id, &Hardpoint, Count, Health, bEquipped}; no CEquip:
//     FUN_004c4320(0, id, -1);
//   an equipment id with any other fate: FUN_004c4320(0, id, -1);
//   a collision group (IsArchGroupID): its CArchGroup's relative health to FUN_004c4000;
//   the root (SUBOBJ_ID_ROOT): CSimple::get_relative_health to FUN_004c4f80.
// 0x0053be35 constructs one more EquipDesc that nothing reads.
void IClientImpl::SendServerDamageObject(unsigned int /*ClientId*/, unsigned int ObjectId, const DamageList& Damage)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    Go->Slot198(Damage);
    if (!IsPlayerShipId(ObjectId))
    {
        return;
    }
    CEqObj* const Eq = static_cast<CEqObj*>(NarrowGameobj(Go, 0x103));
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It)
    {
        unsigned short const Id = It->SubObjId;
        if (SubObjectID::IsEquipID(Id))
        {
            if (It->Fate != DamageEntry::SubObjFate::None)
            {
                RemovePlayerEquipRetail(0, Id, -1);
                continue;
            }
            CEquip* const Equip = Eq->EquipManager.FindByID(Id);
            if (Equip == nullptr)
            {
                RemovePlayerEquipRetail(0, Id, -1);
                continue;
            }
            EquipDesc Desc;
            if (Equip->GetEquipDesc(Desc))
            {
                ModifyPlayerEquipRetail(Desc.Id, &Desc.get_hardpoint(), Desc.Count, Desc.Health,
                                        Desc.bEquipped);
            }
        }
        else if (SubObjectID::IsArchGroupID(Id))
        {
            CArchGroup* const Group = Eq->ArchGroups.FindByID(Id);
            if (Group != nullptr)
            {
                SetPlayerCollisionGroupHealthRetail(Id, Group->GetRelativeHealth());
            }
        }
        else if (Id == SUBOBJ_ID_ROOT)
        {
            SetPlayerHealthScaleRetail(Eq->get_relative_health());
        }
    }
}

// FUN_0053c5d0 (RET 8) -- ch2 ITEMTRACTORED (FPayloadServerItemTractored). The tractoring ship
// (any bucket): FUN_00533100(tractor equip id) finds its equipment-FX wrapper (EquippedObj
// +0xb8 list, wrapper +4 == the CEquip) and FUN_0052f9a0(loot id, units) lets the tractor drop
// the loot (CETractor::GetTractorArm / RemoveTarget). Then, outside single player, the loot (any
// bucket) narrowed to CLoot (0x803, not tested) loses the units (+0xf0).
void IClientImpl::SendServerItemTractored(unsigned int /*ClientId*/, const FPayloadServerItemTractored& Info)
{
    FClientGameobj* const Tractoring = ClientObjDb_FindObject(Info.Field_0x04, 0);
    if (Tractoring != nullptr)
    {
        // 0x0053c5e7..0x0053c603: FUN_00533100 with the word at +8; a wrapper -> FUN_0052f9a0
        // with the loot id (+0) and the word at +0xa (MOVZX).
        FClientEquip* const Wrapper = EquippedObjFindEquipFx(Tractoring, Info.Field_0x08);
        if (Wrapper != nullptr)
        {
            CliTractorEquipFxDropLoot(Wrapper, Info.Field_0x00,
                                      static_cast<int>(Info.Field_0x0a));
        }
    }
    if (SinglePlayer())
    {
        return;
    }
    FClientGameobj* const LootGo = ClientObjDb_FindObject(Info.Field_0x00, 0);
    if (LootGo == nullptr)
    {
        return;
    }
    CLoot* const Loot = static_cast<CLoot*>(NarrowGameobj(LootGo, 0x803));
    Loot->Units -= Info.Field_0x0a;
}

// LAB_0053c660 (RET 8) -- ch2 USE_ITEM (FPayloadServerUseItem). Outside single player: the
// gameobj's CEqObj (0x103, not tested), the item's CECargo (FindByID + CECargo::cast), and
// CECargo::RemoveFromStack(count). Then, for the player's ship, the player's own item
// (FUN_004c45b0 = EquipDescList::find_equipment_item on DAT_00672960): FUN_004c4320(1, id,
// count) while it holds more than count, else FUN_004c4320(0, id, count).
void IClientImpl::SendServerUseItem(unsigned int /*ClientId*/, const FPayloadServerUseItem& Info)
{
    if (!SinglePlayer())
    {
        FClientGameobj* const Go = ClientObjDb_FindObject(Info.Field_0x00, 0);
        if (Go != nullptr)
        {
            CEqObj* const Eq = static_cast<CEqObj*>(NarrowGameobj(Go, 0x103));
            CECargo* const Cargo = CECargo::cast(Eq->EquipManager.FindByID(Info.Field_0x04));
            if (Cargo != nullptr)
            {
                Cargo->RemoveFromStack(Info.Field_0x06);
            }
        }
    }
    if (!IsPlayerShipId(Info.Field_0x00))
    {
        return;
    }
    EquipDesc* const Desc = GetPlayerEquipDescListRetail().find_equipment_item(Info.Field_0x04);
    if (Desc == nullptr)
    {
        return;
    }
    int const Count = static_cast<int>(Info.Field_0x06);
    if (Desc->Count > Count)
    {
        RemovePlayerEquipRetail(1, Desc->Id, Count);
        return;
    }
    RemovePlayerEquipRetail(0, Desc->Id, Count);
}

// ---------------------------------------------------------------------------------------------
// Comm / reputation / cargo (slots 68..89)
// ---------------------------------------------------------------------------------------------

// LAB_0053bfb0 (RET 0x5c) -- ch2 SENDCOMM. CommCostume is the sender's Costume by value (four
// parts, eight accessories, the count -- 13 of the 23 dwords), TextIds the text-id array.
// hud_comm.cpp ClientSendComm.
void IClientImpl::SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                                     unsigned int VoiceId, Costume CommCostume, unsigned int CommType,
                                     const unsigned int* TextIds, int TextCount, unsigned int Mood,
                                     float Duration, bool bReceiverIsOwnShip)
{
    ClientSendComm(ClientId, SenderId, ReceiverId, VoiceId, CommCostume, CommType, TextIds, TextCount,
                   Mood, Duration, bReceiverIsOwnShip);
}

// LAB_0053c2a0 (RET 8) -- ch2 SENDNNMESSAGE: FUN_00489e80(MessageId, 0), the NeuroNet voice queue.
void IClientImpl::SendServerSendNNMessage(unsigned int /*ClientId*/, unsigned int MessageId)
{
    NnVoiceEnqueue(MessageId, 0);
}

// LAB_0053de70 (RET 8): when the hint's Kind (+4) is 1, its Costume (+8, 0xd dwords -- the
// CostumeHint's Cost) is copied out and handed to FUN_004cc900 (hud_comm.cpp
// HudCommPreloadCostume). Kind is read through the pointer with no null test (0053de74),
// although the receivers pass NULL for any wire Kind but 1 -- kept as retail has it.
void IClientImpl::SendServerLoadHint(const unsigned int& /*ClientId*/, BaseHint* Hint)
{
    if (Hint->Kind == 1)
    {
        Costume Copy;
        memcpy(&Copy, &static_cast<CostumeHint*>(Hint)->Cost, sizeof(Copy));
        HudCommPreloadCostume(&Copy);
    }
}

// FUN_0053da90 (RET 8) -- ch2 SETGROUPFEELINGS, Info = FServerSetGroupFeelingsInfo
// (rpc_iclient_args.h: char name[0x10], +0x10 Relation*, +0x14 byte count): the relations
// copied into operator new(count * 8), the name into a TString<16> (strlen clamped to 15),
// Reputation::set(name, copy, count), the copy freed.
void IClientImpl::SendServerSetGroupFeelings(unsigned int /*ClientId*/, const FServerSetGroupFeelingsInfo& Info)
{
    // 0x0053da98..0x0053daa3: operator new(count * 8), untested; zero count still allocates.
    Reputation::Relation* const Copy = static_cast<Reputation::Relation*>(
        ::operator new(static_cast<unsigned int>(Info.RelationCount) * sizeof(Reputation::Relation)));
    // 0x0053dab6..0x0053dad2: dword index against the byte count, re-read every pass; the
    // array pointer is re-read too.
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(Info.RelationCount); ++Index)
    {
        Copy[Index] = Info.Relations[Index];
    }
    // 0x0053dad4..0x0053db0c: TString<16> = the name (strlen clamped to 15).
    TString<16> Name;
    Name = Info.Name;
    // 0x0053db10..0x0053db1b: Reputation::set(name, copy, count byte).
    Reputation::set(Name, Copy, Info.RelationCount);
    // 0x0053db22: FUN_005b7e1d (operator delete).
    ::operator delete(Copy);
}

// LAB_0053bf80 (RET 0x14) -- ch2 REQUEST_RETURNED: the ship (any bucket), then its
// FUN_00546d00(RequestType, Response, Extra) -- the ship's pending-request answer ("stale
// request detected"). The gameobj is not class-tested before the ship method runs on it
// (0x0053bfa2).
void IClientImpl::SendServerRequestReturned(const unsigned int& /*ClientId*/, unsigned int ShipId, int RequestType,
                                            int Response, int Extra)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ShipId, 0);
    if (Go == nullptr)
    {
        return;
    }
    static_cast<FClientShipGameobj*>(Go)->RequestReturned(RequestType, Response, Extra);
}

// FUN_0053deb0 (RET 0x10) -- ch2 REMOTEOBJECTCARGOUPDATE: the gameobj's cobject (vt+0x150) when
// it is a CShip (0x503), CEqObj::sync_cargo(EquipDescList(Cargo)). Flag is not read.
void IClientImpl::SendServerRemoteObjectCargoUpdate(const unsigned int& /*ClientId*/, unsigned int ObjectId,
                                                    const EquipDescVector& Cargo, unsigned char /*Flag*/)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    CEqObj* const Eq =
        static_cast<CEqObj*>(NarrowObject(const_cast<CObject*>(Go->cobject()), 0x503));
    if (Eq == nullptr)
    {
        return;
    }
    EquipDescList const CargoList(Cargo);
    Eq->sync_cargo(CargoList);
}

// FUN_0053df80 (RET 0xc) -- ch2 OBJECTCARGOUPDATE, Item the EquipDesc. Outside single player: the
// gameobj's CEqObj (0x103, not tested); the item's CEquip (FindByID) when it is a CECargo gets
// SetCount(Count) and CEquip vt+0x40 SetHitPoints(Health * its archetype's HitPts); no CEquip
// at all: CEqObj vt+0xb0 add_item(desc). Then, for the player's ship, the player's own item:
// FUN_004c4590 (= FUN_004c43d0) with {Id, &Hardpoint, Count, Health, false} while Count > 0,
// else FUN_004c4320(0, id, -1); no such item: FUN_004c4170(0, desc).
void IClientImpl::SendServerObjectCargoUpdate(const unsigned int& /*ClientId*/, unsigned int ObjectId,
                                              const EquipDesc& Item)
{
    EquipDesc const* const Desc = &Item;
    if (!SinglePlayer())
    {
        FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
        if (Go != nullptr)
        {
            CEqObj* const Eq = static_cast<CEqObj*>(NarrowGameobj(Go, 0x103));
            CEquip* const Equip = Eq->EquipManager.FindByID(Desc->Id);
            if (Equip == nullptr)
            {
                Eq->add_item(*Desc);
            }
            else
            {
                CECargo* const Cargo = CECargo::cast(Equip);
                if (Cargo != nullptr)
                {
                    Cargo->SetCount(static_cast<unsigned>(Desc->Count));
                    Cargo->SetHitPoints(Desc->Health * Cargo->Arch->HitPts);
                }
            }
        }
    }
    if (!IsPlayerShipId(ObjectId))
    {
        return;
    }
    if (GetPlayerEquipDescListRetail().find_equipment_item(Desc->Id) == nullptr)
    {
        AddPlayerEquipRetail(0, Desc); // 0x0053e07e..0x0053e081
        return;
    }
    if (Desc->Count > 0)
    {
        ModifyPlayerEquipRetail(Desc->Id, &Desc->get_hardpoint(), Desc->Count, Desc->Health, false);
        return;
    }
    RemovePlayerEquipRetail(0, Desc->Id, -1);
}

// FUN_0053e090 (RET 8) -- ch2 BURNFUSE (FServerBurnFuseInfo): the gameobj (any bucket, used as
// an EquippedObj, no class test); the fuse id (+0x08) is copied into the argument's own stack
// slot as an ID_String (0x0053e0b5 / 0x0053e0df). Lit (byte +0x18): slot 112 (+0x1c0)(SourceId,
// &FuseId, (word) SubObjId, AtT, Lifetime); else slot 113 (+0x1c4)(&FuseId, (word) SubObjId,
// AtT). Both results are dropped.
void IClientImpl::SendServerBurnFuse(const unsigned int& /*ClientId*/, const FServerBurnFuseInfo& Info)
{
    FClientGameobj* const Found = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Found == nullptr)
    {
        return;
    }
    FClientEquippedObj* const Equipped = static_cast<FClientEquippedObj*>(Found);
    ID_String FuseId;
    FuseId.Id = Info.FuseId;
    if (Info.bLit != 0)
    {
        Equipped->Slot1c0(Info.SourceId, &FuseId, Info.SubObjId, Info.AtT, Info.Lifetime);
        return;
    }
    Equipped->Slot1c4(&FuseId, Info.SubObjId, Info.AtT);
}

// LAB_0053e100 (RET 8) -- ch2 SET_CAMERA: Info is {Position (3 floats), Orient quaternion at
// +0x0c, Fov, Aspect}. The quaternion to a matrix (DAT_006789f8 slot 0x54 QuaternionToMatrix,
// the result unread), a 12-dword Csys {matrix, position}, then FUN_0054bb50(&Csys) -- the static
// camera (space_app.h SetStaticCameraTransform). Fov and Aspect are not read.
void IClientImpl::SendServerSetCamera(const unsigned int& /*ClientId*/, const FPayloadServerSetCamera& Info)
{
    FMatrix Orientation;
    IClientSMath()->QuaternionToMatrix(Orientation, reinterpret_cast<FQuaternion const&>(Info.Orient));
    // 0x0053e12c..0x0053e150: REP MOVSD of the nine matrix dwords, then +0x00 / +0x04 / +0x08.
    float Csys[12];
    memcpy(Csys, &Orientation, 9 * sizeof(float));
    Csys[9] = Info.Position.x;
    Csys[10] = Info.Position.y;
    Csys[11] = Info.Position.z;
    SetStaticCameraTransform(Csys);
}

// LAB_0053e170 (RET 4) -- ch2 REVERT_CAMERA: FUN_0054bc10("player").
void IClientImpl::SendServerRevertCamera(const unsigned int& /*ClientId*/)
{
    RequestSpaceCameraMode("player");
}

// ---------------------------------------------------------------------------------------------
// ch1 common traffic (slots 91..100)
// ---------------------------------------------------------------------------------------------

// LAB_0053c310 (RET 0xc) -- SET_WEAPON_GROUP: DAT_00674c0c FUN_004f5e50(Data, Size).
void IClientImpl::SendCommonSetWeaponGroup(unsigned int /*ClientId*/, const unsigned char* Data, unsigned int Size)
{
    G_WeaponGroups.ReadFromServer(reinterpret_cast<char const*>(Data), static_cast<int>(Size));
}

// FUN_0053c330 (RET 0xc) -- SET_VISITED_STATE, Data = {int count, then count records of 5 bytes
// {dword id, byte flags}}: FUN_004c4e00(id, flags, 0) for each. Size is not read.
void IClientImpl::SendCommonSetVisitedState(unsigned int /*ClientId*/, unsigned char* Data, unsigned int /*Size*/)
{
    unsigned char const* const Info = Data;
    int Count = 0;
    memcpy(&Count, Info, sizeof(Count));
    unsigned char const* Record = Info + 4;
    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned Id = 0;
        memcpy(&Id, Record, sizeof(Id));
        SetPlayerVisitFlagsRetail(Id, Record[4], false);
        Record += 5;
    }
}

// FUN_0053c370 (RET 0xc) -- REQUEST_BEST_PATH, Data = {+4 count, +8 byte, +0xc records of 0x14
// {Vector, +0x0c, +0x10}}: DAT_006720a8 = the byte, FUN_004c46c0 (waypoints reset), then
// FUN_004c4780(&rec, rec+0x10, rec+0x0c) for each record but the last, which gets
// FUN_004c4ea0() in place of rec+0x0c. The count is re-read every pass. Size is not read.
void IClientImpl::SendCommonRequestBestPath(unsigned int /*ClientId*/, const unsigned char* Data,
                                            unsigned int /*Size*/)
{
    unsigned char const* const Info = Data;
    G_bNavMapNoBestPath = Info[8];
    ResetPlayerWaypointsRetail();
    unsigned char const* Record = Info + 0xc;
    for (int Index = 0;; ++Index)
    {
        int Count = 0;
        memcpy(&Count, Info + 4, sizeof(Count));
        if (Index >= Count)
        {
            break;
        }
        Vector Pos;
        memcpy(&Pos, Record, sizeof(Pos));
        unsigned Field0c = 0;
        unsigned Field10 = 0;
        memcpy(&Field0c, Record + 0xc, sizeof(Field0c));
        memcpy(&Field10, Record + 0x10, sizeof(Field10));
        if (Index == Count - 1)
        {
            AddPlayerWaypointRetail(Pos, Field10, GetPlayerWaypointTailTagRetail());
        }
        else
        {
            AddPlayerWaypointRetail(Pos, Field10, Field0c);
        }
        Record += 0x14;
    }
}

// LAB_0053c3d0 (RET 0xc) -- REQUEST_PLAYER_STATS: FUN_004a8e40(Data, Data+0x34, Data+0x34 +
// [Data+0x20]*8) -- the block, its ship-kill table and its reputation table. ClientId and Size
// are not read. FUN_004a8e40 (nn_player_info.cpp) hands all three to NN_PlayerInfo when one is
// open and otherwise the reputation table to NN_Dealer's FUN_00483f30.
void IClientImpl::SendCommonRequestPlayerStats(unsigned int /*ClientId*/, const unsigned char* Data,
                                               unsigned int /*Size*/)
{
    unsigned char const* const Stats = Data;
    unsigned ShipKillCount = 0;
    memcpy(&ShipKillCount, Stats + 0x20, sizeof(ShipKillCount));
    unsigned char const* const ShipKills = Stats + 0x34;
    NnPlayerInfoOnPlayerStats(Stats, ShipKills, ShipKills + ShipKillCount * 8);
}

// LAB_0053c3f0 (RET 0x10) -- POP_UP_DIALOG: FUN_0048a380(&Title, &Text, Buttons) -- the title
// FmtStr, the text FmtStr and the button bits (Server pub::Player::PopUpDialog / rpclocal
// DeliverPopUpDialog pass them in that order).
void IClientImpl::SendCommonPopUpDialog(unsigned int /*ClientId*/, const FmtStr& Title, const FmtStr& Text,
                                        unsigned int Buttons)
{
    NnMissionMessagePopUp(&Title, &Text, Buttons);
}

// LAB_0053c410 (RET 0xc) -- REQUEST_GROUP_POSITIONS: FUN_0048d8a0(Data, Size). FUN_0048d8a0
// reads only its first argument (the list); Size is pushed and never read.
void IClientImpl::SendCommonRequestGroupPositions(unsigned int /*ClientId*/, unsigned char* Data,
                                                  unsigned int /*Size*/)
{
    NavMapSetGroupPositions(Data);
}

// LAB_0053c430 (RET 0xc) -- SET_MISSION_LOG: FUN_004ba430(Data, Size) (0x0053c43a), the story
// star's history replaced by the packed log.
void IClientImpl::SendCommonSetMissionLog(unsigned int /*ClientId*/, unsigned char* Data, unsigned int Size)
{
    StoryStarLoadHistory(Data, static_cast<int>(Size));
}

// LAB_0053c450 (RET 0xc) -- SET_INTERFACE_STATE: FUN_004c8190(*Data) (the first dword).
void IClientImpl::SendCommonSetInterfaceState(unsigned int /*ClientId*/, unsigned char* Data,
                                              unsigned int /*Size*/)
{
    AssignGameSessionFlags(*reinterpret_cast<unsigned const*>(Data));
}

// LAB_0053c460 (RET 0xc) -- REQUEST_RANK_LEVEL: FUN_004c55e0(*Data) (the first dword).
void IClientImpl::SendCommonRequestRankLevel(unsigned int /*ClientId*/, unsigned char* Data,
                                             unsigned int /*Size*/)
{
    SetPlayerRankLevelRetail(*reinterpret_cast<int const*>(Data));
}

// FUN_0053db60 (RET 8) -- ch2 SETDIRECTIVE (FPayloadServerSetDirective). With the player's ship
// (FUN_0054baf0; none does nothing): its CEqObj (0x503, no test) behavior manager, then the kind
// (+0x00): 0..4 build the stack op with its default ctor, copy the payload's fields over it and
// IBehaviorManager::set_current_directive(info, &op); 5 sets the directive with no op and zeroes
// the current behavior's throttle and cruise; any other kind does nothing. The IDirectiveInfo
// is {1, 0, type} (type 0x14 delay, 7 dock, 0x11 follow, 2 goto, 6 idle), for cancel {0, 0, -1}.
void IClientImpl::SendServerSetDirective(const unsigned int& /*ClientId*/, const FPayloadServerSetDirective& Info)
{
    void* const PlayerObj = GetPlayerObjRW();
    if (PlayerObj == nullptr)
    {
        return;
    }
    FClientGameobj* const Player = static_cast<FClientGameobj*>(static_cast<IObjRW*>(PlayerObj));
    CEqObj* const PlayerEq = static_cast<CEqObj*>(NarrowGameobj(Player, 0x503));
    IBehaviorManager* const Behavior = PlayerEq->get_behavior_interface();

    switch (Info.Field_0x00)
    {
    case 0:
    {
        // caseD_0 0x0053ddcf.
        pub::AI::DirectiveCancelOp Op;
        Behavior->set_current_directive(MakeDirectiveInfo(0u, 0xffffffffu), &Op);
        break;
    }
    case 1:
    {
        // caseD_1 0x0053dbf8.
        pub::AI::DirectiveDelayOp Op;
        Op.DelaySeconds = Info.Field_0x0c;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 0x14u), &Op);
        break;
    }
    case 2:
    {
        // caseD_2 0x0053dc26.
        pub::AI::DirectiveDockOp Op;
        Op.DockObjectId = Info.Field_0x10;
        Op.Field_10 = Info.Field_0x14;
        Op.Field_14 = Info.Field_0x08;
        Op.DockPortIndex = Info.Field_0x18;
        Op.Field_1C = static_cast<int32_t>(Info.Field_0x1c);
        Op.Field_20 = Info.Field_0x20;
        Op.Field_24 = Info.Field_0x24;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 7u), &Op);
        break;
    }
    case 3:
    {
        // caseD_3 0x0053ddef.
        pub::AI::DirectiveFollowOp Op;
        Op.TargetId = Info.Field_0x28;
        Op.FollowDistance = Info.Field_0x2c;
        Op.Field_14 = Info.Field_0x30.x;
        Op.Field_18 = Info.Field_0x30.y;
        Op.Field_1C = Info.Field_0x30.z;
        Op.Field_20 = Info.Field_0x3c;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 0x11u), &Op);
        break;
    }
    case 4:
    {
        // caseD_4 0x0053dc7e.
        pub::AI::DirectiveGotoOp Op;
        Op.Field_0C = Info.Field_0x40;
        Op.Destination = Info.Field_0x44;
        Op.Field_1C = Info.Field_0x50;
        Op.Field_20 = Info.Block_0x54.Field_0x00;
        Op.Field_24 = Info.Block_0x54.Field_0x04;
        Op.Field_28 = Info.Block_0x54.Field_0x08;
        Op.Field_2C = Info.Block_0x60.Field_0x00;
        Op.Field_30 = Info.Block_0x60.Field_0x04;
        Op.Field_34 = Info.Block_0x60.Field_0x08;
        Op.Field_38 = Info.Block_0x6c.Field_0x00;
        Op.Field_3C = Info.Block_0x6c.Field_0x04;
        Op.Field_40 = Info.Block_0x6c.Field_0x08;
        Op.Field_44 = Info.Block_0x78.Field_0x00;
        Op.Field_48 = Info.Block_0x78.Field_0x04;
        Op.Field_4C = Info.Block_0x78.Field_0x08;
        Op.Field_50 = Info.Field_0x84;
        Op.Field_54 = Info.Field_0x88;
        Op.Field_58 = Info.Field_0x8c;
        Op.Field_59 = Info.Field_0x8d;
        Op.Field_5A = Info.Field_0x8e;
        Op.Field_5B = Info.Field_0x8f;
        Op.Field_5C = Info.Field_0x1c;
        Op.Field_60 = Info.Field_0x20;
        Op.Field_64 = Info.Field_0x24;
        Op.Field_6C = Info.Field_0x90;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 2u), &Op);
        break;
    }
    case 5:
    {
        // caseD_5 0x0053dbb8.
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 6u), nullptr);
        Behavior->update_current_behavior_throttle(0.0f);
        Behavior->update_current_behavior_cruise(false);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// Misc object updates (slots 109..124)
// ---------------------------------------------------------------------------------------------

// LAB_0053db40 (RET 0xc) -- ch2 SCANNOTIFY: with a player ship (FUN_0054baf0),
// FUN_0048a180(ScannerId, GoodId) -- the scanning object's id and the scanned good.
void IClientImpl::SendServerScanNotify(unsigned int /*ClientId*/, unsigned int ScannerId, unsigned int GoodId)
{
    if (GetPlayerObjRW() == nullptr)
    {
        return;
    }
    NnMissionMessageShowScanNotice(ScannerId, GoodId);
}

// LAB_0053e2b0 (RET 0xc) -- MISCOBJUPDATE 0x0009: the gameobj's cobject (vt+0x150) when it is a
// CShip (0x503) gets the dword GroupId at +0x1b4.
void IClientImpl::SendServerMiscObjUpdateObjectGroup(unsigned int /*ClientId*/, unsigned int ObjectId,
                                                     unsigned int GroupId)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    CShip* const Ship =
        static_cast<CShip*>(NarrowObject(const_cast<CObject*>(Go->cobject()), 0x503));
    if (Ship == nullptr)
    {
        return;
    }
    // 0x0053e2e2..0x0053e2e6: CShip+0x1b4, the multiplayer group id (the server sends it from
    // SystemPlayerView_SendGroupChange, 0x06cf712d).
    Ship->GroupId = GroupId;
}

// LAB_0053e370 (RET 0xc) -- MISCOBJUPDATE 0x0002, the multiplayer mission-offer answer:
// DAT_0067334c = MissionSetBy (0x0053e378), DAT_00673348 = MissionId (0x0053e386), then
// FUN_004557d0 (thiscall) on the offer's ScriptSequence DAT_00673350 when there is one
// (FGfScriptSequence::OnMissionOfferAnswered: DAT_00673350 cleared; with the offer up the
// NN_OfferDialog closed and the current script run out).
void IClientImpl::SendServerMiscObjUpdateMission(unsigned int /*ClientId*/, unsigned int MissionId,
                                                 unsigned int MissionSetBy)
{
    SetMissionOfferAnswerRetail(MissionId, MissionSetBy);
    FGfScriptSequence* const Sequence = GetMissionOfferSequenceRetail();
    if (Sequence != nullptr)
        Sequence->OnMissionOfferAnswered();
}

// LAB_0053e3a0 (RET 8): EAX = [&Motd+4], the string's _Ptr, or MSVCP60's _Nullstr when it is null
// (0053e3a7 TEST / JNZ / 0053e3ab); FUN_0046b0f0(EAX). ClientId is not read.
void IClientImpl::SendServerMiscObjUpdateMotd(unsigned int /*ClientId*/, const std::wstring& Motd)
{
    ChatSetServerMotd(Motd.c_str());
}

// FUN_0053e2f0 (RET 0xc) -- MISCOBJUPDATE 0x0028: the gameobj (any bucket): its cobject when it
// is a CEqObj (0x103) -- Reputation::Vibe::SetAffiliation(its vibe +0x104, Affiliation, true),
// else nothing. No gameobj but ObjectId is the local character (DAT_00673378): SetAffiliation(
// FUN_004c4670(), Affiliation, true).
void IClientImpl::SendServerMiscObjUpdateAffiliation(unsigned int /*ClientId*/, unsigned int ObjectId,
                                                     unsigned int Affiliation)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go != nullptr)
    {
        CEqObj* const Eq =
            static_cast<CEqObj*>(NarrowObject(const_cast<CObject*>(Go->cobject()), 0x103));
        if (Eq == nullptr)
        {
            return;
        }
        int const Vibe = static_cast<int>(Eq->VibeId);
        Reputation::Vibe::SetAffiliation(Vibe, Affiliation, true);
        return;
    }
    if (GetPlayerCharacterId() != ObjectId)
    {
        return;
    }
    int const PlayerVibe = GetPlayerSpawnSystemIdRetail(); // FUN_004c4670 -> DAT_0061226c
    Reputation::Vibe::SetAffiliation(PlayerVibe, Affiliation, true);
}

// FUN_0053e200 (RET 0xc) -- MISCOBJUPDATE 0x0044: FUN_0046add0(PlayerId, Rank); when PlayerId
// is this client (ClientId), Reputation::Vibe::SetRank(FUN_004c4670(), Rank); in single player
// FUN_004b9b20(Rank).
void IClientImpl::SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank)
{
    ChatSetPlayerRank(PlayerId, static_cast<unsigned>(Rank)); // FUN_0046add0
    if (ClientId == PlayerId)
    {
        int const PlayerVibe = GetPlayerSpawnSystemIdRetail(); // FUN_004c4670
        Reputation::Vibe::SetRank(PlayerVibe, static_cast<unsigned>(Rank));
    }
    if (SinglePlayer())
    {
        StoryStarSetRankUp(Rank); // FUN_004b9b20 (0x0053e23f)
    }
}

// LAB_0053e1e0 (RET 0xc) -- MISCOBJUPDATE 0x0005: FUN_0046ad60(PlayerId, GroupId).
void IClientImpl::SendServerMiscObjUpdatePlayerGroup(unsigned int /*ClientId*/, unsigned int PlayerId,
                                                     unsigned int GroupId)
{
    ChatSetPlayerInfoField0(PlayerId, GroupId); // FUN_0046ad60
}

// LAB_0053e250 (RET 0xc) -- MISCOBJUPDATE 0x0084: FUN_0046ae40(PlayerId, SystemId).
void IClientImpl::SendServerMiscObjUpdateSystem(unsigned int /*ClientId*/, unsigned int PlayerId,
                                                unsigned int SystemId)
{
    ChatSetPlayerInfoField8(PlayerId, SystemId); // FUN_0046ae40
}

// LAB_0053e270 (RET 8): nothing in single player (0053e272 CALL SinglePlayer / 0053e27a JNZ ->
// return); otherwise FUN_004c5680 (clear), then for i from 0 while _First is non-null and
// i < (_Last - _First) >> 2 (0053e287..0053e298, both re-read every pass): FUN_004c56b0(
// _First[i]). ClientId is not read.
void IClientImpl::SendServerMiscObjUpdateFormation(unsigned int /*ClientId*/,
                                                   const std::vector<unsigned int>& MemberIds)
{
    if (SinglePlayer())
    {
        return;
    }
    RetailU32QueueClear();
    for (unsigned int Index = 0; Index < MemberIds.size(); ++Index)
    {
        RetailU32QueuePushBack(MemberIds[Index]);
    }
}

// LAB_0053e1a0 (RET 0xc) -- ch2 FORMATION_UPDATE: with a player ship (FUN_0054baf0), its
// IObjAffect set_formation_leader (+0x10)(the gameobj of LeaderId, any bucket, not tested) and
// set_formation_offset (+0x14)(Offset).
void IClientImpl::SendServerFormationUpdate(unsigned int /*ClientId*/, unsigned int LeaderId, const Vector& Offset)
{
    void* const PlayerObj = GetPlayerObjRW();
    if (PlayerObj == nullptr)
    {
        return;
    }
    IObjRW* const Player = static_cast<IObjRW*>(PlayerObj);
    IObjRW* const Leader = ClientObjDb_FindObject(LeaderId, 0);
    Player->set_formation_leader(Leader);
    Player->set_formation_offset(Offset);
}

// LAB_0053e3c0 (RET 0x1c) -- SET_MUSIC: FUN_00553340(Space, Danger, Battle, Override, Fade,
// bFlag) -- the last two swap: the float fade is the sixth argument, the byte flag the fifth.
void IClientImpl::SendServerSetMusic(unsigned int /*ClientId*/, unsigned int SpaceMusic, unsigned int DangerMusic,
                                     unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag, float Fade)
{
    StarSysSetMusicOverride(SpaceMusic, DangerMusic, BattleMusic, OverrideMusic, Fade, bFlag);
}

// LAB_0053e3f0 (RET 4) -- FUN_00553380.
void IClientImpl::SendServerCancelMusic(unsigned int /*ClientId*/)
{
    StarSysClearMusicOverride();
}

// LAB_0053e400 (RET 8) -- ch2 PLAY_SOUND_EFFECT: FUN_0042ae40(&ID_String{SoundId}), then
// FUN_00428680(arch, 0, 0, 0).
void IClientImpl::SendServerPlaySoundEffect(unsigned int /*ClientId*/, unsigned int SoundId)
{
    ID_String Sound;
    Sound.Id = SoundId;
    PlayClientSoundArch(LookupClientSoundArch(&Sound), nullptr, nullptr, 0);
}
