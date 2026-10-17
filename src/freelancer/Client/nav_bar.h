// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NavBar (RTTI .?AUNavBar@@ : WinControl, ButtonMover, ButtonScaler;
// E:\FL\Scratch\Source\Client\GF\NavBar.cpp) -- the base-side hotspot bar: the room-switch
// buttons along the bottom ("ButtonControl1..9" children of the loaded base UI), the service
// tabs above them ("RoomControl1..9": news, missions, repair, dealers, launch), the maneuver frame
// and tab meshes, and the slide / scale animation that opens NN_News, NN_MissionVendor,
// NN_Repair and NN_ShipTrader out of a tab.
//
// Factory WINFACTORY<NavBar> DAT_006687b4 "NavBar" (static init LAB_0043d6b0 -> FUN_004429a0,
// atexit LAB_0043d6d0 -> FUN_00579380). operator new 0x3f8, ctor FUN_0043d6e0, dtor
// FUN_0043da70, scalar deleting dtor FUN_0043d960.
//
// vftable 0x005cb584 (47 slots) -- WinControl's 0x005e5e44 with: 0 FUN_0043d960, 9 Create
// LAB_0043dc40, 11 Update LAB_0043e700, 13 Draw FUN_0043e6c0, 29 LAB_0043efd0 (FILE* save),
// 42 SetProperty LAB_0043ee80, and two new slots: 45 LAB_0043dfe0 (OnLocationEntered) and
// 46 FUN_0053e4a0 (RET 4). IDataViewOwner +0x7c keeps WinControl's 0x005cb574 face.
// ButtonMover +0x32c (0x005cb55c: 0 LAB_004408b0, 1 LAB_0043d8a0) and ButtonScaler +0x34c
// (0x005cb568: 0 LAB_00440560, 1 LAB_0043d8a0).
//
// Retail layout (the ButtonMover / ButtonScaler bases hold std::vectors in the port, so the
// member offsets below are retail's, not this class's):
//   +0x36c std::string VirtualRoom        +0x3c8 VirtualRoomLocation
//   +0x37c std::list<FNavBarButton>       +0x3cc bScriptRunning
//   +0x388 std::map<std::string, char*>   +0x3d0 LayoutDelay
//   +0x39c DefaultMesh                    +0x3d4 bRepairRedirect   +0x3d5 bReturnToDesk
//   +0x3a0 / +0x3ac std::list<std::string> +0x3d8 ManeuverFrame   +0x3dc ManeuverTab
//   +0x3b8 Current                        +0x3e0 PendingCharacter  +0x3e4 bCharacterPending
//   +0x3bc bFadingOut  +0x3c0 FadeTimer   +0x3e8 ScaledBackground  +0x3ec ClosingButton
//   +0x3c4 bFadingIn   +0x3c5 bVirtualRoomPending  +0x3f0 QueuedClose  +0x3f4 QueuedOpen

#include "button_mover.h"
#include "wincontrol_base.h"

#include <list>
#include <map>
#include <stdio.h>
#include <string>

struct RoomButtonInfo;
struct FCharacterObject;

// Retail 0x34-byte list element (ctor FUN_0043fd00).
struct FNavBarButton
{
    // FUN_0043fd00 -- everything 0 except bRoomSwitch = 1; Position is left unwritten.
    FNavBarButton();

    unsigned char bDisabled;         // +0x00 ctor 0
    unsigned char bRoomSwitch;       // +0x01 ctor 1: a room button ("ButtonControlN"), else a
                                     // service tab ("RoomControlN")
    std::string Name;                // +0x04 hotspot name, the IniValues key
    char const* IdsName;             // +0x14 the IDS_HOTSPOT_* name looked up in DAT_00610c80
    WinControl* Control;             // +0x18 the RoomControlN / ButtonControlN child
    RoomButtonInfo const* Hotspot;   // +0x1c the RoomData hotspot, null for the launch tab
    unsigned char bOpen;             // +0x20 its service window is open (slid out)
    float Position[3];               // +0x24 the laid-out offset (FUN_0043e210)
    unsigned Type;                   // +0x30 a location id (room buttons) or hotspot behaviour
};

class NavBar : public WinControl, public ButtonMover, public ButtonScaler
{
public:
    // FUN_0043d6e0
    NavBar();
    // FUN_0043da70
    ~NavBar() override;

    // --- Win3D / WinControl overrides ---
    // slot 9 LAB_0043dc40 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0043e700
    void Update() override;
    // slot 13 FUN_0043e6c0
    void Draw() override;
    // slot 29 LAB_0043efd0 (RET 4) -- the [NavBar] section of IniValues written to File.
    void Slot29(FILE* File) override;
    // slot 42 LAB_0043ee80 (RET 0xc) -- only Msg 0x26 (a button clicked, A = the control).
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // --- new slots ---
    // slot 45 (+0xb4) LAB_0043dfe0 -- rebuild the buttons for the room just entered.
    virtual void OnLocationEntered();
    // slot 46 (+0xb8) FUN_0053e4a0 -- RET 4.
    virtual void Slot46(unsigned A);

    // --- ButtonMover / ButtonScaler ---
    // LAB_004408b0 (this = the +0x32c face)
    void OnMoveFinished() override;
    // LAB_00440560 (this = the +0x34c face)
    void OnScaleFinished() override;

    // FUN_0043e050 -- per button enabled state and room highlight, then FUN_0043e4c0 and
    // FUN_0043e210(1).
    void Refresh();
    // FUN_0043e210 (RET 4) -- lay the service tabs out above the selected room button.
    void LayoutTabs(bool bAnimate);
    // FUN_0043e4c0 -- lay the room buttons out and pick the maneuver frame mesh.
    void LayoutRoomButtons();
    // FUN_0043e570 (RET 4) -- the repair tab enabled only while the ship needs repair
    // (FUN_004b4510) and bForce (force disabled) is clear.
    void RefreshRepairTab(bool bForce);
    // FUN_0043e600 -- SetProperty(0x4e) on NN_Repair, NN_ShipTrader, NN_News, NN_MissionVendor.
    static void NotifyServiceWindows();
    // FUN_0043f060 (RET 8) -- IniValues[Name] (a new 0x80-byte buffer on first use) = Value.
    bool SetIniValue(char const* Name, char const* Value);
    // FUN_0043f2e0 -- rebuild the button list from the current room's hotspots.
    void Rebuild();
    // FUN_0043f8e0 (RET 0x14) -- push a button onto the list, bound to the next free
    // RoomControl / ButtonControl child; null when no such child exists.
    FNavBarButton* AddButton(unsigned Type, int Unused, unsigned char bRoomSwitch,
                             char const* Name, char const* IdsName);
    // FUN_0043fd30 (RET 4) -- fade out to Button's room, or (second call) enter it.
    void SelectRoomButton(FNavBarButton* Button);
    // FUN_0043ffa0 -- the desk camera of Current's room, then fade back in.
    void CameraToDesk();
    // FUN_004400d0 (RET 4) -- slide a service tab out (queued while animating).
    void OpenService(FNavBarButton* Button);
    // FUN_00440260 (RET 4) -- shrink the service background (queued while animating).
    void CloseService(FNavBarButton* Button);
    // FUN_00440410 (thiscall, RET 4; this not read) -- the background scale of a service type.
    static float ServiceScale(unsigned Type);
    // FUN_00440460 (RET 8) -- the service type's background at Scale (FUN_00441ea0).
    void MakeServiceBackground(unsigned Type, float Scale);
    // FUN_00440ac0 (RET 4) -- a button's action.
    void OnButtonClicked(FNavBarButton* Button);
    // FUN_00441700 (RET 8) -- the state transition script From -> To, or To's camera move.
    void ActivateButton(FNavBarButton* From, FNavBarButton* To);
    // FUN_00441a40 (RET 4) -- swap which of the desk (type 6) / dealer (5, 7, 8) tabs is shown.
    void ToggleDealerTabs(bool bShowDesk);
    // FUN_00441b90 (RET 0xc) -- play Name as a Gf script sequence (ScriptDone at its stop).
    void StartScript(char const* Name, unsigned char bFade, unsigned char bLetterbox);
    // FUN_00441c40 (cdecl) -- the stop callback of StartScript and of the camera tracks.
    static void __cdecl ScriptDone(void* Bar, void* Unused, unsigned char bArg);
    // FUN_00441c70 (RET 8) -- a room character was clicked; true when the bar took the click.
    bool CharacterClicked(FCharacterObject* Character, int Role);
    // FUN_00441ea0 (RET 0xc) -- replace the "ScaledBackground" child and scale its instance.
    void MakeScaledBackground(float Scale, char const* Mesh, float const* Position);

    bool IsAnimating() const { return ButtonMover::IsActive() || ButtonScaler::IsActive(); }

    std::string VirtualRoom;                // +0x36c
    std::list<FNavBarButton> Buttons;       // +0x37c
    std::map<std::string, char*> IniValues; // +0x388 values: operator new(0x80)
    char const* DefaultMesh;                // +0x39c set by Create, not by the ctor
    std::list<std::string> RoomControlNames;   // +0x3a0
    std::list<std::string> ButtonControlNames; // +0x3ac
    FNavBarButton* Current;                 // +0x3b8
    unsigned char bFadingOut;               // +0x3bc
    float FadeTimer;                        // +0x3c0
    unsigned char bFadingIn;                // +0x3c4
    unsigned char bVirtualRoomPending;      // +0x3c5
    unsigned VirtualRoomLocation;           // +0x3c8
    unsigned char bScriptRunning;           // +0x3cc
    float LayoutDelay;                      // +0x3d0
    unsigned char bRepairRedirect;          // +0x3d4
    unsigned char bReturnToDesk;            // +0x3d5
    WinControl* ManeuverFrame;              // +0x3d8
    WinControl* ManeuverTab;                // +0x3dc
    FCharacterObject* PendingCharacter;     // +0x3e0
    unsigned char bCharacterPending;        // +0x3e4
    WinControl* ScaledBackground;           // +0x3e8
    FNavBarButton* ClosingButton;           // +0x3ec
    FNavBarButton* QueuedClose;             // +0x3f0
    FNavBarButton* QueuedOpen;              // +0x3f4

private:
    NavBar(NavBar const&) = delete;
    NavBar& operator=(NavBar const&) = delete;
};

// --- cdecl entry points (RoomControl / NavBar.cpp) ---
// FUN_00442060 -- the "NavBar" window, created when missing, told the room was entered (slot
// 45). Caller: the room load FUN_00439d80 (0x0043a05a).
void NavBarEnsure();
// FUN_004420a0 -- every "RoomControl1..9" / "ButtonControl1..9" window deleted (no Destroy).
void NavBarDeleteRoomControls();
// FUN_00442130 -- the "NavBar" window's background, the room controls and the bar itself
// destroyed. Callers: 0x004393aa, 0x0043b55b, 0x0043b7e9, 0x005739b0.
void NavBarDestroy();
// FUN_004421a0 -- the bar hands over to the dealer (FUN_00477bf0(Index, 0, 0)).
void NavBarOpenDealer(int Index);
// FUN_00442200 -- the first button of Type activated (unless a repair redirect runs).
void NavBarOpenButton(unsigned Type);
// FUN_004424f0 -- the open service tab closed (FUN_0043e600 first when bNotify).
void NavBarCloseService(bool bNotify);
// FUN_00442720 -- a NavBar and one of the four service windows exist.
bool NavBarHasServiceWindow();
// FUN_00442790 -- the NavBar's mover or scaler runs (no slot 2 test).
bool NavBarIsAnimating();
// FUN_004427e0 -- FUN_0043e570(bForce) on the bar.
void NavBarRefreshRepairTab(bool bForce);
// FUN_00442810 -- FUN_00441c70 on the bar; false without one.
bool NavBarCharacterClicked(FCharacterObject* Character, int Role);
// FUN_00442850 -- FUN_00441a40(0) on the bar.
void NavBarShowDealerTab();
