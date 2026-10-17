// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>

#include "imeui.h" // _IMEUI_APPEARANCE (single declaration)

#define MAX_CANDLIST 10
#define MAX_COMPSTRING_SIZE 256

typedef void (*PFN_DRAWRECT)(int x1, int y1, int x2, int y2, DWORD color);
typedef void (*PFN_DRAWFAN)(void* Layout, unsigned Count);
typedef void (*PFN_SENDCHAR)(unsigned Character);
typedef void (*PFN_SENDKEY)(unsigned Key);

// Global IME UI engine functions matching retail disassembly
bool ImeUi_Initialize(HWND Window, bool bDisabled = false);
void ImeUi_Uninitialize();
void ImeUi_SetState(bool bState);
DWORD ImeUi_GetState();
void ImeUi_SetSupportLevel(DWORD Level);
DWORD ImeUi_GetSupportLevel();
void ImeUi_SetAppearance(const _IMEUI_APPEARANCE* Appearance);
void ImeUi_GetAppearance(_IMEUI_APPEARANCE* Appearance);
void ImeUi_SetInsertMode(bool bInsert);
bool ImeUi_GetInsertMode();
bool ImeUi_IgnoreHotKey(const MSG* Message);
void ImeUi_SetCaretPosition(int X, int Y);
void ImeUi_SetFontAppearance(CImeUiFont_Base* Font, unsigned Color, const RECT* Rect);
bool ImeUi_IsCompositionEmpty();
bool ImeUi_IsSendingKey();
void ImeUi_SetMessageWindow(HWND Window);
void ImeUi_SetActive(bool bInactive);
void ImeUi_SetScreenSize(int Width, int Height);
void ImeUi_FinalizeString(bool bSendMsg);
void ImeUi_Render(bool bDrawComposition, bool bShowToolbar);
int ImeUi_ProcessMessage(HWND Window, UINT MessageId, WPARAM MessageParam, LPARAM MessageData, BOOL* Handled);
void ImeUi_CheckLanguage();
void ImeUi_UpdateCandidateList(HWND Window);
void ImeUi_GetReadingString(HWND Window);
DWORD ImeUi_GetCodePage();

// Global callbacks registered by IMEFocus
extern PFN_DRAWRECT GDrawRect;
extern PFN_DRAWFAN GDrawFan;
extern PFN_SENDCHAR GSendChar;
extern PFN_SENDKEY GSendKey;
void ImeUi_RenderToolbar(const _IMEUI_APPEARANCE& Appearance, const RECT& Bounding, int ScreenWidth, int ScreenHeight, const DWORD& State, const DWORD& Language, const wchar_t* const& Symbol);
