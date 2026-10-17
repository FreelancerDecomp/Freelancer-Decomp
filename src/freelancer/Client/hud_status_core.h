// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "button_mover.h"
#include "event_handler.h"
#include "hud_element_list.h"
#include "hud_target.h"
#include "wincontrol_base.h"

#include <string>
#include <vector>

class CEqObj;
class INI_Reader;

// -------------------------------------------------------------------------------------------
// FUN_004d4b50 (cdecl) -- the shield hit points of a CEqObj, shared by HUD_Core (0x004d57fb),
// HUD_Status (0x004d97f7), the targetable-objects list (0x004ef74c, 0x004f134d) and 0x004758c7.
//
// *OutMax = *OutCur = 0. An object whose id is DAT_00674950 (CreateID("Ku07_02"), static
// initializer LAB_004d4b30) has no shield read at all. Otherwise, for every CEShield of the
// equipment manager (traverser mask 0x100):
//   Cap = GetMaxHitPoints(); Floor = float(ftol(OfflineThreshold * Cap));
//   *OutMax += max(Cap - Floor, 0)                 (a NaN difference is added as it is)
//   IsActive() and OutAnyActive -> *OutAnyActive = 1
//   IsFunctioning() -> *OutCur += max(GetHitPoints() - Floor, 0)
// OutAnyActive may be null (HUD_Core passes 0); the other two may not.
// -------------------------------------------------------------------------------------------
void HudSampleShieldHitPoints(CEqObj* Object, float* OutMax, float* OutCur, bool* OutAnyActive);

class HUD_Status;

// DAT_00674aa0 -- the one HUD_Status, written by its ctor (0x004da229) and cleared by its dtor
// (0x004da799). Readers do not test it for null.
HUD_Status* GetHudStatus();

// 0x004c870f / 0x004c8722 (HUD_Steer::Render) and the camera code at 0x00549e30..: the bytes
// +0x5b9 / +0x5ba of DAT_00674aa0, read without a null test, as retail does.
bool HudStatusIsTurretView();
bool HudStatusIsRearView();

// DAT_00674b04 -- the one HUD_Target (the target panel, W3-C). Its ctor FUN_004e08b0 writes it
// (0x004e0983) and its dtor clears it (0x004e0d29); HUD_Core::OnEvent, 0x004c19bf and nine other
// sites read it without a null test. Defined in hud_status_core.cpp; the HUD_Target ctor / dtor
// in hud_target_panel.cpp set and clear it.
extern HudTargetBase* G_HudTargetPanel;

// -------------------------------------------------------------------------------------------
// HUD_Core / [Core] -- Client\Space\HUD_Core.cpp. The gauge window under the crosshair: the
// power / shield / hull chip bars and the speed and thrust read-outs.
//
// RTTI HUD_Core : HUDItem @0, WinControl @0x38 (IDataViewOwner @0xb4), Event::Handler @0x364.
// Size 0x388. vftables: HUDItem face 0x005d7d0c (11 slots, override 0 FUN_004d4f00), WinControl
// face 0x005d7c54 (45 slots, overrides 0 LAB_004d5b60 thunk, 9 FUN_004d4fa0 Create, 13
// LAB_004d56d0 Draw), IDataViewOwner 0x005d7c44 (inherited), Handler 0x005d7c3c (LAB_004d5370).
// The HUDItem Render slot is not overridden: the window draws from the Win3D pass.
//
// Factory: the static HUDFACTORY<HUD_Core> DAT_00674954 ("Core"; XC LAB_004d4b00, ctor
// FUN_004d5a80, atexit LAB_004d4b20 -> FUN_004cf010), Create FUN_004d5af0.
// -------------------------------------------------------------------------------------------

class HUD_Core : public HUDItem, public WinControl, public EventHandler
{
public:
    // FUN_004d4db0 (thiscall, RET 4) -- the reader is not read.
    explicit HUD_Core(INI_Reader& Reader);
    // FUN_004d4f20 (scalar deleting FUN_004d4f00; WinControl-face thunk LAB_004d5b60)
    ~HUD_Core() override;

    // WinControl slot 9 FUN_004d4fa0 (RET 8) -- build the gauge window's controls.
    bool Create(Win3D* NewParent, void* Data) override;
    // WinControl slot 13 LAB_004d56d0
    void Draw() override;

    // Event::Handler slot 0 LAB_004d5370 (RET 4) -- command 0x6d toggles the status and target
    // panels (see .cpp).
    bool OnEvent(unsigned MsgId) override;

    // FUN_004d5410 (thiscall, RET 0xc; `this` is not read) -- one row of up to 16 chips at
    // design height DesignY, filled to Ratio.
    void DrawBars(float DesignY, unsigned long Color, float Ratio);

    WinControl* HealthBar;     // +0x368 (WinControl +0x330) "HealthBar"
    WinControl* ShieldBar;     // +0x36c (+0x334) "ShieldBar"
    WinControl* PowerBar;      // +0x370 (+0x338) "PowerBar"
    WinControl* VelocityText;  // +0x374 (+0x33c) "VelocityText"
    WinControl* ThrustText;    // +0x378 (+0x340) "ThrustText"
    int LastVelocity;          // +0x37c (+0x344) ctor -999
    int LastThrust;            // +0x380 (+0x348) ctor -1
    WinControl* Background2;   // +0x384 (+0x34c) "Background2"

private:
    HUD_Core(HUD_Core const&) = delete;
    HUD_Core& operator=(HUD_Core const&) = delete;
};

// -------------------------------------------------------------------------------------------
// HUD_Status / [Status] -- Client\Space\HUD_Status.cpp. The player-status panel.
//
// RTTI HUD_Status : HUD_Target_Base (HUDItem @0, WinControl @0x38, ButtonMover @0x364,
// Event::Handler @0x384) + ButtonScaler @0x4e0. Size 0x5ec. Create FUN_004de840 =
// operator new(0x5ec) + ctor FUN_004da170.
// vftables: HUDItem face 0x005d85bc (17 slots), WinControl face 0x005d8504 (45 slots),
// IDataViewOwner 0x005d84f4, ButtonMover 0x005d84e8, Handler 0x005d84e0, ButtonScaler
// 0x005d84d4.
//
// Retail overrides, and where the port stands on each:
//   HUDItem   0 FUN_004da610 dtor (ported, FUN_004da740), 5 LAB_004db0d0 SetTarget,
//             12 LAB_004dccf0, 13 LAB_004db120 (ported)
//   WinControl 9 FUN_004da930 Create, 13 FUN_004db2c0 Draw, 19 LAB_004db8f0 MouseEvent,
//             42 FUN_004db1c0 SetProperty (ported)
//             11 LAB_004db340 Update (ported; the caption builder FUN_004e8100 is HudBuildObjectCaption)
//             (4 HitTest is HUD_Target_Base's LAB_004e7d20, inherited)
//   ButtonMover  0 LAB_004dcf80, ButtonScaler 0 LAB_004dd080 (ported)
//   Handler   0 LAB_004dba50 OnEvent (ported)
// -------------------------------------------------------------------------------------------

// The heap caption at HUD_Status +0x5d0: text and capacity in wide chars, grown by FUN_004142a0
// (the same two-word head Font::String has). ctor 0x004da1cf / 0x004da1d5 zero both words;
// the dtor 0x004da7e6..0x004da803 frees the text when there is one and zeroes both again.
// The two words are FHudWideText (hud_target.h) -- the one type HudBuildObjectCaption fills;
// this adds only the owner's ctor / dtor.
struct FHudStatusCaption : FHudWideText
{
    FHudStatusCaption();
    ~FHudStatusCaption();

private:
    FHudStatusCaption(FHudStatusCaption const&) = delete;
    FHudStatusCaption& operator=(FHudStatusCaption const&) = delete;
};

// One row of the status panel's weapon list (retail element 0x24 bytes, FUN_004dd9e0).
struct FHudStatusWeapon
{
    unsigned short SubId;   // +0x00 CEquip +0x08
    int AmmoCount;          // +0x04 CELauncher::GetAmmoCount (slot +0xa0)
    int IdsName;            // +0x08 the launcher archetype's ids_name (Root +0x14)
    unsigned char bActive;  // +0x0c CEquip::IsActive (slot +0x04)
    std::string Hardpoint;  // +0x10 EquipDesc +0x08 (VC6 std::string, 0x10 bytes)
    int SortKey;            // +0x20 FUN_004dd730(Hardpoint): the DAT_00615240 row
};

// One entry of the hardpoint -> hp-type-row cache (retail element 0x14 bytes, FUN_004dd730).
struct FHudStatusHpRow
{
    std::string Hardpoint;  // +0x00 (VC6 std::string)
    int Row;                // +0x10
};

class HUD_Status : public HudTargetBase, public ButtonScaler
{
public:
    // FUN_004da170 (thiscall, RET 4) -- the reader is not read.
    explicit HUD_Status(INI_Reader& Reader);
    // FUN_004da740 (scalar deleting FUN_004da610)
    ~HUD_Status() override;

    // HUDItem slot 5 LAB_004db0d0 (RET 4) -- HUDItem::SetTarget, the view object, and the
    // weapon groups' ship (DAT_00674c0c).
    void SetTarget(IObjRW* NewTarget) override;
    // HUDItem slot 12 LAB_004dccf0 -- open (mover, then scaler) or close (scaler, then mover).
    void Slot12() override;
    // HUDItem slot 13 LAB_004db120 (RET 4) -- SetProperty(1, bHide, 0) on the weapon rows and
    // the scroll bar (while Field5a0), then on ViewPanel and CloseButton.
    void Slot13(bool bHide) override;

    // WinControl slot 19 LAB_004db8f0 (RET 4) -- a right click (-1, 2) on a sub-target toggles
    // that launcher.
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // WinControl slot 42 FUN_004db1c0 (RET 0xc) -- close button, weapon-row clicks, the scroll
    // bar; everything goes on to HUD_Target_Base's FUN_004e7950 except the close button.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // ButtonMover slot 0 LAB_004dcf80 / ButtonScaler slot 0 LAB_004dd080 -- the second and the
    // last stage of Slot12's open / close.
    void OnMoveFinished() override;
    void OnScaleFinished() override;

    // FUN_004db930 (thiscall, RET 4) -- toggle launcher SubId of the target (command 0x19), aim
    // it when it is an active gun, then store the current weapon group.
    void ToggleLauncher(unsigned short SubId);

    // WinControl slot 9 FUN_004da930 (RET 8) -- build the panel's controls.
    bool Create(Win3D* NewParent, void* Data) override;
    // WinControl slot 13 FUN_004db2c0
    void Draw() override;

    // FUN_004de0a0 (thiscall) -- rebuild Weapons, clamp WeaponTopRow and colour the visible
    // rows (the active launcher's colour, pulsed under the cursor). Callers: Draw 0x004db326.
    void RefreshWeaponRows();

    // WinControl slot 11 LAB_004db340 (this = the WinControl face) -- the session-flag and
    // mouse-flight latches, the special-mode captions, HUD_Target_Base's update, then once a
    // second the ship-state bytes, the repair / battery counts, the loot button and the rows.
    void Update() override;

    // FUN_004dca20 (thiscall) -- show / hide and word the two special-mode captions for the
    // turret view, the rear view, mouse flight and the tradelane. Callers: Update 0x004db397 /
    // 0x004db3b6 / 0x004db54f, OnEvent.
    void UpdateSpecialModeCaptions();

    // FUN_004de2f0 (thiscall) -- the nanobot and shield-battery counts of the player's
    // DAT_00672960 list, capped at 999, into RepairCountText / BatteryCountText. Callers:
    // Update 0x004db85b, OnEvent.
    void UpdateRepairBatteryCounts();

    // FUN_004de3a0 (thiscall; this is not read) -- true when the player ship, out of cruise
    // and of a tradelane with behaviour byte +0x168 set, has loot (bucket 7) within 1000 m
    // that it has cargo room for. Caller: Update 0x004db862.
    bool IsLootInReach();

    // FUN_004ddeb0 (thiscall) -- RebuildWeaponList, clamp WeaponTopRow, fill and show up to 10
    // rows from it, size the scroll bar, hide the rest. Callers: Update 0x004db88f, OnEvent.
    void ShowWeaponRows();

    // Event::Handler slot 0 LAB_004dba50 (this = the handler face at +0x384) -- the status
    // panel's commands: afterburner, auto-level / avoidance, the panel and list toggles, fire
    // weapon 1..10, select / fire / store weapon groups 1..6, missiles / torpedoes / cruise
    // disruptors, mines, countermeasures, nanobots, batteries and tractor-all. False while
    // there is no server, the HUD may not pick, or the player is not a ship; true otherwise
    // for every id of its switch.
    bool OnEvent(unsigned MsgId) override;

    // FUN_004dd4e0 (thiscall, RET 4) -- fire the player's seeker-munition guns of one kind in a
    // single slot_34 call at the targetable-objects aim point: Mode 0 missiles (neither of the
    // others), 1 torpedoes (mounted on an hp of type 0x0a / 0x23 / 0x24), 2 cruise disruptors.
    // Callers: OnEvent 0xbc / 0xc0 / 0xc1.
    void FireSeekerGuns(int Mode);

    // FUN_004dcc60 (thiscall, RET 4) -- fire weapon group Group at the aim point: every active
    // launcher (null id list) when Group is ActiveWeaponGroup, else the group's launchers.
    // Callers: OnEvent 0xac..0xb0.
    void FireWeaponGroup(int Group);

    // FUN_004de4c0 (thiscall; this is not read) -- every loot within 1000 m, ordered by value
    // (volume 0 first, then nanobots / batteries, then by good price), handed to the ship's
    // tractor. Caller: OnEvent 0x6f.
    void TractorInAllLoot();

    // FUN_004dd9e0 (thiscall) -- rebuild Weapons from the player ship's launchers (mask 0xe0),
    // ordered by SortKey, then by hardpoint name (DACOM stricmp). Callers: Create 0x004dafe2,
    // 0x004ddeb8, FUN_004de0a0 0x004de0ad.
    void RebuildWeaponList();

    // FUN_004dd730 (thiscall, RET 4) -- the DAT_00615240 row of the highest hp type the player
    // ship's archetype allows on Hardpoint; cached per name in HpRowCache. 0 when nothing fits.
    int FindHardpointRow(char const* Hardpoint);

    unsigned char Byte500;         // +0x500 ctor 1
    unsigned char Pad501[3];
    // +0x504 ctor 0: the weapon group OnEvent 0x91..0x96 selected last. Firing that group
    // (FireWeaponGroup, OnEvent 0xab) fires every active launcher instead of the group's list.
    int ActiveWeaponGroup;
    WinControl* WeaponText[10];        // +0x508 "WeaponText%d" (Create)
    WinControl* WeaponNumberText[10];  // +0x530 "WeaponNumberText%d"
    WinControl* AmmoText[10];          // +0x558 "AmmoText%d"
    WinControl* CloseButton;       // +0x580 "StatusCloseButton"
    unsigned char Byte584;         // +0x584 ctor 0
    unsigned char Byte585;         // +0x585 ctor 0
    unsigned char Byte586;         // +0x586 ctor 0
    unsigned char Pad587;
    float Field588;                // +0x588 ctor 1.0f (DAT_005d8484)
    std::vector<FHudStatusWeapon> Weapons; // +0x58c (retail VC6 vector, 0x10 bytes)
    WinControl* MinimizedFrame;    // +0x59c "StatusMinimizedFrame"
    int Field5a0;                  // +0x5a0 ctor 1 -- Slot13 touches the weapon rows while set
    int Field5a4;                  // +0x5a4 ctor 1
    WinControl* RepairButton;      // +0x5a8 "HudRepairButton"
    WinControl* RepairCountText;   // +0x5ac "RepairCountText"
    WinControl* UseBatteryButton;  // +0x5b0 "UseBatteryButton"
    WinControl* BatteryCountText;  // +0x5b4 "BatteryCountText"
    unsigned char Byte5b8;         // +0x5b8 ctor 0
    // +0x5b9: the turret view is up. Flipped by the space state's command switch (0x0054a655),
    // not by OnEvent (its switch stops at 0xc1). Read by HUD_Steer::Render 0x004c870f,
    // client_gameobj, UpdateSpecialModeCaptions and OnEvent 0xb7..0xba.
    unsigned char bTurretView;
    // +0x5ba: the rear view is up -- the key-down byte of the command record at 0x0054a70b,
    // cleared by command 0x39 at 0x0054a752 (the same switch).
    unsigned char bRearView;
    unsigned char Pad5bb;
    WinControl* SpecialModeText;       // +0x5bc "SpecialModeText"
    WinControl* SpecialModeTextCancel; // +0x5c0 "SpecialModeTextCancel"
    UiScroll* WeaponListScroll;    // +0x5c4 "WeaponListScroll"
    int WeaponTopRow;              // +0x5c8 ctor 0 -- first Weapons row shown (FUN_004de0a0)
    unsigned char Byte5cc;         // +0x5cc ctor 1
    unsigned char Byte5cd;         // +0x5cd ctor 0
    unsigned char Pad5ce[2];
    FHudStatusCaption Caption;     // +0x5d0 filled by FUN_004e8100 (Update 0x004db4b2)
    WinControl* LootButton;        // +0x5d8 "LootButton"
    std::vector<FHudStatusHpRow> HpRowCache; // +0x5dc (retail VC6 vector)

private:
    HUD_Status(HUD_Status const&) = delete;
    HUD_Status& operator=(HUD_Status const&) = delete;
};
