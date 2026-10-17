// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The cinematic letterbox: the black bars that slide in when a Thorn script takes the camera
// (ScriptCamera's activate slot is one of its two callers) and slide out again when it gives it
// back. It is the second of the two full-screen effects in the client, and like FadeToBlack it
// is a handful of .data globals rather than an object.
//
// What the bars actually are: the camera's VIEWPORT is narrowed vertically until the visible
// rectangle has a 1.85 aspect -- the widescreen cinema ratio, held as a literal float at
// .rdata 0x005c9020. Nothing is drawn; the letterbox simply stops the renderer from using the
// top and bottom of the surface, and whatever was already there stays black.
//
// Retail's globals:
//
//   0x006166b0  Amount     0 = no bars, 1 = full 1.85 bars. Interpolated.
//   0x006166ac  Target     where Amount is heading
//   0x00610690  Speed      Amount units per second
//   0x006166b4  bAnimating whether Amount is still moving
//   0x006166b8  LastCamera the camera the current bars were applied to
//
// The LastCamera field is what makes the effect survive a camera switch: the update reapplies
// the viewport whenever the camera it is handed differs from the one it saw last, even if the
// amount has not moved at all.

struct FSpaceCamera;
class FRoomCamera;

// FUN_0041a0f0. Aim the bars at Target, moving at Speed per second. ScriptCamera calls this with
// (1.0f, 3.0f) when a script takes over and (0.0f, 3.0f) when it lets go, so the bars take a
// third of a second either way.
//
// If the screen is already covered by the fade (IsFadeOpaque), the amount JUMPS to the target
// instead of animating -- there is nothing to see, so there is no reason to spend the third of
// a second. Retail checks this first, before storing anything else.
void SetLetterboxTarget(float Target, float Speed);

// FUN_0041a3d0. Forgets which camera the bars were last applied to, which forces the next update
// to reapply them. FGameCamera's slot 0x5c calls this straight after it resets the viewport,
// because that reset has just wiped the bars out of the camera.
void ForgetLetterboxCamera();

// FUN_0041a3e0 / FUN_0041a400 / FUN_0041a410.
bool IsLetterboxVisible();          // strictly positive amount
float GetLetterboxAmount();
float GetLetterboxTarget();

// FUN_0041a420. Advances the amount and, when anything changed, recomputes the camera's
// viewport. Called once a frame from FGameCamera's update slot (0x0051b4b0) with the capped
// frame time. A null camera advances the amount and nothing more.
void UpdateLetterbox(FSpaceCamera* Cam, float DeltaSeconds);

// FUN_0041a140. The room camera's twin of the above: the same amount step and bar geometry, but
// the room camera (an FRoomCamera, room runtime +0x500) has no viewport setter, so its viewport,
// half extents, tangents, half-angles, aspect and side planes are rebuilt inline from its own
// slot-6 half-angle. Callers: the Gameflow tick (0x00558b66) with the frame time, the Gameflow
// letterbox reset FUN_00558d60 (0x00558d74) with 0, and the menu's intro tick (0x0057426c).
// A null camera advances the amount and nothing more. Shares DAT_006166b8 with UpdateLetterbox.
void UpdateRoomLetterbox(FRoomCamera* Cam, float DeltaSeconds);

// The bar height in pixels for a given amount, on the current surface. Exposed because it is the
// whole of the geometry and is worth being able to check on its own:
//
//   bars = floor((Height - Width / ((1.85 - Width/Height) * Amount + Width/Height)) * 0.5 + 0.5)
//
// At Amount 0 the divisor is the surface's own aspect, the division gives the height back and
// the bars are zero. At Amount 1 the divisor is 1.85 and what is left is the 1.85 rectangle,
// centred. Retail computes this inline at 0x0041a4e9; it is a function here so the harness can
// pin it down without driving a camera.
int GetLetterboxBarHeight(float Amount);
