// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// CharacterObject (RTTI .?AVCharacterObject@@, vtable 0x005cb930, operator new(0x3b0) in
// FUN_00435b80) -- E:\FL\Scratch\Source\Client\gf\ObjectDeformable.cpp (the FDUMP file name at
// .rdata 0x005cb964). The room object a person in a base is: a deformable body built from a
// Costume's bodyparts (DALib::Deform), up to eight accessory meshes hung on its hardpoints, a
// screen hotspot the mouse talks to, an idle script of its own (a Gf script sequence) and the
// behaviour script a click starts.
//
// Retail slots over RoomObject (0x005e14a4): 0 FUN_004440a0 (deleting dtor), 3 LAB_004449a0
// (SetFloat1C), 5 LAB_004449d0 (Update), 6 LAB_00444970 (GetEngineInstance), 7 LAB_00445150
// (DrawOpaque), 10..12 FUN_00445a90 / FUN_00445be0 / FUN_00445c10 (hotspot events).
//
// Built by the room (RoomControl FUN_00449e60 -> FUN_00435960 -> FUN_00435b80) for every
// character record the server sends, and by the Gf script spawn (FUN_0044e870, 0x0044ea12) for
// a DEFORMABLE entity no room character stands in for.
//
// The fields keep their retail offsets in comments; the port's RoomObject base is retail's size
// only in iterator-debug builds, and nothing reads a character by raw offset.

#include "common_csys.h"
#include "common_goods.h" // CharacterProperties, HardpointSummary, CharPlaceInfo
#include "gf_hotspot.h"
#include "room_control.h"

class DetailSwitchTable;
struct FGfScriptSequence;
struct ICamera;

// The per-part LOD switch table a character body draws with (0x124 bytes). CharacterObject
// keeps one at +0x30; the "pc" characters (CharacterProperties +0x1c == 1) all share the one at
// DAT_00668948 instead (static init 0x00443e40 zero-fills it; FUN_00443e90 releases it).
struct FCharacterLodSet
{
    void* Instance;                   // +0x000 the IDeformable instance
    int Counts[4];                    // +0x004 the LOD count of each bodypart (0 = no part)
    float Switch[4][16];              // +0x014 each LOD's switch distance
    DetailSwitchTable* Tables[4];     // +0x114 the bodypart's DetailSwitchTable
};

// FUN_00443b60's part table is IDeformable::Create's FDeformPartDesc / FDeformLodDesc
// (engine/engbase.h): sixteen LOD slots per part, whose Fraction comes back filled.

// The description FUN_00435b80 builds on the stack and CharacterObject's ctor reads (0xa0 bytes;
// ctor FUN_00443a90, dtor FUN_00443b10).
struct FCharacterDesc
{
    // FUN_00443a90 -- CharacterProperties constructed, Placement 0, PlacementIndex -1,
    // bScriptSpatial 0, Pos 0, Orient identity.
    FCharacterDesc();
    // FUN_00443b10 -- CharacterProperties::destroy, then its destructor.
    ~FCharacterDesc();

    CharacterProperties Props;     // +0x00
    CharPlaceInfo* Placement;      // +0x64 the room placement the character stands on, or null
    int PlacementIndex;            // +0x68 its index in the room's +0x504 vector, -1 = none
    unsigned char bScriptSpatial;  // +0x6c handed to the own script's sequence +0x08
    Vector Pos;                    // +0x70
    Matrix Orient;                 // +0x7c

private:
    FCharacterDesc(FCharacterDesc const&) = delete;
    FCharacterDesc& operator=(FCharacterDesc const&) = delete;
};

struct FCharacterObject;

// The Character's hotspot (vftable 0x005cb9ec, operator new(0x28) at 0x00444764): the screen
// rectangle of its bounding sphere.
class FCharacterHotspot : public FHotspot
{
public:
    // 0x00444781..0x00444791: FUN_00555db0, then +0x20 = 0 and +0x24 = 0.
    FCharacterHotspot();
    // FUN_00444960 (FUN_00444940 the deleting form): the Hotspot destructor.
    ~FCharacterHotspot() override;

    // FUN_00555f60 (thiscall, RET 4) -- the rectangle around the owner's engine instance
    // (Engine slot 36) projected with radius 1.0: x - 0.6r, y - r, 1.2r wide, 2r high, each
    // floor(v + 0.5); +0x24 = 0. Not projected: +0x24 = 1 and the rectangle zero.
    void UpdateScreenRect(ICamera* Camera);

    FCharacterObject* Character;  // +0x20
    unsigned char bOffscreen;     // +0x24
};

struct FCharacterObject : FRoomObject
{
    // FUN_00443ed0 (thiscall, RET 8) -- members built, the handles -1, then Load(Desc).
    FCharacterObject(FRoomRuntime* InOwner, FCharacterDesc const& Desc);
    // FUN_004440c0 (FUN_004440a0 is the scalar deleting destructor).
    ~FCharacterObject() override;

    void SetFloat1C(float Value) override;          // 3  LAB_004449a0
    void Update(double DeltaSeconds) override;      // 5  LAB_004449d0
    void* GetEngineInstance() override;             // 6  LAB_00444970
    void DrawOpaque(ICamera* Camera) override;      // 7  LAB_00445150
    void Slot10(unsigned long Cookie, int const* Point) override; // 10 FUN_00445a90 (enter)
    void Slot11(unsigned long Cookie, int const* Point) override; // 11 FUN_00445be0 (leave)
    void Slot12(unsigned long Cookie, int const* Point) override; // 12 FUN_00445c10 (click)

    // FUN_00445280 (thiscall) -- restart the own script: a live sequence is restarted (or
    // played when it never was), else the idle script (+0x1ac) is created and played.
    void RestartOwnScript();
    // FUN_00445340 (thiscall, RET 8) -- a new Gf sequence playing ScriptName for this
    // character, framed by the placement; null for no name, FDUMP when none can be allocated.
    FGfScriptSequence* CreateOwnScript(char const* ScriptName, unsigned char bSpatial);
    // FUN_00445580 (thiscall, RET 8) -- the own script becomes ScriptName (unless it already
    // is): the old one destroyed, the new one created and, while not taken over, played.
    void SetOwnScript(char const* ScriptName, unsigned char bSpatial);
    // FUN_00445a60 (thiscall) -- the hotspot answers the mouse only with a behaviour or a
    // role.
    void UpdateHotspotEnabled();

    // FUN_00445540 (thiscall, RET 4) -- RoomObject +0x04 cleared; with bRelease the +0x360 /
    // +0x368 pair is cleared and the +0x364 name freed, otherwise +0x36c = 2. Caller:
    // Thorn_Deformable's slot 14 (0x004521f5) with the owning script's +0x55.
    void ResetScriptState(unsigned char bRelease);
    // FUN_004454e0 (thiscall, RET 4) -- a Gf script takes the character over: RoomObject +0x04
    // set (no more self-update); with bKeepOwnScript clear the character's own sequence (+0x360)
    // is destroyed through the Gf script manager (FUN_004568d0), +0x360 / +0x368 cleared, the
    // +0x364 name freed, and +0x36c = 1. Caller: the spawn's FUN_0044e870 (0x0044ea43) with the
    // owning script's +0x55.
    void TakeOverForScript(unsigned char bKeepOwnScript);
    // FUN_00445620 (thiscall, RET 4) -- the part named Name: a cached hit from Parts, else
    // CompoundInstanceFindFirstName on the engine instance, cached in the first unused entry
    // (entry 0 when all five are used). -1 when there is none.
    long FindPart(char const* PartName);
    // FUN_004456e0 / FUN_004456f0 (thiscall, RET 4) -- ++ / -- ActiveMotions unless Handle is -1.
    void AddMotionHandle(int Handle);
    void RemoveMotionHandle(int Handle);
    // FUN_00445700 / FUN_00445710 (thiscall, RET 4) -- ++ / -- ActiveAims unless Handle is -1.
    void AddAimHandle(void* Handle);
    void RemoveAimHandle(void* Handle);
    // FUN_00445720 (thiscall, RET 4) -- one of the six "$BGCS_*" context tokens rewritten into
    // the voice-message name voices.ini carries, from this character's costume gender (+0x178),
    // its room's base ([+0x08]+0x34) and its faction (+0x164). Returns the static buffer
    // DAT_00668848 on every exit (valid until the next call). Callers: FUN_004570a0 (0x00457129,
    // 0x00457162) with the speaking actor.
    char const* ResolveBgcsContextToken(char const* TemplateName) const;

    // One cached part lookup (retail +0x324, five of 0xc bytes). LAB_00444060 builds one,
    // FUN_00444080 tears it down.
    struct FPartCacheEntry
    {
        FPartCacheEntry();
        ~FPartCacheEntry();

        char* Name;          // +0x00 _strdup'd, freed with free()
        long Part;           // +0x04 -1 = not resolved
        unsigned char bUsed; // +0x08
    };

    static const int kPartCacheEntries = 5;
    static const int kMaxAccessories = 8;

    FCharacterLodSet Lod;              // +0x030 (Lod.Instance: the IDeformable instance)
    CharacterProperties Props;         // +0x154 the server's record (Props.VoiceId is +0x1b4)
    CharPlaceInfo* Placement;          // +0x1b8
    int PlacementIndex;                // +0x1bc -1 = not on a room placement
    int LodIds[4];                     // +0x1c0 FUN_00444f90's pick per part, -1 = none
    long AccessoryInstances[kMaxAccessories];      // +0x1d0 -1 = none
    HardpointSummary AccessoryHardpoints[kMaxAccessories]; // +0x1f0
    int AccessoryCount;                // +0x2f0
    FCharacterHotspot* Hotspot;        // +0x2f4
    unsigned char bHovered;            // +0x2f8 the mouse is over the hotspot (name tooltip)
    wchar_t* DisplayName;              // +0x2fc Props.NameIds as ids, or Props.NameWide
    int Role;                          // +0x300 0, 1 bartender, 2 trader, 3 shipdealer, 4 equipment
    HardpointSummary Hardpoints;       // +0x304 the body's hardpoints
    FPartCacheEntry Parts[kPartCacheEntries]; // +0x324
    FGfScriptSequence* OwnScript;      // +0x360 the idle script (FUN_00445340)
    char* OwnScriptName;               // +0x364 malloc'd
    unsigned char bOwnScriptPlaying;   // +0x368 FUN_00454f80 ran on +0x360
    int ScriptState;                   // +0x36c 0 own, 1 taken over (FUN_004454e0), 2 released
    int ActiveMotions;                 // +0x370 motion handles Thorn_Deformable holds
    int ActiveAims;                    // +0x374 aim handles Thorn_Deformable holds
    unsigned char bPoseDirty;          // +0x378 the pose moved: the next Update walks the mixer
    float LastPos[3];                  // +0x37c the engine instance's position last Update
    float LastOrient[9];               // +0x388 and its orientation
    unsigned char bHasLastPose;        // +0x3ac
    unsigned char bSharedDeform;       // +0x3ad Props +0x1c == 1: the body is DAT_00668948's

private:
    // FUN_00444290 (thiscall, RET 4) -- the record, role, body, accessories, hotspot, name and
    // own script.
    void Load(FCharacterDesc const& Desc);
    // FUN_00444f90 (thiscall, RET 4) -- LodIds from the camera distance.
    void PickLods(ICamera* Camera);

    FCharacterObject(FCharacterObject const&) = delete;
    FCharacterObject& operator=(FCharacterObject const&) = delete;
};

// FUN_00443e90 (cdecl) -- the shared "pc" body (DAT_00668948) destroyed (IDeformable slot 6)
// and its table cleared. Caller: the Gameflow base leave FUN_00558a50 (0x00558a71).
void ReleaseSharedCharacterLod();

// FUN_00435b80 (cdecl) -- a new CharacterObject for Owner from Props at Placement (index
// PlacementIndex) and Frame.
FCharacterObject* CreateCharacterObject(CharacterProperties const& Props, CharPlaceInfo* Placement,
                                        int PlacementIndex, FRoomRuntime* Owner,
                                        unsigned char bScriptSpatial, Csys const& Frame);

// FUN_00435dd0 (cdecl) -- play the behaviour BehaviorKey of Room's script behaviours for
// Character as a letterboxed Gf sequence (the behaviour's scripts, dialog texts and characters),
// named after Placement; Callback / CallbackA go to the sequence's stop callback.
void StartCharacterBehaviorScript(FCharacterObject* Character, FRoomRuntime* Room,
                                  CharPlaceInfo const* Placement, unsigned BehaviorKey,
                                  void(__cdecl* Callback)(void*, void*, unsigned char),
                                  void* CallbackA);

// FUN_00435cf0 (cdecl) -- the player character of the current room (FUN_00449cd0) put at the
// current room's setpoint SetpointName (stricmp over RoomData +0x164): IDeformable slots 19 / 20
// on its instance, then its slot 3 with the setpoint's height. Callers: NavBar 0x0043ea45 /
// 0x0043ea7c / 0x0043eab3 / 0x0043fff4 / 0x00440027 / 0x00440057.
void PlacePlayerAtSetpoint(char const* SetpointName);
