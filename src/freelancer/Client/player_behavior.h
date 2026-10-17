// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The player's behaviour command layer: FUN_004c7950 and the family of one-line forwarders that
// sit beside it at 0x004c7970 .. 0x004c7ae0, plus the throttle setter FUN_004c7af0.
//
// Every one of them has the same body in retail: fetch the player's IObjRW (FUN_0054baf0), ask
// COMMON.DLL::GetBehaviorManager for its IBehaviorManager, and -- only when that comes back
// non-null -- forward one argument to a single method on it. The input handlers at 0x004d7f00 ..
// 0x004d8700 are the callers, and the docking camera's attach slot uses two of them.

// FUN_0054baf0: the player's IObjRW, i.e. the watchable at DAT_00679744 minus eight, or null.
void* GetPlayerObjRW();

// FUN_004c7950 -- IBehaviorManager::reset_current_behavior_direction.
void ResetPlayerBehaviorDirection();

// FUN_004c7a30 -- IBehaviorManager::update_current_behavior_slide_strafe_burst.
// Dir is Common's StrafeDir (common_cequip.h): 0 none, 1 left, 2 right, 3 up, 4 down. It is taken
// as an int so that callers which only issue the command -- the docking camera among them -- do
// not have to pull Common's equipment headers into their translation unit. See docs/re/deviations.md.
void SetPlayerStrafeBurst(int Dir);

// FUN_004c7970 -- IBehaviorManager::update_current_behavior_afterburner.
void SetPlayerAfterburner(bool bOn);

// FUN_004c79a0 -- IBehaviorManager::update_current_behavior_auto_level.
void SetPlayerAutoLevel(bool bOn);

// FUN_004c79d0 -- IBehaviorManager::update_current_behavior_auto_avoidance.
void SetPlayerAutoAvoidance(bool bOn);

// FUN_004c7a60 -- IBehaviorManager::update_current_behavior_brake_reverse.
void SetPlayerBrakeReverse(bool bOn);

// FUN_004c7a90 -- IBehaviorManager::update_current_behavior_cruise.
void SetPlayerCruise(bool bOn);

// FUN_004c7ac0 -- IBehaviorManager::update_current_behavior_engage_engine.
void SetPlayerEngageEngine(bool bOn);

// FUN_004c7af0: clamps to [-1, 1], stores the result in the retail global DAT_00673564, forwards
// it as the throttle, and then -- only when the clamped value is exactly zero -- drops cruise.
void SetPlayerThrottle(float Throttle);

// DAT_00673564, the last value SetPlayerThrottle stored. FUN_004c7a00 reads it back after
// refreshing it from the ship; that refresh is not ported yet (see the .cpp).
float GetLastPlayerThrottle();

// FUN_004c78d0 -- IBehaviorManager::update_current_behavior_direction, gated exactly as retail
// gates it: only forwarded when X and Y are both zero, or when the HUD_Status bytes
// DAT_00674aa0+0x5b9 (turret view) / +0x5ba (rear view) are both clear (hud_status_core.h;
// read only when the direction is not zero, with no null test).
// All three retail call sites (0x004d803e, 0x004d807d, 0x0051d78f) pass a genuine third float.
// The first two are HUD_Maneuvers::OnEvent 0x81 / 0x82 (hud_maneuvers.cpp): (0, 0, trail
// distance), so for them the gate always forwards.
void SetPlayerBehaviorDirectionGated(float X, float Y, float Z);

// FUN_004c7b90 -- reads the player's engine CEEngine+0x54 (bCruiseActive) directly, i.e.
// IBehaviorManager is bypassed and CEEngine::IsCruiseEngaged() is read straight off the player's
// engine equipment. False with no ship or no engine.
bool GetPlayerEngineCruiseActive();

// FUN_004c7be0 -- CEEngine::IsActive() (CEquip vtable slot 1) on the player's engine equipment.
// False with no ship or no engine.
bool GetPlayerEngineIsActive();

// -------------------------------------------------------------------------------------------
// Mouse-flight lock toggle: .data:0067354c, and the pair of getters that read it.
//
// FUN_004c7270 and FUN_004c7c80 both open with `FUN_0045a470()` -- NnMainButtonHasActivePanel
// (nn_modal.h), a query on the NN_MainButton singleton DAT_0066d3f0. That class is not ported, so
// the query answers false and both getters come down to the toggle below.
//
// The flag itself is also read by FUN_004c7cb0 (the mouse-flight direction updater) alongside
// DAT_00616850 & 1 (the left mouse button, GetClientMouseButtons), which is the basis for calling
// this a lock toggle rather than a plain hold.

// FUN_004c7270 -- true unless mouse-flight is locked on. Named for the OR-gate it feeds in
// FUN_004c7cb0: mouse-flight direction updates run when the toggle is set OR the (unported)
// right-mouse-held bit is set.
bool IsMouseFlightUnlocked();

// FUN_004c7c80 -- the toggle's raw state.
bool IsMouseFlightLocked();

// FUN_004c7c40 -- flip the toggle and stop UI sound slot 0x1c (FindUiSoundSlot table, already
// ported in menu_ui_init.cpp).
void ToggleMouseFlightLock();

// FUN_004c80f0 -- the raw mouse-message hook FUN_0045a270 (NnMainButtonOnUnclaimedMouse,
// 0x0045a439) forwards an unclaimed press to while the player is in space. Retail only reacts to
// WM_LBUTTONDOWN (0x201): it arms the one-shot trigger UpdateMouseFlightDirection consumes to
// start a hold-to-engage cycle.
void OnMouseFlightRawMessage(unsigned Msg);

// FUN_004c7cb0 -- the mouse-flight hold-to-engage state machine. Every frame: while the lock
// toggle is off and neither the left mouse button (DAT_00616850 bit 0) nor
// OnMouseFlightRawMessage's one-shot trigger is set, the timer stays
// at zero and any previously-engaged direction is reset to (0,0,0). Once triggered (or with the
// lock toggle on), the cursor position at the moment the timer was zero is latched as the origin;
// each frame accumulates GetClientFrameTimeCapped() into the timer, and once it reaches 0.25s
// (DAT_00612270) the normalized cursor offset from screen centre is forwarded once via
// SetPlayerBehaviorDirectionGated and the state latches "engaged". Before the timer reaches the
// threshold, moving the cursor more than a (800/width)*50 px radius from the latched origin forces
// the timer to the threshold outright, so the very next frame engages immediately instead of
// waiting out the rest of the hold.
//
// FUN_0045a470 (see above) is NnMainButtonHasActivePanel at every site.
// FUN_0059daa0 (Win3DGetHover, the Win3D object under the mouse, DAT_0067dcd8) gates the frames
// before the first engage: non-null zeroes the timer and returns (0x004c7d59..0x004c7d7b).
// The surface size is the created one (DAT_00610850 / DAT_00610854), not the options mode.
// Retail also calls `IDPMsgHandler::OnAddHost(DAT_00674a58, 0)` (0x004c8079) when the reset
// branch fires AND the ship's current behavior index is 0x12. The name is identical-code
// folding: the target is 0x0053e4a0, a bare `RET 4`, and DAT_00674a58 is the HUD_Maneuvers
// singleton (hud_maneuvers.h) passed as `this`. The call does nothing, so nothing is missing.
// Caller: the space frame LAB_0054b000 at 0x0054b0e9 (space_state.cpp UpdateSpaceState).
void UpdateMouseFlightDirection();

// FUN_004c7c60 -- true once UpdateMouseFlightDirection's hold timer (.data:00673550) has reached
// the 0.25s threshold (DAT_00612270). Previously left `simplified` because the timer's only writer
// (FUN_004c7cb0) wasn't ported yet; now that UpdateMouseFlightDirection owns G_MouseFlightHoldTimer,
// this is a straight comparison against it.
bool IsMouseFlightHoldThresholdReached();

// 0x004c75c5 .. 0x004c75ee, the tail of the HUD loader FUN_004c73d0 (hud_element_list.cpp):
// DAT_0067354c (lock) = 0, DAT_00673550 (hold timer) = 0, DAT_00673554 / DAT_00673555 = 0,
// DAT_00673558 / DAT_0067355c (latched origin) = 0. Retail writes the globals inline; the port's
// copies live in this translation unit, so the loader calls this.
void ResetMouseFlightState();

// FUN_004c83a0 / FUN_004c83b0 -- two raw setters for .data:00673568/0067356c. Both globals are
// write-only across the whole PE (no read anywhere in freelancer.exe.c); purpose/owner not
// identified, but the writes themselves are trivial and fully faithful regardless.
void SetUnknownFloat673568(float Value);
void SetUnknownFloat67356c(float Value);
