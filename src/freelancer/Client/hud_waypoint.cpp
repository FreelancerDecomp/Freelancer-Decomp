// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// HUD_Waypoint -- see hud_waypoint.h for the class, its vftables and the named gaps.

#include "hud_waypoint.h"

#include "best_path.h"
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830)
#include "client_gameobj.h"         // FClientGameobj, FClientSolarGameobj (FUN_005475b0)
#include "client_objdb.h"           // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "game_options.h"           // GetGameOption (FUN_005792b0)
#include "hud_command_dispatch.h"   // FSpaceCommand, SpaceSendCommand (FUN_0054bcb0)
#include "input_manager.h"          // GetClientMouseCursorDrawFlag (FUN_0041dd90)
#include "nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"           // the player waypoint list, GetPlayerConnectionId
#include "star_sphere.h"            // Win3DIdentityRotation3x3 (FUN_004215b0)

#include "common_archetype.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "common_id.h"              // CreateID
#include "common_misc.h"            // SUBOBJ_ID_NONE
#include "common_reputation.h"      // Reputation::Vibe
#include "common_text.h"            // FmtStr
#include "common_universe.h"        // Archetype::GetSolarID

#include "dacom.h"                  // FDUMP

#include <math.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(HUD_Waypoint) == 0x390, "retail HUD_Waypoint is 0x390 bytes");

namespace
{
    // DAT_00674bc8.
    HUD_Waypoint* G_HudWaypoint = nullptr;

    // LAB_004f3aa0 (static initializer): DAT_00674bb4 = CreateID("objective_reached").
    unsigned const G_VoiceObjectiveReached = CreateID("objective_reached");

    // DAT_005d97b8 15.0f, DAT_005ca230 100.0f, DAT_005c99c8 0.01f, DAT_005d97b4 150.0f,
    // DAT_005c8a60 16.0f, DAT_005d7e80 300.0f, DAT_005d97b0 250.0f, DAT_005c77f4 pi / 180
    // (float), DAT_005ca228 2.0 (double).
    float const kScaleZeroDistance = 15.0f;
    float const kScaleFullDistance = 100.0f;
    float const kScalePerMeter = 0.01f;
    float const kReachedDistance = 150.0f;
    float const kBestPathMatchSq = 16.0f;
    float const kUserPickDistance = 300.0f;
    float const kSpinDegPerSecond = 250.0f;
    float const kDegToRad = 0.0174532924f;
    double const kSameSpotAxis = 2.0;

    // FUN_00489e80(Id, 0) -- the NN voice queue (nn_messaging.cpp).
    void QueueWaypointVoice(unsigned VoiceId)
    {
        NnVoiceEnqueue(VoiceId, 0);
    }

    IObjRW* WatchedObjRw(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The client gameobj behind a watched IObjRW (retail calls its extension slots straight
    // through the IObjRW vftable).
    FClientGameobj* AsGameobj(IObjRW* Obj)
    {
        return static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Obj));
    }

    // The release sequence the listing inlines (0x004f3f7a..0x004f3fb1, 0x004f40b2..0x004f40e9,
    // 0x004f4445..0x004f4473): with a watched waypoint, its slot 87 (+0x15c) with 1, then the
    // watcher dropped.
    void ReleaseWaypointObject(BaseWatcher& Watcher)
    {
        IObjRW* const Obj = WatchedObjRw(Watcher);
        if (Obj == nullptr)
            return;
        AsGameobj(Obj)->Slot15c(true);
        Watcher.set(nullptr);
    }

    // Command 0x1c (target) on object ObjectId: a zeroed 0x28-byte record, +0x00 = 0x1c,
    // +0x10 = the id, +0x14 = SUBOBJ_ID_NONE, +0x26 = 1, through FUN_0054bcb0.
    void SendTargetCommand(unsigned ObjectId)
    {
        FSpaceCommand Command = {};
        Command.Id = 0x1c;
        Command.ObjectId = ObjectId;
        Command.SubObjectId = SUBOBJ_ID_NONE;
        Command.Byte26 = 1;
        SpaceSendCommand(&Command);
    }

    // The FPU distance the listing builds from two positions: the three differences A - B,
    // then sqrt(dz*dz + dy*dy + dx*dx) in that order.
    double Distance(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - static_cast<double>(B.x);
        double const Dy = static_cast<double>(A.y) - static_cast<double>(B.y);
        double const Dz = static_cast<double>(A.z) - static_cast<double>(B.z);
        return sqrt(Dz * Dz + Dy * Dy + Dx * Dx);
    }

    // The same distance when the listing loads z first (SetWaypoint 0x004f480c..0x004f482c):
    // sqrt(dx*dx + dy*dy + dz*dz).
    double DistanceXFirst(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - static_cast<double>(B.x);
        double const Dy = static_cast<double>(A.y) - static_cast<double>(B.y);
        double const Dz = static_cast<double>(A.z) - static_cast<double>(B.z);
        return sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }
}

HUD_Waypoint* GetHudWaypoint()
{
    return G_HudWaypoint;
}

HUD_Waypoint::HUD_Waypoint(INI_Reader& /*Reader*/)
    : SpawnCounter(0)
    , SpinAngle(0.0f)
    , bUser(0)
    , Index(0)
    , Scale(1.0f)
    , bReached(0)
    , Id(-1)
    , Field388(0)
    , bActive(0)
{
    // FUN_004f3af0: HUDItem inlined (0x004f3b0f..0x004f3b40), WinControl FUN_0059f860
    // (0x004f3b50), the +0x364 watcher zeroed, the two vptrs, DAT_00674bc8 = this (0x004f3b91),
    // HUDItem's name "Waypoint", then Create's body inlined (SetOffset slot 31, SetPosition slot
    // 5 with +0x60, Win3D::Create FUN_004b5060(0, 0) called directly, 0x004f3bb8..0x004f3c09),
    // +0x36c / +0x370 = 0, the Win3D name "HUD_Waypoint", +0x36c = +0x384 = -1, the class name,
    // +0x374 = 0, +0x37c = 1.0f, +0x380 = 0, +0x388 = 0, +0x38c = 0 (0x004f3c18..0x004f3c94).
    // +0x378 is not written (operator_new memory); the port starts it at 0.
    G_HudWaypoint = this;
    HUDItem::Name = "Waypoint";
    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    Win3D::Create(nullptr, nullptr);
    SpawnCounter = 0;
    SpinAngle = 0.0f;
    Win3D::SetName("HUD_Waypoint");
    SpawnCounter = -1;
    Id = -1;
    ClassName = "HUD_Waypoint";
}

HUD_Waypoint::~HUD_Waypoint()
{
    // FUN_004f3cd0: the vptrs, DAT_00674bc8 = 0 (0x004f3d16), the +0x364 watcher cleared,
    // ~WinControl FUN_0059fa50, HUDItem's vptr and watcher -- the member and base destructors.
    G_HudWaypoint = nullptr;
}

bool HUD_Waypoint::Create(Win3D* NewParent, void* Data)
{
    // LAB_004f3d70 (this = the WinControl face).
    SetOffset(0.0f, 0.0f, -1.0f);   // slot 31 (+0x7c)
    SetPosition(Position);          // slot 5 (+0x14)
    return Win3D::Create(NewParent, Data);
}

void HUD_Waypoint::Update()
{
    // LAB_004f4c50 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0                // FUN_0041dd90
        && GetGameOption(kGameOptInterfaceGroup) != 0      // FUN_005792b0(6)
        && GetGameOption(kGameOptHud) != 0                 // FUN_005792b0(0xa)
        && GetPlayerObjRW() != nullptr)                    // FUN_0054baf0
    {
        SetHiddenWithChildren(false);                      // FUN_005a0c30(0)
        Win3D::Update();                                   // tail jump FUN_005a17b0
        return;
    }
    SetHiddenWithChildren(true);                           // FUN_005a0c30(1)
}

void HUD_Waypoint::UpdateInactive()
{
    // FUN_004d6140 (this = the WinControl face; the body HUD_CruiseProgress shares).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);             // FUN_005a0c30(0)
    }
    Win3D::UpdateInactive();
}

void HUD_Waypoint::Render(FSpaceCamera* /*Camera*/, float DeltaSeconds)
{
    // LAB_004f3de0 (RET 8).
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return;
    Scale = 1.0f;                                                   // 0x004f3dfe

    if (WatchedObjRw(Waypoint) != nullptr && bActive != 0)
    {
        // 0x004f3e2f..0x004f3e6b: the player must still target the waypoint Solar.
        IObjRW* Target = nullptr;
        Player->get_target(Target);                                 // slot 29 (+0x74)
        if (Target == nullptr)
        {
            bActive = 0;                                            // 0x004f4120
        }
        else if (Target->get_id() != WatchedObjRw(Waypoint)->get_id()) // slot 8 (+0x20) both
        {
            bActive = 0;
        }
        else
        {
            bool bFound = false;
            if (bUser != 0)
            {
                // 0x004f3e7b..0x004f3ead: the entry with our id is still in the player's list.
                int const Count = GetPlayerWaypointCountRetail();   // FUN_004c4690
                for (int i = 0; i < Count; ++i)
                {
                    if (GetPlayerWaypointRetail(i)->AutoId == Id)   // FUN_004c46a0, no null test
                    {
                        bFound = true;
                        break;
                    }
                }
            }
            else
            {
                // 0x004f3eb4..0x004f3f74: a best-path record in the player's system lies within
                // 4 m (squared 16) of the waypoint Solar.
                int const Count = BestPathGetWaypointCount();       // FUN_004c4fb0
                Vector const WaypointPos = WatchedObjRw(Waypoint)->get_position(); // slot 0
                for (int i = 0; i < Count; ++i)
                {
                    FBestPathWaypoint* const Record = BestPathGetWaypoint(i); // FUN_004c4ff0
                    Vector RecordPos;
                    unsigned RecordSystem = 0;
                    if (!BestPathWaypointResolvePosition(Record, &RecordPos, &RecordSystem))
                        continue;                                   // FUN_004c52d0
                    if (GetPlayerStarSystemIdRetail() != RecordSystem) // FUN_004c4830
                        continue;
                    // FUN_00422b80(Pos - WaypointPos), then dy^2 + dz^2 + dx^2 against 16
                    // (TEST AH,5 / JNP: strictly below).
                    float const Dx = RecordPos.x - WaypointPos.x;
                    float const Dy = RecordPos.y - WaypointPos.y;
                    float const Dz = RecordPos.z - WaypointPos.z;
                    double const Sq = static_cast<double>(Dy) * Dy + static_cast<double>(Dz) * Dz +
                                      static_cast<double>(Dx) * Dx;
                    if (Sq < kBestPathMatchSq)
                    {
                        bFound = true;
                        break;
                    }
                }
            }

            if (!bFound)
            {
                // 0x004f3f7a..0x004f3fd6: drop the waypoint; a best-path one moves on to the
                // route's first point.
                ReleaseWaypointObject(Waypoint);
                if (bUser != 0)
                    return;
                HudWaypointSelectBestPath(0);                       // FUN_004cef30(0)
                return;
            }

            // 0x004f3fdd..0x004f4044: the distance from the waypoint to the player sets the
            // scale -- 0 inside 15 m (TEST AH,5 / JP keeps >= or NaN), dist * 0.01 below 100 m.
            double const Dist = Distance(Player->get_position(), Target->get_position());
            if (Dist < kScaleZeroDistance)
            {
                Scale = 0.0f;
            }
            else if (Dist < kScaleFullDistance)
            {
                Scale = static_cast<float>(static_cast<double>(kScalePerMeter) * Dist);
            }

            // 0x004f404a..0x004f411b: first time within 150 m -- reached.
            if (bReached == 0 && Dist < kReachedDistance)
            {
                bReached = 1;
                QueueWaypointVoice(G_VoiceObjectiveReached);
                if (bUser != 0)
                {
                    // 0x004f408f..0x004f40b0: Index + 1 removals of entry Index, i.e. this entry
                    // and every one before it.
                    if (Index >= 0)
                    {
                        for (int Left = Index + 1; Left != 0; --Left)
                            RemovePlayerWaypointRetail(Index);      // FUN_004c46e0
                    }
                    ReleaseWaypointObject(Waypoint);
                    // 0x004f40ef..0x004f4116: the entry now at Index becomes the waypoint.
                    FPlayerWaypointRecord* const Next = GetPlayerWaypointRetail(Index);
                    if (Next != nullptr)
                        SetWaypoint(&Next->Pos, true, Index, Next->AutoId);
                }
            }
        }
    }
    else
    {
        // 0x004f412c..0x004f41bc: no live waypoint -- the first player waypoint within 300 m
        // counts as reached, and it and every entry before it are removed.
        int const Count = GetPlayerWaypointCountRetail();
        for (int i = 0; i < Count; ++i)
        {
            FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(i);
            if (Record == nullptr)
                continue;
            if (Distance(Player->get_position(), Record->Pos) < kUserPickDistance)
            {
                QueueWaypointVoice(G_VoiceObjectiveReached);
                for (int Remove = i; Remove >= 0; --Remove)
                    RemovePlayerWaypointRetail(Remove);
                break;
            }
        }
    }

    // 0x004f41c0..0x004f428b: spin the waypoint Solar about its Y axis.
    if (WatchedObjRw(Waypoint) == nullptr)
        return;
    // FLD Dt, FMUL 250.0f, FADD +0x370, FST +0x370 (the sum stays unrounded on the stack),
    // FMUL pi / 180, FST a float copy; FCOS runs on the unrounded angle, FSIN on the float copy.
    double const Sum = static_cast<double>(DeltaSeconds) * kSpinDegPerSecond +
                       static_cast<double>(SpinAngle);
    SpinAngle = static_cast<float>(Sum);
    double const Radians = Sum * static_cast<double>(kDegToRad);
    float const RadiansF = static_cast<float>(Radians);
    float const Cos = static_cast<float>(cos(Radians));
    float const Sin = static_cast<float>(sin(static_cast<double>(RadiansF)));
    float Rotation[9] = {Cos, 0.0f, Sin, 0.0f, 1.0f, 0.0f, -Sin, 0.0f, Cos};
    HudMatrixRotateRows12(Rotation, 0.0f);                          // FUN_004c8b80(0)
    HudMatrixRotateRows01(Rotation, 0.0f);                          // FUN_004c8bc0(0)

    // 0x004f4259..0x004f428b: into the CObject's orientation (+0x08) when it is a CSimple
    // ((class & 3) == 3).
    IObjRW* const Obj = WatchedObjRw(Waypoint);
    if (Obj == nullptr)
        return;
    CObject* const Object = static_cast<CBase*>(static_cast<IObjInspectImpl*>(Obj))->Object;
    if (Object == nullptr)
        return;
    if ((static_cast<unsigned>(Object->ObjectClass) & 3u) != 3u)
        return;
    memcpy(&Object->Orientation, Rotation, sizeof(Rotation));
}

void HUD_Waypoint::SetWaypoint(Vector const* Pos, bool bNewUser, int NewIndex, int NewId)
{
    // FUN_004f42e0 (RET 0x10).
    //
    // 0x004f4300..0x004f4440: the same entry, and its Solar within 2 m on every axis (FABS,
    // TEST AH,5 / JP: below 2.0 only) -- just target it again.
    IObjRW* const Current = WatchedObjRw(Waypoint);
    if (Current != nullptr && static_cast<unsigned char>(bNewUser) == bUser && NewIndex == Index &&
        NewId == Id)
    {
        Vector const WaypointPos = Current->get_position();
        double const Dx = static_cast<double>(WaypointPos.x) - static_cast<double>(Pos->x);
        float const Dy = WaypointPos.y - Pos->y;
        float const Dz = WaypointPos.z - Pos->z;
        if (fabs(Dx) < kSameSpotAxis && fabs(static_cast<double>(Dy)) < kSameSpotAxis &&
            fabs(static_cast<double>(Dz)) < kSameSpotAxis)
        {
            bActive = 1;
            SendTargetCommand(WatchedObjRw(Waypoint)->get_id());
            return;
        }
    }

    // 0x004f4445..0x004f4498: drop the old Solar, remember the entry.
    ReleaseWaypointObject(Waypoint);
    bUser = static_cast<unsigned char>(bNewUser);
    Index = NewIndex;
    Id = NewId;

    // 0x004f4487..0x004f44cc: the "waypoint" solar archetype, loaded, and its companion's
    // Load(0). Retail tests neither for null.
    ID_String ArchName;
    ArchName.Id = CreateID("waypoint");
    Archetype::Solar* const Arch = Archetype::GetSolar(Archetype::GetSolarID(ArchName));
    Arch->load();                                                   // Root slot 3 (+0x0c)
    Arch->Companion->Load(0);                                       // ICliObj slot 8 (+0x20)

    // 0x004f44cf..0x004f4557: CSolar::CreateParms on the stack. FUN_004f4930 (0x004f44d3) is
    // the CEqObj::CreateParms default constructor (EqObjCreateParmsDefaults). Then the CSolar
    // part: ID_String::clear on +0xb8 / +0xc0 / +0xcc and zeros through +0xe0, and the anim list.
    CSolar::CreateParms Parms;
    EqObjCreateParmsDefaults(Parms);
    Parms.NicknameSrc.clear();
    Parms.VisitValue = 0;
    Parms.ParentNicknameId = 0;                                     // ID_String::clear
    Parms.bDestructible = 0;
    Parms.bDynamic = 0;
    Parms.DestSystem = 0;
    Parms.DestGateId = 0;                                           // ID_String::clear
    Parms.PrevRing = 0;
    Parms.NextRing = 0;
    Parms.TradelaneSpaceNameId = 0;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0;

    // 0x004f456a..0x004f45e1: nickname "waypoint_target<old counter>", the counter incremented;
    // the id goes into Id, the nickname and the reputation group, which gets an entry and
    // affiliation -1.
    char Nickname[0x80];
    int const Suffix = SpawnCounter;
    // 004f4570..004f4577: PTR_DAT_00613ed0 -> CreateParms+0x04 before CSolar::init.
    Parms.ObjDb = GetClientObjDb();
    SpawnCounter = Suffix + 1;
    sprintf(Nickname, "waypoint_target%d", Suffix);
    unsigned const NewObjectId = CreateID(Nickname);
    Parms.NicknameSrc.Id = NewObjectId;
    Parms.Id = NewObjectId;
    Parms.VibeId = NewObjectId;
    int const& RepGroup = reinterpret_cast<int const&>(Parms.VibeId);
    Reputation::Vibe::Alloc(RepGroup);
    Reputation::Vibe::SetAffiliation(RepGroup, 0xffffffffu, false);

    // 0x004f45e4..0x004f46a7: the name is mad-lib ids 0x442 with no values, the second name
    // empty; no infocard.
    FmtStr Name;
    FmtStr Name2;
    Name.begin_mad_lib(0x442);
    Name.end_mad_lib();
    Reputation::Vibe::SetName(RepGroup, Name, Name2, nullptr);
    Reputation::Vibe::SetInfocard(RepGroup, 0);

    // 0x004f46ad..0x004f46cf
    Parms.Pos = *Pos;

    // 0x004f46bc..0x004f4757: a client Solar (FUN_005475b0), slot 90 create, CSolar::init on its
    // CObject when that is an EqObj ((class & 0x303) == 0x303; retail calls init on whatever
    // that yields), slot 92, FUN_00540c30(Solar, 0).
    FClientSolarGameobj* const Solar = new FClientSolarGameobj();
    Solar->create(Arch);
    CObject* const SolarObject = static_cast<CBase*>(Solar)->Object;
    CSolar* SolarCObject = nullptr;
    if (SolarObject != nullptr && (static_cast<unsigned>(SolarObject->ObjectClass) & 0x303u) == 0x303u)
        SolarCObject = static_cast<CSolar*>(SolarObject);
    SolarCObject->init(Parms);
    Solar->Slot170();
    ClientObjDb_RegisterGameobj(Solar, false);

    // 0x004f475a..0x004f4767
    Arch->Release();
    SolarCObject->enable_collisions_r(false);

    // 0x004f476d..0x004f47b6: target it.
    SendTargetCommand(Solar->get_id());

    // 0x004f47bb..0x004f47e1: watch it; live, not reached yet.
    Waypoint.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Solar)));
    bActive = 1;
    bReached = 0;

    // 0x004f47e7..0x004f4841: already within 100 m (TEST AH,0x41 / JP: at most 100) counts as
    // reached.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr)
    {
        Vector const& WaypointPos = WatchedObjRw(Waypoint)->get_position();
        if (DistanceXFirst(Player->get_position(), WaypointPos) <= kScaleFullDistance)
            bReached = 1;
    }
    // 0x004f4848..0x004f490c: the two FmtStr and the CreateParms die here.
}

bool HUD_Waypoint::GetWaypointEntry(unsigned char* OutUser, int* OutIndex)
{
    // FUN_004f42a0 (RET 8).
    if (WatchedObjRw(Waypoint) == nullptr || bActive == 0)
        return false;
    *OutUser = bUser;
    *OutIndex = Index;
    return true;
}

bool HUD_Waypoint::IsWaypointAt(Vector const* Pos)
{
    // FUN_004f4b90 (RET 4): as SetWaypoint's same-spot test -- dx kept unrounded, dy / dz
    // stored as floats, each FABS below 2.0.
    if (WatchedObjRw(Waypoint) == nullptr || bActive == 0)
        return false;
    Vector const WaypointPos = WatchedObjRw(Waypoint)->get_position();
    double const Dx = static_cast<double>(WaypointPos.x) - static_cast<double>(Pos->x);
    float const Dy = WaypointPos.y - Pos->y;
    float const Dz = WaypointPos.z - Pos->z;
    if (!(fabs(Dx) < kSameSpotAxis))
        return false;
    if (!(fabs(static_cast<double>(Dy)) < kSameSpotAxis))
        return false;
    if (!(fabs(static_cast<double>(Dz)) < kSameSpotAxis))
        return false;
    return true;
}

float HUD_Waypoint::GetScale() const
{
    // FUN_004f4ca0.
    return Scale;
}

IObjRW* HUD_Waypoint::GetWaypointObject() const
{
    // FUN_004f4cb0.
    return WatchedObjRw(Waypoint);
}

bool HUD_Waypoint::IsActive() const
{
    // FUN_004f4d20.
    return bActive != 0;
}

void HudWaypointOnPlayerWaypointsCleared()
{
    // FUN_004f4cd0.
    if (G_HudWaypoint == nullptr)
        return;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return;
    if (WatchedObjRw(G_HudWaypoint->Waypoint) == nullptr)
        return;
    if (G_HudWaypoint->bActive == 0)
        return;
    // 0x004f4d03..0x004f4d1b: the IObjAffect face (+4), slot 18 (+0x48).
    static_cast<IObjAffect*>(Player)->set_target(nullptr, SUBOBJ_ID_NONE,
                                                 static_cast<int>(GetPlayerConnectionId()));
}

int HudWaypointGetObjectCount()
{
    // FUN_004c83c0: MOV ECX,[DAT_00674bc8] / CALL FUN_004f4cb0 / NEG / SBB / NEG.
    return G_HudWaypoint->GetWaypointObject() != nullptr ? 1 : 0;
}

IObjRW* HudWaypointGetObject(int ObjectIndex)
{
    // FUN_004c83e0: a non-zero index gives 0; 0 tail-jumps to FUN_004f4cb0.
    if (ObjectIndex != 0)
        return nullptr;
    return G_HudWaypoint->GetWaypointObject();
}

void EqObjCreateParmsDefaults(CEqObj::CreateParms& Parms)
{
    // FUN_004f4930, in the listing's store order.
    Parms.HitPts = -1.0f;                                           // +0xc 0xbf800000
    Parms.Id = 0;                                                   // +0x0
    Parms.ObjDb = nullptr;                                          // +0x4
    Parms.OwnerPlayer = 0;                                          // +0x8
    Parms.Pos.x = 0.0f;                                             // +0x10..+0x18
    Parms.Pos.y = 0.0f;
    Parms.Pos.z = 0.0f;
    Parms.Velocity.x = 0.0f;                                        // +0x40..+0x54
    Parms.Velocity.y = 0.0f;
    Parms.Velocity.z = 0.0f;
    Parms.AngularVelocity.x = 0.0f;
    Parms.AngularVelocity.y = 0.0f;
    Parms.AngularVelocity.z = 0.0f;
    Parms.Pad58 = 0;                                                // +0x58
    Win3DIdentityRotation3x3(reinterpret_cast<float*>(&Parms.Orient)); // FUN_004215b0 on +0x1c
    Parms.DockingCount = 0;                                         // +0x5c
    Parms.VibeId = 0xffffffffu;                                     // +0x60
    Parms.SpaceCostume.PartIds[0] = 0;                              // +0x64..+0x70
    Parms.SpaceCostume.PartIds[1] = 0;
    Parms.SpaceCostume.PartIds[2] = 0;
    Parms.SpaceCostume.PartIds[3] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;                          // +0x94
    Parms.VoiceId = 0;                                              // +0x98
    // +0x9c..+0xa8 (allocator byte, three null pointers) and +0xac..+0xb4 (allocator byte,
    // operator new(0x10) head linked to itself, size 0): Equipment and ArchGroupDescs, which
    // their std constructors already left empty. +0x74..+0x90 (AccessoryIds) are not written.
}

void HudWaypointSelectBestPath(int PointIndex)
{
    // FUN_004cef30 (cdecl).
    if (PointIndex >= BestPathGetWaypointCount())                  // FUN_004c4fb0, signed
        return;
    FBestPathWaypoint* const Record = BestPathGetWaypoint(PointIndex); // FUN_004c4ff0

    // 0x004cef53..0x004cef94: a record that names an object -- target it.
    IObjRW* const Obj = BestPathWaypointResolveObject(Record);     // FUN_004c5110
    if (Obj != nullptr)
    {
        SendTargetCommand(Obj->get_id());
        return;
    }

    // 0x004cefa2..0x004cefc6: otherwise its position becomes the waypoint (the system is not
    // read; DAT_00674bc8 is not tested).
    Vector Pos;
    unsigned System = 0;
    if (BestPathWaypointResolvePosition(Record, &Pos, &System))    // FUN_004c52d0
        G_HudWaypoint->SetWaypoint(&Pos, false, PointIndex, PointIndex);
}

void HudWaypointSelectPlayerWaypoint(int Index)
{
    // FUN_004ceeb0 (cdecl).
    if (Index >= GetPlayerWaypointCountRetail())                   // FUN_004c4690, signed
        return;
    FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(Index); // FUN_004c46a0

    // 0x004ceec7..0x004cef0e: a waypoint on an object -- target it.
    if (Record->ObjectId != 0)
    {
        SendTargetCommand(Record->ObjectId);
        return;
    }

    // 0x004cef0f..0x004cef1d: otherwise its position becomes the waypoint (DAT_00674bc8 is not
    // tested).
    G_HudWaypoint->SetWaypoint(&Record->Pos, true, Index, Record->AutoId);
}

namespace
{
    // The static HUDFACTORY<HUD_Waypoint> DAT_00674b88: XC entry LAB_004f3ac0 runs the ctor
    // FUN_004f4d30 ("Waypoint", appended to the factory chain by FUN_004c72b0) and registers
    // LAB_004f3ae0 with atexit (-> FUN_004cf010, the unlink). Its Create FUN_004f4da0 is
    // operator new(0x390) + FUN_004f3af0, null when the allocation fails.
    HUDFACTORY<HUD_Waypoint> G_HudWaypointFactory("Waypoint");
}
