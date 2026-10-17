// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Landing-point header kept for docs that link here. The retail GUI hierarchy (RTTI, restored
// 2026-09-25) lives in:
//   - win3d.{h,cpp}             -- Win3D (0x7c), the root, and the Win3D manager (FUN_0059d5a0..)
//   - wincontrol_base.{h,cpp}   -- WinControl : Win3D, IDataViewOwner (0x32c)
//   - text_win / win_object / list_box -- TextWin, WinObject, ListBox : Win3D
//   - rich_text_win.{h,cpp}     -- BaseRichTextWin : WinControl, RichTextWin, RichTextEditWin
//   - ui_*.{h,cpp}              -- the PAGE_*.txt UI* widgets on those bases
//   - ui_page.{h,cpp}           -- UIPage::Page / MasterWin : Win3D, IUIPage
//   - win_factory               -- WINFACTORY<T> / FUN_0059db20
// The port-only KeyMap / CharacterSelect WinControl dialogs and the RichTextPane stand-in were
// removed with the collapsed scaffold WinControl.
//
// (The flat FMenuUiWidget registry is gone, 2026-10-07.)
//
// Wave-2 refusals (nominal consumer only — do not invent screens):
//   - Options Video/Audio/Credits: retail NeuroNet sub-panels inside FUN_004a9790, not WinControl
//   - win3d-plumb: MenuSetup2D/space/fade already call win3d_* without WinControl parenting
//
// VMS/NeuroNet boundary: retail CharacterSelect OnCreate (FUN_0056d8f0) and UIWin costume
// (FUN_0059b020 / Deform_exref) are NOT ported here.

#include "wincontrol_base.h"
