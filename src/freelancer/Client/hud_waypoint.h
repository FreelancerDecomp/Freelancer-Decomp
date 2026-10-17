// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// -------------------------------------------------------------------------------------------
// HUD_Waypoint / [Waypoint] -- the waypoint the player flies to: either one of the player's own
// waypoints (the list DAT_00672978, player_spawn.h) or a point of the current best path
// (DAT_00672950, best_path.h). The point is a client-side Solar ("waypoint" archetype, nickname
// "waypoint_target<n>") that the element spawns, targets through command 0x1c, spins and
// scales, and replaces with the next point once the ship gets within 150 m.
//
// RTTI HUD_Waypoint : HUDItem @0, WinControl @0x38 (IDataViewOwner @0xb4). Size 0x390
// (HUDFACTORY Create FUN_004f4da0 = operator new(0x390) + FUN_004f3af0).
// vftables:
//   HUDItem face 0x005d9784 -- overrides 0 FUN_004f3cb0 (scalar deleting dtor around
//     FUN_004f3cd0) and 6 LAB_004f3de0 Render.
//   WinControl face 0x005d96cc -- overrides 0 LAB_004f4e10 (thunk, ECX -= 0x38), 9 LAB_004f3d70
//     Create (shared with HUD_DamageIndicator), 11 LAB_004f4c50 Update, 12 FUN_004d6140
//     UpdateInactive (ICF with HUD_CruiseProgress::UpdateInactive).
//
// Factory: the static HUDFACTORY<HUD_Waypoint> DAT_00674b88 ("Waypoint"; XC entry
// LAB_004f3ac0, ctor FUN_004f4d30, atexit LAB_004f3ae0 -> FUN_004cf010), vftable 0x005d97e0.
//
// DAT_00674bc8 is the live instance (ctor 0x004f3b91, dtor 0x004f3d16). It is read by
// FUN_004f4cd0, HUD_Comm's FUN_004cef30 (0x004cefc0) and the other getters' callers.
//
// Named gaps (see the .cpp):
//   * best-path waypoints: the list is read faithfully, but its writers are not ported, and
//     the record resolvers FUN_004c5110 / FUN_004c52d0 are holes (best_path.h).
// -------------------------------------------------------------------------------------------

#include "hud_element_list.h"
#include "wincontrol_base.h"

#include "common_goods.h" // BaseWatcher
#include "common_cobject.h" // CEqObj::CreateParms (EqObjCreateParmsDefaults)

class INI_Reader;
class Vector;
struct IObjRW;

class HUD_Waypoint : public HUDItem, public WinControl
{
public:
    // FUN_004f3af0 (thiscall, RET 4) -- the reader is not read.
    explicit HUD_Waypoint(INI_Reader& Reader);
    // FUN_004f3cd0 (scalar deleting FUN_004f3cb0; WinControl-face thunk LAB_004f4e10)
    ~HUD_Waypoint() override;

    // HUDItem slot 6 LAB_004f3de0 (RET 8) -- keep, advance or drop the waypoint, spin it.
    void Render(FSpaceCamera* Camera, float DeltaSeconds) override;

    // WinControl slot 9 LAB_004f3d70 (RET 8) -- SetOffset(0, 0, -1), SetPosition(Position),
    // Win3D::Create.
    bool Create(Win3D* NewParent, void* Data) override;
    // WinControl slot 11 LAB_004f4c50 -- shown only with the cursor drawn, the HUD options on
    // and a player ship; the child walk only then.
    void Update() override;
    // WinControl slot 12 FUN_004d6140 -- the show half of Update, then Win3D's inactive walk.
    void UpdateInactive() override;

    // FUN_004f42e0 (thiscall, RET 0x10) -- make Pos the waypoint: bUser / Index / Id say which
    // list entry it is. The same entry at (within 2 m) the same place only re-targets it;
    // otherwise the old waypoint Solar is released and a new one is spawned and targeted.
    // Callers: Render 0x004f4116, HUD_Comm 0x004cefc6 (FUN_004cef30), 0x004cef1d,
    // 0x004d0062, 0x004d010a.
    void SetWaypoint(Vector const* Pos, bool bUser, int Index, int Id);
    // FUN_004f42a0 (thiscall, RET 8) -- with a live waypoint: its list and index, true.
    // Caller 0x00475a2e.
    bool GetWaypointEntry(unsigned char* OutUser, int* OutIndex);
    // FUN_004f4b90 (thiscall, RET 4) -- a live waypoint within 2 m of Pos on every axis.
    // Callers: HUD_Comm 0x004d4171, 0x004d41dd, 0x004d4245, 0x004d429e.
    bool IsWaypointAt(Vector const* Pos);
    // FUN_004f4ca0 -- FLD [this+0x37c]. Caller 0x00548347.
    float GetScale() const;
    // FUN_004f4cb0 -- the waypoint Solar's IObjRW, or null. Callers 0x004c83c6, 0x004c83ee.
    IObjRW* GetWaypointObject() const;
    // FUN_004f4d20 -- MOV AL,[this+0x38c]. Caller 0x0054833a.
    bool IsActive() const;

    BaseWatcher Waypoint;          // +0x364 the waypoint Solar (its Watchable, IObjRW + 8)
    int SpawnCounter;              // +0x36c -1; "waypoint_target%d" uses it, then ++
    float SpinAngle;               // +0x370 degrees, += 250 * Dt in Render
    unsigned char bUser;           // +0x374 1: a player waypoint, 0: a best-path point
    unsigned char Pad375[3];
    int Index;                     // +0x378 the entry's index in its list
    float Scale;                   // +0x37c 1.0; 0 inside 15 m, Dist / 100 inside 100 m
    unsigned char bReached;        // +0x380 the "objective_reached" latch
    unsigned char Pad381[3];
    int Id;                        // +0x384 -1; the player waypoint's AutoId
    int Field388;                  // +0x388 0; not read in this file
    unsigned char bActive;         // +0x38c the waypoint is targeted
    unsigned char Pad38d[3];

private:
    HUD_Waypoint(HUD_Waypoint const&) = delete;
    HUD_Waypoint& operator=(HUD_Waypoint const&) = delete;
};

// DAT_00674bc8 -- the live HUD_Waypoint, or null.
HUD_Waypoint* GetHudWaypoint();

// FUN_004f4cd0 (cdecl) -- the player's waypoint list was emptied: with a live, active waypoint
// and a player ship, clear the ship's target (IObjAffect::set_target(0, SUBOBJ_ID_NONE,
// player connection)). Callers: FUN_004c46c0 (tail JMP, player_spawn.cpp) and 0x004c4aaa.
void HudWaypointOnPlayerWaypointsCleared();

// FUN_004c83c0 (cdecl) -- 1 when the waypoint has a Solar, else 0 (NEG / SBB / NEG on
// FUN_004f4cb0; DAT_00674bc8 is not tested). Caller: the space draw 0x0051c4f1.
int HudWaypointGetObjectCount();
// FUN_004c83e0 (cdecl) -- object Index of that list: the waypoint Solar for 0, null otherwise.
// Caller: the space draw 0x0051c501.
IObjRW* HudWaypointGetObject(int ObjectIndex);

// FUN_004f4930 (thiscall, returns this) -- freelancer.exe's own compiled default constructor of
// CEqObj::CreateParms (0xb8 bytes in retail): the CSimple part (+0x0..+0x8 = 0, +0xc = -1.0f,
// +0x10..+0x18 = 0, FUN_004215b0 identity at +0x1c, +0x40..+0x54 = 0), +0x58 / +0x5c = 0,
// +0x60 = -1, +0x64..+0x70 = 0, +0x94 / +0x98 = 0, the +0x9c equipment vector empty and the
// +0xac collision-group list with a fresh self-linked head. +0x74..+0x90 are not written.
// The port runs it on a freshly constructed Parms: the two containers are the port's std
// containers, which their own constructors leave empty, as retail's inline construction does.
// Callers: HUD_Waypoint::SetWaypoint 0x004f44d3 and the multiplayer CREATESOLAR build
// 0x0053ad98 (FUN_0053ac10).
void EqObjCreateParmsDefaults(CEqObj::CreateParms& Parms);

// FUN_004cef30 (cdecl) -- make best-path point Index the waypoint: target the object its
// record names, else put the waypoint at the record's position. Retail keeps it with HUD_Comm
// (0x004cef30); Render calls it with 0 when a best-path waypoint is lost.
void HudWaypointSelectBestPath(int Index);

// FUN_004ceeb0 (cdecl) -- make the player's waypoint Index the waypoint: a waypoint on an object
// sends "select target" (0x1c) for it; otherwise HUD_Waypoint::SetWaypoint(&Pos, true, Index,
// AutoId). Retail keeps it with HUD_Comm (0x004ceeb0). Callers: 0x00494017,
// HUD_Targetable_Objects::PickUnderCursor 0x004edbf7 / 0x004ede8a.
void HudWaypointSelectPlayerWaypoint(int Index);
