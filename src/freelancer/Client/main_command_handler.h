// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// main.cpp's global key-command handler (retail object DAT_0067ea60, vftable 005e6d20).
//
//   LAB_005b23a0  static initializer (XC entry 0061013c): FUN_005b23c0 ctor + atexit LAB_005b2550
//   FUN_005b23c0  ctor -- registers USER_EXIT_GAME 0xc5, USER_PAUSE 0x14, USER_RESET 0x15,
//                 USER_FULLSCREEN 0x17, USER_SCREEN_SHOT 0xcb through FUN_005760d0
//   LAB_005b2440  vt[0] OnCommand(MsgId) -- the switch below
//   LAB_005b2550  atexit -> thunk_FUN_00576010 (Event::Handler dtor: unregister)
//
// The object is a file-scope static in main_command_handler.cpp; nothing else calls into it.

// DAT_0067ecbd -- "leave the session" flag. USER_EXIT_GAME sets it (005b245e). Retail also
// writes it at 0054abe5 / 0054ae4d / 005b292b / 005b29ac / 005b2ad0 / 005b2c50 and reads it in
// the frame function FUN_005b2890 (005b28cf / 005b29f8 / 005b2a16): a set flag sends the frame
// to state 6 (IntroFocus) and is cleared there (TickClientFrame, client_frame_tick.cpp).
bool ClientQuitRequested();
void SetClientQuitRequested(bool bQuit);

// FUN_00425170 -- USER_SCREEN_SHOT: next free "<GetScreenShotPath>\screen<N>.BMP", then
// IRPTexture::PrintScreen(0, path). Returns the running shot counter (DAT_006679fc), or -1 when
// GetScreenShotPath fails.
int TakeClientScreenShot();
