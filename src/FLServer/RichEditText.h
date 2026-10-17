// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Rich-edit text helpers shared by the FLServer dialogs (retail .text:00409800..004098c9,
// all cdecl, UTF-16 through code page 1200).
#pragma once

// .text:00409800: EM_GETTEXTEX with GETTEXTEX{cb = BufBytes, GT_DEFAULT, 1200, NULL, NULL};
// returns SendMessage's result.
int GetRichEditText(HWND Window, wchar_t* Buf, int BufBytes);

// .text:00409840: EM_SETTEXTEX with SETTEXTEX{ST_DEFAULT, 1200} (replaces the whole text).
void SetRichEditText(HWND Window, const wchar_t* Text);

// .text:00409880: moves the caret to the end (WM_GETTEXTLENGTH, EM_SETSEL(len, len)) and
// replaces the selection with EM_SETTEXTEX{ST_SELECTION, 1200}.
void AppendRichEditText(HWND Window, const wchar_t* Text);
