// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail "Client/Space" HUD family: freelancer.exe .text ~0x004c9f00-0x00500000+, found while
// auditing this range (decomp-tracker agent claude-sonnet-freelancer-batch50a). A prior marathon
// note flagged part of this range (roughly 0x004ee260-0x004f0900) as an "in-space HUD
// scanner/target-box overlay island". That hypothesis is CONFIRMED for that specific slice (see
// HUD_Targetable_Objects.cpp below) but does NOT describe the whole range: this is actually a run
// of at least eight distinct retail Client/Space/*.cpp files packed back to back, the same pattern
// already documented for 0x00520000+ in space_client_objects_fx.h. None of these files exist in
// the port. Boundaries below were derived the same way as that header: finding the nearest
// preceding function definition for each embedded "E:\FL\Scratch\Source\Client\..." path string
// (freelancer.exe.c), so they are approximate transition points, not confirmed file-end addresses.
//
// ----------------------------------------------------------------------------------------------
// 0) Costume/bodypart assignment helpers -- freelancer.exe ~0x004c9f00-0x004cad10
// No embedded path string found (no FDUMP call site in this sub-cluster), identified instead via
// CostumeDescriptions/bodypart/Deform_exref calls. FUN_004c9f00 assigns up to 8 costume "bodypart"
// slots (a param_1 struct of 3+ uint ids read via param_1[0], param_1[1], param_1[2]...) by calling
// CostumeDescriptions::find_bodypart per slot and forwarding the resolved mesh/callback pointer
// (or the exported default _C_exref) into FUN_004ca220/FUN_004ca2f0 (an add/insert pair operating
// on a fixed array of 8 x 6-dword slot records at param_1_00+0x1a, plus a second 8 x (dword+byte)
// array at +0x4a -- likely "attached accessory" and "hidden bodypart" tables). FUN_004ca070 is a
// bounded (0x7f-char cap) string/record-shift helper used while re-inserting a slot. This is
// exactly the "resolve each accessory and connect its mesh to the character skeleton" step that
// src/freelancer/Client/engine_character.h's EngineCharacterCostumeCache::FindOrCreate documents
// as its own FULLPORT-DEFERRED gap (IHardpoint has no real implementation yet) -- so this
// sub-cluster is that same missing mesh/hardpoint-attachment logic, one level lower than the
// cache. FUN_004ca3c0/FUN_004ca480/FUN_004ca4b0/FUN_004ca530/FUN_004cba10/FUN_004cba60/
// FUN_004cc660 all call the Deform_exref DACOM component (skeletal deform, same one
// engine_character.cpp's ReleaseEntry cites as unported) -- per-bodypart attach/detach against the
// deform target. FUN_004cb250 (large, 200+ lines) combines CostumeDescriptions + CShip +
// Deform_exref + wcscat and is most likely HUD_Comm.cpp's own "build the talking-head portrait
// bust for the currently-speaking NPC" routine (see file 1 below) rather than part of this
// low-level file -- i.e. this costume/deform helper cluster is a shared utility, called into by at
// least HUD_Comm.cpp's portrait display and probably the character-select/NN_Inventory screens
// noted elsewhere. Not ported.
//
// ----------------------------------------------------------------------------------------------
// 1) HUD_Comm.cpp -- freelancer.exe ~0x004cae10-0x004d3db0ish (approximate; see caveat below)
// UPDATE 2026-09-25 (exe-w3d): superseded. HUD_Comm.cpp is exactly 0x004ca640-0x004cefe0 (RTTI
// HUD_Receiver, factory "Receiver") and is ported in hud_comm.{h,cpp}; the costume/deform helpers
// of file 0 (0x004c98a0-0x004ca610) are engine_character.{h,cpp}. 0x004cf000 onwards is ContactList
// / HUD_Status, not HUD_Comm. The text below is kept only as the history of the first pass.
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_Comm.cpp" (freelancer.exe.c lines
// ~134563-136658, i.e. FUN_004cae10 through at least FUN_004ce000). This is the in-space radio
// "chatter"/comm system: a voice-line playback QUEUE with permutation-based ordering (FUN_004cae10
// reads a resource-string-encoded permutation list, e.g. "3,1,2,4", via wcstol, falling back to
// in-order playback with a warning FDUMP "No permutation for comm, playing %d messages in order"
// when none is given), a distance/priority cutoff for ambient "walla" chatter (FUN_004ce000 checks
// WALLA_PRIORITY_CUTOFF_exref / CHATTER_MAX_DIST_exref globals and FDUMPs "invalid VOICE id (%u)
// attempted to speak" on bad input), and BaseWatcher-based bookkeeping (FUN_004cae10 also walks a
// small watcher-slot array releasing per-entry BaseWatcher/vtable-slot-0x60 handles). Likely also
// owns the "talking head" portrait bust shown during a comm message (see FUN_004cb250 in file 0
// above). No radio-chatter/comm-queue system exists anywhere in src/freelancer/Client. Not ported.
// CAVEAT: only two citing functions were found in this sub-range; the upper boundary before
// HUD_DamageIndicator.cpp (~0x004d72a0) is wide enough (~0x9000 bytes) that it likely contains
// additional, still-uncited HUD_Comm.cpp code plus possibly one more unidentified file -- treat
// the 0x004d3db0-0x004d72a0 stretch as "probably still this family, exact file unconfirmed".
//
// ----------------------------------------------------------------------------------------------
// 2) HUD_DamageIndicator.cpp -- freelancer.exe 0x004d62a0-0x004d7520 (+ Create 0x004f3d70)
// UPDATE 2026-09-25 (exe-w3d): the files in this stretch are now pinned down by RTTI and ported:
//   0x004d4b00-0x004d5b00  HUD_Core           -> hud_status_core.{h,cpp}
//   0x004d5b60-0x004d6260  HUD_CruiseProgress -> hud_cruise_progress.{h,cpp}
//   0x004d62a0-0x004d7520  HUD_DamageIndicator (the hit-direction flash) -> hud_damage_indicator.*
//   0x004d7570-0x004da0c0  HUD_Maneuvers (the maneuver bar)  -> hud_maneuvers.{h,cpp}
//   0x004da100-0x004df100  HUD_Status (the weapon list, 0x004dd9e0) -> hud_status_core.{h,cpp}
// What an earlier pass took for a separate "HUD weapon-group/ammo display" file around
// 0x004d88f0-0x004dd9e0 is HUD_Maneuvers, HUD_Status and the weapon groups
// (hud_weapon_groups.{h,cpp}).
//
// ----------------------------------------------------------------------------------------------
// 3) HUD_Target_Base.cpp -- freelancer.exe ~0x004e70e0-0x004ed9f0
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_Target_Base.cpp" (freelancer.exe.c
// line ~149469, inside FUN_004e70e0). This is the ONE-TIME target-box UI window construction code:
// FUN_004e0ee0 (just before this file's own citation, likely its tail-end helper or a shared
// caller) builds two named UI sub-windows via FUN_005a0db0 using literal resource/style strings
// "Interface\\hud\\HUD_target.cmp"/"TargetBackground" and
// "Interface\\hud\\HUD_target_min.cmp"/"TargetMinimizedFrame", then wires up an "ui_window_open"
// callback -- i.e. this constructs the WinControl-derived (interface id 12 in decomp-tracker)
// target-box background frame and its minimized variant as child windows. Ties the "target box" of
// the original HUD-scanner hypothesis directly to the already-catalogued WinControl UI-dialog
// epic: these are two more (previously unidentified) WinControl implementations. Not ported (no
// WinControl base exists in the port at all, per that interface's notes).
//
// ----------------------------------------------------------------------------------------------
// 4) HUD_Targetable_Objects.cpp -- freelancer.exe ~0x004ed9f0-0x004f8160ish
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_Targetable_Objects.cpp"
// (freelancer.exe.c line ~153102, inside FUN_004ed9f0/FUN_004edeb0). THIS is the file the original
// "HUD scanner/target-box overlay island" hypothesis was pointing at, and the hypothesis is
// directly confirmed by reading FUN_004ee260 (804 instructions): it calls CShip::get_target,
// projects the target's world position to screen space via the camera-frustum constants
// (_DAT_0067dc60/64/70/74/9c), calls the render-engine vtable (slot 0xa8, a draw-primitive/line
// call) to draw the lead-indicator/scanner box, and formats the target's display name via
// FUN_004f2910/FUN_004f2a80 (string builder) for the on-screen contact label. Sibling functions in
// this file: FUN_004edeb0/FUN_004eada0/FUN_004ebf70/FUN_004eeea0/FUN_004ef030 (takes an
// IObjInspect*)/FUN_004f0900 (per-contact box update, called from FUN_004ee260's scanner-list
// loop) all operate on the same CShip/IObjRW-based per-contact bookkeeping. FUN_004cf6a0 (found
// much earlier in the .c file, address-wise inside the costume/HUD_Comm range but logically
// belonging here) loads a literal mesh path "interface\\HUD\\HUD_contactship_3d" -- the 3D
// scanner-blip icon mesh -- strongly suggesting Ghidra/the linker did not keep this file's code
// perfectly contiguous with its own citations (COMDAT reordering), so a few outlying functions
// from this file may sit earlier in the address range than the bulk of it.
//
// Port (2026-09-25): the element is HudTargetableObjects (hud_targetable_objects.{h,cpp}); the
// draw tree FUN_004ee260 -> FUN_004ef030 (DrawOnScreenTarget) / FUN_004f0900
// (DrawOffScreenContact) / FUN_004f0620 (DrawWaypointArrow) is ported from the listing. The one
// open hole in it is FUN_004eef50's text setter FUN_0058a580 (see the decomp tracker).
//
// ----------------------------------------------------------------------------------------------
// 5) Client\space\Effect.cpp -- freelancer.exe ~0x004f8160-0x004fd5a0
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp" (freelancer.exe.c line
// ~160456, inside FUN_004f8160). Generic client-side effect-instance base (spawn/update/teardown
// of a running particle/FX instance); distinct from the already-known space_env_fx.h /
// space_client_objects_fx.h effect classes. Not ported.
//
// ----------------------------------------------------------------------------------------------
// 6) EffectTypes.cpp -- freelancer.exe ~0x004fd5a0-0x004fda00
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\Space\\EffectTypes.cpp" (freelancer.exe.c line
// ~163090, inside FUN_004fd5a0, an INI_Reader*-taking parser). This is the effect-TYPE table: a
// named-effect-type registry parsed from an ini ([EffectTypes] or similar), evidenced by the
// shared string literal &PTR_s_EFT_NONE_00612a68 ("EFT_NONE", the null/default effect-type enum
// name) referenced from FUN_004fd500/FUN_004fd560/FUN_004fd5a0. Not ported.
//
// ----------------------------------------------------------------------------------------------
// 7) FlashLight.cpp -- freelancer.exe ~0x004fda00-0x004fedd0
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\space\\FlashLight.cpp" (freelancer.exe.c line
// ~163312, inside FUN_004fda00). Client-side dynamic point-light/flashlight effect object (distinct
// from gamelit.cpp's per-object light class documented in space_client_objects_fx.h -- likely a
// specific handheld/turret-mounted spotlight variant). Not ported.
//
// ----------------------------------------------------------------------------------------------
// 8) GateTunnel.cpp -- freelancer.exe ~0x004fedd0-0x00500000+ (continues past this header's
// surveyed range; not bounded on the far end by this pass)
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\Space\\GateTunnel.cpp" (freelancer.exe.c line
// ~164108, inside FUN_004fedd0, an ID_String*-taking function). Client-side jump-gate "tunnel"
// warp-effect visual (the swirling tunnel shown during a jump). Not ported.
//
// ----------------------------------------------------------------------------------------------
// Cross-cutting note: many small classes throughout files 0-8 above install a fixed combination of
// vtable pointers at construction (e.g. &PTR_FUN_005d6eac + &PTR_LAB_005c8944 + an
// &PTR_OnReceive_005dXXXX table, sometimes alongside a WinControl-tagged primary vtable such as
// &PTR_FUN_005d764c/&PTR_FUN_005d5544/&PTR_FUN_005d74d0). The "OnReceive" naming and the
// IDPMsgHandler::OnAddHost/OnDisconnect symbols already noted on decomp-tracker's WinControl
// interface (id 12) suggest a shared "networked/creatable object" mixin base used by BOTH UI
// dialogs (WinControl-derived panels like the target-box frames in file 3) and plain game/FX
// objects (HUD contacts, effects, gate tunnels) alike -- i.e. IDPMsgHandler is Freelancer's generic
// "receives a server/game message" interface, not GUI-specific. Not individually decoded per
// instance this pass (~80+ install sites observed); see decomp-tracker item-level blocker_reason
// notes for the specific PTR_ combination each constructor installs.
