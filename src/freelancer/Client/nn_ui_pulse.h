// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// NeuroNet UI pulse colour (FUN_00561da0) + phase helpers.
//
// Retail globals:
//   DAT_0067dce8  phase accumulator (float seconds)
//   DAT_0067dce4  direction byte (0/1)
//   DAT_00679bb0  idle cyan   — LAB_00561c00 → bytes 87,C3,E0,FF
//   DAT_00679ba4  pulse amber — LAB_00561c60 → bytes F5,EA,52,FF
//   DAT_00679b78  mid grey    — lazy 0xFFB9B9B9 inside FUN_00561da0
//
// FUN_0059d570 = phase / 0.3; FUN_0059d560 = direction; FUN_0059d580 = reset
// (dir=0, phase=0.3f*0.5f = 0.15). Advance is the head of FUN_0059e080 (phase += dt, wrap
// every 0.3s flipping dir) — not the ToolTip/scene walk that follows.

// FUN_0059d580 — reset pulse on hover/focus enter (FUN_0055b8a0).
void NnUiPulseReset();

// Pulse-advance slice of FUN_0059e080 (DAT_0067dce8 / DAT_0067dce4 only). Retail passes the
// frame delta as a double (FADD double ptr [ESP+4]); Win3DUpdateAll is the only caller.
void NnUiPulseAdvance(double DeltaSeconds);

// FUN_00561da0 — write blended ARGB into *OutArgb.
void NnUiPulseBlendArgb(unsigned long* OutArgb);

// FUN_00561f20 (cdecl) — the same pulse around a caller colour: BaseArgb takes the idle
// colour's place and a grey made from the Base/amber midpoint takes the mid grey's place.
// 14 retail callers (UIScroll arrows 005961a3 / 00596323, draw 005965d7, HUD, ...).
void NnUiPulseBlendFromArgb(unsigned long* OutArgb, unsigned long BaseArgb);

// FUN_0045a650 (cdecl) -- the palette colour of a reputation Attitude: at or below -0.6 red
// (DAT_00679ba8), at or below -0.2 white (DAT_00679b84), at or above 0.6 green
// (DAT_00679bac), otherwise white (two branches, both DAT_00679b84). Callers: the nav map's
// zone shading (0x0049ce39) and 0x004a82c9.
void NnReputationColorArgb(unsigned long* OutArgb, float Attitude);

// Idle palette colour DAT_00679bb0 (LAB_00561c00).
unsigned long NnUiPulseIdleArgb();

// FUN_0059d570 -- the pulse phase over its period, DAT_0067dce8 / 0.3f (DAT_005e5dc4).
float NnUiPulsePhaseNorm();

// FUN_0059d560 -- the pulse direction byte DAT_0067dce4.
unsigned char NnUiPulseDirection();

// DAT_00679bb4 -- the HUD chrome RGBA bytes 87,C3,E0,C0. Its only writer in the PE is the
// static initializer LAB_00561be0; ~20 sites read it (ToolTip LineLoop, WinControl::Draw
// FUN_005a0760 which scales each byte by 1/255 into the mesh holder's tint, HUD panels).
extern unsigned char G_HudChromeColorRgba[4];

// NeuroNet UI palette -- one 4-byte colour per static initializer, all in this TU's XC run
// (LAB_00561be0 .. LAB_00561d80). Memory bytes are R,G,B,A, so the little-endian dword is
// 0xAABBGGRR -- which is how most retail readers load them (MOV r32). None of them has a
// writer in the PE besides its initializer. LAB_00561c80 and LAB_00561d20 seed two more
// slots that nothing reads; they are not declared.
extern unsigned long G_NnPaletteArgb_679bb0; // LAB_00561c00  87,C3,E0,FF  idle cyan
extern unsigned long G_NnPaletteArgb_679bac; // LAB_00561c20  3B,BF,1D,FF
extern unsigned long G_NnPaletteArgb_679ba8; // LAB_00561c40  BF,1D,1D,FF
extern unsigned long G_NnPaletteArgb_679ba4; // LAB_00561c60  F5,EA,52,FF  pulse amber
extern unsigned long G_NnPaletteArgb_679b9c; // LAB_00561ca0  88,00,C2,FF
extern unsigned long G_NnPaletteArgb_679b98; // LAB_00561cc0  48,48,E0,FF
extern unsigned long G_NnPaletteArgb_679b94; // LAB_00561ce0  80,80,80,FF  grey
extern unsigned long G_NnPaletteArgb_679b90; // LAB_00561d00  A8,FB,C5,FF
extern unsigned long G_NnPaletteArgb_679b88; // LAB_00561d40  FF,7E,FD,FF
extern unsigned long G_NnPaletteArgb_679b84; // LAB_00561d60  FF,FF,FF,FF  white
extern unsigned long G_NnPaletteArgb_679b80; // LAB_00561d80  CE,5A,FF,FF
