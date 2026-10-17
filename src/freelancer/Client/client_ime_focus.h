// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FUN_00564060 — construct the client IMEFocus singleton (pre-init tail 005b1f5f).
// Retail installs PTR_FUN_005e2210 (FUN_00564150 / LAB_00563e70 / LAB_00563cf0) over the
// DLL vtable; the port uses ClientIMEFocus (below) for the same three slots.

#include "imeui.h"

#include <stddef.h>

class FontStringChild;

#pragma pack(push, 4)
// Client-side IMEFocus — listing PTR_FUN_005e2210. Retail operator_new(0x1c).
struct ClientIMEFocus : public IMEFocus
{
    explicit ClientIMEFocus(HWND Hwnd);
    ~ClientIMEFocus() override;

    void DrawRect(int X1, int Y1, int X2, int Y2, DWORD Color) override; // LAB_00563e70
    void DrawTextW(const wchar_t* Text, unsigned Color, void* Layout,
                   unsigned Count) override; // LAB_00563cf0

    FontStringChild* ConfirmText; // retail wrapper +0x18
};
#pragma pack(pop)

static_assert(sizeof(IMEFocus) == 0x18, "IMEFocus base 0x18");
static_assert(offsetof(ClientIMEFocus, ConfirmText) == 0x18, "Confirm at +0x18");
static_assert(sizeof(ClientIMEFocus) == 0x1c, "client wrapper 0x1c");

IMEFocus* EnsureClientIMEFocus();

// FUN_005641d0 (cdecl) -- delete the IMEFocus FUN_00564060 stored in DAT_0067a4a4 and zero it.
// Caller: the client shutdown 0x005b2349, after StringShutdown and before CoUninitialize.
void ReleaseClientIMEFocus();

class IFont;

// DAT_0067a4a0 -- "ConfirmPlayerText" (FUN_004177b0), fetched on first use at every site
// (ClientIMEFocus ctor, FLImeInputBox ctor 0x00564296 / 0x005642ff / 0x0056433e).
IFont* ClientConfirmPlayerFont();

// DAT_0067a480 / DAT_0067a484 -- the CRCs of "IMEbkground" / "IMEtext" (static initializers
// LAB_00563ad0 / LAB_00563ab0).
unsigned long ClientImeBackgroundKey();
unsigned long ClientImeTextKey();

// FUN_00417790 -- swap the font atlas material key; the previous one goes to OutPrev.
void ClientImeSwapFontMaterialKey(unsigned* OutPrev, unsigned NewKey);
