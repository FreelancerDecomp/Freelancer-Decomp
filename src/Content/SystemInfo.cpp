// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU58 (06f71fc0-06f802b0): SystemInfo and the system table DAT_06fd3a5c.
// Lane CNT-POP (docs/re/content.md, TU map).

#include "SystemInfo.h"

#include "Cargo.h"              // GetHostileFactionShares (TU44 FUN_06f35190)
#include "ContentInternal.h"
#include "ContentPlayerState.h"
#include "ContentSort.h"        // ContentSort (VC6 _Sort_0; TU58 instance FUN_06f7f370)
#include "ContentStaticIds.h"   // TU58 static-init thunks 06f71fc0-06f721a0
#include "DockQueues.h"         // CollectFormationMembers (TU53 FUN_06f66220)
#include "Encounters.h"
#include "FactionProps.h"       // GetFactionProps (TU48 FUN_06f50650), GetFormation (FUN_06f4f3d0)
#include "MissionObject.h"      // IsSpaceObjTypeSet (TU53 FUN_06f683b0), FindObjectIdByName
#include "NpcShip.h"            // FContentNpcShip, FContentNpcShipParams (TU43)
#include "Populators.h"
#include "RandomEncounters.h"   // FRandEncTLParams, CreateRandEncParamsByType (TU31 FUN_06efb8e0)
#include "RefHandle.h"          // TRefHandle (FUN_06f28e10)
#include "common_ini.h"
#include "common_server.h"
#include "server_cobject.h"     // pub::SpaceObj::Destroy, DESTROY_NORMAL

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

// LAB_06f75ee0 constructs the map, FUN_06f75f10 (atexit) destroys it (FUN_06f2be60).
std::map<unsigned int, FSystemInfo> GSystemInfos;

// FUN_06f721c0 (06f721c0-06f72379, RET 4). The CommReferrable base ctor is inlined in
// retail (+0x04 = 0, +0x08 = 0); every container starts empty, +0x11c = 1, +0x120 = 0.
FSystemInfo::FSystemInfo(unsigned int const* InSystemId)
    : SystemId(*InSystemId)
{
}

// FUN_06f75830 (06f75830-06f7583c, RET 4).
void FSystemInfo::Load(bool bLoadEncounters)
{
    LoadFromReader(nullptr, bLoadEncounters);
}

// FUN_06f74ae0 (06f74ae0-06f756aa, RET 8).
void FSystemInfo::LoadFromReader(INI_Reader* Reader, bool bLoadEncounters)
{
    // 06f74b0b-06f74b1c: count first, then read only on the 0 -> 1 transition.
    int const PreviousCount = LoadCount;
    LoadCount = PreviousCount + 1;
    if (PreviousCount != 0)
        return;

    INI_Reader SystemReader;
    if (Reader == nullptr)
    {
        pub::BuildSystemReader(SystemReader, SystemId);
        Reader = &SystemReader;
    }

    LocalFaction = 0;
    // 06f74b59-06f74b78: the Trade_Lane_Ring objects of the system, keyed by nickname.
    std::map<unsigned int, FTradeLaneRingInfo> Rings;

    if (bLoadEncounters)
    {
        // 06f74b8c-06f74bc7: operator new(0x14) + map ctor FUN_06f1f4f0.
        EncounterParameters = new std::map<unsigned int, FEncounterParameters*>();
    }

    while (Reader->read_header())
    {
        if (Reader->is_header("System") || Reader->is_header("SystemInfo"))
        {
            // 06f7529f-06f752e7
            while (Reader->read_value())
            {
                if (Reader->is_value("local_faction"))
                    pub::Reputation::GetReputationGroup(LocalFaction, Reader->get_value_string());
            }
        }
        else if (bLoadEncounters && Reader->is_header("EncounterParameters"))
        {
            // 06f74c2d-06f74da7: "nickname = ...", "filename = ...".
            Reader->read_value();
            unsigned int EncounterId = CreateID(Reader->get_value_string());
            Reader->read_value();
            char FileName[0x104];
            char const* FileNameValue = Reader->get_value_string();
            if (FileNameValue == nullptr)
            {
                FileName[0] = '\0';
            }
            else
            {
                // FUN_06ea9f40: bounded copy into the 0x104 buffer.
                strncpy_s(FileName, sizeof(FileName), FileNameValue, _TRUNCATE);
            }

            INI_Reader EncounterReader;
            if (OpenContentDataIni(FileName, &EncounterReader))
            {
                // 06f74cd1-06f74d63: operator new(0x4c) + FUN_06ef3830; then insert
                // {id, null} (FUN_06f7ac90 + FUN_06f7a7b0) and store the object in the
                // node found or made (a repeated id keeps its node, the old object leaks).
                FEncounterParameters* Parameters =
                    new FEncounterParameters(&EncounterReader, &EncounterId, &SystemId);
                std::pair<std::map<unsigned int, FEncounterParameters*>::iterator, bool> const
                    Inserted = EncounterParameters->insert(
                        std::pair<unsigned int const, FEncounterParameters*>(EncounterId, nullptr));
                Inserted.first->second = Parameters;
            }
        }
        else if (Reader->is_header("Object"))
        {
            // 06f74dc1-06f74e1e: the per-object locals.
            unsigned int ObjectId = 0;
            Vector Position;
            Position.x = 0.0f;
            Position.y = 0.0f;
            Position.z = 0.0f;
            // 06f74dc3-06f74de3: FUN_06efc7a0 sets the rotation to identity, then the
            // translation words are zeroed; the 0x30 block is copied whole below.
            FTransform Placement;
            SetMatrixIdentity(&Placement.Rotation);
            Placement.Translation.X = 0.0f;
            Placement.Translation.Y = 0.0f;
            Placement.Translation.Z = 0.0f;
            unsigned int PrevRing = 0;
            unsigned int NextRing = 0;
            unsigned int Reputation = 0;   // retail leaves it uninitialised (ESP+0xb8)
            bool bRing = false;
            bool bJumpGate = false;
            bool bJumpHole = false;
            unsigned int GotoSystem = 0;
            unsigned int GotoObject = 0;

            if (!Reader->read_value())
                continue;

            do
            {
                if (Reader->is_value("nickname"))
                {
                    // 06f74e43-06f74e8c. Retail also copies value 0 into a 0x40-byte local
                    // string (ESP+0x180) that nothing reads afterwards.
                    ObjectId = pub::GetNicknameId(Reader->get_value_string(0));
                }
                else if (Reader->is_value("pos"))
                {
                    // 06f74ea2-06f74eea: read z, y, x.
                    float Z = Reader->get_value_float(2);
                    float Y = Reader->get_value_float(1);
                    float X = Reader->get_value_float(0);
                    Position.x = X;
                    Position.y = Y;
                    Position.z = Z;
                }
                else if (Reader->is_value("rotate"))
                {
                    // 06f74f04-06f7502d: degrees -> radians (DAT_06fb6060 = 0.0174532924f),
                    // then the rotation matrix, computed on the x87 stack.
                    float RotZ = Reader->get_value_float(2);
                    float RotY = Reader->get_value_float(1);
                    float RotX = Reader->get_value_float(0);
                    float const DegToRad = 0.0174532924f;
                    float AngleX = static_cast<float>(static_cast<double>(RotX) * DegToRad);
                    float AngleY = static_cast<float>(static_cast<double>(RotY) * DegToRad);
                    float AngleZ = static_cast<float>(static_cast<double>(RotZ) * DegToRad);

                    double SinX = std::sin(static_cast<double>(AngleX));
                    double CosX = std::cos(static_cast<double>(AngleX));
                    double SinY = std::sin(static_cast<double>(AngleY));
                    double CosY = std::cos(static_cast<double>(AngleY));  // FSTP double [0x38]
                    double SinZ = std::sin(static_cast<double>(AngleZ));
                    double CosZ = std::cos(static_cast<double>(AngleZ));

                    double SinYSinX = SinY * SinX;                        // kept on the stack
                    float SinZCosX = static_cast<float>(SinZ * CosX);     // FSTP float [0x1c]
                    float CosZCosX = static_cast<float>(CosZ * CosX);     // FSTP float [0x14]

                    FMatrix Rotated;
                    Rotated.D[0][0] = static_cast<float>(CosZ * CosY);
                    Rotated.D[0][1] = static_cast<float>(SinYSinX * CosZ - SinZCosX);
                    Rotated.D[0][2] = static_cast<float>(CosZCosX * SinY + SinZ * SinX);
                    Rotated.D[1][0] = static_cast<float>(SinZ * CosY);
                    Rotated.D[1][1] = static_cast<float>(SinYSinX * SinZ + CosZCosX);
                    Rotated.D[1][2] = static_cast<float>(SinZCosX * SinY - CosZ * SinX);
                    Rotated.D[2][0] = static_cast<float>(-SinY);
                    Rotated.D[2][1] = static_cast<float>(CosY * SinX);
                    Rotated.D[2][2] = static_cast<float>(CosY * CosX);
                    Placement.Rotation = Rotated;                         // REP MOVSD x9
                }
                else if (Reader->is_value("archetype"))
                {
                    // 06f7504b-06f750ab (DACOM stricmp): any other archetype ends the
                    // section's value loop.
                    if (_stricmp("Trade_Lane_Ring", Reader->get_value_string(0)) == 0)
                        bRing = true;
                    else if (_stricmp("jumpgate", Reader->get_value_string(0)) == 0)
                        bJumpGate = true;
                    else if (_stricmp("jumphole", Reader->get_value_string(0)) == 0)
                        bJumpHole = true;
                    else
                        break;
                }
                else if (Reader->is_value("prev_ring"))
                {
                    PrevRing = pub::GetNicknameId(Reader->get_value_string(0));
                }
                else if (Reader->is_value("next_ring"))
                {
                    NextRing = pub::GetNicknameId(Reader->get_value_string(0));
                }
                else if (Reader->is_value("goto"))
                {
                    pub::GetSystemID(GotoSystem, Reader->get_value_string(0));
                    GotoObject = pub::GetNicknameId(Reader->get_value_string(1));
                }
                else if (Reader->is_value("reputation"))
                {
                    pub::Reputation::GetReputationGroup(Reputation, Reader->get_value_string(0));
                }
            } while (Reader->read_value());

            // 06f75174-06f75205
            if (bRing)
            {
                FTradeLaneRingInfo Ring;
                Ring.ObjectId = ObjectId;
                Ring.Position = Position;
                Ring.Placement = Placement;                               // FUN_06f594c0
                Ring.PrevRing = PrevRing;
                Ring.NextRing = NextRing;
                Ring.Reputation = Reputation;
                Rings.insert(std::pair<unsigned int const, FTradeLaneRingInfo>(ObjectId, Ring));
            }

            // 06f7520a-06f7529d
            if (bJumpGate || bJumpHole)
            {
                FSystemJump Jump;
                Jump.ObjectId = ObjectId;
                Jump.Type = bJumpGate ? 1 : (bJumpHole ? 2 : -1);
                Jump.Position = Position;
                Jump.GotoSystem = GotoSystem;
                Jump.GotoObject = GotoObject;
                Jumps.insert(Jumps.end(), 1, Jump);                       // FUN_06f7bcc0
            }
        }
    }

    // 06f752fd-06f7535e: collect the zones.
    std::vector<FZonePathLabel> PathLabels;
    FSystemZoneEnumerator Enumerator;
    Enumerator.System = this;
    Enumerator.bLoadEncounters = bLoadEncounters;
    Enumerator.Zones = &Zones;
    Enumerator.PathLabels = &PathLabels;
    pub::System::EnumerateZones(SystemId, Enumerator);

    if (bLoadEncounters)
    {
        // 06f75365-06f753d5: the EncounterParameters objects only live for the enumeration:
        // every non-null value is deleted (FUN_06f792a0(1)), then the map (FUN_06f79780 +
        // operator delete).
        if (EncounterParameters != nullptr)
        {
            for (std::map<unsigned int, FEncounterParameters*>::iterator It =
                     EncounterParameters->begin();
                 It != EncounterParameters->end(); ++It)
            {
                if (It->second != nullptr)
                    delete It->second;
            }
            delete EncounterParameters;
        }
        EncounterParameters = nullptr;
    }

    // 06f753df-06f75422
    ZoneById.clear();
    for (std::vector<FSystemZone>::iterator It = Zones.begin(); It != Zones.end(); ++It)
        ZoneById[It->ZoneId] = &*It;

    BuildPatrolPaths(&PathLabels);                                        // 06f7542b
    BuildTradeLanes(&Rings);                                              // 06f75437

    if (bLoadEncounters)
    {
        // 06f7543c-06f75467
        for (std::vector<FSystemZone>::iterator It = Zones.begin(); It != Zones.end(); ++It)
        {
            if (It->Population.bHasPopulationKeys)
                It->Population.Validate();
        }
    }
}

// FUN_06f75840 (06f75840-06f75938).
void FSystemInfo::Unload()
{
    if (--LoadCount != 0)
        return;
    Jumps.clear();
    ZoneById.clear();
    Zones.clear();
}

// FUN_06f748a0 (06f748a0-06f74ab9, RET 4).
void FSystemInfo::LoadSolarNpcs(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (Reader->is_header("Object"))
        {
            // 06f748ef-06f7494e: the 0x470 block (CommReferrable, FUN_06eb7260 parameters,
            // behavior -1, Personality, no pilot, reputation -1); parameters +0x3c = this.
            FSolarNpcDesc Desc;
            Desc.System = this;
            while (Reader->read_value())
                ParseSolarNpcKey(Reader, &Desc);                          // FUN_06ed5590

            // 06f7497a-06f749bc: docked = a base was given, or the named object has a
            // docking target.
            bool bDocked;
            if (Desc.Nickname.Length != 0 && Desc.Base == nullptr)
            {
                unsigned int const ObjectId = pub::GetNicknameId(Desc.Nickname.Data);
                unsigned int DockTarget;
                pub::SpaceObj::GetDockingTarget(ObjectId, DockTarget);
                bDocked = DockTarget != 0;
            }
            else
            {
                bDocked = Desc.Base != nullptr;
            }

            // 06f749bf-06f749ea: a named object with a behavior, a base, a docking target,
            // a reputation or a msg_id_prefix becomes a solar NPC.
            if (Desc.Nickname.Length != 0 &&
                (Desc.StateGraph != -1 || Desc.Base != nullptr || bDocked ||
                 Desc.ReputationGroup != 0xffffffffu || Desc.MsgIdPrefix.Length != 0))
            {
                FSolarNpc Npc(&Desc);                                     // FUN_06ed5720
                SolarNpcs.insert(SolarNpcs.end(), 1, Npc);                // FUN_06f7b8e0
            }
            // 06f74a3d: the parameters' handle is released by Desc's destructor
            // (FUN_06f28e10).
        }
        else if (Reader->is_header("SystemInfo"))
        {
            // 06f74a51-06f74a87
            while (Reader->read_value())
            {
                if (Reader->is_value("rpop_solar_detection"))
                    bRpopSolarDetection = Reader->get_value_bool(0);
            }
        }
    }
    Reader->reset();                                                      // 06f74a9d
}

// FUN_06f75940 (06f75940-06f75bba).
void FSystemInfo::LoadFull()
{
    INI_Reader Reader;
    pub::BuildSystemReader(Reader, SystemId);                             // 06f7597f
    LoadSolarNpcs(&Reader);
    if (pub::SinglePlayer())
        SpacePopSetSolarDetection(bRpopSolarDetection);                   // FUN_06f5a7c0
    Reader.reset();
    LoadFromReader(&Reader, true);                                        // 06f759c4
    RebuildTradeLanes();                                                  // 06f759cb FUN_06f77090
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
        Npc->SubmitPersonalityState(true);                                // FUN_06ed5960
    // 06f759f9-06f75bba: the inlined INI_Reader destructor.
}

// FUN_06f75bc0 (06f75bc0-06f75d0a).
void FSystemInfo::UnloadFull()
{
    DestroyObjectsIfUnoccupied();                                         // 06f75bc7
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
    {
        Npc->SubmitPersonalityState(false);                               // FUN_06ed5960
        Npc->DeleteReputationHandle();                                    // FUN_06ed5940
    }
    SolarNpcs.clear();                                                    // 06f75c00-06f75c48
    MissionObjects.clear();                                               // 06f75c4e-06f75c6e
    Field_0xa8.clear();                                                   // 06f75c74-06f75c94
    if (pub::SinglePlayer())
        SpacePopSetSolarDetection(1);                                     // FUN_06f5a7c0
    // 06f75cae-06f75d00: the body of FUN_06f75840, inlined.
    Unload();
}

// FUN_06f72a20 (06f72a20-06f72a72, RET 4): the re-find after the insert is discarded.
void FSystemInfo::AddPlayer(FContentPlayerState* Player)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        if (*It == Player)
            return;
    }
    Players.insert(Players.end(), 1, Player);                            // FUN_06f937c0
}

// FUN_06f72c30 (06f72c30-06f72c8d, RET 4).
void FSystemInfo::RemovePlayer(FContentPlayerState* Player)
{
    // 06f72c33-06f72c6e: find, then erase the first match (none -> nothing erased).
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        if (*It == Player)
        {
            Players.erase(It);
            break;
        }
    }
    if (pub::SinglePlayer())                                              // 06f72c75
        DestroyObjectsIfUnoccupied();                                     // 06f72c81
}

// FUN_06f72c90 (06f72c90-06f72ca5, RET 4): the argument is never read.
void FSystemInfo::OnPlayerChanged(FContentPlayerState* Unused)
{
    (void)Unused;
    if (pub::SinglePlayer())
        DestroyObjectsIfUnoccupied();                                     // 06f72c9f
}

// FUN_06f72a80 (06f72a80-06f72c29).
void FSystemInfo::DestroyObjectsIfUnoccupied()
{
    // 06f72a9c-06f72ac2: any player with a ship (+0x10) -> nothing to do.
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        if ((*It)->ShipObject != nullptr)
            return;
    }

    // 06f72ac4-06f72b3f: a copy of the SpaceObjs list (FUN_06ed0230 insert of the whole
    // range into a fresh list); every copied object destroys its space object
    // (FUN_06f34670). The copy is destroyed at 06f72bc9-06f72c16 (EH state 0).
    std::list<FContentSpaceObj*> const SpaceObjsCopy(SpaceObjs.begin(), SpaceObjs.end());
    for (std::list<FContentSpaceObj*>::const_iterator It = SpaceObjsCopy.begin();
         It != SpaceObjsCopy.end(); ++It)
        (*It)->DestroySpaceObj();

    // 06f72b41-06f72bc4: a copy of MissionObjects (operator new(size * 4) +
    // FUN_06eed270, EH state 1); each id: Destroy(id, DESTROY_NORMAL), then
    // RemoveMissionObject(id).
    std::vector<unsigned int> const MissionObjectsCopy(MissionObjects);
    for (std::vector<unsigned int>::const_iterator It = MissionObjectsCopy.begin();
         It != MissionObjectsCopy.end(); ++It)
    {
        pub::SpaceObj::Destroy(*It, DESTROY_NORMAL);
        RemoveMissionObject(*It);                                         // 06f72baf
    }
}

// FUN_06f743e0 (06f743e0-06f74593, RET 4): VC6 insert(end, 1, id) with its inlined
// reallocation (growth policy of the container differs, contents do not).
void FSystemInfo::AddMissionObject(unsigned int SpaceObjId)
{
    MissionObjects.push_back(SpaceObjId);
}

// FUN_06f74620 (06f74620-06f746d7, RET 4).
void FSystemInfo::RemoveMissionObject(unsigned int SpaceObjId)
{
    // 06f74623-06f74692: erase the first match; the reputation out-param reuses the
    // argument's stack slot.
    for (std::vector<unsigned int>::iterator It = MissionObjects.begin();
         It != MissionObjects.end(); ++It)
    {
        if (*It != SpaceObjId)
            continue;
        MissionObjects.erase(It);
        int Reputation;
        if (pub::SpaceObj::GetRep(SpaceObjId, Reputation) == 0)          // 06f74679, JNZ skip
            pub::Reputation::Free(Reputation);                            // 06f7468b
        break;
    }
    // 06f74694-06f746d5: erase the first match from +0xa8.
    for (std::vector<unsigned int>::iterator It = Field_0xa8.begin(); It != Field_0xa8.end(); ++It)
    {
        if (*It == SpaceObjId)
        {
            Field_0xa8.erase(It);
            break;
        }
    }
}

// FUN_06f745a0 (06f745a0-06f74615, RET 8).
void FSystemInfo::SetObjectFlag(unsigned int SpaceObjId, bool bFlag)
{
    std::vector<unsigned int>::iterator It = Field_0xa8.begin();
    while (It != Field_0xa8.end() && *It != SpaceObjId)
        ++It;
    if (bFlag)
    {
        // 06f745cb-06f745e3: FUN_06f937c0 insert(end, 1, id) when absent.
        if (It == Field_0xa8.end())
            Field_0xa8.insert(Field_0xa8.end(), 1, SpaceObjId);
    }
    else if (It != Field_0xa8.end())
    {
        Field_0xa8.erase(It);                                             // 06f745f1-06f7460c
    }
}

// FUN_06f746e0 (06f746e0-06f746e6).
std::vector<unsigned int>* FSystemInfo::GetMissionObjects()
{
    return &MissionObjects;
}

// FUN_06f746f0 (06f746f0-06f74892). The loop body is RemoveMissionObject inlined
// (06f747ac-06f74860: erase from +0x98 with GetRep/Free on a hit, then erase from +0xa8).
void FSystemInfo::DestroyMissionObjects()
{
    std::vector<unsigned int> const MissionObjectsCopy(MissionObjects);   // 06f74708-06f74785
    for (std::vector<unsigned int>::const_iterator It = MissionObjectsCopy.begin();
         It != MissionObjectsCopy.end(); ++It)
    {
        pub::SpaceObj::Destroy(*It, DESTROY_NORMAL);                      // 06f74796
        RemoveMissionObject(*It);
    }
}

// FUN_06f74380 (06f74380-06f743ce, RET 4).
void FSystemInfo::AddSpaceObj(FContentSpaceObj* Object)
{
    SpaceObjs.push_back(Object);                                          // node at the tail
    Object->SetSystem(this);                                              // FUN_06eb7090
}

// FUN_06f74310 (06f74310-06f74373, RET 8).
void FSystemInfo::RemoveSpaceObj(unsigned int SpaceObjId, bool bDelete)
{
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if ((*It)->SpaceObjId != SpaceObjId)
            continue;
        // 06f74337-06f7434a: vtable slot 0 (scalar deleting dtor) with 1 on a non-null
        // object.
        if (bDelete)
            delete *It;
        SpaceObjs.erase(It);
        return;
    }
}

// FUN_06f73930 (06f73930-06f73981, RET 4).
FContentSpaceObj* FSystemInfo::FindShip(unsigned int SpaceObjId)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship != nullptr && Ship->SpaceObjId == SpaceObjId)
            return Ship;
    }
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if ((*It)->SpaceObjId == SpaceObjId)
            return *It;
    }
    return nullptr;
}

// FUN_06f73a40 (06f73a40-06f73ab9, RET 4).
FContentSpaceObj* FSystemInfo::FindShipOrSolar(unsigned int SpaceObjId)
{
    FContentSpaceObj* Found = FindShip(SpaceObjId);                        // inlined
    if (Found != nullptr)
        return Found;
    // 06f73a96-06f73ab5: no break -- the last match wins.
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
    {
        if (Npc->SpaceObjId == SpaceObjId)
            Found = &*Npc;
    }
    return Found;
}

// FUN_06f73990 (06f73990-06f739b9, RET 4).
FContentSpaceObj* FSystemInfo::FindSpaceObj(unsigned int SpaceObjId)
{
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if ((*It)->SpaceObjId == SpaceObjId)
            return *It;
    }
    return nullptr;
}

// FUN_06f739c0 (06f739c0-06f73a2f, RET 4): the inlined strcmp of the argument's chars
// against the object's nickname chars (+0xd8).
FContentSpaceObj* FSystemInfo::FindSpaceObjByNickname(char const* Nickname)
{
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if (strcmp(Nickname, (*It)->Nickname.Data) == 0)
            return *It;
    }
    return nullptr;
}

// FUN_06f72980 (06f72980-06f72a13, RET 0xc).
void FSystemInfo::SendPendingComms(unsigned int Arg1, unsigned int Unused2, unsigned int Key)
{
    (void)Unused2;
    // 06f7298e-06f729b4: the objects of +0xdc.
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
        (*It)->SendPendingComm(Arg1, Key, 1);                              // FUN_06eb6e00
    // 06f729b6-06f729dd: the solar NPCs of +0x88.
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
        Npc->SendPendingComm(Arg1, Key, 1);
    // 06f729df-06f72a0d: the ship (+0x10) of every player of +0xb8, when set.
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship != nullptr)
            Ship->SendPendingComm(Arg1, Key, 1);
    }
}

// Squared distance from Point to Position as TU58's proximity loops compute it: the three
// differences stay on the x87 stack, summed as (z*z + x*x) + y*y (06f72f2b-06f72f39,
// 06f731b6-06f731c4; FUN_06f72dd0's (x*x + z*z) + y*y is the same sum).
static double DistanceSquaredBetween(Vector const* Point, Vector const* Position)
{
    double const DeltaX = static_cast<double>(Point->x) - Position->x;
    double const DeltaY = static_cast<double>(Point->y) - Position->y;
    double const DeltaZ = static_cast<double>(Point->z) - Position->z;
    return DeltaZ * DeltaZ + DeltaX * DeltaX + DeltaY * DeltaY;
}

// FUN_06f72dd0 (06f72dd0-06f72e97, RET 0xc).
bool FSystemInfo::IsPlayerShipNear(Vector const* Point, float const* Radius, float* OutDistanceSq)
{
    // 06f72dd3-06f72deb: the square is kept as a float (stored into the argument slot).
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    *OutDistanceSq = -1.0f;
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr)
            continue;
        Vector Position;
        Ship->GetPosition(&Position);                                     // FUN_06eb70f0
        double const DistanceSq = DistanceSquaredBetween(Point, &Position);
        // 06f72e48-06f72e51: FCOM, TEST AH,0x41, JNP -> less or equal: found.
        if (DistanceSq <= RadiusSq)
        {
            *OutDistanceSq = -1.0f;                                       // 06f72e8b
            return true;
        }
        // 06f72e53-06f72e69: the first distance (the -1.0f bit pattern still stored) or a
        // smaller one (FCOM, TEST AH,5, JP skips greater / equal / unordered) is kept.
        if (*OutDistanceSq == -1.0f || DistanceSq < *OutDistanceSq)
            *OutDistanceSq = static_cast<float>(DistanceSq);
    }
    return false;
}

// FUN_06f72ea0 (06f72ea0-06f730bb, RET 0xc). The out vector is erased first (FUN_06f6a640 +
// FUN_06ea1490 on [first, last)); the append is VC6 insert(end, 1, player) with its inlined
// reallocation (FUN_06fa3db0 / FUN_06eed270 / FUN_06f38a50).
void FSystemInfo::FindPlayersNear(Vector const* Point, float const* Radius,
                                  std::vector<FContentPlayerState*>* Out)
{
    Out->clear();
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr)
            continue;
        Vector Position;
        Ship->GetPosition(&Position);                                     // FUN_06eb70f0
        // 06f72f3b-06f72f4a: FCOMP, TEST AH,0x41, JP skips greater / unordered.
        if (DistanceSquaredBetween(Point, &Position) <= RadiusSq)
            Out->push_back(*It);
    }
}

// FUN_06f730c0 (06f730c0-06f7312b, RET 0xc).
bool FSystemInfo::IsSeenByPlayerShip(Vector const* Point, float const* MaxDistance,
                                     float const* ConeAngle)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr)
            continue;
        FSpaceObjPlacement Placement;
        Ship->GetPlacement(&Placement);                                   // FUN_06eb70a0
        if (IsPointInViewCone(Point, &Placement, MaxDistance, ConeAngle))  // FUN_06f672d0
            return true;
    }
    return false;
}

// FUN_06f73130 (06f73130-06f731ff, RET 8). Both GetField120 outputs reuse the argument
// slots; only the +0x124 id is read.
bool FSystemInfo::IsNearPlayerField124Object(Vector const* Point, float const* Radius)
{
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr || !Ship->HasField120())                      // FUN_06f57710
            continue;
        unsigned int Id120;
        unsigned int Id124;
        Ship->GetField120(&Id120, &Id124);                                // FUN_06f57720
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Id124, Position, Orientation);         // 06f73198, result ignored
        // 06f731c6-06f731d5: FCOMP, TEST AH,0x41, JNP -> less or equal: true.
        if (DistanceSquaredBetween(Point, &Position) <= RadiusSq)
            return true;
    }
    return false;
}

// FUN_06f73210 (06f73210-06f732d8, RET 8).
bool FSystemInfo::IsNearPlayerField120Segment(Vector const* Point, float const* Radius)
{
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr || !Ship->HasField120())                      // FUN_06f57710
            continue;
        unsigned int Id120;
        unsigned int Id124;
        Ship->GetField120(&Id120, &Id124);                                // FUN_06f57720
        Vector Start;
        Vector End;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Id120, Start, Orientation);            // 06f7327a
        pub::SpaceObj::GetLocation(Id124, End, Orientation);              // 06f7328b
        Vector Closest;
        // 06f732a1-06f732b2: FUN_06f611a0, FCOMP, TEST AH,0x41, JNP -> less or equal: true.
        if (DistanceSquaredToSegment(Point, &Start, &End, &Closest) <= RadiusSq)
            return true;
    }
    return false;
}

// FUN_06f732e0 (06f732e0-06f7367a, RET 0xc).
int FSystemInfo::CreateNpcShip(FContentNpcShipParams const* Params, unsigned int DockTarget,
                               FContentNpcShip** OutShip)
{
    // 06f73308-06f734a5: the whole block is copied (FUN_06f73680 + the inlined rest, EH states
    // 0..3) and destroyed on both exits (06f7355f-06f735ec / 06f7360d-06f7365b).
    FContentNpcShipParams Copy(*Params);
    Copy.System = this;                                                   // 06f734b5

    if ((DockTarget & 0x3fffffffu) != 0)
    {
        // 06f735f5-06f73608: TU53 FUN_06f60340 queues the block for launch from the dock
        // target; its bool is returned as the result.
        return QueueDockLaunch(&Copy, DockTarget, OutShip) ? 1 : 0;
    }

    // 06f734bf-06f734ec: operator new(0x5b0) + FUN_06f336a0.
    FContentNpcShip* Ship = new FContentNpcShip(&Copy);
    if (OutShip != nullptr)
        *OutShip = Ship;                                                  // 06f73505
    // 06f73507-06f7354c: appended to SpaceObjs; the ship took System from the block.
    SpaceObjs.push_back(Ship);
    // 06f7353c-06f73557: controller 0 gets message 0x1010 with the ship.
    unsigned int const Controller = 0;
    FContentNpcShip* MsgShip = Ship;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::NpcShipCreated, &MsgShip);
    return 0;                                                             // 06f735f1
}

// FUN_06f73810 (06f73810-06f738b1, RET 0xc). The created ship is written into the OutShips
// argument slot (06f73834); CreateNpcShip writes it on every path, so a local suffices.
int FSystemInfo::CreateNpcShips(std::list<FContentNpcShipParams> const* ParamsList,
                                unsigned int DockTarget, std::list<FContentNpcShip*>* OutShips)
{
    FContentNpcShip* Ship = nullptr;
    for (std::list<FContentNpcShipParams>::const_iterator It = ParamsList->begin();
         It != ParamsList->end(); ++It)
    {
        int const Result = CreateNpcShip(&*It, DockTarget, &Ship);
        // 06f73847-06f73854: a failed SpacePop ship (block +0x6c) stops the loop.
        if (Result != 0 && It->ControllerId == GSpacePopControllerId)
            return Result;
        // 06f73856-06f73892: push_back when both the list and the ship are non-null.
        if (OutShips != nullptr && Ship != nullptr)
            OutShips->push_back(Ship);
    }
    return 0;
}

// FUN_06f738c0 (06f738c0-06f7392a, RET 8).
void FSystemInfo::AddJumpingInSpaceObj(FContentSpaceObj* Object, unsigned int GateId)
{
    // 06f738c4-06f7390d: the AddSpaceObj body (FUN_06f74380) inlined.
    SpaceObjs.push_back(Object);
    Object->SetSystem(this);                                              // FUN_06eb7090
    // 06f7390d-06f7391e: the id goes through the first argument's stack slot.
    unsigned int const ShipId = Object->SpaceObjId;
    pub::SpaceObj::JumpIn(ShipId, GateId);
}

// FUN_06f73bb0 (06f73bb0-06f73be6, RET 4): SETZ on the argument, then FUN_06f33fd0 on the
// value of every node of SpaceObjs (no type check).
void FSystemInfo::SetNpcShipsField1b1(bool bClear)
{
    bool const bValue = !bClear;
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
        static_cast<FContentNpcShip*>(*It)->SetField1b1(bValue);
}

// FUN_06f73bf0 (06f73bf0-06f73dc1, RET 4). The squared distance out-parameter of
// IsPlayerShipNear reuses the argument slot; the radii are one stack float rewritten
// before each call.
double FSystemInfo::GetNpcShipLifeRate(FContentNpcShip* Ship)
{
    float const PlayerNear = -1.0f;                                       // 06f73bfe local_40
    if (Ship->HasField120())                                              // FUN_06f57710
        return PlayerNear;

    float DistanceSq;
    float Radius;
    Vector Position;
    if (pub::SinglePlayer())                                              // 06f73c13
    {
        Radius = 2500.0f;
        Ship->GetPosition(&Position);
        if (IsPlayerShipNear(&Position, &Radius, &DistanceSq))            // 06f73c42
            return PlayerNear;

        // 06f73c4f-06f73cbe: IsSeenByPlayerShip inlined -- 4000 / 70 degrees.
        float const ConeAngle = 70.0f;
        float const MaxDistance = 4000.0f;
        Ship->GetPosition(&Position);
        for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
        {
            FContentPlayerShip* PlayerShip = (*It)->ShipObject;
            if (PlayerShip == nullptr)
                continue;
            FSpaceObjPlacement Placement;
            PlayerShip->GetPlacement(&Placement);                         // FUN_06eb70a0
            if (IsPointInViewCone(&Position, &Placement, &MaxDistance, &ConeAngle))  // 06f73ca3
                return PlayerNear;
        }

        Radius = 4000.0f;
        Ship->GetPosition(&Position);
        if (IsNearPlayerField124Object(&Position, &Radius))               // 06f73cdc
            return PlayerNear;
    }
    else
    {
        Radius = 2500.0f;
        Ship->GetPosition(&Position);
        if (IsPlayerShipNear(&Position, &Radius, &DistanceSq))            // 06f73d7a
            return PlayerNear;
        Radius = 4000.0f;
        Ship->GetPosition(&Position);
        if (IsNearPlayerField124Object(&Position, &Radius))               // 06f73d9f
            return PlayerNear;
    }

    // 06f73cf3-06f73d0c (both branches).
    Radius = 2500.0f;
    Ship->GetPosition(&Position);
    if (IsNearPlayerField120Segment(&Position, &Radius))                  // 06f73d05
        return PlayerNear;

    // 06f73d12-06f73d21: FCOMP 6250000.0f (DAT_06fb6054), TEST AH,0x41, JNZ -> less, equal
    // or unordered: 15.0f (DAT_06fbbf08).
    if (!(DistanceSq > 6250000.0f))
        return 15.0f;
    // 06f73d23-06f73d46: (sqrt - 2500.0) * 0.01 + 1.0 (doubles DAT_06fbbc68 / DAT_06fb7a88 /
    // DAT_06fb70c0); FCOM 15.0f, TEST AH,0x41, JNZ -> not greater: the value, else 15.0f.
    double const Rate = (std::sqrt(static_cast<double>(DistanceSq)) - 2500.0) * 0.01 + 1.0;
    if (!(Rate > 15.0f))
        return Rate;
    return 15.0f;
}

// FUN_06f73dd0 (06f73dd0-06f73f93, RET 4).
void FSystemInfo::DestroyFlaggedObjectsAwayFromPlayers(float Unused)
{
    (void)Unused;
    // 06f73deb-06f73e08: no buffer or no element -> nothing.
    if (Field_0xa8.empty())
        return;

    // 06f73e0e-06f73e88: the ids whose object has no player ship within 10000
    // (DAT_06fbbee4) -- VC6 insert(end, 1, id) (FUN_06f937c0) into a local vector.
    std::vector<unsigned int> AwayIds;
    float const Radius = 10000.0f;
    for (std::vector<unsigned int>::iterator It = Field_0xa8.begin(); It != Field_0xa8.end(); ++It)
    {
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(*It, Position, Orientation);          // 06f73e4d, result ignored
        float DistanceSq;
        if (!IsPlayerShipNear(&Position, &Radius, &DistanceSq))           // 06f73e63
            AwayIds.insert(AwayIds.end(), 1, *It);
    }

    // 06f73e8e-06f73f6e: Destroy(id, 0), then the RemoveMissionObject body inlined (erase
    // from +0x98 with GetRep / Reputation::Free on a hit, erase from +0xa8).
    for (std::vector<unsigned int>::const_iterator It = AwayIds.begin(); It != AwayIds.end(); ++It)
    {
        pub::SpaceObj::Destroy(*It, DESTROY_NORMAL);                      // 06f73ea6
        RemoveMissionObject(*It);
    }
}

// The player-target test FUN_06f73fa0 inlines twice (06f740fc-06f7414d, 06f741de-06f74229):
// true when the ship of some player of Players targets ShipId (pub::SpaceObj::GetTarget).
// Retail compares the out value whatever GetTarget returned; the port starts it at 0.
static bool IsTargetedByPlayerShip(std::vector<FContentPlayerState*> const& Players,
                                   unsigned int ShipId)
{
    for (std::vector<FContentPlayerState*>::const_iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* PlayerShip = (*It)->ShipObject;
        if (PlayerShip == nullptr)
            continue;
        unsigned int const PlayerShipId = PlayerShip->SpaceObjId;
        unsigned int TargetId = 0;
        pub::SpaceObj::GetTarget(PlayerShipId, TargetId);                 // 06f7412d / 06f7420d
        if (TargetId == ShipId)
            return true;
    }
    return false;
}

// FUN_06f73fa0 (06f73fa0-06f7430a, RET 4). EH: the local list (state 0) and the formation
// vector (state 1) are RAII locals here.
void FSystemInfo::UpdateNpcShipLives(float DeltaTime)
{
    if (SpaceObjs.empty())                                                // 06f73fbb +0xe4
        return;

    std::list<FContentSpaceObj*> Expired;                                 // 06f73fd3
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        FContentNpcShip* Ship = static_cast<FContentNpcShip*>(*It);

        // 06f74017-06f7404f: a formation member (slot +0x14c non-zero, formation +0x154 set)
        // only has its life refilled: FCOMP, TEST AH,5, JP -> copy only when less.
        if (Ship->FormationSlot != 0 && Ship->FormationId != 0xffffffffu)
        {
            if (Ship->Field_0x1c8 < Ship->Field_0x1bc)
                Ship->Field_0x1c8 = Ship->Field_0x1bc;
            continue;
        }
        // 06f74054-06f74065: FCOMP -1.0f (DAT_06fbbee0), TEST AH,0x44, JNP -> equal: immortal.
        if (Ship->Field_0x1bc == -1.0f)
            continue;

        // 06f7406b-06f7408a: the product stays on the x87 stack for the compare and is kept as
        // a float (local_48) for the subtraction; FCOMP 0.0f, TEST AH,5, JP -> not less.
        double const Product = GetNpcShipLifeRate(Ship) * DeltaTime;
        float const Step = static_cast<float>(Product);
        if (Product < 0.0)
        {
            // 06f7408c-06f740af: a player is near -- refill as above.
            if (Ship->Field_0x1c8 < Ship->Field_0x1bc)
                Ship->Field_0x1c8 = Ship->Field_0x1bc;
            continue;
        }
        // 06f740b4-06f740cb: FCOMP 0.0f, TEST AH,1, JNZ -> less or unordered: skip.
        if (!(Ship->Field_0x1c8 >= 0.0f))
            continue;
        Ship->Field_0x1c8 = static_cast<float>(static_cast<double>(Ship->Field_0x1c8) - Step);
        // 06f740d9-06f740e6: FCOMP 0.0f, TEST AH,5, JP -> only a negative life expires.
        if (!(Ship->Field_0x1c8 < 0.0f))
            continue;
        Ship->Field_0x1c8 = 0.0f;                                         // 06f740ec

        if (pub::SinglePlayer() && IsTargetedByPlayerShip(Players, Ship->SpaceObjId))
            continue;

        if (Ship->FormationId == 0xffffffffu)
        {
            Expired.push_back(Ship);                                      // 06f7416f FUN_06f1e390
            continue;
        }

        // 06f74179-06f74272: the whole formation expires together unless one of its ships is
        // a player's (FUN_06f49bc0) or, in single player, targeted by a player.
        std::vector<FContentShipBase*> Members;                           // FUN_06ead6a0
        CollectFormationMembers(&Members, Ship->FormationId);             // FUN_06f66220
        bool bKeep = false;
        for (std::vector<FContentShipBase*>::iterator Member = Members.begin();
             Member != Members.end(); ++Member)
        {
            unsigned int const MemberId = (*Member)->SpaceObjId;
            if (FindContentPlayerStateByShip(&MemberId) != nullptr)
            {
                bKeep = true;
                break;
            }
            if (pub::SinglePlayer() && IsTargetedByPlayerShip(Players, MemberId))
            {
                bKeep = true;
                break;
            }
        }
        if (!bKeep)
        {
            for (std::vector<FContentShipBase*>::iterator Member = Members.begin();
                 Member != Members.end(); ++Member)
                Expired.push_back(*Member);                               // 06f74259
        }
    }

    // 06f74292-06f742b0: FUN_06f34670 (pub::SpaceObj::Destroy, DESTROY_NORMAL) on each.
    for (std::list<FContentSpaceObj*>::iterator It = Expired.begin(); It != Expired.end(); ++It)
        (*It)->DestroySpaceObj();
}

// FUN_06f72cb0 (06f72cb0-06f72d01, RET 4).
FSystemZone* FSystemInfo::FindZone(unsigned int ZoneId)
{
    std::map<unsigned int, FSystemZone*>::iterator It = ZoneById.find(ZoneId);
    if (It == ZoneById.end())
        return nullptr;
    return It->second;
}

// FUN_06f72d10 (06f72d10-06f72dbe, RET 4).
FSystemZone* FSystemInfo::FindSmallestZoneAt(Vector const* Position)
{
    unsigned int ZoneIds[8];
    unsigned int Count;
    FSystemZone* Best = nullptr;
    float BestRadius = 3.40282347e+38f;                  // 0x7f7fffff
    pub::System::InZones(SystemId, *Position, 0.0f, ZoneIds, 8, Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        // FUN_06f7b0f0 is map::find (lower_bound, then 06f7b12b-06f7b130 returns end when
        // key < node key).
        std::map<unsigned int, FSystemZone*>::iterator It = ZoneById.find(ZoneIds[Index]);
        if (It == ZoneById.end())
            continue;
        FSystemZone* Zone = It->second;
        if (Zone == nullptr)
            continue;
        float Radius = pub::Zone::GetRadius(Zone->ZoneId);
        if (Radius < BestRadius)
        {
            BestRadius = Radius;
            Best = Zone;
        }
    }
    return Best;
}

// FUN_06f78ef0 (06f78ef0-06f790f8, RET 8). FUN_06f79100 returns a copy of the value of
// lower_bound(Id) when its key equals Id, else of the head node's value (retail reads
// whatever the head holds; the port's find() == end() dereference faults instead). The
// copy is then assigned field by field: +0x00..+0x14, +0x18 (erase, reserve, append each),
// +0x28, +0x2c (FUN_06f79980 operator=), +0x3c (erase, reserve, append each).
void FSystemInfo::GetEncounterParameters(unsigned int Id, FEncounterParameters* Out)
{
    FEncounterParameters const Found = *EncounterParameters->find(Id)->second;
    Out->Nickname = Found.Nickname;
    Out->SystemId = Found.SystemId;
    Out->ZoneId = Found.ZoneId;
    Out->RerollInterval = Found.RerollInterval;
    Out->LastRerollTime = Found.LastRerollTime;
    Out->Difficulty = Found.Difficulty;
    Out->Permutations.clear();                                              // FUN_06f7c1f0
    Out->Permutations.reserve(Found.Permutations.size());                   // FUN_06f7ace0
    for (std::vector<FEncounterPermutation>::const_iterator It = Found.Permutations.begin();
         It != Found.Permutations.end(); ++It)
    {
        // 06f78fa0-06f78fca: each permutation through a stack copy (FUN_06efa740, EH state 1),
        // inserted at the end (FUN_06f7c010), then freed.
        FEncounterPermutation const Copy(*It);
        Out->Permutations.insert(Out->Permutations.end(), Copy);            // FUN_06f7c010
    }
    Out->CurrentPermutation = Found.CurrentPermutation;
    Out->Formations = Found.Formations;                                     // FUN_06f79980
    Out->Factions.Items.clear();                                            // FUN_06ef6750
    Out->Factions.Items.reserve(Found.Factions.Items.size());               // FUN_06ef7860
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
             Found.Factions.Items.begin();
         It != Found.Factions.Items.end(); ++It)
        Out->Factions.Items.insert(Out->Factions.Items.end(), *It);         // FUN_06ef6bb0
}

namespace
{
    // The comparator of the sort FUN_06f7f370: path labels by id, unsigned (JC).
    bool PathLabelLess(FZonePathLabel const& A, FZonePathLabel const& B)
    {
        return A.Label.Id < B.Label.Id;
    }
}

// FUN_06f76380 (06f76380-06f767b9, RET 4).
int FSystemInfo::BuildPatrolPaths(std::vector<FZonePathLabel>* Labels)
{
    // 06f763a0-06f763b9: FUN_06f7f370, retail's std::sort instance (_Sort_0 with _Sort
    // FUN_06f7f990, _Median FUN_06f7fea0, _Unguarded_insert FUN_06f7fba0, _Insertion_sort
    // FUN_06f7fc80: threshold 16, median of first/mid/last-1). ContentSort keeps its order of
    // labels with equal ids -- the zones of one path_label reach the FPatrolPath ctor in it.
    ContentSort(Labels->data(), Labels->data() + Labels->size(), PathLabelLess);

    // 06f763be-06f763d2: erase(begin, end) on +0x50.
    Paths.clear();

    // 06f763e0-06f764d5: one path per run of equal labels; the run is then erased.
    while (!Labels->empty())
    {
        FZonePathLabel* const Begin = Labels->data();
        FZonePathLabel* const End = Begin + Labels->size();
        FZonePathLabel* First = Begin;
        FZonePathLabel* Last = Begin;
        while (Last != End && Last->Label.Id == First->Label.Id)
            ++Last;

        {
            // 06f76430-06f76496: the local path (EH state 0) is inserted and destroyed
            // (FUN_06f798c0 / FUN_06eec8d0 / FUN_06f79880) before the run is erased.
            FPatrolPath Path(&First, &Last, this);                        // FUN_06f37110
            Paths.insert(Paths.end(), Path);                              // FUN_06f7a470
        }
        // 06f7649b-06f764ce: FUN_06f7e0c0 moves + FUN_06f7acb0.
        Labels->erase(Labels->begin() + (First - Begin), Labels->begin() + (Last - Begin));
    }

    // 06f764da-06f7658a: every zone of a path points at it. Retail dereferences the zone
    // without a test (the path's zones all came from this system's zone list).
    for (std::vector<FPatrolPath>::iterator Path = Paths.begin(); Path != Paths.end(); ++Path)
    {
        std::vector<unsigned int> PathZoneIds;
        Path->GetZoneIds(&PathZoneIds);                                   // FUN_06f37ff0
        for (std::vector<unsigned int>::iterator ZoneId = PathZoneIds.begin();
             ZoneId != PathZoneIds.end(); ++ZoneId)
        {
            std::map<unsigned int, FSystemZone*>::iterator Found = ZoneById.find(*ZoneId);
            FSystemZone* Zone = (Found != ZoneById.end()) ? Found->second : nullptr;
            Zone->SetPath(&*Path);                                        // FUN_06fa8120
        }
    }

    // 06f76594-06f765ce: up to 32 objects of the system, any distance, type mask 0x600d60.
    unsigned int ObjectIds[0x20];
    Vector Origin;
    Origin.x = 0.0f;
    Origin.y = 0.0f;
    Origin.z = 0.0f;
    unsigned int ObjectCount;
    pub::System::ScanObjects(SystemId, ObjectIds, 0x20, Origin, -1.0f, 0x600d60, ObjectCount);

    // 06f765d4-06f765e9: FLD float DAT_06fbbee8 (25e6), FSQRT, FSTP float.
    float const Radius = static_cast<float>(std::sqrt(static_cast<double>(25000000.0f)));

    for (unsigned int ObjectIndex = 0; ObjectIndex < ObjectCount; ++ObjectIndex)
    {
        // 06f76600-06f76641
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(ObjectIds[ObjectIndex], Position, Orientation);
        unsigned int ZoneIds[0x4b];
        unsigned int ZoneCount;
        pub::System::InZones(SystemId, Position, Radius, ZoneIds, 0x4b, ZoneCount);

        // 06f76648-06f7670f: the paths of those zones, each once.
        std::vector<FPatrolPath*> NearPaths;
        for (unsigned int ZoneIndex = 0; ZoneIndex < ZoneCount; ++ZoneIndex)
        {
            std::map<unsigned int, FSystemZone*>::iterator Found = ZoneById.find(ZoneIds[ZoneIndex]);
            if (Found == ZoneById.end())
                continue;
            FSystemZone* Zone = Found->second;
            if (Zone == nullptr || Zone->Path == nullptr)
                continue;
            FPatrolPath* Path = Zone->GetPath();                          // FUN_06fa8130
            bool bKnown = false;
            for (std::vector<FPatrolPath*>::iterator It = NearPaths.begin(); It != NearPaths.end(); ++It)
            {
                if (*It == Path)
                {
                    bKnown = true;
                    break;
                }
            }
            if (!bKnown)
                NearPaths.insert(NearPaths.end(), Path);                  // FUN_06eb5770
        }

        // 06f76719-06f76738
        for (std::vector<FPatrolPath*>::iterator It = NearPaths.begin(); It != NearPaths.end(); ++It)
            (*It)->RegisterObject(ObjectIds[ObjectIndex], &Position);     // FUN_06f378c0
    }

    // 06f7677a-06f767a1: the size of +0x50.
    return static_cast<int>(Paths.size());
}

// The ring-record half of FUN_06f76b10 (06f76bfb-06f76c98 and twice more): the pair's
// ObjectId, Position, the whole 0x30 transform (FUN_06f594c0) and Reputation.
static FTradeLaneRing MakeTradeLaneRing(FTradeLaneRingInfo const& Info)
{
    FTradeLaneRing Ring;
    Ring.ObjectId = Info.ObjectId;
    Ring.Position = Info.Position;
    Ring.Placement = Info.Placement;
    Ring.Reputation = Info.Reputation;
    return Ring;
}

// 06f76b70-06f76bf2: FUN_06eac620 x2 / FUN_06ead6a0 x2 (empty vectors), FUN_06f143d0
// (identity rotation, zero translation), -1.0f, -1.0f, 0, 0. Start/End stay unset.
FTradeLane::FTradeLane()
{
    SetMatrixIdentity(&Placement.Rotation);
    Placement.Translation.X = 0.0f;
    Placement.Translation.Y = 0.0f;
    Placement.Translation.Z = 0.0f;
}

// FUN_06f77e00 (06f77e00-06f77e6a, RET 8): map find (FUN_06fa7200 lower_bound + key test).
bool FSystemInfo::FindTradeLaneByRing(unsigned int const* RingId, FTradeLane** OutLane)
{
    std::map<unsigned int, FTradeLane*>::iterator It = TradeLaneByRing.find(*RingId);
    if (It == TradeLaneByRing.end())
    {
        *OutLane = nullptr;
        return false;
    }
    *OutLane = It->second;
    return true;
}

// FUN_06f77bd0 (06f77bd0-06f77df9, RET 8; added by CNT-AI6). TradeLaneByRing lower_bound
// (FUN_06fa7200) + key test; the hit's lane Rings (lane +0) assigned to *OutRings with the
// inlined vector operator= (self-assignment returns at once; element copies of 0x44 bytes,
// FUN_06f30960 for the tail, reallocation when the capacity is short).
bool FSystemInfo::GetTradeLaneRingsByRing(unsigned int const* RingId,
                                          std::vector<FTradeLaneRing>* OutRings)
{
    std::map<unsigned int, FTradeLane*>::iterator It = TradeLaneByRing.find(*RingId);
    if (It == TradeLaneByRing.end())
        return false;
    *OutRings = It->second->Rings;
    return true;
}

namespace
{
    // FUN_06f778b0 (thiscall on the transform, RET 4): the rotation from Orientation, a zero
    // translation.
    void SetRingPlacement(FTransform* Out, Matrix const* Orientation)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Out->Rotation.D[Row][Column] = Orientation->E[Row][Column];
        }
        Out->Translation.X = 0.0f;
        Out->Translation.Y = 0.0f;
        Out->Translation.Z = 0.0f;
    }

    // FUN_06f77090 06f7715a-06f771e2 (and the two chain walks): one live ring, reputation -1.
    FTradeLaneRing MakeLiveRing(unsigned int RingId)
    {
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(RingId, Position, Orientation);
        FTradeLaneRing Ring;
        SetRingPlacement(&Ring.Placement, &Orientation);                    // FUN_06f778b0
        Ring.ObjectId = RingId;
        Ring.Position = Position;
        Ring.Reputation = 0xffffffffu;
        return Ring;
    }
}

// FUN_06f77090 (06f77090-06f778a1).
int FSystemInfo::RebuildTradeLanes()
{
    TradeLanes.clear();                                                     // FUN_06f7a350

    // 06f770d4-06f77111: every trade lane ring of the system (type 0x80, radius -1).
    unsigned int Rings[0x80];
    unsigned int RingCount = 0;
    Vector Origin;
    Origin.x = 0.0f;
    Origin.y = 0.0f;
    Origin.z = 0.0f;
    pub::System::ScanObjects(SystemId, Rings, 0x80, Origin, -1.0f, 0x80, RingCount);

    for (unsigned int Index = 0; Index < RingCount; ++Index)
    {
        if (Rings[Index] == 0)
            continue;
        // 06f7715a-06f771e2: the chain starts with this ring.
        std::vector<FTradeLaneRing> Chain;
        Chain.insert(Chain.end(), MakeLiveRing(Rings[Index]));              // FUN_06f792f0
        unsigned int Next;
        unsigned int Prev;
        pub::SpaceObj::GetTradelaneNextAndPrev(Rings[Index], Next, Prev);

        // 06f77210-06f77305: the next rings go to the back; each one is taken off the scan
        // list -- the first match from Index on, else every match below Index.
        while (Next != 0)
        {
            Chain.insert(Chain.end(), 1, MakeLiveRing(Next));               // FUN_06f7a050
            unsigned int Slot = Index;
            for (; Slot < RingCount; ++Slot)
            {
                if (Rings[Slot] == Next)
                {
                    Rings[Slot] = 0;
                    break;
                }
            }
            if (Slot == RingCount)
            {
                for (int Below = static_cast<int>(Index) - 1; Below >= 0; --Below)
                {
                    if (Rings[Below] == Next)
                        Rings[Below] = 0;
                }
            }
            unsigned int const Current = Next;
            pub::SpaceObj::GetTradelaneNextAndPrev(Current, Next, Prev);
        }

        // 06f7730b-06f77429: the previous rings go to the front; every match below Index and
        // the first from Index on leave the scan list.
        pub::SpaceObj::GetTradelaneNextAndPrev(Rings[Index], Next, Prev);
        while (Prev != 0)
        {
            Chain.insert(Chain.begin(), 1, MakeLiveRing(Prev));             // FUN_06f7a050
            for (int Below = static_cast<int>(Index) - 1; Below >= 0; --Below)
            {
                if (Rings[Below] == Prev)
                    Rings[Below] = 0;
            }
            for (unsigned int Slot = Index; Slot < RingCount; ++Slot)
            {
                if (Rings[Slot] == Prev)
                {
                    Rings[Slot] = 0;
                    break;
                }
            }
            unsigned int const Current = Prev;
            pub::SpaceObj::GetTradelaneNextAndPrev(Current, Next, Prev);
        }

        // 06f7742f-06f77542: the lane (FTradeLane defaults) with the chain, the first ring's
        // position and rotation and the last ring's position.
        FTradeLane Lane;
        Lane.Rings = Chain;                                                 // FUN_06f68ac0
        Lane.StartPosition = Chain.front().Position;
        Lane.Placement = Chain.front().Placement;
        Lane.EndPosition = Chain.back().Position;
        TradeLanes.insert(TradeLanes.end(), 1, Lane);                       // FUN_06f7b4d0
    }

    // 06f775cd-06f7785c: the rings of every lane enter TradeLaneByRing and get their attack
    // candidates; the affiliation carries over from ring to ring when a lookup fails.
    for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin(); Lane != TradeLanes.end(); ++Lane)
    {
        unsigned int Affiliation = 0xffffffffu;
        float LaneMax = 0.0f;
        for (std::vector<FTradeLaneRing>::size_type RingIndex = 0; RingIndex < Lane->Rings.size(); ++RingIndex)
        {
            FTradeLaneRing const Ring = Lane->Rings[RingIndex];
            TradeLaneByRing[Ring.ObjectId] = &*Lane;                        // FUN_06f003e0 + 06f776b8
            int Rep = -1;
            pub::SpaceObj::GetSolarRep(Ring.ObjectId, Rep);
            pub::Reputation::GetAffiliation(Rep, Affiliation);

            FTradeLaneRingCandidates Found;
            Found.RingIndex = static_cast<int>(RingIndex);
            CollectRingCandidates(&Ring, &Ring.Position, &Affiliation, &Found);   // FUN_06f778e0

            // 06f77734-06f7777e: the ring's largest weight, then the lane's (TEST AH,5 JP /
            // TEST AH,0x41 JNZ: NaN never replaces).
            float RingMax = 0.0f;
            for (std::vector<std::pair<FSystemZone*, float>>::const_iterator It = Found.Candidates.begin();
                 It != Found.Candidates.end(); ++It)
            {
                if (RingMax < It->second)
                    RingMax = It->second;
            }
            if (RingMax > LaneMax)
                LaneMax = RingMax;
            if (!Found.Candidates.empty())
            {
                // 06f7778b-06f777d1: a copy with the ring's maximum.
                FTradeLaneRingCandidates Entry;
                Entry.RingIndex = Found.RingIndex;
                Entry.Candidates = Found.Candidates;                        // FUN_06f55880
                Entry.MaxWeight = RingMax;
                Lane->RingCandidates.insert(Lane->RingCandidates.end(), 1, Entry);   // FUN_06f7b1d0
            }
        }
        Lane->MaxWeight = LaneMax;                                          // 06f77842
    }
    return static_cast<int>(TradeLanes.size());
}

// FUN_06f778e0 (06f778e0-06f77b3b, RET 0x10).
void FSystemInfo::CollectRingCandidates(FTradeLaneRing const* /*Ring*/, Vector const* Position,
                                        unsigned int const* Affiliation,
                                        FTradeLaneRingCandidates* Out)
{
    // 06f77903-06f77998: identity orientation at Position, the SpacePop zone lists.
    std::vector<FSystemZone*> AtShip;
    std::vector<FSystemZone*> Near;
    std::vector<FSystemZone*> Ahead;
    FSpaceObjPlacement Placement;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
            Placement.Orientation.E[Row][Column] = Row == Column ? 1.0f : 0.0f;   // FUN_06efc7a0
    }
    Placement.Position = *Position;
    bool bBlocked;
    CollectSpacePopZones(this, &Placement, &AtShip, &Near, &Ahead, &bBlocked);  // FUN_06f5b000

    // 06f779c2-06f77ae7: the zones at the ring, then the near ones. The running maximum
    // kept at 06f77a03 / 06f77aa1 is never read.
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        std::vector<FSystemZone*> const& Zones = Pass == 0 ? AtShip : Near;
        for (std::vector<FSystemZone*>::const_iterator It = Zones.begin(); It != Zones.end(); ++It)
        {
            FSystemZone* Zone = *It;
            if (Zone->Path == nullptr)
                continue;
            float HostileFrom;
            float HostileTo;
            GetHostileFactionShares(Zone->GetPopulationFactions(), Affiliation, &HostileFrom,
                                    &HostileTo);                           // FUN_06fa7fb0 / FUN_06f35190
            if (!(HostileTo > 0.0f))
                continue;
            float const Weight = Zone->GetTradelaneAttack();                // FUN_06fa7fd0
            if (Weight > 0.0f)
                Out->Candidates.insert(Out->Candidates.end(), 1,
                                       std::pair<FSystemZone*, float>(Zone, Weight));   // FUN_06f96770
        }
    }
}

namespace
{
    // 06f787c2-06f78825 / 06f788c6-06f7892c (inlined twice): Source arrives as a by-value copy
    // (the Common weighted_vector copy ctor at 06f787c9 / 06f788ce, EH states 3 / 5), Out is
    // cleared (FUN_06f7a020), then the pairs of the copy whose faction is hostile
    // (GetFactionFeelingMask bit 4) to Affiliation are appended in order (FUN_06ef6bb0); the
    // copy is freed at the end (06f78825 / 06f7892c). Out may be the object Source was copied
    // from (the encounter loop).
    void CollectHostileFactions(weighted_vector<unsigned int> Source, unsigned int Affiliation,
                                weighted_vector<unsigned int>* Out)
    {
        Out->Items.clear();                                                 // FUN_06f7a020
        for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
                 Source.Items.begin();
             It != Source.Items.end(); ++It)
        {
            if ((GetFactionFeelingMask(It->first, Affiliation) & 4) != 0)   // FUN_06f350f0
                Out->Items.push_back(*It);                                  // FUN_06ef6bb0
        }
    }
}

// FUN_06f782e0 (06f782e0-06f78d89, RET 0x10).
void FSystemInfo::SpawnTradeLaneAttack(FContentPlayerShip* PlayerShip, int const* Direction,
                                       int EntryIndex, FTradeLane** Lane)
{
    // 06f782fb-06f78326: the player's flag 8 stops it (the state is not null-tested).
    unsigned int ShipId = PlayerShip->SpaceObjId;
    if ((FindContentPlayerStateByShip(&ShipId)->GetFlags() & 8) != 0)     // FUN_06f49bc0 / 06f471a0
        return;

    // 06f78333-06f7835e: (now - LastAttackTime) against the double 180.0 (DAT_06fbbf90; TEST
    // AH,0x41 JZ: greater goes on); otherwise only a LastAttackTime equal to 0 goes on (TEST
    // AH,0x44 JP: unequal and NaN return).
    FTradeLane* const TheLane = *Lane;
    if (!(GetContentGameTime() - static_cast<double>(TheLane->LastAttackTime) > 180.0)
        && TheLane->LastAttackTime != 0.0f)
        return;
    TheLane->LastAttackTime = static_cast<float>(GetContentGameTime());   // FUN_06efec10

    // 06f78373-06f78392: rand() * 2^-15 (DAT_06fb4448) against MaxWeight (TEST AH,5 JP: only
    // below goes on).
    if (!(static_cast<double>(rand()) * (1.0 / 32768.0) < static_cast<double>(TheLane->MaxWeight)))
        return;

    // 06f78398-06f783d0: a destroyed ring (relative health equal to 0; TEST AH,0x44 JNP) stops
    // it. A failed call leaves retail's slot holding the ship id -- non-zero, so it goes on.
    for (std::vector<FTradeLaneRing>::iterator Ring = TheLane->Rings.begin();
         Ring != TheLane->Rings.end(); ++Ring)
    {
        float Health = 1.0f;
        pub::SpaceObj::GetRelativeHealth(Ring->ObjectId, Health);
        if (Health == 0.0f)
            return;
    }

    // 06f783d2-06f785b5: a copy of the ring entries (FUN_06f308e0); draw one by weight until a
    // ring ahead of the ship -- beyond the next ring in the travel direction -- has no player
    // ship within 5000 (DAT_06fbbf04); every other draw is erased (FUN_06f7d0f0 / FUN_06f7a320).
    std::vector<FTradeLaneRingCandidates> Candidates = TheLane->RingCandidates;
    FTradeLaneRingCandidates* Selected = nullptr;
    // [ESP+0x78]: FUN_06f72dd0's distance out-slot; the same slot is GetSolarRep's out-param
    // below (06f78747), so a failed GetSolarRep leaves the selected ring's distance bits there.
    float DistanceSq = 0.0f;
    while (!Candidates.empty() && Selected == nullptr)                    // FUN_06eedfe0
    {
        FTradeLaneRingCandidates* Pick = PickByWeight(
            Candidates.data(), Candidates.data() + Candidates.size(),
            [](FTradeLaneRingCandidates const& Entry) { return Entry.MaxWeight; });
        int const Travel = *Direction;
        if ((Travel == 1 && Pick->RingIndex > EntryIndex + 1)
            || (Travel == -1 && Pick->RingIndex < EntryIndex - 1))
        {
            // 06f784d4-06f78565: the ring copied (FUN_06f594c0 for its transform).
            FTradeLaneRing const Ring = TheLane->Rings[Pick->RingIndex];
            float const Radius = 5000.0f;
            if (!IsPlayerShipNear(&Ring.Position, &Radius, &DistanceSq))   // FUN_06f72dd0
            {
                Selected = Pick;
                continue;
            }
        }
        Candidates.erase(Candidates.begin() + (Pick - Candidates.data()));
    }
    if (Selected == nullptr)
        return;

    // 06f785bb-06f78635: one of the ring's attack zones by weight.
    FSystemZone* Zone = PickByWeight(
        Selected->Candidates.data(), Selected->Candidates.data() + Selected->Candidates.size(),
        [](std::pair<FSystemZone*, float> const& Entry) { return Entry.second; })->first;

    // 06f78637-06f786ee: room for an index per encounter of the zone (FUN_06eb6b30 capacity).
    std::vector<int> Indices;
    Indices.reserve(Zone->Population.Encounters.size());

    // 06f786f0-06f7876d: the ring's owner (ring copied again; GetSolarRep / GetAffiliation,
    // results ignored). Rep is the [ESP+0x78] slot: GetSolarRep writes it only on success,
    // otherwise it still holds the selected ring's DistanceSq bits (written by FUN_06f72dd0 in
    // the iteration that selected it). Affiliation ([ESP+0x1c], the last draw's target) is
    // always written: pub::Reputation::GetAffiliation stores -1 on failure.
    FTradeLaneRing const Ring = TheLane->Rings[Selected->RingIndex];
    int Rep;
    std::memcpy(&Rep, &DistanceSq, sizeof(Rep));
    unsigned int Affiliation = 0;
    pub::SpaceObj::GetSolarRep(Ring.ObjectId, Rep);
    pub::Reputation::GetAffiliation(Rep, Affiliation);

    // 06f78774-06f78860: the encounters whose factions (+0x3c) hold one hostile to the owner
    // (the index compares unsigned, JNC).
    for (int Index = 0; static_cast<unsigned int>(Index) < Zone->Population.Encounters.size(); ++Index)
    {
        // 06f787ab-06f787c2: Hostile starts as a copy of the encounter's factions (+0x3c,
        // FUN_06f55880, EH state 2) and is then filtered in place from a second copy.
        weighted_vector<unsigned int> Hostile(Zone->Population.Encounters[Index].Factions);
        CollectHostileFactions(Hostile, Affiliation, &Hostile);
        if (!Hostile.Items.empty())                                         // FUN_06eae300
            Indices.push_back(Index);                                       // FUN_06eb5770
    }

    // 06f78865-06f788a7: one of them uniformly -- (rand() * count) / 32768 (CDQ / AND 0x7fff /
    // SAR 0xf: signed, truncating) -- and its first formation (the vector's begin, not tested).
    int const Count = static_cast<int>(Indices.size());
    int const Picked = Indices.data()[(rand() * Count) / 32768];
    FEncounterFormation* Formation = Zone->Population.Encounters[Picked].Formations.data();

    // 06f788a9-06f7892c: the formation's factions (+0x14) hostile to the owner.
    weighted_vector<unsigned int> HostileFactions;                          // FUN_06ead6a0
    CollectHostileFactions(Formation->Factions, Affiliation, &HostileFactions);

    // 06f78931-06f78956: a "TLParams" object (type 3); the result is not tested. The local at
    // [ESP+0x10] (EH state 6) is a ref holder: its unwind step, and the release inlined at
    // 06f78cf9-06f78cfe, is FUN_06f28e10.
    TRefHandle<FRandEncParams> Params;
    CreateRandEncParamsByType(3, &Params.Pointer);                          // FUN_06efb8e0
    FRandEncTLParams* Attack = static_cast<FRandEncTLParams*>(Params.Pointer);

    // 06f78956-06f78a04: the ring as target (+0xb4, +0x88, +0x84), the direction (+0xa8), the
    // attack zone (+0xb8: EBP, reloaded with the zone at 06f78635).
    Attack->TargetTradelane = Ring.ObjectId;
    Attack->TargetObjectId = Ring.ObjectId;
    Attack->GotoTargetId = Ring.ObjectId;
    Attack->Direction = *Direction;
    Attack->TargetZone = Zone;

    // 06f78a0a-06f78a91: the attackers' faction by weight.
    Attack->AttackerRep = static_cast<int>(PickByWeight(
        HostileFactions.Items.data(), HostileFactions.Items.data() + HostileFactions.Items.size(),
        [](std::pair<unsigned int, float> const& Entry) { return Entry.second; })->first);

    // 06f78a94-06f78b19: the system, one formation, the spawn point (0, 0, dir * 1400) of
    // weight 80 (FUN_06f7ae20, before the list head), SpacePop's controller.
    Attack->System = this;
    Attack->FormationsToSpawn = 1;
    FRandEncPosition Position;
    Position.Position.x = 0.0f;
    Position.Position.y = 0.0f;
    Position.Position.z = static_cast<float>(*Direction * 1400);
    Position.Weight = 80.0f;
    Attack->Positions.push_back(Position);
    Attack->FormationsToSpawn = 1;
    Attack->ControllerId = GSpacePopControllerId;                           // DAT_06fd0e64

    // 06f78b35-06f78cdb: per formation to spawn, the encounter formation as a formation line:
    // formation_by_class through the faction's formations (FUN_06f50650 / FUN_06f4f3d0, result
    // ignored), else formation; the summed ship count ranges; one ship choice (the line's
    // classes, weight 1) per ship line; weight 1 (FUN_06f79c40 copy + 06f78c72).
    for (int Formed = 0; Formed < Attack->FormationsToSpawn; ++Formed)
    {
        FRandEncFormation Entry;
        Entry.Nickname.clear();
        if (!Formation->FormationByClass.IsEmpty())
            GetFactionProps(&Formation->Faction)->GetFormation(&Formation->FormationByClass.Id,
                                                               &Entry.Nickname.Id);
        else
            Entry.Nickname.Id = Formation->Formation.Id;
        Entry.Formation = FindFormation(&Entry.Nickname.Id);                // FUN_06efd1a0
        Entry.MinCount = 0;
        Entry.MaxCount = 0;
        for (std::vector<FEncounterShip>::const_iterator Line = Formation->Ships.begin();
             Line != Formation->Ships.end(); ++Line)
        {
            Entry.MinCount += Line->MinCount;
            Entry.MaxCount += Line->MaxCount;
            FRandEncShipChoice Choice;
            Choice.ShipClasses = Line->ShipClasses;                         // FUN_06eb35b0
            Choice.Weight = 1.0f;
            Entry.ShipChoices.push_back(Choice);                            // FUN_06f1e3e0
        }
        // 06f78c5e-06f78cb0: a copy of Entry (FUN_06f79c40, EH state 9) gets weight 1.0
        // (+0x24, 06f78c72) and is inserted at the end (FUN_06f7ae80); the copy, then Entry, are
        // destroyed (FUN_06f1d620 x2).
        {
            FRandEncFormation Weighted(Entry);
            Weighted.Weight = 1.0f;
            Attack->Formations.push_back(Weighted);                         // FUN_06f7ae80
        }
    }

    // 06f78ce1-06f78ce6: slot 4 -- the TL override FUN_06f83af0.
    Attack->Spawn(false);
}

// FUN_06f78110 (06f78110-06f782d9, RET 0xc).
void FSystemInfo::AddTradeLaneShip(unsigned int const* RingId, int const* Direction,
                                   unsigned int const* ShipId)
{
    // 06f78114-06f7814d: lower_bound + key test (FUN_06fa7200, JC); retail reads the head
    // node's value for an unknown ring.
    FTradeLane* TheLane = TradeLaneByRing.find(*RingId)->second;
    if (*Direction == 1)
        TheLane->ForwardShips.push_back(*ShipId);           // FUN_06f937c0 insert(end, 1, id)
    else
        TheLane->BackwardShips.push_back(*ShipId);          // the same insert inlined
}

// FUN_06f78dd0 (06f78dd0-06f78ed5, RET 8).
void FSystemInfo::RemoveTradeLaneShip(unsigned int const* ShipId, unsigned int const* RingId)
{
    // 06f78dd8-06f78df9: the ring's lane (map::find FUN_06f7b0f0, not tested), else the first.
    FTradeLane* TheLane = *RingId != 0 ? TradeLaneByRing.find(*RingId)->second
                                       : TradeLanes.data();
    // EDI: set by the first removal and never reset, so later lanes skip BackwardShips.
    bool bRemoved = false;
    while (TheLane != TradeLanes.data() + TradeLanes.size())
    {
        // 06f78e14-06f78e5c: the first match in ForwardShips.
        std::vector<unsigned int>::iterator Found =
            std::find(TheLane->ForwardShips.begin(), TheLane->ForwardShips.end(), *ShipId);
        if (Found != TheLane->ForwardShips.end())
        {
            TheLane->ForwardShips.erase(Found);
            TheLane->LastForwardLeaveTime = static_cast<float>(GetContentGameTime());
            bRemoved = true;
        }
        // 06f78e5d-06f78ea8.
        if (!bRemoved)
        {
            Found = std::find(TheLane->BackwardShips.begin(), TheLane->BackwardShips.end(), *ShipId);
            if (Found != TheLane->BackwardShips.end())
            {
                TheLane->BackwardShips.erase(Found);
                TheLane->LastBackwardLeaveTime = static_cast<float>(GetContentGameTime());
                bRemoved = true;
            }
        }
        // 06f78ead-06f78eca: one lane for a given ring, else the next.
        if (*RingId != 0)
            break;
        ++TheLane;
    }
}

// FUN_06f78ee0 (06f78ee0-06f78ee6).
std::vector<FContentPlayerState*>* FSystemInfo::GetPlayers()
{
    return &Players;
}

// FUN_06f767c0 (06f767c0-06f767c6).
std::vector<FTradeLane>* FSystemInfo::GetTradeLanes()
{
    return &TradeLanes;
}

// FUN_06f767d0 (06f767d0-06f7695f, RET 0xc).
void FSystemInfo::FindTradeLanesNear(Vector const* Position, float Radius,
                                     std::vector<FTradeLane*>* Out)
{
    // 06f767da-06f7680c: erase(begin, end).
    Out->clear();

    // 06f76802-06f76820: FCOMP DAT_06fb5e84 (-1.0f), TEST AH,0x44, JP -> only the equal
    // case (C3 alone) falls through to "every lane".
    if (Radius == -1.0f)
    {
        for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
             Lane != TradeLanes.end(); ++Lane)
        {
            FTradeLane* LanePtr = &*Lane;
            std::vector<FTradeLane*>::iterator Found = Out->begin();
            while (Found != Out->end() && *Found != LanePtr)
                ++Found;
            if (Found == Out->end())
                Out->push_back(LanePtr);          // FUN_06eb5770 insert(end) + FUN_06eb5f30
        }
        return;
    }

    // 06f768b2-06f768c0: the squared radius is stored as a float.
    float const RadiusSquared = Radius * Radius;
    for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
         Lane != TradeLanes.end(); ++Lane)
    {
        Vector Closest;
        double const Distance = DistanceSquaredToSegment(Position, &Lane->StartPosition,
                                                         &Lane->EndPosition, &Closest);
        // 06f768e3-06f768ef: FCOMP RadiusSquared, TEST AH,5, JP skip -> taken when C0
        // alone (Distance < RadiusSquared).
        if (!(Distance < static_cast<double>(RadiusSquared)))
            continue;
        FTradeLane* LanePtr = &*Lane;
        std::vector<FTradeLane*>::iterator Found = Out->begin();
        while (Found != Out->end() && *Found != LanePtr)
            ++Found;
        if (Found == Out->end())
            Out->insert(Out->end(), 1, LanePtr);  // FUN_06f937c0; the re-find is discarded
    }
}

// FUN_06f792c0 (RET 4): both ring lists non-empty and their first rings have the same id.
static bool SameFirstRing(std::vector<FTradeLaneRing> const& List, FTradeLane const& Lane)
{
    if (List.empty() || Lane.Rings.empty())
        return false;
    return List.front().ObjectId == Lane.Rings.front().ObjectId;
}

// FUN_06f76970 (06f76970-06f76b08, RET 0xc).
void FSystemInfo::FindTradeLaneRingListsNear(Vector const* Position, float Radius,
                                             std::vector<std::vector<FTradeLaneRing>>* Out)
{
    // 06f76976-06f769b4: FUN_06f6a410 + element dtors FUN_06f81940 = erase(begin, end).
    Out->clear();

    if (Radius == -1.0f)                          // 06f769b0-06f769d2, as in FUN_06f767d0
    {
        for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
             Lane != TradeLanes.end(); ++Lane)
        {
            std::vector<std::vector<FTradeLaneRing>>::iterator Found = Out->begin();
            while (Found != Out->end() && !SameFirstRing(*Found, *Lane))
                ++Found;
            if (Found == Out->end())
                Out->push_back(Lane->Rings);      // FUN_06f7c830 insert(end) + FUN_06f7b160
        }
        return;
    }

    float const RadiusSquared = Radius * Radius;  // 06f76a5e-06f76a68
    for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
         Lane != TradeLanes.end(); ++Lane)
    {
        Vector Closest;
        double const Distance = DistanceSquaredToSegment(Position, &Lane->StartPosition,
                                                         &Lane->EndPosition, &Closest);
        if (!(Distance < static_cast<double>(RadiusSquared)))   // TEST AH,5 / JP
            continue;
        std::vector<std::vector<FTradeLaneRing>>::iterator Found = Out->begin();
        while (Found != Out->end() && !SameFirstRing(*Found, *Lane))
            ++Found;
        if (Found == Out->end())
            Out->insert(Out->end(), 1, Lane->Rings);   // FUN_06f7cd60; re-find discarded
    }
}

// FUN_06f76b10 (06f76b10-06f77086, RET 4).
int FSystemInfo::BuildTradeLanes(std::map<unsigned int, FTradeLaneRingInfo>* Rings)
{
    // 06f76b2b-06f76b44: erase(begin, end) on +0xe8.
    TradeLanes.clear();

    unsigned int const RingIdMask = 0x3fffffff;
    // 06f76b5c-06f76b61 tests the size once; 06f77042 tests it again after every lane,
    // and 06f76faf-06f76fb4 restarts from begin() each time.
    while (!Rings->empty())
    {
        std::map<unsigned int, FTradeLaneRingInfo>::iterator It = Rings->begin();
        FTradeLane Lane;

        // 06f76bf7-06f76c98: the first ring goes at the end of the empty list.
        Lane.Rings.insert(Lane.Rings.end(), MakeTradeLaneRing(It->second));   // FUN_06f792f0
        Lane.Placement.Rotation = It->second.Placement.Rotation;             // REP MOVSD x9
        if ((It->second.PrevRing & RingIdMask) == 0)
            Lane.StartPosition = It->second.Position;
        if ((It->second.NextRing & RingIdMask) == 0)
            Lane.EndPosition = It->second.Position;

        unsigned int const FirstId = It->second.ObjectId;                   // ESP+0x38
        unsigned int PrevKey = It->second.PrevRing;                         // ESP+0x20
        unsigned int NextKey = It->second.NextRing;                         // ESP+0x1c
        Rings->erase(It);                                                   // FUN_06f205e0

        // 06f76d1f-06f76e40: walk prev_ring; each ring goes in front of the one reached
        // before it (the end when that one is not in the list).
        unsigned int LastId = FirstId;
        It = Rings->find(PrevKey);                                          // FUN_06f7b0f0
        while (It != Rings->end())
        {
            std::vector<FTradeLaneRing>::iterator Before = Lane.Rings.begin();
            while (Before != Lane.Rings.end() && Before->ObjectId != LastId)
                ++Before;
            Lane.Rings.insert(Before, 1, MakeTradeLaneRing(It->second));   // FUN_06f7a050
            if ((It->second.PrevRing & RingIdMask) == 0)
                Lane.StartPosition = It->second.Position;
            LastId = PrevKey;
            PrevKey = It->second.PrevRing;
            Rings->erase(It);
            It = Rings->find(PrevKey);
        }

        // 06f76e4c-06f76f93: walk next_ring from the first ring; each ring goes right after
        // the one reached before it. That one is always in the list (it was inserted in
        // this pass), so retail's "end + 0x44" for a miss is never taken.
        LastId = FirstId;
        It = Rings->find(NextKey);
        while (It != Rings->end())
        {
            std::vector<FTradeLaneRing>::iterator After = Lane.Rings.begin();
            while (After != Lane.Rings.end() && After->ObjectId != LastId)
                ++After;
            Lane.Rings.insert(After + 1, 1, MakeTradeLaneRing(It->second)); // FUN_06f7a050
            if ((It->second.NextRing & RingIdMask) == 0)
                Lane.EndPosition = It->second.Position;
            LastId = NextKey;
            NextKey = It->second.NextRing;
            Rings->erase(It);
            It = Rings->find(NextKey);
        }

        TradeLanes.insert(TradeLanes.end(), 1, Lane);                       // FUN_06f7b4d0
    }

    // 06f77051-06f7706e: size of +0xe8.
    return static_cast<int>(TradeLanes.size());
}

// TU53 FUN_06f611a0 (06f611a0-06f61347, cdecl).
double __cdecl DistanceSquaredToSegment(Vector const* Point, Vector const* Start,
                                        Vector const* End, Vector* OutClosest)
{
    // 06f611a9-06f611fb: both differences go through FUN_06eb7810 (float stores).
    float const ToPointX = Point->x - Start->x;
    float const ToPointY = Point->y - Start->y;
    float const ToPointZ = Point->z - Start->z;
    float const AlongX = End->x - Start->x;
    float const AlongY = End->y - Start->y;
    float const AlongZ = End->z - Start->z;

    // 06f61200-06f6123a: z, y, x order; the quotient stays on the x87 stack for the first
    // compare and is stored as a float for the rest.
    double const Dot = static_cast<double>(AlongZ) * ToPointZ
                     + static_cast<double>(AlongY) * ToPointY
                     + static_cast<double>(AlongX) * ToPointX;
    double const LengthSquared = static_cast<double>(AlongZ) * AlongZ
                               + static_cast<double>(AlongY) * AlongY
                               + static_cast<double>(AlongX) * AlongX;
    double const Fraction = Dot / LengthSquared;
    float const StoredFraction = static_cast<float>(Fraction);

    // 06f6123e-06f61249: FCOMP 1.0, TEST AH,0x41, JNZ -> fall through only when greater.
    bool bInterpolated = false;
    if (Fraction > 1.0)
    {
        *OutClosest = *End;
    }
    // 06f612ab-06f612c0: FCOMP 0.0, TEST AH,5, JP -> fall through only when less.
    else if (StoredFraction < 0.0f)
    {
        *OutClosest = *Start;
    }
    else
    {
        // 06f612d1-06f61318: the z product is rounded to a float before the add.
        float const ScaledZ = static_cast<float>(static_cast<double>(AlongZ) * StoredFraction);
        OutClosest->x = static_cast<float>(static_cast<double>(AlongX) * StoredFraction + Start->x);
        OutClosest->y = static_cast<float>(static_cast<double>(AlongY) * StoredFraction + Start->y);
        OutClosest->z = ScaledZ + Start->z;
        bInterpolated = true;
    }

    if (!bInterpolated)
    {
        // 06f6125e-06f612a7: the offset goes through FUN_06eb7810 (float stores).
        float const OffsetX = Point->x - OutClosest->x;
        float const OffsetY = Point->y - OutClosest->y;
        float const OffsetZ = Point->z - OutClosest->z;
        return static_cast<double>(OffsetZ) * OffsetZ + static_cast<double>(OffsetY) * OffsetY
             + static_cast<double>(OffsetX) * OffsetX;
    }

    // 06f6131b-06f6133c: the offset stays on the x87 stack.
    double const OffsetX = static_cast<double>(Point->x) - OutClosest->x;
    double const OffsetY = static_cast<double>(Point->y) - OutClosest->y;
    double const OffsetZ = static_cast<double>(Point->z) - OutClosest->z;
    return OffsetZ * OffsetZ + OffsetY * OffsetY + OffsetX * OffsetX;
}

// The integer guess of the reciprocal square root TU53 FUN_06f672d0 inlines twice:
// 0x5f3759df - (bits >> 1), SAR on the float's bits.
static float ReciprocalSqrtGuess(float Value)
{
    int Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    int const GuessBits = 0x5f3759df - (Bits >> 1);
    float Guess;
    std::memcpy(&Guess, &GuessBits, sizeof(Guess));
    return Guess;
}

// TU53 FUN_06f672d0 (06f672d0-06f6743b, cdecl). Constants: DAT_06fb605c = 0.5f,
// DAT_06fb6058 = 1.5f, DAT_06fb6060 = 0.0174532924f (degrees to radians).
bool __cdecl IsPointInViewCone(Vector const* Point, FSpaceObjPlacement const* Viewer,
                               float const* MaxDistance, float const* ConeAngle)
{
    // 06f672db-06f67318: dy and dz are stored as floats, dx stays on the x87 stack; the sum
    // is dz*dz + dy*dy + dx*dx, kept as a float in two stack slots (bits and local_28).
    double const DeltaX = static_cast<double>(Point->x) - Viewer->Position.x;
    float const DeltaY = static_cast<float>(static_cast<double>(Point->y) - Viewer->Position.y);
    double const DeltaZWide = static_cast<double>(Point->z) - Viewer->Position.z;
    float const DeltaZ = static_cast<float>(DeltaZWide);
    double const LengthSq = DeltaZWide * DeltaZ + static_cast<double>(DeltaY) * DeltaY
                          + DeltaX * DeltaX;
    float const StoredLengthSq = static_cast<float>(LengthSq);

    // 06f6731c-06f6734e: |d| = y * (1.5 - y*y*L*0.5) * L; FCOMP MaxDistance, TEST AH,0x41,
    // JP -> greater or unordered: false.
    float const LengthGuess = ReciprocalSqrtGuess(StoredLengthSq);
    double const Length = (1.5 - static_cast<double>(LengthGuess) * LengthGuess * LengthSq * 0.5)
                        * LengthGuess * LengthSq;
    if (!(Length <= *MaxDistance))
        return false;

    // 06f67354-06f6738a: the third orientation column, negated (-m02 stored as a float);
    // its square as (z*z + y*y) + x*x.
    double const ForwardX = -Viewer->Orientation.E[0][2];
    double const ForwardY = -Viewer->Orientation.E[1][2];
    double const ForwardZ = -Viewer->Orientation.E[2][2];
    double const ForwardSq = ForwardZ * ForwardZ + ForwardY * ForwardY + ForwardX * ForwardX;
    float const ForwardGuess = ReciprocalSqrtGuess(static_cast<float>(ForwardSq));
    // 06f6738e: from here the length square is the float copy (local_28).
    double const LengthSqRounded = StoredLengthSq;
    float const LengthGuess2 = ReciprocalSqrtGuess(StoredLengthSq);

    // 06f673a0-06f673cb: (Fz*dz + Fy*dy) + Fx*dx.
    double const Dot = ForwardZ * DeltaZ + ForwardY * DeltaY + ForwardX * DeltaX;
    // 06f673cd-06f67407: both Newton steps, then |F| * |d| as
    // A*B * yL * L * yF * F; the cosine to the point is Dot over it.
    double const ForwardStep = 1.5 - static_cast<double>(ForwardGuess) * ForwardGuess * ForwardSq * 0.5;
    double const LengthStep = 1.5 - static_cast<double>(LengthGuess2) * LengthGuess2
                                    * LengthSqRounded * 0.5;
    double const Norms = ForwardStep * LengthStep * LengthGuess2 * LengthSqRounded
                       * ForwardGuess * ForwardSq;
    double const CosToPoint = Dot / Norms;

    // 06f67409-06f6742c: cos(angle * 0.5 * deg2rad); FCOMPP, TEST AH,0x41, JP -> the
    // half-angle cosine greater than the point's, or unordered: false.
    double const CosHalfAngle = std::cos(static_cast<double>(*ConeAngle) * 0.5
                                         * static_cast<double>(0.0174532924f));
    return CosHalfAngle <= CosToPoint;
}

// FUN_06f72930 (06f72930-06f7296e, RET 0x1c).
int FSystemInfo::SendComm(unsigned int SenderId, unsigned int TargetId, unsigned int TextId,
                          unsigned int VoiceId, unsigned int CommType,
                          Costume const* CommCostume, float Duration)
{
    int TextCount = 1;                                      // 06f7295e local = 1
    return SendSpaceComm(this, SenderId, TargetId, &TextId, &TextCount, VoiceId, CommType,
                         CommCostume, 0, Duration, true);
}

// TU53 FUN_06f60dd0 (06f60dd0-06f60e0d, cdecl): argument 1 is never read.
int __cdecl SendSpaceComm(void const* Unused, unsigned int SenderId, unsigned int TargetId,
                          unsigned int* TextIds, int const* TextCount, unsigned int VoiceId,
                          unsigned int CommType, Costume const* CommCostume, unsigned int Mood,
                          float Duration, bool bInterrupt)
{
    (void)Unused;
    return pub::SpaceObj::SendComm(SenderId, TargetId, VoiceId, CommCostume, CommType,
                                   TextIds, *TextCount, Mood, Duration, bInterrupt);
}

// FUN_06f75f20 (06f75f20-06f75f25).
std::map<unsigned int, FSystemInfo>* __cdecl GetSystemInfos()
{
    return &GSystemInfos;
}

// FUN_06f75f30 (06f75f30-06f75f73): &node value, or null on a miss.
FSystemInfo* __cdecl FindSystemInfo(unsigned int const* SystemId)
{
    std::map<unsigned int, FSystemInfo>::iterator It = GSystemInfos.find(*SystemId);
    if (It == GSystemInfos.end())
        return nullptr;
    return &It->second;
}

// FUN_06f75f80 (06f75f80-06f7613c): one SystemInfo per universe system; an id already in
// the table keeps its entry (unique insert).
void __cdecl BuildSystemInfos()
{
    unsigned int SystemId = 0;
    if (!pub::NextSystemID(SystemId))
        return;
    do
    {
        FSystemInfo Info(&SystemId);                                      // FUN_06f721c0
        std::pair<unsigned int const, FSystemInfo> Entry(SystemId, Info); // FUN_06f79d30
        GSystemInfos.insert(Entry);
    } while (pub::NextSystemID(SystemId));
}

// FUN_06f76140 (06f76140-06f761f8): erase(begin, end), the map object stays.
void __cdecl ClearSystemInfos()
{
    GSystemInfos.clear();
}

// FUN_06f76200 (06f76200-06f76342).
void __cdecl LoadDynSysInfo(INI_Reader* Reader)
{
    GSystemInfos.clear();
    BuildSystemInfos();
    while (Reader->read_header())
    {
        if (!Reader->is_header("DynSysInfo"))
            continue;
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                // 06f76307-06f7632b: the find result (FUN_06f7b0f0) is discarded.
                unsigned int SystemId;
                pub::GetSystemID(SystemId, Reader->get_value_string());
                std::map<unsigned int, FSystemInfo>::iterator Found =
                    GSystemInfos.find(SystemId);
                (void)Found;
                break;
            }
        }
    }
}

// FUN_06f76350 (06f76350-06f76378): walks the table and does nothing with it.
void __cdecl VisitSystemInfos()
{
    for (std::map<unsigned int, FSystemInfo>::iterator It = GSystemInfos.begin();
         It != GSystemInfos.end(); ++It)
    {
    }
}

// FUN_06f73ac0 (06f73ac0-06f73bac, RET 8; ported by CNT-AI5).
//   Length 0 -> 0 (06f73ad5).
//   "Player" (REPE CMPSB over 7 bytes incl. the NUL, 06f73aef): FUN_06f49b50(&ControllerId);
//     none -> in single player with a non-empty player map (DAT_06fd0bcc) the first state
//     (begin node +0x10); a null state or a state without ship (+0x10) -> 0; else ship +4.
//   Otherwise the SpaceObjs walk: FUN_06ed01c0(Name, obj +0xd8) = TString == chars; the
//     first match with a non-null object returns its +4 (a null object falls through).
//   Fallback: pub::GetNicknameId(Name chars), kept when FUN_06f683b0(id, this) is true.
unsigned int FSystemInfo::FindObjectIdByName(TString<0x30> const* Name, unsigned int ControllerId)
{
    if (Name->Length == 0)
        return 0;

    if (strcmp(Name->Data, "Player") == 0)
    {
        FContentPlayerState* State = FindContentPlayerStateByLiveMission(&ControllerId);
        if (State == nullptr)
        {
            if (!pub::SinglePlayer())
                return 0;
            State = GetFirstContentPlayerState();
            if (State == nullptr)
                return 0;
        }
        FContentPlayerShip* Ship = State->ShipObject;
        if (Ship == nullptr)
            return 0;
        return Ship->SpaceObjId;
    }

    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if (*Name == (*It)->Nickname.Data && *It != nullptr)
            return (*It)->SpaceObjId;
    }

    unsigned int Id = pub::GetNicknameId(Name->Data);
    if (!IsSpaceObjTypeSet(Id, this))
        Id = 0;
    return Id;
}

// ---------------------------------------------------------------------------------------
// Spawn placement helpers (CNT-GS9; callers in TU53 SpawnPlacement.cpp)
// ---------------------------------------------------------------------------------------

namespace
{
    // .rdata 06fbbedc: the ship id CanUseDockTarget gets for a ship not created yet (0).
    unsigned int const GNoDockingShipId = 0;
}

// FUN_06f72380 (06f72380-06f724e2, RET 0x20).
int FSystemInfo::CollectDockTargets(std::vector<unsigned int> const* ClassIds, int const* ShipRep,
                                    unsigned int const* Group, unsigned int const* ExcludeId,
                                    Vector const* Position, float Range,
                                    unsigned int const* TypeMask, std::vector<unsigned int>* Out)
{
    unsigned int Ids[0x20];
    unsigned int Count;
    pub::System::ScanObjects(SystemId, Ids, 0x20, *Position, Range, *TypeMask, Count);   // 06f723b1
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        unsigned int const* Id = &Ids[Index];
        if (*Id == *ExcludeId)                                                      // 06f723eb
            continue;
        if (IsContentNpcGateLocked(Id))                                             // FUN_06f6d280
            continue;
        unsigned int Type;
        pub::SpaceObj::GetType(*Id, Type);                                          // 06f7240c
        if (!CanUseDockTarget(ClassIds, ShipRep, Group, &GNoDockingShipId, Id))     // FUN_06f6cb90
            continue;
        if ((Type & 0xc40) == 0)
        {
            // 06f7243f-06f72455: the ship's reputation, or the group when it has none.
            unsigned int const OwnerGroup = *ShipRep != -1 ? static_cast<unsigned int>(*ShipRep)
                                                           : *Group;
            if (!IsGroupAtDockBase(&OwnerGroup, Id))                                // FUN_06f6cb20
                continue;
            // 06f7246b: the type is tested again (always clear here).
            if ((Type & 0xc40) == 0)
            {
                unsigned int DockingTarget;
                pub::SpaceObj::GetDockingTarget(*Id, DockingTarget);                // 06f7247b
                if (DockingTarget == 0)
                    continue;
            }
        }
        Out->push_back(*Id);                                                        // FUN_06f937c0
    }
    return static_cast<int>(Out->size());
}

// FUN_06f724f0 (06f724f0-06f725bb, RET 0x1c).
unsigned int FSystemInfo::PickDockTarget(std::vector<unsigned int> const* ClassIds,
                                         int const* ShipRep, unsigned int const* Group,
                                         unsigned int const* ExcludeId, Vector const* Position,
                                         float Range, unsigned int const* TypeMask)
{
    std::vector<unsigned int> Targets;
    CollectDockTargets(ClassIds, ShipRep, Group, ExcludeId, Position, Range, TypeMask, &Targets);
    if (Targets.empty())
        return 0;
    // 06f72584-06f7259d: (rand() * count) / 32768 (CDQ / AND 0x7fff / SAR 0xf).
    return Targets[(rand() * static_cast<int>(Targets.size())) / 32768];
}

// FUN_06f725c0 (06f725c0-06f72929, RET 0x24).
unsigned int FSystemInfo::PickPathDockTarget(std::vector<unsigned int> const* ClassIds,
                                             int const* ShipRep, unsigned int const* Group,
                                             unsigned int const* ExcludeId, FPatrolPath* Path,
                                             int First, int Last, float MinDistance,
                                             unsigned int const* TypeMask)
{
    std::vector<FPatrolPathObject> Objects;
    Path->FindObjectsInRange(&Objects, First, Last, *TypeMask);                    // FUN_06f37aa0
    std::vector<unsigned int> Candidates;
    Candidates.reserve(Objects.size());                                           // 06f72661-06f726d3
    for (std::vector<FPatrolPathObject>::const_iterator Object = Objects.begin();
         Object != Objects.end(); ++Object)
    {
        unsigned int const Id = Object->ObjectId;
        if (Id == *ExcludeId)
            continue;
        // 06f7270f-06f7277b: the squared distance from *ExcludeId (0.0 without one); both
        // locations are fetched for every object.
        double DistanceSq = 0.0;                                                    // DAT_06fb444c
        if (*ExcludeId != 0)
        {
            Vector ExcludePosition;
            Vector ObjectPosition;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(*ExcludeId, ExcludePosition, Orientation);
            pub::SpaceObj::GetLocation(Id, ObjectPosition, Orientation);
            Vector const Delta = SubtractVectors(ObjectPosition, ExcludePosition);  // 06f7274b
            DistanceSq = (static_cast<double>(Delta.z) * Delta.z +
                          static_cast<double>(Delta.y) * Delta.y) +
                         static_cast<double>(Delta.x) * Delta.x;
        }
        // TEST AH,1 / JNZ: C0 (less or unordered) skips.
        if (!(DistanceSq >= static_cast<double>(MinDistance)))
            continue;
        if (IsContentNpcGateLocked(&Id))                                            // FUN_06f6d280
            continue;
        unsigned int Type;
        pub::SpaceObj::GetType(Id, Type);                                           // 06f727a9
        if (!CanUseDockTarget(ClassIds, ShipRep, Group, &GNoDockingShipId, &Id))    // FUN_06f6cb90
            continue;
        if ((Type & 0xc40) == 0)
        {
            unsigned int const OwnerGroup = *ShipRep != -1 ? static_cast<unsigned int>(*ShipRep)
                                                           : *Group;
            if (!IsGroupAtDockBase(&OwnerGroup, &Id))                               // FUN_06f6cb20
                continue;
        }
        Candidates.push_back(Id);                                                   // FUN_06eb5770
    }
    if (Candidates.empty())
        return 0;
    return Candidates[(rand() * static_cast<int>(Candidates.size())) / 32768];     // 06f728ba
}

// FUN_06f77b40 (06f77b40-06f77bcb, RET 8).
int FSystemInfo::GetTradeLaneRingOrder(unsigned int const* FirstRing,
                                       unsigned int const* SecondRing)
{
    if (*FirstRing == 0 || *SecondRing == 0)
        return 0;
    // 06f77b57-06f77b79: map::find (FUN_06f7b0f0); end gives a null lane, not tested.
    std::map<unsigned int, FTradeLane*>::iterator Found = TradeLaneByRing.find(*FirstRing);
    FTradeLane* TheLane = Found != TradeLaneByRing.end() ? Found->second : nullptr;
    std::vector<FTradeLaneRing> const& Rings = TheLane->Rings;
    size_t FirstIndex = 0;
    while (FirstIndex < Rings.size() && Rings[FirstIndex].ObjectId != *FirstRing)
        ++FirstIndex;
    size_t SecondIndex = 0;
    while (SecondIndex < Rings.size() && Rings[SecondIndex].ObjectId != *SecondRing)
        ++SecondIndex;
    // 06f77bb0-06f77bbd: SETE on "second == first + 1", then 2 * flag - 1.
    return SecondIndex == FirstIndex + 1 ? 1 : -1;
}

// FUN_06f77e70 (06f77e70-06f78062, RET 0x10).
bool FSystemInfo::GetLaneShipDistance(int const* Direction, unsigned int const* RingId,
                                      Vector const* Position, float* OutDistance)
{
    // 06f77e7a-06f77e86: map::find (FUN_06f7b0f0), not tested.
    FTradeLane* TheLane = TradeLaneByRing.find(*RingId)->second;
    std::vector<unsigned int> const& Ships =
        *Direction == 1 ? TheLane->ForwardShips : TheLane->BackwardShips;
    if (Ships.empty())
        return false;

    float Nearest = -1.0f;                                                          // the arg slot
    Vector NearestPosition;
    for (std::vector<unsigned int>::const_iterator Ship = Ships.begin(); Ship != Ships.end(); ++Ship)
    {
        Vector ShipPosition;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(*Ship, ShipPosition, Orientation);              // 06f77eff
        float const Dx = Position->x - ShipPosition.x;                              // FUN_06eb7810
        float const Dy = Position->y - ShipPosition.y;
        float const Dz = Position->z - ShipPosition.z;
        double const DistanceSq = (static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy) +
                                  static_cast<double>(Dx) * Dx;
        // 06f77f45-06f77f5f: the first ship (Nearest == -1, TEST AH,0x44 / JNP), or a strictly
        // nearer one (FCOM, TEST AH,5 / JP skips when C0 is clear).
        if (Nearest == -1.0f || DistanceSq < static_cast<double>(Nearest))
        {
            Nearest = static_cast<float>(DistanceSq);
            NearestPosition = ShipPosition;
        }
    }

    float const Distance = static_cast<float>(std::sqrt(static_cast<double>(Nearest)));
    *OutDistance = Distance;
    float const Ax = Position->x - NearestPosition.x;                               // FUN_06eb7810
    float const Ay = Position->y - NearestPosition.y;
    float const Az = Position->z - NearestPosition.z;
    // The lane's z axis: the third column of its placement.
    double const Zx = TheLane->Placement.Rotation.D[0][2];
    double const Zy = TheLane->Placement.Rotation.D[1][2];
    double const Zz = TheLane->Placement.Rotation.D[2][2];
    if (*Direction == 1)
    {
        // 06f77ff2-06f78013: TEST AH,5 / JP -- negative only when the dot product is below 0.
        double const Dot = (Az * Zz + Ay * Zy) + Ax * Zx;
        if (Dot < 0.0)
            *OutDistance = -Distance;
    }
    else
    {
        // 06f78028-06f78045: TEST AH,0x41 / JNZ -- negative only when it is above 0.
        double const Dot = (Az * Zz + Zy * Ay) + Zx * Ax;
        if (Dot > 0.0)
            *OutDistance = -Distance;
    }
    return true;
}

// FUN_06f78070 (06f78070-06f78101, RET 8).
double FSystemInfo::GetTimeSinceLaneLeave(int const* Direction, unsigned int const* RingId)
{
    // 06f78077-06f780a9: lower_bound + key test (FUN_06fa7200), not tested.
    FTradeLane* TheLane = TradeLaneByRing.find(*RingId)->second;
    float const LeaveTime =
        *Direction == 1 ? TheLane->LastForwardLeaveTime : TheLane->LastBackwardLeaveTime;
    // TEST AH,0x44 / JNP: exactly -1 -> -1.
    if (LeaveTime == -1.0f)
        return -1.0f;                                                               // DAT_06fb5e84
    return GetContentGameTime() - LeaveTime;                                        // FUN_06efec10
}
