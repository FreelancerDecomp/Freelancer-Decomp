// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IFLListBox — the 10-slot abstract base at offset 0 of CFLListBox (pure vtable 0041dcd8).
// Kept free of MFC types so that non-MFC translation units (StarSystemList.cpp) can fill a list
// through it. Retail names are unknown; slot order is retail's.
#pragma once

#include <windows.h>          // DWORD_PTR, COLORREF
#include "server_playerdb.h"  // CiWString

class IFLListBox
{
public:
	virtual void AddItem(const wchar_t* Text, DWORD_PTR Data) = 0;             // +0x00 0x00410040
	virtual void GetItemText(int Index, CiWString& OutText) = 0;               // +0x04 0x00410520
	virtual int SelectItem(const wchar_t* Text) = 0;                           // +0x08 0x00410650
	virtual void SetItemColor(DWORD_PTR Data, COLORREF Color) = 0;            // +0x0c 0x004100a0
	virtual void RemoveItemColor(DWORD_PTR Data) = 0;                          // +0x10 0x00410240
	virtual COLORREF GetItemColor(DWORD_PTR Data) = 0;                         // +0x14 0x004101e0
	virtual void SetItemItalic(DWORD_PTR Data, bool bItalic) = 0;              // +0x18 0x00410340
	virtual bool IsItemItalic(DWORD_PTR Data) = 0;                             // +0x1c 0x004103d0
	virtual void SetItemBold(DWORD_PTR Data, bool bBold) = 0;                  // +0x20 0x00410430
	virtual bool IsItemBold(DWORD_PTR Data) = 0;                               // +0x24 0x004104c0
};
