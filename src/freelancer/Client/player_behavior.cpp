// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_0054baf0 and the player behaviour command family at 0x004c7950 .. 0x004c7b8b.

#include "player_behavior.h"

#include "player_spawn.h"

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "game_options.h"
#include "globals.h"
#include "hud_status_core.h" // HudStatusIsTurretView / HudStatusIsRearView (+0x5b9 / +0x5ba)
#include "input_manager.h"
#include "menu_ui_init.h"
#include "nn_modal.h" // NnMainButtonHasActivePanel (FUN_0045a470)
#include "space_letterbox.h"
#include "win3d.h" // Win3DGetHover (FUN_0059daa0)

namespace
{
    // 0x0051eac1 and every other camera in the file use the same eight: a BaseWatcher stores a
    // Watchable*, which is the IObjRW plus eight.
    const int kWatchableOffset = 8;

    // .data:00673564. Retail keeps the clamped throttle here rather than on any object, and
    // FUN_004c7a00 both refreshes and returns it.
    float G_PlayerThrottle = 0.0f;

    // .data:0067354c. See the mouse-flight lock toggle section in the header.
    bool G_MouseFlightLocked = false;

    // UpdateMouseFlightDirection's own state, .data:00673550/554/555/558/55c.
    float G_MouseFlightHoldTimer = 0.0f;    // _DAT_00673550 -- seconds accumulated this hold
    bool G_MouseFlightEngaged = false;      // DAT_00673554 -- latched true once forwarded
    bool G_MouseFlightButtonTrigger = false;// DAT_00673555 -- OnMouseFlightRawMessage's one-shot
    float G_MouseFlightOriginX = 0.0f;      // _DAT_00673558 -- cursor X latched at timer==0
    float G_MouseFlightOriginY = 0.0f;      // _DAT_0067355c -- cursor Y latched at timer==0 (same
                                             // invert-aware reading as every other cursor Y below)

    // 0x004c7d90/0x004c7e5f/0x004c7f4c/0x004c88a9-equivalent: the hold threshold. 0.25s
    // (DAT_00612270, .rdata 0x3E800000).
    constexpr float kMouseFlightHoldThreshold = 0.25f;

    // The repeated `cVar4 = FUN_0045a470(); if ((cVar4=='\0' && DAT_0067354c) ||
    // DAT_00612270<=_DAT_00673550) && GetGameOption(INVERSE_MOUSE) then height-Y else Y` block --
    // identical at all four sites in FUN_004c7cb0 (FUN_0045a470 is NnMainButtonHasActivePanel).
    // Reading G_MouseFlightHoldTimer live (rather than taking it as a parameter) reproduces each
    // call site's own point-in-time value automatically.
    int MouseFlightAdjustedCursorY(int RawCursorY)
    {
        bool const bLockOrHoldReady =
            (!NnMainButtonHasActivePanel() && G_MouseFlightLocked) ||
            (G_MouseFlightHoldTimer >= kMouseFlightHoldThreshold);
        // 0x004c7ddb / 0x004c7e84 / 0x004c7f75: DAT_00610854 (the created surface height) minus
        // DAT_00616844.
        if (bLockOrHoldReady && GetGameOption(kGameOptInverseMouse) != 0)
            return static_cast<int>(G_RenderSurfaceHeight) - RawCursorY;
        return RawCursorY;
    }

    // The three lines every function in this family opens with. Retail spells them out at each
    // site; there is no shared helper in the binary, but the sequence is byte-identical.
    IBehaviorManager* PlayerBehaviorManager()
    {
        // 0x004c7950/0x004c7956: FUN_0054baf0's result goes straight into GetBehaviorManager,
        // null included -- Common's own entry test (062d4ba6 `TEST EDI,EDI / JNZ`) is what turns
        // a missing ship into a null manager.
        return GetBehaviorManager(static_cast<IObjRW*>(GetPlayerObjRW()));
    }

    // Shared preamble of FUN_004c7b90 / FUN_004c7be0: fetch the player's CEqObj directly (not
    // through GetBehaviorManager's vtable-slot-84 path) and validate it is a ship.
    //
    // 0x004c7b99: `ADD EAX,0xc` then a null test -- the only way that is zero after a proven
    // non-null pointer is EAX == -0xc, so -0xc is a distinguished "no object" sentinel some
    // ObjRW accessors return.
    // 0x004c7ba0: `MOV EAX,[EAX+0x4]` off the +0xc'd pointer reads [ObjRW+0x10], the CEqObj*.
    // 0x004c7ba7: [CEqObj+0x4c] & 0x503 == 0x503 is the ship class check (CObject::ObjectClass).
    CEqObj* GetPlayerShipEqObj()
    {
        void* const ObjRW = GetPlayerObjRW();
        if (ObjRW == nullptr || ObjRW == reinterpret_cast<void*>(-0xc))
            return nullptr;

        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(ObjRW) + 0x10);
        if (EqObj == nullptr)
            return nullptr;

        if ((static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;

        return EqObj;
    }

    // Shared tail of FUN_004c7b90 / FUN_004c7be0: the player's engine equipment, or null.
    // 0x004c7bbd/0x004c7c0d: `LEA ECX,[EAX+0xe4]` is CEqObj::EquipManager taken by address (an
    // embedded member, not a pointer), matching CShip::collect_force_components' own
    // `EquipManager.FindFirst(0x20000)` elsewhere in Common.
    CEEngine* GetPlayerEngineEquip()
    {
        CEqObj* const Ship = GetPlayerShipEqObj();
        if (Ship == nullptr)
            return nullptr;

        CEquip* const Eq = const_cast<CEquip*>(Ship->EquipManager.FindFirst(0x20000));
        return CEEngine::cast(Eq);
    }
}

void* GetPlayerObjRW()
{
    // 0x0054baf0: read DAT_00679744, and when it is set subtract eight. A null watchable yields a
    // null object rather than the address -8.
    void* const Watched = GetPlayerShipWatcher();
    if (Watched == nullptr)
        return nullptr;

    return static_cast<unsigned char*>(Watched) - kWatchableOffset;
}

void ResetPlayerBehaviorDirection()
{
    // 0x004c7965: a tail JMP, so the manager's own return -- void -- is this function's.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->reset_current_behavior_direction();
}

void SetPlayerStrafeBurst(int Dir)
{
    // 0x004c7a4a.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_slide_strafe_burst(static_cast<StrafeDir>(Dir));
}

void SetPlayerAfterburner(bool bOn)
{
    // 0x004c798a.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_afterburner(bOn);
}

void SetPlayerAutoLevel(bool bOn)
{
    // 0x004c79ba.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_auto_level(bOn);
}

void SetPlayerAutoAvoidance(bool bOn)
{
    // 0x004c79ea.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_auto_avoidance(bOn);
}

void SetPlayerBrakeReverse(bool bOn)
{
    // 0x004c7a7a.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_brake_reverse(bOn);
}

void SetPlayerCruise(bool bOn)
{
    // 0x004c7aaa.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_cruise(bOn);
}

void SetPlayerEngageEngine(bool bOn)
{
    // 0x004c7ada.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_engage_engine(bOn);
}

void SetPlayerThrottle(float Throttle)
{
    // 0x004c7afe: the argument is stored FIRST, unclamped, and only then overwritten if it is out
    // of range. Nothing observes the intermediate value, but the order is retail's.
    G_PlayerThrottle = Throttle;

    // 0x004c7af8: the low comparison is against a DOUBLE -1.0 at .rdata:005d5060, while the high
    // one at 0x004c7b1a is against a FLOAT 1.0 at .rdata:005c75dc. Widening the float argument to
    // compare it against the double is what the FPU does either way, so the asymmetry costs
    // nothing -- it is just how the two constants were emitted.
    if (static_cast<double>(Throttle) < -1.0)
        G_PlayerThrottle = -1.0f;               // 0x004c7b0a, 0xbf800000
    else if (Throttle > 1.0f)
        G_PlayerThrottle = 1.0f;                // 0x004c7b27, 0x3f800000

    // 0x004c7b58.
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_throttle(G_PlayerThrottle);

    // 0x004c7b5e..0x004c7b6f: FCOMP 0.0f (DAT_005c7474), `TEST AH,0x44 / JP`. Equal leaves AH&0x44
    // = 0x40 (odd parity, PF=0): no jump, fall through to the cruise drop. Greater / less (0) and
    // unordered (0x44) have even parity and jump past it. A zero throttle cancels cruise.
    if (G_PlayerThrottle == 0.0f)
    {
        // 0x004c7b71: retail re-fetches the manager rather than reusing the one above.
        IBehaviorManager* const Again = PlayerBehaviorManager();
        if (Again != nullptr)
            Again->update_current_behavior_cruise(false);   // 0x004c7b80, PUSH 0
    }
}

float GetLastPlayerThrottle()
{
    // FUN_004c7a00 (cdecl, float on the FPU). 0x004c7a00..0x004c7a0f: no player object ->
    // .rdata:005c7474 (0.0f).
    if (GetPlayerObjRW() == nullptr)
        return 0.0f;

    // 0x004c7a10..0x004c7a27: FUN_0054baf0 again, then vt+0x4c = IObjInspect::get_throttle
    // (IObjRW's first base) into DAT_00673564, and that global is what comes back. The int
    // result is discarded.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    Player->get_throttle(G_PlayerThrottle);
    return G_PlayerThrottle;
}

void SetPlayerBehaviorDirectionGated(float X, float Y, float Z)
{
    // 0x004c78d0..0x004c790c: X and Y both compare equal to zero (TEST AH,0x44: NaN is not
    // zero), OR -- read only when they do not -- the HUD_Status bytes DAT_00674aa0+0x5b9 (turret
    // view) and +0x5ba (rear view) are both clear. HUD_Status is read without a null test.
    bool const bDirIsZero = (X == 0.0f) && (Y == 0.0f);
    if (bDirIsZero || (!HudStatusIsTurretView() && !HudStatusIsRearView()))
    {
        IBehaviorManager* const Manager = PlayerBehaviorManager();
        if (Manager != nullptr)
        {
            Vector Dir;
            Dir.x = X;
            Dir.y = Y;
            Dir.z = Z;
            Manager->update_current_behavior_direction(Dir);
        }
    }
}

bool GetPlayerEngineCruiseActive()
{
    CEEngine* const Engine = GetPlayerEngineEquip();
    if (Engine == nullptr)
        return false;
    return Engine->IsCruiseEngaged();
}

bool GetPlayerEngineIsActive()
{
    CEEngine* const Engine = GetPlayerEngineEquip();
    if (Engine == nullptr)
        return false;
    return Engine->IsActive();
}

bool IsMouseFlightUnlocked()
{
    // 0x004c7270: `if (FUN_0045a470()) return true; return DAT_0067354c == 0;`
    if (NnMainButtonHasActivePanel())
        return true;
    return !G_MouseFlightLocked;
}

bool IsMouseFlightLocked()
{
    // 0x004c7c80: `if (FUN_0045a470()) return false; return DAT_0067354c != 0;`
    if (NnMainButtonHasActivePanel())
        return false;
    return G_MouseFlightLocked;
}

void ToggleMouseFlightLock()
{
    // 0x004c7c40.
    G_MouseFlightLocked = !G_MouseFlightLocked;
    PlayOrRewindUiSoundSlot(0x1c); // 004c7c52: FUN_005645c0(0x1c)
}

void OnMouseFlightRawMessage(unsigned Msg)
{
    // 0x004c80f0: `if (param_1 == 0x201) DAT_00673555 = 1;` -- WM_LBUTTONDOWN only.
    if (Msg == 0x201u)
        G_MouseFlightButtonTrigger = true;
}

void UpdateMouseFlightDirection()
{
    // 0x004c7cc5..0x004c7d0a: the ship's behavior interface, when the player is in a ship in the
    // required object-class state (GetPlayerShipEqObj already reproduces retail's -0xc-sentinel
    // and 0x503 class-mask checks byte for byte).
    CEqObj* const ShipEqObj = GetPlayerShipEqObj();
    IBehaviorManager* const ShipBehavior =
        (ShipEqObj != nullptr) ? ShipEqObj->get_behavior_interface() : nullptr;

    // 0x004c7d1c: `pIVar9[0x168] == 0`. IBehaviorManager has no name for this byte yet (see
    // common_iface.h's Opaque168 field); read it raw the same way retail's own byte-sized
    // "IBehaviorManager*" indexing does.
    bool bSuppressMouseFlight = false;
    if (ShipBehavior != nullptr)
    {
        unsigned char const Opaque168 = reinterpret_cast<unsigned char const*>(ShipBehavior)[0x168];
        bSuppressMouseFlight = (Opaque168 == 0);
    }

    // 0x004c7d33/0x004c7d47: the cinematic letterbox, or riding a tradelane, forces the reset path
    // below regardless of the byte above.
    if (IsLetterboxVisible() ||
        (ShipEqObj != nullptr && static_cast<CShip*>(ShipEqObj)->is_using_tradelane()))
    {
        bSuppressMouseFlight = true;
    }

    // 0x004c7d5a: `((DAT_00616850 & 1) != 1) && (DAT_0067354c == 0)`. DAT_00616850 is the mouse
    // button bitmask FUN_0041fa10 / FUN_0041fd90 keep (input_manager.cpp); bit 0 is the left
    // button.
    bool const bMouseButtonHeld = (GetClientMouseButtons() & 1u) == 1u;

    if ((!bMouseButtonHeld && !G_MouseFlightLocked) || bSuppressMouseFlight)
    {
        // 0x004c7fdd..0x004c807e: reset -- timer 0; when engaged, both latches cleared and the
        // direction zeroed. The tail's call at 0x004c8079 (ship behavior +0xc0 == 0x12) goes to
        // 0x0053e4a0, a bare RET 4 on the HUD_Maneuvers singleton: it has no effect (header).
        G_MouseFlightHoldTimer = 0.0f;
        if (G_MouseFlightEngaged)
        {
            G_MouseFlightEngaged = false;
            G_MouseFlightButtonTrigger = false;

            IBehaviorManager* const Manager = PlayerBehaviorManager();
            if (Manager != nullptr)
            {
                Vector Zero;
                Zero.x = 0.0f;
                Zero.y = 0.0f;
                Zero.z = 0.0f;
                Manager->update_current_behavior_direction(Zero);
            }
        }
        return;
    }

    // 0x004c7e35: `DAT_00673555 || DAT_0067354c`.
    if (!(G_MouseFlightButtonTrigger || G_MouseFlightLocked))
        return;

    // 0x004c7d59..0x004c7d7b: not engaged yet and FUN_0059daa0 (the Win3D object under the mouse,
    // DAT_0067dcd8) non-null -- the timer goes back to zero and nothing else happens.
    if (!G_MouseFlightEngaged && Win3DGetHover() != nullptr)
    {
        G_MouseFlightHoldTimer = 0.0f;
        return;
    }

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);

    // 0x004c7d83..0x004c7df7: FCOMP 0.0 / TEST AH,0x44 / JP -- only an ordered-equal zero timer
    // latches the origin (DAT_00616840 FILD, and the invert-aware y).
    if (G_MouseFlightHoldTimer == 0.0f)
    {
        G_MouseFlightOriginX = static_cast<float>(CursorX);
        G_MouseFlightOriginY = static_cast<float>(MouseFlightAdjustedCursorY(CursorY));
    }

    // 0x004c7dfd..0x004c7e19: FUN_0042d680 (double on the FPU) + the float timer; FST float
    // stores the sum, FCOMP compares the unrounded sum against DAT_00612270 (TEST AH,1 / JNZ:
    // less or unordered goes to the cancel-radius test at 0x004c7f2c).
    double const HoldSum =
        GetClientFrameTimeCapped() + static_cast<double>(G_MouseFlightHoldTimer);
    G_MouseFlightHoldTimer = static_cast<float>(HoldSum);

    if (HoldSum >= static_cast<double>(kMouseFlightHoldThreshold))
    {
        // 0x004c7e1f..0x004c7e39: x from the centre of the created surface (DAT_00610850 * 0.5
        // kept on the FPU), divided by the same half width, stored as float.
        double const HalfW = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * 0.5;
        float const NormX =
            static_cast<float>((static_cast<double>(CursorX) - HalfW) / HalfW);
        // 0x004c7e3b..0x004c7ea7: the half height is stored as a float first; the invert-aware
        // y minus it, over it.
        float const HalfH =
            static_cast<float>(static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)) * 0.5);
        int const AdjustedY = MouseFlightAdjustedCursorY(CursorY);
        float const NormY = static_cast<float>(
            (static_cast<double>(AdjustedY) - static_cast<double>(HalfH)) /
            static_cast<double>(HalfH));

        // 0x004c7eab..0x004c7f18: both zero (TEST AH,0x44 / JP|JNP: ordered-equal), or the
        // HUD_Status turret / rear view bytes clear -> the behavior manager's direction
        // {x, y, 0}. That is FUN_004c78d0's body, inlined here byte for byte.
        SetPlayerBehaviorDirectionGated(NormX, NormY, 0.0f);

        // 0x004c7f20: DAT_00673554 = 1.
        G_MouseFlightEngaged = true;
        return;
    }

    // 0x004c7f2c..0x004c7fdc: below the threshold. dx is stored as a float, dy stays on the FPU;
    // the radius is 800 / DAT_00610850 * 50 (DAT_005d6e20, DAT_005d440c). FCOMPP r*r against
    // dx*dx + dy*dy, TEST AH,5 / JP: only an ordered r*r < sum forces the timer to the
    // threshold (DAT_00612270).
    float const DeltaX =
        static_cast<float>(static_cast<double>(CursorX) - static_cast<double>(G_MouseFlightOriginX));
    int const AdjustedY2 = MouseFlightAdjustedCursorY(CursorY);
    double const DeltaY =
        static_cast<double>(AdjustedY2) - static_cast<double>(G_MouseFlightOriginY);
    double const CancelRadius =
        800.0 / static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * 50.0;
    double const DistSq =
        static_cast<double>(DeltaX) * static_cast<double>(DeltaX) + DeltaY * DeltaY;
    if (CancelRadius * CancelRadius < DistSq)
        G_MouseFlightHoldTimer = kMouseFlightHoldThreshold;
}

void ResetMouseFlightState()
{
    // 0x004c75c5 .. 0x004c75ee (inside FUN_004c73d0), in retail's store order.
    G_MouseFlightLocked = false;
    G_MouseFlightHoldTimer = 0.0f;
    G_MouseFlightEngaged = false;
    G_MouseFlightButtonTrigger = false;
    G_MouseFlightOriginX = 0.0f;
    G_MouseFlightOriginY = 0.0f;
}

bool IsMouseFlightHoldThresholdReached()
{
    // 0x004c7c60: DAT_00612270 (0.25f) <= _DAT_00673550, i.e. the hold timer UpdateMouseFlightDirection
    // maintains has reached the engage threshold.
    return G_MouseFlightHoldTimer >= kMouseFlightHoldThreshold;
}

namespace
{
    float G_UnknownFloat673568 = 0.0f; // .data:00673568
    float G_UnknownFloat67356c = 0.0f; // .data:0067356c
}

void SetUnknownFloat673568(float Value)
{
    // 0x004c83a0.
    G_UnknownFloat673568 = Value;
}

void SetUnknownFloat67356c(float Value)
{
    // 0x004c83b0.
    G_UnknownFloat67356c = Value;
}
