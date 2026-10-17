// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FadeToBlack -- the full-screen fade the client runs over every transition. The retail source
// file names itself in the binary: `E:\FL\Scratch\Source\Client\cmn\FadeToBlack.cpp`, printed by
// the render path at 0x00412454.
//
// It is a plain global state machine, no object: a current level, a target, a speed and a
// colour, all in .data. The level is what the rest of the client reads -- the letterbox uses it
// to decide whether a change can be applied instantly, and the space HUD dims against it.
//
// Retail's globals, and what they are here:
//
//   0x006105a8  Level      the live 0..1 opacity. INITIALISED TO 1.0 in .data, so the client
//                          starts fully faded out and the first fade-in is what reveals it.
//   0x006105ac  bAnimating whether the level is still moving toward the target -- .data 01, so
//                          the boot fade-in (1.0 -> target 0) runs without any setter call
//   0x00616510  Target     where the level is heading
//   0x00616508  Speed      level units per second: 1 / Duration
//   0x0061650c  Duration   the duration the speed was derived from; 1.0f at startup (seeded
//                          by the static initializer LAB_00411f90, not by the PE image)
//   0x00616514  bSkipStep  set by every setter; makes the next tick advance by zero seconds
//   0x006164f0  Colour[3]  the colour faded to, as three floats
//   0x006164fc  Default[3] the colour the setters reset to -- all zero in .data, and NOTHING in
//                          the whole exe ever writes it, so "fade to black" is literal
//
// The render half (FUN_00412430) is NOT here; see the note at the bottom of the file.

// FUN_00412010. True once the fade is (near enough) fully opaque -- retail's threshold is a
// literal 0.999, not 1.0, so a fade that has all but finished already counts as covering the
// screen. This is what lets the letterbox snap instead of animating: nobody can see the change.
bool IsFadeOpaque();

// FUN_00412030 / FUN_00412040.
float GetFadeLevel();
float GetFadeTarget();

// The duration the current speed was derived from (0x0061650c). The docking camera compares
// its remaining time against this to decide when to start fading out.
float GetFadeDuration();

// FUN_00412050. Aim at a new level at the given speed, leaving the colour alone.
void SetFadeTarget(float Target, float Speed);

// FUN_00412070. Put the level AND the target at Level immediately, stop animating, and reset the
// colour to the default. This is the hard cut every scene change uses -- and every one of its
// nine call sites passes 1.0f, i.e. "cut to a covered screen".
void SetFadeImmediate(float Level);

// FUN_004120d0. Fade IN (target 0) over one second, resetting the colour. bStartOpaque forces
// the level to 1.0 first, so the fade starts from a covered screen rather than wherever it was.
void FadeIn(bool bStartOpaque);

// FUN_00412170. Fade OUT (target 1) over one second from a clear screen, resetting the colour.
void FadeOut();

// FUN_004121d0 / FUN_004122a0. The same two fades with a colour and a duration. A duration that
// is not strictly positive is ignored and the previous one kept -- retail's guard is
// `if (0.0 < Duration)`, so zero and negatives both fall through to the old value.
void FadeInColoured(const float* Colour3, float Duration, bool bStartOpaque);
void FadeOutColoured(const float* Colour3, float Duration);

// FUN_00412360. Advances the level. Two quirks worth knowing, both reproduced:
//   * the first tick after any setter advances by ZERO seconds (the bSkipStep flag), so a fade
//     never eats the frame it was started on;
//   * every other tick is clamped to 0.1 seconds, so a long stall cannot jump the fade.
void UpdateFade(float DeltaSeconds);

// The colour currently being faded to, three floats. Read-only view of 0x006164f0.
const float* GetFadeColour();

// FUN_00412430. Draws the fade: one screen-covering quad at the current colour and level,
// through the Win3D immediate-mode layer. Cam supplies the rectangle through its slot 0x14; a
// null Cam covers the whole surface.
//
// Two details worth knowing. The quad is inset by HALF A PIXEL on every edge -- retail writes
// left-0.5 / top-0.5 / right+0.5 / bottom+0.5 -- which is the usual D3D texel-centre correction
// and is what stops a one-pixel seam at the edges. And blending is enabled only while the level
// is UNDER 0.999: a fully covered screen draws opaque, which is both faster and exactly what
// IsFadeOpaque already reports.
//
// Draws nothing at all when the level is not strictly positive. Callers: the space frame
// LAB_0054b000 (the active space camera, through AsCamera) and FUN_00558c10 (the room camera).
struct ICamera;
void DrawFade(ICamera* Cam);
