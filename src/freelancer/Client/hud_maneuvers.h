// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// -------------------------------------------------------------------------------------------
// HUD_Maneuvers / [Maneuvers] -- the maneuver bar at the top of the screen: four maneuver
// buttons from hud.ini (FreeFlight, Goto, Dock, Formation), the frame behind them, the tab under
// the selected one and the cruise button. Also the handler of the maneuver key commands
// (strafe, brake, cruise, engine kill, throttle, trail distance, cancel).
//
// RTTI HUD_Maneuvers : HUDItem @0, WinControl @0x38 (IDataViewOwner @0xb4), Event::Handler
// @0x364, IObjAffect::RequestCB @0x368. Size 0x3ec (HUDFACTORY Create FUN_004d9a00 is
// operator new(0x3ec) + FUN_004d75e0).
// vftables:
//   HUDItem face 0x005d8310 -- only slot 0 overridden (FUN_004d7a50, scalar deleting dtor
//     around FUN_004d7a70).
//   WinControl face 0x005d8344 -- overrides 0 LAB_004da0c0 (thunk, ECX -= 0x38), 4 LAB_004d9820
//     HitTest, 9 FUN_004d7b50 Create, 11 LAB_004d85c0 Update, 12 FUN_004d8570 UpdateInactive,
//     13 LAB_004d8690 Draw, 42 LAB_004d8da0 SetProperty.
//   Event::Handler 0x005d82fc -- slot 0 LAB_004d7f60 OnEvent.
//   IObjAffect::RequestCB 0x005d82f4 -- slot 0 LAB_004d9410 OnResult.
//
// Factory: the static HUDFACTORY<HUD_Maneuvers> DAT_00674a1c ("Maneuvers"; XC entry
// LAB_004d7570, ctor FUN_004d9990, atexit LAB_004d7590 -> FUN_004cf010), vftable 0x005d8470.
//
// DAT_00674a58 is the one instance (ctor 0x004d76bb, dtor 0x004d7aca). Outside this file it is
// read by 0x00463e9d (switch case 7 at 0x00463e8b, after FUN_00462e30: RequestManeuver(0x13, 0)
// when the player's behaviour is 0xa, formation; nn_group_control.cpp) and 0x004c8071 (FUN_004c7cb0,
// which passes it to 0x0053e4a0 -- `RET 4`).
// -------------------------------------------------------------------------------------------

#include "event_handler.h"
#include "hud_element_list.h"
#include "wincontrol_base.h"

#include "common_goods.h"   // BaseWatcher
#include "common_iface.h"   // IObjAffect::RequestCB
#include "common_tstring.h"

#include <vector>

class INI_Reader;
struct FButtonMontageTile; // nn_montage_draw.h

// FUN_004d9ca0 (thiscall on the table, RET 0xc) -- the first entry of a {name, value} table
// whose name is non-null and stricmp-equal to Name: *OutValue = its value, true; none of the
// first Count entries: false, *OutValue untouched. Callers: the HUD_Maneuvers ctor 0x004d77b2
// (table DAT_00612638, four entries) and 0x0043fb9b (the IDS_HOTSPOT table DAT_00610c80,
// 0x13 entries; not ported).
struct FHudNamedValue
{
    char const* Name;   // +0x00
    int Value;          // +0x04
};
bool HudLookupNamedValue(FHudNamedValue const* Table, char const* Name, int* OutValue,
                         unsigned Count);

// One [Maneuvers] "maneuver" line (0x148 bytes, vector element of HUD_Maneuvers +0x37c):
//   maneuver = <behaviour name>, <tooltip ids>, <ids>, <mesh>, <gray mesh>
struct FHudManeuver
{
    TString<32> Name;              // +0x000 value 0
    TString<128> Mesh;             // +0x024 value 3
    TString<128> GrayMesh;         // +0x0a8 value 4 (hovered / selected / unavailable)
    int ToolTipIds;                // +0x12c value 1 (ftol) -- the button's tooltip
    int Ids2;                      // +0x130 value 2 (ftol) -- not read in this file
    int CommandId;                 // +0x134 key command of the button (Dock 0x79, Goto 0x7a,
                                   //        Formation 0x7f, FreeFlight 0xc6; else 0)
    int BehaviorId;                // +0x138 BehaviorTypes::get_behavior_id(Name)
    unsigned char bSelected;       // +0x13c written by UpdateButtons, not read in this file
    unsigned char Pad13d[3];
    FButtonMontageTile* MontageTile;     // +0x140 FUN_0055a360(Mesh)
    FButtonMontageTile* GrayMontageTile; // +0x144 FUN_0055a360(GrayMesh)
};

class HUD_Maneuvers : public HUDItem,
                      public WinControl,
                      public EventHandler,
                      public IObjAffect::RequestCB
{
public:
    // FUN_004d75e0 (thiscall, RET 4) -- the "maneuver" lines of the reader's section.
    explicit HUD_Maneuvers(INI_Reader& Reader);
    // FUN_004d7a70 (scalar deleting FUN_004d7a50; WinControl-face thunk LAB_004da0c0)
    ~HUD_Maneuvers() override;

    // WinControl slot 4 LAB_004d9820 (RET 4) -- WinControl's hit test, else the tab's box.
    bool HitTest(float const* Ray) override;
    // WinControl slot 9 FUN_004d7b50 (RET 8) -- frame, four buttons, tab, cruise button, the
    // 24 key commands.
    bool Create(Win3D* NewParent, void* Data) override;
    // WinControl slot 11 LAB_004d85c0 -- the throttle ramp, then UpdateButtons or hide.
    void Update() override;
    // WinControl slot 12 FUN_004d8570 -- the show half, then the inactive child walk.
    void UpdateInactive() override;
    // WinControl slot 13 LAB_004d8690 -- with "autohide_maneuver_bar" only under the mouse.
    void Draw() override;
    // WinControl slot 42 LAB_004d8da0 (RET 0xc) -- 0x26 (a child button clicked).
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler slot 0 LAB_004d7f60 (RET 4).
    bool OnEvent(unsigned MsgId) override;

    // IObjAffect::RequestCB slot 0 LAB_004d9410 (RET 0xc) -- the formation request's answer.
    bool OnResult(int Param1, int Param2, int Param3) override;

    // FUN_004d8e20 (thiscall, RET 8) -- put the player ship on behaviour BehaviorId (a
    // directive through IBehaviorManager::set_current_directive) and tell the server
    // (IServer::SetManeuver). Callers: OnEvent 0x83 / 0xf, OnResult, ActivateManeuver and
    // 0x00463eef.
    bool RequestManeuver(int BehaviorId, bool bFlag);
    // FUN_004d9470 (thiscall, RET 4) -- a maneuver button or its key.
    void ActivateManeuver(FHudManeuver* Maneuver);
    // FUN_004d88f0 (thiscall) -- mesh, colour, selection and position of the four buttons.
    void UpdateButtons();
    // FUN_004d8bf0 (thiscall, RET 4) -- the tab and the cruise button for BehaviorId.
    void UpdateTabAndCruise(int BehaviorId);
    // FUN_004d95b0 (thiscall, RET 4) -- the frame mesh for Count buttons (-1: count them).
    void SetFrameButtonCount(int Count);
    // FUN_004d9750 (thiscall; `this` not read) -- the ship's target is a trade-lane ring (type
    // bit 0x80) whose shield is down.
    bool IsTargetTradeLaneDisrupted();

    WinControl* Buttons[4];              // +0x36c "ManeuverButton0..3"
    std::vector<FHudManeuver> Maneuvers; // +0x37c (VC6: +0x380 first, +0x384 last, +0x388 end)
    int FormationState;                  // +0x38c 0; 1 request sent, 2 / 3 OnResult
    BaseWatcher FormationTarget;         // +0x390
    float Field398[3];                   // +0x398 ctor {0.039, 0.039, 0}; not read here
    int Field3a4;                        // +0x3a4 ctor / ActivateManeuver -1; not read here
    int Field3a8;                        // +0x3a8 ctor / ActivateManeuver -1; not read here
    float TrailDistance;                 // +0x3ac 100; commands 0x81 / 0x82 in [20, 2000]
    float TrailField14;                  // +0x3b0 0; DirectiveTrailOp +0x14
    int FrameButtonCount;                // +0x3b4 SetFrameButtonCount's last count
    WinControl* Frame;                   // +0x3b8 "ManeuverFrame"
    WinControl* Tab;                     // +0x3bc "ManeuverTab"
    int bTabMeshReset;                   // +0x3c0 1 once the tab carries CITY_maneuvertab1
    WinControl* CruiseButton;            // +0x3c4 "ManeuverCruiseButton"
    int Field3c8;                        // +0x3c8 ctor 0x13; not read here
    unsigned char Byte3cc;               // +0x3cc ctor 0; not read here
    unsigned char Byte3cd;               // +0x3cd ctor 0; not read here
    unsigned char Pad3ce[2];
    float FrameHalfExtents[3];           // +0x3d0
    float TabHalfExtents[3];             // +0x3dc
    float ThrottleRate;                  // +0x3e8 commands 0x43 / 0x44: +1 / -1 / 0

private:
    HUD_Maneuvers(HUD_Maneuvers const&) = delete;
    HUD_Maneuvers& operator=(HUD_Maneuvers const&) = delete;
};

// DAT_00674a58 -- the live HUD_Maneuvers, or null.
HUD_Maneuvers* GetHudManeuvers();
