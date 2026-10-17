// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_character_object.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "cargo_bar_control.h" // ToolTipHoverInit / ToolTipHoverSetCaption
#include "client_cliobj.h"     // PushMaterialPathPair, ResolveMaterialEntry, ReleaseMaterialEntrySlot
#include "game_options.h"      // GetGameOption (FUN_005792b0)
#include "gf_script_object.h"  // G_GfScriptManager (DAT_0066d2d0), FGfScriptSequence
#include "globals.h"           // GetClientFrameTimeCapped (FUN_0042d680), G_RenderSurfaceWidth
#include "character_select_equip.h" // GetPlayerVisitFlagsRetail / SetPlayerVisitFlagsRetail
#include "input_manager.h"     // MouseSetCursor (FUN_0041dde0)
#include "menu_universe_base.h" // GetClientLocationRoomScriptBehaviors (FUN_0043ad50)
#include "mirror_property_bucket.h"
#include "misc_util.h"         // MiscUtilLoadEngineInstance, MiscUtilLoadIdsCopy, MiscUtilSphereInView
#include "nav_bar.h"           // NavBarCharacterClicked (FUN_00442810), NavBarOpenButton (FUN_00442200)
#include "nn_tooltip.h"        // NnToolTipHideGlobal (FUN_004be390)
#include "player_spawn.h"      // GetPlayerConnectionId (DAT_00673344)
#include "server_connect.h"    // GetLocalServerInterface (DAT_0067ecd0)
#include "space_camera.h"      // FSpaceCamera (DAT_0067dbf8)
#include "tooltip_hover.h"     // FToolTipHoverInfo / NnToolTipFeedHover (FUN_004be370)
#include "widescreen.h"      // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)
#include "win3d.h"

#include "DALib.h"
#include "common_cobject.h"    // CompoundInstanceFindFirstName
#include "common_iface.h"      // ScriptBehavior
#include "common_ini.h"        // StringAlloc, RefreshLoadingProgress
#include "common_misc.h"       // malloc_strcpy / malloc_strfree / malloc_wstrcpy / get_projected_bounding_sphere
#include "common_newchar.h"    // RtcSlider
#include "common_reputation.h" // CostumeDescriptions, DetailSwitchTable
#include "common_server.h"     // IServer
#include "common_universe.h"   // Universe::get_base (FUN_00445720)
#include "dacom.h"             // FDUMP, DACOM_CRC
#include "engine/engbase.h"
#include "x86math.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
    char const kObjectDeformableCpp[] = "E:\\FL\\Scratch\\Source\\Client\\gf\\ObjectDeformable.cpp";

    // DAT_00668848 (0x00668848..0x00668947, bss) -- FUN_00445720's result buffer; every exit
    // returns its address. That function is its only reader and writer.
    char G_BgcsContextBuffer[256];

    // DAT_00668948 (0x124 bytes) -- the body every "pc" character shares. The static
    // initializer 0x00443e40 zeroes the instance and both four-entry arrays and registers an
    // empty atexit (DAT_00443e80 is a bare RET); zero initialization here is that.
    FCharacterLodSet G_SharedCharacterLod;

    void* const kNoInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));

    // FUN_00443b60 (thiscall on Set, RET 4) -- build Set's deformable from the four bodyparts
    // (0 body, 1 head, 2 left hand, 3 right hand) and read back its LOD switch table.
    void BuildCharacterDeformable(FCharacterLodSet* Set, bodypart const* const* BodyParts)
    {
        // 0x00443b6e..0x00443b9d: "low_character_lod" sets Flags bit 1 (Deformable2 skips LOD 0)
        // and raises the LOD limit of the non-first parts from 2 to 3.
        unsigned long PartFlags = 0;
        unsigned long ChildLodLimit = 2;
        if (RtcSlider::is_enabled("low_character_lod"))
        {
            PartFlags = 2;
            ChildLodLimit = 3;
        }

        // 0x00443b9f..0x00443bc1: the four entries {0, 0, 0, 0, 0, -1}.
        FDeformPartDesc Entries[4];
        int Index = 0;
        for (Index = 0; Index < 4; ++Index)
        {
            Entries[Index].LodCount = 0;
            Entries[Index].Lods = nullptr;
            Entries[Index].Field08 = 0;
            Entries[Index].Path = nullptr;
            Entries[Index].Flags = 0;
            Entries[Index].LodLimit = 0xFFFFFFFFu;
        }

        // 0x00443bc3..0x00443c79: one entry per bodypart with a mesh: its full file name, the
        // sixteen LOD slots with FileSystem and Name zeroed (Deformable2 then opens the part by
        // Path), LodCount 16.
        FDeformLodDesc LodSlots[4][16];
        char Paths[4][0x400];
        int Count = 0;
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
                continue;
            strcpy(Paths[Count], GetFullFilename(Part->Mesh.c_str()));
            int Lod = 0;
            for (Lod = 0; Lod < 16; ++Lod)
            {
                LodSlots[Count][Lod].FileSystem = nullptr;
                LodSlots[Count][Lod].Name = nullptr;
            }
            Entries[Count].LodCount = 16;
            Entries[Count].Lods = LodSlots[Count];
            Entries[Count].Field08 = 0;
            Entries[Count].Path = Paths[Count];
            Entries[Count].Flags = PartFlags;
            Entries[Count].LodLimit = (Index == 0) ? 0xFFFFFFFFu : ChildLodLimit;
            ++Count;
        }

        // 0x00443c7f..0x00443c95: the table header {Count, Entries}.
        struct FDeformPartTable
        {
            int Count;
            FDeformPartDesc* Entries;
        };
        FDeformPartTable Table;
        Table.Count = Count;
        Table.Entries = Entries;
        RefreshLoadingProgress();

        // 0x00443c97..0x00443ce5: a material-library reference per bodypart mesh
        // (FUN_0041aa50(mesh, 1, -1)) while the body loads.
        Archetype::FCliMaterialEntry* Materials[4];
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
                continue;
            Materials[Index] = Archetype::ResolveMaterialEntry(Part->Mesh.c_str(), true,
                                                               0xffffffffu);
        }
        RefreshLoadingProgress();

        // 0x00443ce9..0x00443d04: IDeformable slot 3 into Set +0x00.
        DALib::Deform->Create(&Set->Instance, reinterpret_cast<int*>(&Table), nullptr, nullptr,
                              nullptr);
        RefreshLoadingProgress();

        // 0x00443d09..0x00443d2d: the references released (FUN_0041b030).
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
                continue;
            Archetype::ReleaseMaterialEntrySlot(&Materials[Index]);
        }
        RefreshLoadingProgress();

        // 0x00443d31..0x00443e25: per bodypart, the LOD count the part came back with and each
        // LOD's switch distance (DetailSwitchTable::get_switch_distance(fraction * 100.0)); the
        // last one is the table's distance at 0. A part without a mesh gets 0 and no table.
        int EntryIndex = 0;
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
            {
                Set->Counts[Index] = 0;
                Set->Tables[Index] = nullptr;
                continue;
            }
            int const LodCount = Entries[EntryIndex].LodCount;
            Set->Counts[Index] = LodCount;
            int Lod = 0;
            for (Lod = 0; Lod < LodCount; ++Lod)
            {
                DetailSwitchTable* const Switch = Part->Switch;
                float const Fraction = static_cast<float>(
                    static_cast<double>(Entries[EntryIndex].Lods[Lod].Fraction) * 100.0);
                Set->Switch[Index][Lod] = Switch->get_switch_distance(Fraction);
                Set->Tables[Index] = Switch;
            }
            if (Set->Counts[Index] > 0)
                Set->Switch[Index][Set->Counts[Index] - 1] = Part->Switch->get_switch_distance(0.0f);
            ++EntryIndex;
        }
    }

    // The dealer screen a role opens (0x00445ce1..0x00445d1e and LAB_00445d30): 2 -> 5,
    // 3 -> 7, 4 -> 8 (NavBarOpenButton, FUN_00442200); the bartender (1) and no role open
    // nothing.
    void OpenRoleDealer(int Role)
    {
        if (Role == 1)
            return;
        if (Role == 2)
            NavBarOpenButton(5);
        else if (Role == 3)
            NavBarOpenButton(7);
        else if (Role == 4)
            NavBarOpenButton(8);
    }

    // LAB_00445d30 (cdecl) -- the stop callback of a click's behaviour script: A is the
    // character (the sequence's +0x1c).
    void __cdecl OnBehaviorScriptStopped(void* A, void* /*B*/, unsigned char /*bArg*/)
    {
        OpenRoleDealer(static_cast<FCharacterObject*>(A)->Role);
    }

    // FUN_00437020 (thiscall on the bucket, RET 4) -- the first node whose behaviour has Key,
    // or null.
    MetaNode<ScriptBehavior>* FindBehaviorNode(TMirrorPropertyBucket<ScriptBehavior>* Bucket,
                                                unsigned Key)
    {
        MetaNode<ScriptBehavior>* Cursor = nullptr;
        while (Bucket->List.traverse(Cursor, true))
        {
            if (static_cast<unsigned>(Cursor->Data->BehaviorId) == Key)
                return Cursor;
        }
        return nullptr;
    }
}

// ============================================================================================
// FCharacterDesc
// ============================================================================================

FCharacterDesc::FCharacterDesc()
    : Placement(nullptr)
    , PlacementIndex(-1)
    , bScriptSpatial(0)
{
    // FUN_00443a90: CharacterProperties constructed (the member), +0x64 = 0, +0x6c = 0,
    // +0x68 = -1, +0x70..+0x78 = 0, +0x7c / +0x8c / +0x9c = 1.0 and the rest of the matrix 0.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FCharacterDesc::~FCharacterDesc()
{
    // FUN_00443b10: CharacterProperties::destroy, then ~CharacterProperties (the member).
    Props.destroy();
}

// ============================================================================================
// FCharacterHotspot
// ============================================================================================

FCharacterHotspot::FCharacterHotspot()
    : Character(nullptr)
    , bOffscreen(0)
{
}

FCharacterHotspot::~FCharacterHotspot()
{
    // FUN_00444960: vptr, then JMP FUN_00555ea0 (~FHotspot).
}

void FCharacterHotspot::UpdateScreenRect(ICamera* Camera)
{
    // FUN_00555f60. 0x00555f66..0x00555f96: the owner's engine instance position.
    float const* const Where = DALib::Engine->GetInstancePosition(Character->GetEngineInstance());
    Vector Center;
    Center.x = Where[0];
    Center.y = Where[1];
    Center.z = Where[2];

    // 0x00555f97..0x00555fc3: get_projected_bounding_sphere(Camera, Center, 1.0, x, y, r, z).
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    float Radius = 0.0f;
    float Depth = 0.0f;
    if (get_projected_bounding_sphere(Camera, Center, 1.0f, ScreenX, ScreenY, Radius, Depth) == 0)
    {
        // 0x0055606b..0x00556083.
        bOffscreen = 1;
        ScreenRect.Left = 0;
        ScreenRect.Top = 0;
        ScreenRect.Width = 0;
        ScreenRect.Height = 0;
        return;
    }

    // 0x00555fc9..0x0055605b: floor(v + 0.5) of x - 0.6r (the 0.6f constant widened, 0x005e1220),
    // y - r, r * 1.2f (0x005c9800) and r + r.
    bOffscreen = 0;
    int const Left = static_cast<int>(floor(static_cast<double>(ScreenX) -
                                            static_cast<double>(Radius) *
                                                static_cast<double>(0.6f) +
                                            0.5));
    int const Top = static_cast<int>(
        floor(static_cast<double>(ScreenY) - static_cast<double>(Radius) + 0.5));
    int const Width = static_cast<int>(
        floor(static_cast<double>(Radius) * static_cast<double>(1.2f) + 0.5));
    int const Height = static_cast<int>(
        floor(static_cast<double>(Radius) + static_cast<double>(Radius) + 0.5));
    ScreenRect.Left = Left;
    ScreenRect.Top = Top;
    ScreenRect.Width = Width;
    ScreenRect.Height = Height;
}

// ============================================================================================
// FCharacterObject
// ============================================================================================

FCharacterObject::FPartCacheEntry::FPartCacheEntry()
    : Name(nullptr)
    , Part(-1)
    , bUsed(0)
{
    // LAB_00444060.
}

FCharacterObject::FPartCacheEntry::~FPartCacheEntry()
{
    // FUN_00444080: +0x04 = -1, the name freed (not cleared), +0x08 = 0.
    Part = -1;
    if (Name != nullptr)
        free(Name);
    bUsed = 0;
}

FCharacterObject::FCharacterObject(FRoomRuntime* InOwner, FCharacterDesc const& Desc)
    : FRoomObject(InOwner)
{
    // FUN_00443ed0. 0x00443f00..0x00443f1c: +0x30 = 0, the four counts and the four tables 0.
    Lod.Instance = nullptr;
    int Index = 0;
    for (Index = 0; Index < 4; ++Index)
    {
        Lod.Counts[Index] = 0;
        Lod.Tables[Index] = nullptr;
    }
    // 0x00443f1e..0x00443f74: CharacterProperties, the eight HardpointSummaries, +0x304 and the
    // five part slots (the members). 0x00443f7f..0x00443fab: +0x2f4 = +0x300 = +0x1b8 = 0,
    // +0x1c0..+0x1cc = -1.
    Hotspot = nullptr;
    Role = 0;
    Placement = nullptr;
    for (Index = 0; Index < 4; ++Index)
        LodIds[Index] = -1;
    // 0x00443fb1..0x00443fdd: each accessory -1 and its summary cleared.
    for (Index = 0; Index < kMaxAccessories; ++Index)
    {
        AccessoryInstances[Index] = -1;
        AccessoryHardpoints[Index].clear();
    }
    // 0x00443fdf..0x0044402e.
    AccessoryCount = 0;
    bHovered = 0;
    DisplayName = nullptr;
    Hardpoints.clear();
    OwnScript = nullptr;
    OwnScriptName = nullptr;
    bOwnScriptPlaying = 0;
    ScriptState = 0;
    ActiveMotions = 0;
    ActiveAims = 0;
    bPoseDirty = 0;
    bHasLastPose = 0;
    // 0x00444034.
    Load(Desc);
}

FCharacterObject::~FCharacterObject()
{
#ifdef FREELANCER_WALK_MODE
    BaseWalkCharacterChanged(this, true);
#endif
    // FUN_004440c0. 0x004440e6..0x00444118: still taken over by a script.
    if (ScriptState == 1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kObjectDeformableCpp, 0x175,
              "Character is being destroyed while still scripted, GAME IS ABOUT TO CRASH!!!\n");
    }
    // 0x0044411b..0x00444141: the own script forgotten (not destroyed) and its name freed.
    if (OwnScript != nullptr)
    {
        OwnScript = nullptr;
        bOwnScriptPlaying = 0;
        malloc_strfree(&OwnScriptName);
    }
    // 0x00444144..0x00444196: every accessory instance destroyed (IEngine slot 24) and its
    // summary destroyed.
    int Index = 0;
    for (Index = 0; Index < AccessoryCount; ++Index)
    {
        if (AccessoryInstances[Index] != -1)
        {
            DALib::Engine->DestroyInstance(reinterpret_cast<void*>(AccessoryInstances[Index]));
            AccessoryInstances[Index] = -1;
        }
        AccessoryHardpoints[Index].destroy();
    }
    AccessoryCount = 0;
    // 0x0044418f..0x004441bb.
    malloc_wstrfree(&DisplayName);
    Hardpoints.destroy();
    Props.destroy();
    // 0x004441c1..0x004441fc: an own body is destroyed (IDeformable slot 6); the shared one
    // is left alone.
    if (bSharedDeform == 0)
    {
        if (Lod.Instance != nullptr)
            DALib::Deform->Destroy(Lod.Instance);
        Lod.Instance = nullptr;
        for (Index = 0; Index < 4; ++Index)
        {
            Lod.Counts[Index] = 0;
            Lod.Tables[Index] = nullptr;
        }
    }
    // 0x004441fe..0x0044420e: the hotspot through its deleting destructor.
    if (Hotspot != nullptr)
    {
        delete Hotspot;
        Hotspot = nullptr;
    }
    // 0x00444214..0x0044426a: the members (FUN_00444080 x5, ~HardpointSummary, x8,
    // ~CharacterProperties) and FUN_005594b0.
}

void FCharacterObject::Load(FCharacterDesc const& Desc)
{
#ifdef FREELANCER_WALK_MODE
    BaseWalkCharacterChanged(this, false);
#endif
    // FUN_00444290. 0x004442ae..0x004442fa: the record, placement and index; a "pc" record
    // (+0x1c == 1) shares the one body.
    RefreshLoadingProgress();
    Props = Desc.Props;
    Placement = Desc.Placement;
    PlacementIndex = Desc.PlacementIndex;
    bSharedDeform = (Props.CharType == 1) ? 1 : 0;
    Role = 0;

    // 0x00444300..0x004443af: the role from the placement's setpoint segment.
    if (Placement != nullptr)
    {
        SetpointProperties Setpoint;
        Setpoint.extract_from_name(Placement->Name);
        if (_stricmp(Setpoint.Segment, "bartender") == 0) // DACOM stricmp
            Role = 1;
        else if (_stricmp(Setpoint.Segment, "trader") == 0)
            Role = 2;
        else if (_stricmp(Setpoint.Segment, "shipdealer") == 0)
            Role = 3;
        else if (_stricmp(Setpoint.Segment, "equipment") == 0)
            Role = 4;
    }

    // 0x004443b5..0x00444434: the four bodyparts (the head is costume part 0, the body part
    // 1) and the accessories.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    bodypart const* BodyParts[4];
    BodyParts[1] = Descs->find_bodypart(1, Props.Cost.PartIds[0]);
    BodyParts[0] = Descs->find_bodypart(0, Props.Cost.PartIds[1]);
    BodyParts[2] = Descs->find_bodypart(2, Props.Cost.PartIds[2]);
    BodyParts[3] = Descs->find_bodypart(3, Props.Cost.PartIds[3]);
    accessory const* Accessories[kMaxAccessories];
    int Index = 0;
    for (Index = 0; Index < static_cast<int>(Props.Cost.AccessoryCount); ++Index)
        Accessories[Index] = Descs->find_accessory(Props.Cost.AccessoryIds[Index]);

    // 0x00444436..0x004444c1: the body -- built for this character, or the shared one (built
    // the first time) copied in.
    if (bSharedDeform == 0)
    {
        BuildCharacterDeformable(&Lod, BodyParts);
    }
    else
    {
        if (G_SharedCharacterLod.Instance == nullptr)
            BuildCharacterDeformable(&G_SharedCharacterLod, BodyParts);
        Lod.Instance = G_SharedCharacterLod.Instance;
        for (Index = 0; Index < 4; ++Index)
        {
            Lod.Counts[Index] = G_SharedCharacterLod.Counts[Index];
            Lod.Tables[Index] = G_SharedCharacterLod.Tables[Index];
            int Lod16 = 0;
            for (Lod16 = 0; Lod16 < 16; ++Lod16)
                Lod.Switch[Index][Lod16] = G_SharedCharacterLod.Switch[Index][Lod16];
        }
    }

    // 0x004444c3..0x00444527: with a root bone, the pose (IDeformable slots 19 / 20) and the
    // height (slot 3, with the position's y).
    int Bone = -1;
    DALib::Deform->GetFirstBone(Lod.Instance, &Bone);
    if (Bone != -1)
    {
        DALib::Deform->SetPosePos(Lod.Instance, const_cast<float*>(&Desc.Pos.x));
        SetFloat1C(Desc.Pos.y);
        DALib::Deform->SetPoseOri(Lod.Instance, const_cast<Matrix*>(&Desc.Orient));
    }
    // 0x0044452a..0x0044453b.
    Hardpoints.analyze_instance(Bone);
    RefreshLoadingProgress();

    // 0x00444541..0x0044474a: each accessory with a mesh: its material libraries as {name, -1}
    // pairs, the mesh loaded (FUN_0041c540, the material entries into RoomObject +0x20), its
    // hardpoints analysed, and its AccessoryHardpoint connected to the body's
    // CharacterHardpoint (IHardpoint slot 6) when both exist.
    for (Index = 0; Index < static_cast<int>(Props.Cost.AccessoryCount); ++Index)
    {
        accessory const* const Found = Accessories[Index];
        if (Found == nullptr || Found->Mesh.empty())
            continue;

        Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> Paths;
        Paths.Cookie = 0;
        Paths.Begin = nullptr;
        Paths.End = nullptr;
        Paths.Capacity = nullptr;
        std::list<std::string>::const_iterator It = Found->Materials.begin();
        for (; It != Found->Materials.end(); ++It)
        {
            CacheString Name;
            Name.clear();
            Name.Value = StringAlloc(It->c_str(), true);
            CacheString Pair;
            Pair.clear();
            Archetype::PushMaterialPathPair(&Paths, Name.Value, 0xffffffffu);
        }

        void* const Instance = MiscUtilLoadEngineInstance(Found->Mesh.c_str(), DALib::Data,
                                                          nullptr, nullptr, &Paths,
                                                          &MaterialEntries, nullptr);
        AccessoryInstances[Index] = reinterpret_cast<long>(Instance);
        AccessoryHardpoints[Index].analyze_instance(AccessoryInstances[Index]);
        if (AccessoryInstances[Index] != -1)
        {
            char const* const CharacterHp = Found->CharacterHardpoint.c_str();
            char const* const AccessoryHp = Found->AccessoryHardpoint.c_str();
            int const CharacterIndex = Hardpoints.find_name(CharacterHp);
            int const AccessoryIndex = AccessoryHardpoints[Index].find_name(AccessoryHp);
            if (CharacterIndex != -1 && AccessoryIndex != -1)
            {
                FHardpointKey AccessoryKey;
                AccessoryKey.Name = AccessoryHp;
                AccessoryKey.Crc = DACOM_CRC::GetCRC32(AccessoryHp);
                FHardpointKey CharacterKey;
                CharacterKey.Name = CharacterHp;
                CharacterKey.Crc = DACOM_CRC::GetCRC32(CharacterHp);
                void* const AccessoryPart = reinterpret_cast<void*>(
                    AccessoryHardpoints[Index].get_instance(AccessoryIndex));
                void* const CharacterPart =
                    reinterpret_cast<void*>(Hardpoints.get_instance(CharacterIndex));
                DALib::Hardpoint->ConnectHardpoints(CharacterPart, &CharacterKey, AccessoryPart,
                                                    &AccessoryKey);
            }
        }
        // 0x004446ff..0x00444723: the pair list's buffer.
        delete[] Paths.Begin;
    }

    // 0x00444750..0x0044475c.
    AccessoryCount = static_cast<int>(Props.Cost.AccessoryCount);
    RefreshLoadingProgress();

    // 0x00444762..0x004447e2: the hotspot, owned by and answering to this character; enabled
    // only with a behaviour or a role (FUN_00445a60, inlined).
    Hotspot = new FCharacterHotspot();
    Hotspot->Character = this;
    Hotspot->bEnabled = 1;
    Hotspot->Owner = this;
    Hotspot->Cookie = 0;
    UpdateHotspotEnabled();

    // 0x004447e2..0x00444808: the name -- the record's ids (FUN_0041c770), else its wide name.
    DisplayName = nullptr;
    if (Props.NameIds != 0)
        MiscUtilLoadIdsCopy(&DisplayName, Props.NameIds);
    else
        malloc_wstrcpy(&DisplayName, Props.NameWide);

    // 0x0044480b..0x004448b2: no self-update until the room starts it; the idle script
    // (+0x1ac) becomes the own script unless it already is.
    bNoUpdate = 1;
    unsigned char const bSpatial = Desc.bScriptSpatial;
    char const* const IdleScript = Props.Script;
    bool bSame = false;
    if (IdleScript != nullptr && OwnScriptName != nullptr &&
        _stricmp(IdleScript, OwnScriptName) == 0) // DACOM stricmp
    {
        bSame = true;
    }
    if (!bSame)
    {
        if (OwnScript != nullptr)
        {
            G_GfScriptManager.DestroySequence(OwnScript);
            OwnScript = nullptr;
            bOwnScriptPlaying = 0;
            malloc_strfree(&OwnScriptName);
        }
        malloc_strcpy(&OwnScriptName, IdleScript);
        OwnScript = CreateOwnScript(IdleScript, bSpatial);
        if (ScriptState == 0 && OwnScript != nullptr)
        {
            OwnScript->Play();
            bOwnScriptPlaying = 1;
        }
    }

    // 0x004448b9..0x00444919: the script run on by one full step, then by a random fraction
    // (rand() / 32768, DAT_005caeec) when that is above 0 (`TEST AH,0x41` + JNZ: not greater
    // skips).
    if (OwnScript != nullptr)
        OwnScript->AdvanceCurrent(1.0f);
    float const Random = static_cast<float>(rand()) * 3.0517578125e-05f;
    if (OwnScript != nullptr && Random > 0.0f)
        OwnScript->AdvanceCurrent(Random);

    // 0x0044491e..0x00444925.
    RenderMode = 0;
    LightGroup = 0;
    Layer = 0;
}

void* FCharacterObject::GetEngineInstance()
{
    // LAB_00444970: IDeformable slot 18 into an int that starts at -1.
    int Bone = -1;
    DALib::Deform->GetFirstBone(Lod.Instance, &Bone);
    return reinterpret_cast<void*>(static_cast<intptr_t>(Bone));
}

void FCharacterObject::SetFloat1C(float Value)
{
    // LAB_004449a0: IDeformable slot 24 with the value, +0x1c = value, +0x378 = 1.
    DALib::Deform->SetHeight(Lod.Instance, Value);
    Float1C = Value;
    bPoseDirty = 1;
}

void FCharacterObject::Update(double DeltaSeconds)
{
    // LAB_004449d0. 0x004449ef..0x00444a0f: the OBJECTS and CHARACTERS options.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptCharacters) == 0)
        return;

    // 0x00444a15..0x00444a2f: a script let go of the character -- its own script again, and
    // no self-update.
    if (ScriptState == 2)
    {
        ScriptState = 0;
        RestartOwnScript();
        bNoUpdate = 1;
    }

    // 0x00444a33..0x00444b07: the instance's pose; a pose other than last frame's (compared
    // dword by dword, CMPSD) marks the pose dirty.
    float Pos[3];
    memcpy(Pos, DALib::Engine->GetInstancePosition(GetEngineInstance()), sizeof(Pos));
    float Orient[9];
    memcpy(Orient, DALib::Engine->GetInstanceOrientation(GetEngineInstance()), sizeof(Orient));
    if (bHasLastPose == 0 || memcmp(LastPos, Pos, sizeof(Pos)) != 0 ||
        memcmp(LastOrient, Orient, sizeof(Orient)) != 0)
    {
        bPoseDirty = 1;
    }
    memcpy(LastPos, Pos, sizeof(Pos));
    memcpy(LastOrient, Orient, sizeof(Orient));
    bHasLastPose = 1;

    // 0x00444b0e..0x00444b6a: with motions, aims or a dirty pose, the mixer walked
    // (IAnimation2 slot 27) and the instance updated (IEngine slot 27) by the frame time.
    if (ActiveMotions != 0 || ActiveAims != 0 || bPoseDirty != 0)
    {
        float const Time = static_cast<float>(DeltaSeconds);
        DALib::Anim->MixerWalk(static_cast<unsigned long>(
                                   reinterpret_cast<uintptr_t>(GetEngineInstance())),
                               Time);
        void* TimeArg = nullptr;
        memcpy(&TimeArg, &Time, sizeof(Time));
        DALib::Engine->UpdateInstance(GetEngineInstance(), 0, TimeArg);
    }

    // 0x00444b6d..0x00444b84: over a 3D window the arrow cursor, and no name.
    if (Win3DGetHover() != nullptr)
    {
        MouseSetCursor("arrow", false);
        bHovered = 0;
    }

    // 0x00444b8a..0x00444dfa: hovered with a name -- the name as a tooltip keyed on this
    // object, pointing along the view ray through the hotspot's top-left corner 30 pixels up.
    if (bHovered == 0)
        return;
    if (DisplayName == nullptr)
        return;

    FToolTipHoverInfo Info;
    ToolTipHoverInit(&Info);
    ToolTipHoverSetCaption(&Info, DisplayName);
    Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));

    // 0x00444c09..0x00444cb2: the corner on the Win3D camera's near plane (DAT_0067dc60 /
    // 0067dc64 centre, 0067dc70 / 0067dc74 scale, 0067dc9c znear), turned by its orientation
    // DAT_0067dbfc (FUN_00423b90).
    FSpaceCamera* const Cam = Win3DGetCamera();
    FVector Local;
    Win3DScreenToNearPlane(Cam, Local, static_cast<float>(Hotspot->ScreenRect.Left),
                           static_cast<float>(Hotspot->ScreenRect.Top + -0x1e));
    FMatrix CamOrientation;
    memcpy(&CamOrientation, Cam->World, sizeof(CamOrientation));
    FVector World;
    Win3DTransformByMatrix(World, CamOrientation, Local);

    // 0x00444cb7..0x00444d73: normalised ((z^2 + x^2) + y^2, 1 / sqrt, DAT_005c75dc) into the
    // world position; zero half extents; live.
    double const X = World.X;
    double const Y = World.Y;
    double const Z = World.Z;
    double const Inv = 1.0 / sqrt(Z * Z + X * X + Y * Y);
    Info.WorldPos[0] = static_cast<float>(X * Inv);
    Info.WorldPos[1] = static_cast<float>(Y * Inv);
    Info.WorldPos[2] = static_cast<float>(Z * Inv);
    Info.HalfExtents[0] = 0.0f;
    Info.HalfExtents[1] = 0.0f;
    Info.HalfExtents[2] = 0.0f;
    Info.bLive = 1;
    NnToolTipFeedHover(&Info);

    // 0x00444d82..0x00444dfa: the record torn down (~FToolTipHoverInfo).
}

void FCharacterObject::PickLods(ICamera* Camera)
{
    // FUN_00444f90. 0x00444f9b..0x0044501f: the camera distance, 1.0 without an instance.
    float Distance = 1.0f;
    void* const Instance = GetEngineInstance();
    if (Instance != kNoInstance)
    {
        float const* const Where = DALib::Engine->GetInstancePosition(Instance);
        float const X = Where[0];
        float const Y = Where[1];
        float const Z = Where[2];
        float const* const Eye = Camera->GetPosition();
        double const Dx = static_cast<double>(X) - Eye[0];
        double const Dy = static_cast<double>(Y) - Eye[1];
        double const Dz = static_cast<double>(Z) - Eye[2];
        Distance = static_cast<float>(sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
    }
    // 0x00445023..0x0044503f: the half field of view (ICamera slot 6) and the surface width.
    float const HalfFov = Camera->GetHalfFovXDeg();
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: slot 6 is the angle on a 4:3 surface, so the LOD pairs it with the width
    // of that surface and picks what a 4:3 screen of this height would.
    float const SurfaceWidth = static_cast<float>(WidescreenEquivalentSurfaceWidth());
#else
    float const SurfaceWidth = static_cast<float>(static_cast<int>(G_RenderSurfaceWidth));
#endif

    // 0x00445043..0x0044511c: per part with LODs, the first LOD whose switch distance is not
    // below the adjusted distance (`TEST AH,1` + JZ: not less is the hit); none -> -1. With
    // "low_character_lod" and the adjusted distance beyond the 100% switch distance (`TEST
    // AH,5` + JP: only C0 alone falls through), one LOD coarser, or past the last: the first
    // part keeps its last LOD, the others none. The picks are packed.
    int Written = 0;
    int Part = 0;
    for (Part = 0; Part < 4; ++Part)
    {
        int const Count = Lod.Counts[Part];
        if (Count <= 0)
            continue;
        float const Adjusted =
            Lod.Tables[Part]->adjust_distance(Distance, HalfFov, SurfaceWidth);
        int Pick = -1;
        int Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            if (!(Lod.Switch[Part][Index] < Adjusted))
                break;
        }
        if (Index < Count)
        {
            Pick = Index;
            if (RtcSlider::is_enabled("low_character_lod"))
            {
                float const Full = Lod.Tables[Part]->get_switch_distance(100.0f);
                if (Full < Adjusted)
                {
                    if (Index + 1 < Count)
                        Pick = Index + 1;
                    else if (Part == 0)
                        Pick = Lod.Counts[0] - 1;
                    else
                        Pick = -1;
                }
            }
        }
        LodIds[Written] = Pick;
        ++Written;
    }
    // 0x00445122..0x00445138: the rest -1.
    for (; Written < 4; ++Written)
        LodIds[Written] = -1;
}

void FCharacterObject::DrawOpaque(ICamera* Camera)
{
    // LAB_00445150. 0x00445156..0x00445188: OBJECTS and CHARACTERS on, LIKE_PETAL off.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptCharacters) == 0)
        return;
    if (GetGameOption(kGameOptLikePetal) != 0)
        return;

    // 0x0044518e..0x004451e0: the hotspot follows the room camera; the body's bounding sphere
    // (IDeformable slot 38) must touch the room camera's view (FUN_0041c8d0).
    ICamera* const RoomCamera = GetRoomRuntimeCamera(Owner);
    Hotspot->UpdateScreenRect(RoomCamera);
    float Radius = 0.0f;
    float Center[3];
    DALib::Deform->GetBoundSphere(Lod.Instance, &Radius, Center);
    if (!MiscUtilSphereInView(RoomCamera, Center, Radius))
        return;
    if (Camera == nullptr)
        return;
    void* const Instance = GetEngineInstance();
    if (Instance == kNoInstance)
        return;

    // 0x004451fc..0x0044522c: the LODs, then IDeformable slot 15 with the frame time.
    PickLods(Camera);
    float const FrameTime = static_cast<float>(GetClientFrameTimeCapped());
    DALib::Deform->SetLod(Lod.Instance, FrameTime, LodIds);

    // 0x0044522c..0x0044523d: the shadow (FUN_0044b370 with the instance, the height and 1).
    float const Height = GetFloat1C();
    unsigned long HeightBits = 0;
    memcpy(&HeightBits, &Height, sizeof(HeightBits));
    RoomControlDrawShadow(Owner, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)),
                          HeightBits, 1);
    // 0x00445242..0x00445246: FUN_0044ae80.
    RoomControlLightObject(Owner, this);
    // 0x0044524b..0x00445264: IDeformable slot 16 (camera, LODs, 0, &out, 0). The out int is
    // an uninitialised stack slot in retail.
    int DrawnLod = 0;
    DALib::Deform->Draw(Lod.Instance, Camera, LodIds, nullptr, &DrawnLod, 0);
}

void FCharacterObject::RestartOwnScript()
{
    // FUN_00445280. 0x00445283..0x0044529e: a live sequence restarts (FUN_004562e0) when it
    // played before, else plays.
    if (OwnScript != nullptr)
    {
        if (bOwnScriptPlaying != 0)
        {
            OwnScript->RestartCurrent();
            return;
        }
        OwnScript->Play();
        bOwnScriptPlaying = 1;
        return;
    }
    // 0x004452a3..0x00445332: the idle script, unless it already is the own one.
    char const* const IdleScript = Props.Script;
    if (IdleScript != nullptr && OwnScriptName != nullptr &&
        _stricmp(IdleScript, OwnScriptName) == 0) // DACOM stricmp
    {
        return;
    }
    if (OwnScript != nullptr)
    {
        G_GfScriptManager.DestroySequence(OwnScript);
        OwnScript = nullptr;
        bOwnScriptPlaying = 0;
        malloc_strfree(&OwnScriptName);
    }
    malloc_strcpy(&OwnScriptName, IdleScript);
    OwnScript = CreateOwnScript(IdleScript, 0);
    if (ScriptState != 0)
        return;
    if (OwnScript == nullptr)
        return;
    OwnScript->Play();
    bOwnScriptPlaying = 1;
}

FGfScriptSequence* FCharacterObject::CreateOwnScript(char const* ScriptName, unsigned char bSpatial)
{
    // FUN_00445340. 0x0044535a..0x00445362: no name, no script.
    if (ScriptName == nullptr || ScriptName[0] == '\0')
        return nullptr;

    // 0x00445368..0x0044537c: a sequence of the room's.
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Owner, 0);
    if (Sequence == nullptr)
    {
        // 0x00445490..0x004454b1.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kObjectDeformableCpp, 0x453,
              "Cannot allocate thorn script\n");
        return Sequence;
    }

    // 0x00445382..0x004453c1: the script and its own frame; the play frame starts at the
    // identity.
    IScriptEngine* const Engine = ThornScriptLoad(ScriptName);
    Csys const ScriptFrame = ThornScriptGetCsys(Engine, nullptr);
    Csys Frame;
    Frame.Pos.x = 0.0f;
    Frame.Pos.y = 0.0f;
    Frame.Pos.z = 0.0f;
    MatrixSetIdentity(Frame.Orient);

    // 0x004453c6..0x0044541a: on a placement, the placement's frame taken back through the
    // script's own (FUN_00435c90: Placement * ScriptFrame^-1), and its name.
    char const* PlacementName = nullptr;
    if (Placement != nullptr)
    {
        Csys const PlacementFrame = Placement->Frame;
        PlacementName = Placement->Name;
        Frame = PlacementFrame * ScriptFrame.get_inverse();
    }

    // 0x0044541d..0x00445472: the script, this character in the mirror, and the sequence's
    // flags: bind the mirror's first, loop, +0x05 set, no letterbox, spatial as asked.
    Sequence->AddScript(ScriptName, 0, &Frame, Engine);
    Sequence->Mirror.Add(this, nullptr);
    Sequence->BindMode = 1;
    Sequence->Field_0x05 = 1;
    Sequence->bLoop = 1;
    Sequence->bLetterbox = 0;
    Sequence->bApplySpatial = bSpatial;
    Sequence->bRestoreCamera = 0;
    Sequence->bHoldTime = 0;
    Sequence->Priority = 0;
    Sequence->Callback = nullptr;
    Sequence->CallbackA = nullptr;
    Sequence->CallbackB = nullptr;
    // 0x00445473..0x00445478: named after the placement.
    if (PlacementName != nullptr)
        Sequence->SetName(PlacementName);
    return Sequence;
}

void FCharacterObject::SetOwnScript(char const* ScriptName, unsigned char bSpatial)
{
    // FUN_00445580. 0x00445586..0x004455a3: already the own script.
    if (ScriptName != nullptr && OwnScriptName != nullptr &&
        _stricmp(ScriptName, OwnScriptName) == 0) // DACOM stricmp
    {
        return;
    }
    // 0x004455a5..0x004455d8.
    if (OwnScript != nullptr)
    {
        G_GfScriptManager.DestroySequence(OwnScript);
        OwnScript = nullptr;
        bOwnScriptPlaying = 0;
        malloc_strfree(&OwnScriptName);
    }
    // 0x004455db..0x00445614.
    malloc_strcpy(&OwnScriptName, ScriptName);
    OwnScript = CreateOwnScript(ScriptName, bSpatial);
    if (ScriptState != 0)
        return;
    if (OwnScript == nullptr)
        return;
    OwnScript->Play();
    bOwnScriptPlaying = 1;
}

void FCharacterObject::UpdateHotspotEnabled()
{
    // FUN_00445a60: no behaviour (+0x1b0 == -1) and no role -> disabled.
    if (Props.BehaviorId == 0xffffffffu && Role == 0)
    {
        Hotspot->bEnabled = 0;
        return;
    }
    Hotspot->bEnabled = 1;
}

void FCharacterObject::Slot10(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // FUN_00445a90 (RET 8). Not while a window holds the mouse.
    if (Win3DAnyCaptured())
        return;
    // 0x00445aa0..0x00445ad1: the name shows; the cursor by role.
    bHovered = 1;
    char const* Cursor = "talk_blowoff";
    if (Role == 2)
        Cursor = "talk_commodity_dealer";
    else if (Role == 3)
        Cursor = "talk_ship_dealer";
    else if (Role == 4)
        Cursor = "talk_equip_dealer";

    // 0x00445ad6..0x00445bbf: with a behaviour the room knows, the cursor by its kind: none
    // shown (+0x0a clear) "talk_story"; else by +0x220 blowoff / bribe / rumor / mission /
    // info / story, another kind keeping the role's cursor.
    unsigned const BehaviorId = Props.BehaviorId;
    if (BehaviorId != 0xffffffffu)
    {
        MetaNode<ScriptBehavior>* const Node =
            FindBehaviorNode(GetClientLocationRoomScriptBehaviors(GetRoomRuntimeRoomId(Owner)),
                             BehaviorId);
        if (Node != nullptr && Node->Data != nullptr)
        {
            ScriptBehavior const* const Behavior = Node->Data;
            if (Behavior->Flag0a == 0)
            {
                Cursor = "talk_story";
            }
            else
            {
                unsigned const Kind = Behavior->Field220;
                if (Kind == 0)
                    Cursor = "talk_blowoff";
                else if (Kind == 1)
                    Cursor = "talk_bribe";
                else if (Kind == 2)
                    Cursor = "talk_rumor";
                else if (Kind == 3)
                    Cursor = "talk_mission";
                else if (Kind == 4)
                    Cursor = "talk_info";
                else if (Kind == 5)
                    Cursor = "talk_story";
            }
        }
    }
    MouseSetCursor(Cursor, false);
}

void FCharacterObject::Slot11(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // FUN_00445be0 (RET 8): unless a window holds the mouse, the arrow, no name, the tooltip
    // hidden (FUN_004be390).
    if (Win3DAnyCaptured())
        return;
    MouseSetCursor("arrow", false);
    bHovered = 0;
    NnToolTipHideGlobal();
}

void FCharacterObject::Slot12(unsigned long Cookie, int const* Point)
{
#ifdef FREELANCER_WALK_MODE
    BaseWalkBeforeInteraction();
#endif
    // FUN_00445c10 (RET 8). Not while a window holds the mouse.
    if (Win3DAnyCaptured())
        return;
    // 0x00445c20..0x00445c2f: the leave handling first (slot 11).
    Slot11(Cookie, Point);

    // 0x00445c32..0x00445c72: the character's faction (+0x164) marked met in the visit map
    // (flags | 0x41, the server told) unless bit 0 is already set.
    unsigned const Faction = Props.FactionId;
    if (Faction != 0xffffffffu)
    {
        unsigned char Flags = 0;
        GetPlayerVisitFlagsRetail(Faction, &Flags, 0, 0); // 0x00445c4c
        if ((Flags & 1) == 0)
            SetPlayerVisitFlagsRetail(Faction, static_cast<unsigned char>(Flags | 0x41), true);
    }

    // 0x00445c75..0x00445c96: the NavBar may take the click; a character under a script's
    // control does nothing.
    if (NavBarCharacterClicked(this, Role))
        return;
    if (ScriptState != 0)
        return;

    // 0x00445c9c..0x00445cde: with a behaviour, its script (FUN_00435dd0, the dealer opened
    // when it stops) and the server told (IServer slot 41 GFObjSelect).
    if (Props.BehaviorId != 0xffffffffu)
    {
        StartCharacterBehaviorScript(this, Owner, Placement, Props.BehaviorId,
                                     &OnBehaviorScriptStopped, this);
        static_cast<IServer*>(GetLocalServerInterface())->GFObjSelect(Props.Key,
                                                                      GetPlayerConnectionId());
        return;
    }
    // 0x00445ce1..0x00445d1e: else the role's dealer.
    OpenRoleDealer(Role);
}

void FCharacterObject::TakeOverForScript(unsigned char bKeepOwnScript)
{
#ifdef FREELANCER_WALK_MODE
    BaseWalkCharacterScriptStarting(this);
#endif
    // FUN_004454e0. 0x004454e9 `JNZ` on a set argument goes straight to +0x04 = 1 (0x00445533).
    // Clear: a non-null +0x360 is handed to FUN_004568d0 on DAT_0066d2d0 (0x004454fb), +0x360 =
    // 0, +0x368 = 0, malloc_strfree(&+0x364) -- all inside the non-null branch; then +0x36c = 1
    // and +0x04 = 1 (0x00445521..0x0044552c) either way.
    if (bKeepOwnScript == 0)
    {
        if (OwnScript != nullptr)
        {
            G_GfScriptManager.DestroySequence(OwnScript);
            OwnScript = nullptr;
            bOwnScriptPlaying = 0;
            malloc_strfree(&OwnScriptName);
        }
        ScriptState = 1;
    }
    bNoUpdate = 1;
}

void FCharacterObject::ResetScriptState(unsigned char bRelease)
{
    // FUN_00445540. 0x00445548 writes +0x04 before the branch; 0x0044554b `JZ` on a zero
    // argument takes the +0x36c = 2 side.
    bNoUpdate = 0;
    if (bRelease == 0)
    {
        ScriptState = 2;
        return;
    }
    OwnScript = nullptr;
    bOwnScriptPlaying = 0;
    malloc_strfree(&OwnScriptName);
}

long FCharacterObject::FindPart(char const* PartName)
{
    // FUN_00445620. The scan (0x00445631..0x0044566d) returns a used entry whose name matches
    // (DACOM stricmp) and whose part is not -1; on the way it remembers the first unused entry
    // (0x00445659 `TEST EBX,EBX; JGE`). None unused -> entry 0 (0x00445671). The lookup runs on
    // slot 6's instance; a hit replaces the entry's part and name (CRT free / _strdup, not the
    // malloc_str* pair) and marks it used. The result is returned either way.
    int Unused = -1;
    int Index = 0;
    for (; Index < kPartCacheEntries; ++Index)
    {
        FPartCacheEntry const& Entry = Parts[Index];
        if (Entry.bUsed != 0 && Entry.Name != nullptr && _stricmp(Entry.Name, PartName) == 0 &&
            Entry.Part != -1)
            return Entry.Part;
        if (Unused < 0 && Entry.bUsed == 0)
            Unused = Index;
    }
    if (Unused < 0)
        Unused = 0;

    long const Part = CompoundInstanceFindFirstName(
        static_cast<long>(reinterpret_cast<intptr_t>(GetEngineInstance())), PartName);
    if (Part != -1)
    {
        FPartCacheEntry& Entry = Parts[Unused];
        Entry.Part = Part;
        if (Entry.Name != nullptr)
        {
            std::free(Entry.Name);
            Entry.Name = nullptr;
        }
        Entry.Name = _strdup(PartName);
        Entry.bUsed = 1;
    }
    return Part;
}

void FCharacterObject::AddMotionHandle(int Handle)
{
    // FUN_004456e0.
    if (Handle != -1)
        ++ActiveMotions;
}

void FCharacterObject::RemoveMotionHandle(int Handle)
{
    // FUN_004456f0.
    if (Handle != -1)
        --ActiveMotions;
}

void FCharacterObject::AddAimHandle(void* Handle)
{
    // FUN_00445700.
    if (reinterpret_cast<intptr_t>(Handle) != -1)
        ++ActiveAims;
}

void FCharacterObject::RemoveAimHandle(void* Handle)
{
    // FUN_00445710.
    if (reinterpret_cast<intptr_t>(Handle) != -1)
        --ActiveAims;
}

char const* FCharacterObject::ResolveBgcsContextToken(char const* TemplateName) const
{
    // FUN_00445720. Every exit returns DAT_00668848 (0x00445a44 / 0x00445923 / 0x0044599a, and
    // EAX loaded before the 0x00445871 branch). Retail's Universe::IBase calls go through its
    // vtable (+0x04 nickname, +0x14 BGCS_base_run_by); the port's IBase publishes the same two
    // strings as fields.

    // 0x00445728..0x004457a8: GetCostumeDescriptions()->get_costume_gender(this +0x178):
    // 1 -> "gcs_ml_" (DAT_005cbb64), 2 -> "gcs_fm_" (DAT_005cbb5c), else "" (DAT_005c865c) and
    // the FDUMP at ObjectDeformable.cpp:0x51f.
    int const Gender = GetCostumeDescriptions()->get_costume_gender(Props.Cost);
    if (Gender == 1)
        strcpy(G_BgcsContextBuffer, "gcs_ml_");
    else if (Gender == 2)
        strcpy(G_BgcsContextBuffer, "gcs_fm_");
    else
    {
        G_BgcsContextBuffer[0] = '\0';
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kObjectDeformableCpp, 0x51f,
              "Invalid gender for character, cannot find GCS voice profile message name\n");
    }

    unsigned const BaseId = GetRoomRuntimeBaseId(Owner); // [[this+8]+0x34]

    if (_stricmp(TemplateName, "$BGCS_Base_Name") == 0)
    {
        // 0x004457ca..0x00445881: append "W02aF_" (DAT_005cbb08..0e), then the base nickname,
        // then drop a trailing "_base" (s__base_005cbb00) when the whole buffer is longer.
        strcat(G_BgcsContextBuffer, "W02aF_");
        strcat(G_BgcsContextBuffer, Universe::get_base(BaseId)->Nickname);
        int const Length = static_cast<int>(strlen(G_BgcsContextBuffer));
        int const SuffixLength = static_cast<int>(strlen("_base"));
        if (Length <= SuffixLength)
            return G_BgcsContextBuffer;
        char* const Suffix = G_BgcsContextBuffer + (Length - SuffixLength);
        if (_stricmp(Suffix, "_base") != 0)
            return G_BgcsContextBuffer;
        *Suffix = '\0';
        return G_BgcsContextBuffer;
    }

    if (_stricmp(TemplateName, "$BGCS_Base_Run_By") == 0)
    {
        // 0x00445898..0x0044592c: append the base's BGCS_base_run_by; an empty one is reported
        // with a second get_base for the nickname (ObjectDeformable.cpp:0x54a).
        // 0x0044589f..0x004458ac: get_base, then IBase vt+0x14 (the BGCS string's c_str).
        char const* const RunBy = Universe::get_base(BaseId)->GetBgcsBaseRunBy();
        strcat(G_BgcsContextBuffer, RunBy);
        if (RunBy[0] != '\0')
            return G_BgcsContextBuffer;
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: No BGCS_base_run_by specified for base %s, cannot resolve "
              "voice profile message %s\n",
              kObjectDeformableCpp, 0x54a, Universe::get_base(BaseId)->Nickname, TemplateName);
        return G_BgcsContextBuffer;
    }

    // 0x0044592f..0x004459a3: My_Faction ML/FM -> "D06aG_" (DAT_005cba3c..42), Faction_Rebuff
    // ML/FM -> "A03aG_" (DAT_005cba44..4a); anything else is ObjectDeformable.cpp:0x571.
    char const* Prefix = nullptr;
    if (_stricmp(TemplateName, "$BGCS_My_Faction_ML") == 0 ||
        _stricmp(TemplateName, "$BGCS_My_Faction_FM") == 0)
    {
        Prefix = "D06aG_";
    }
    else if (_stricmp(TemplateName, "$BGCS_Faction_Rebuff_ML") == 0 ||
             _stricmp(TemplateName, "$BGCS_Faction_Rebuff_FM") == 0)
    {
        Prefix = "A03aG_";
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Unknown GCS generic voice profile message %s\n",
              kObjectDeformableCpp, 0x571, TemplateName);
        return G_BgcsContextBuffer;
    }

    // 0x004459e4..0x00445a42: append the prefix, then Reputation::get_nickname of +0x164 into a
    // zeroed TString<16> (0x00445a02 / 0x00445a07) and append its text.
    strcat(G_BgcsContextBuffer, Prefix);
    TString<16> Nickname;
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    Reputation::get_nickname(Nickname, Props.FactionId);
    strcat(G_BgcsContextBuffer, Nickname.Data);
    return G_BgcsContextBuffer;
}

// ============================================================================================
// Free functions
// ============================================================================================

void ReleaseSharedCharacterLod()
{
    // FUN_00443e90: IDeformable slot 6 on a live instance, then the instance and both
    // four-entry arrays zeroed.
    if (G_SharedCharacterLod.Instance != nullptr)
        DALib::Deform->Destroy(G_SharedCharacterLod.Instance);
    G_SharedCharacterLod.Instance = nullptr;
    int Index = 0;
    for (Index = 0; Index < 4; ++Index)
    {
        G_SharedCharacterLod.Counts[Index] = 0;
        G_SharedCharacterLod.Tables[Index] = nullptr;
    }
}

FCharacterObject* CreateCharacterObject(CharacterProperties const& Props, CharPlaceInfo* Placement,
                                        int PlacementIndex, FRoomRuntime* Owner,
                                        unsigned char bScriptSpatial, Csys const& Frame)
{
    // FUN_00435b80. 0x00435b9d..0x00435c23: the description (FUN_00443a90) filled from the
    // arguments.
    FCharacterDesc Desc;
    Desc.Props = Props;
    Desc.Placement = Placement;
    Desc.PlacementIndex = PlacementIndex;
    Desc.bScriptSpatial = bScriptSpatial;
    Desc.Pos = Frame.Pos;
    Desc.Orient = Frame.Orient;
    // 0x00435c1e..0x00435c55: operator new(0x3b0) + FUN_00443ed0(Owner, &Desc); a failed
    // allocation gives null. 0x00435c66: the description's destructor (FUN_00443b10).
    FCharacterObject* const Character = new (std::nothrow) FCharacterObject(Owner, Desc);
    return Character;
}

void StartCharacterBehaviorScript(FCharacterObject* Character, FRoomRuntime* Room,
                                  CharPlaceInfo const* Placement, unsigned BehaviorKey,
                                  void(__cdecl* Callback)(void*, void*, unsigned char),
                                  void* CallbackA)
{
    // FUN_00435dd0. 0x00435dd5..0x00435e1b: the room's behaviour of that key (FUN_0043ad50 +
    // FUN_00437020); no node or no record, nothing.
    unsigned const RoomId = (Room != nullptr) ? GetRoomRuntimeRoomId(Room) : 0;
    char const* const PlacementName = (Placement != nullptr) ? Placement->Name : nullptr;
    MetaNode<ScriptBehavior>* const Node =
        FindBehaviorNode(GetClientLocationRoomScriptBehaviors(RoomId), BehaviorKey);
    if (Node == nullptr)
        return;
    ScriptBehavior const* const Behavior = Node->Data;
    if (Behavior == nullptr)
        return;

    // 0x00435e21..0x00435e31.
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Room, 0);
    if (Sequence == nullptr)
        return;

    // 0x00435e37..0x00435eaa: each of the behaviour's scripts with a file, at the identity.
    Csys Frame;
    Frame.Pos.x = 0.0f;
    Frame.Pos.y = 0.0f;
    Frame.Pos.z = 0.0f;
    MatrixSetIdentity(Frame.Orient);
    int Index = 0;
    for (Index = 0; Index < Behavior->StringArray2Count; ++Index)
    {
        char const* const File = Behavior->StringArray2[Index];
        if (File == nullptr || File[0] == '\0')
            continue;
        Sequence->AddScript(File, static_cast<int>(Behavior->StringArray2Dword[Index]), &Frame,
                            nullptr);
    }

    // 0x00435eac..0x00435ed1: the dialog (+0x220 / +0x224 / +0x228 and the two texts).
    Sequence->SetDialog(static_cast<int>(Behavior->Field220), static_cast<int>(Behavior->Field224),
                        static_cast<int>(Behavior->Field228), Behavior->NameFmtStr,
                        Behavior->DescFmtStr);

    // 0x00435ed6..0x00435f44: +0x14 set -- this character alone, bound as the mirror's first;
    // clear -- the listed characters by key under their names, bound by name.
    int BindMode = 0;
    if (Behavior->Flag14 != 0)
    {
        Sequence->Mirror.Add(Character, nullptr);
        BindMode = 1;
    }
    else
    {
        BindMode = 2;
        for (Index = 0; Index < Behavior->IdArrayCount; ++Index)
        {
            char const* const Name = Behavior->StringArray[Index];
            FCharacterObject* const Listed =
                RoomControlFindCharacterByKey(Room, Behavior->IdArray[Index]);
            Sequence->Mirror.Add(Listed, Name);
        }
    }

    // 0x00435f46..0x00435fb8: letterboxed, named after the placement, the stop callback, the
    // ambience and music ducked, the behaviour's location / skip / server fields, +0x16 for a
    // dealer role; then played.
    Sequence->BindMode = BindMode;
    Sequence->bLetterbox = 1;
    Sequence->bApplySpatial = Behavior->Flag08;
    Sequence->SetName(PlacementName);
    Sequence->Priority = 0;
    Sequence->Callback = Callback;
    Sequence->CallbackB = nullptr;
    Sequence->bDuckAmbient = 1;
    Sequence->bDuckMusic = 1;
    Sequence->CallbackA = CallbackA;
    Sequence->Location = Behavior->Field0c;
    Sequence->Field_0x15 = Behavior->Flag0a;
    Sequence->RtcId = Behavior->Field25c;
    int const Role = Character->Role;
    Sequence->Field_0x16 = (Role == 2 || Role == 3 || Role == 4) ? 1 : 0;
    Sequence->Play();
}

void PlacePlayerAtSetpoint(char const* SetpointName)
{
    // FUN_00435cf0. 0x00435cf7..0x00435d1b: the current room runtime (FUN_0043a460) and the
    // current location's RoomData (FUN_0043a3d0 on DAT_00668740); neither is null-tested.
    FRoomRuntime* const Runtime = static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
    RoomData* const Room = LookupClientLocationRoom(GetClientLocationCurrentRoom());

    // 0x00435d23..0x00435d53: the first setpoint (+0x164, the size re-read every pass) whose
    // name stricmp-equals SetpointName; none -> return.
    SetpointInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Room->Setpoints.size(); ++Index)
    {
        SetpointInfo const* const Setpoint = Room->Setpoints[Index];
        if (_stricmp(Setpoint->Name, SetpointName) == 0) // DACOM stricmp
        {
            Found = Setpoint;
            break;
        }
    }
    if (Found == nullptr)
        return;

    // 0x00435d55..0x00435d78: the frame copied out (position, then the nine-float orientation).
    Vector Pos = Found->Frame.Pos;
    Matrix Orient = Found->Frame.Orient;

    // 0x00435d7a..0x00435dbc: the room's player character (FUN_00449cd0); with one,
    // IDeformable slot 19 (position) and slot 20 (orientation) on its +0x30 instance, then its
    // slot 3 (+0x0c) with the position's Y.
    FCharacterObject* const Player = RoomControlPlayerCharacter(Runtime);
    if (Player == nullptr)
        return;
    void* const Instance = Player->Lod.Instance;
    DALib::Deform->SetPosePos(Instance, &Pos.x);
    DALib::Deform->SetPoseOri(Instance, &Orient);
    Player->SetFloat1C(Pos.y);
}
