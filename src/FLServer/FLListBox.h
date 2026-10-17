// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLListBox — FLServer's owner-draw list box (Arial Unicode MS, per-item colour / italic /
// bold keyed by the item data). Item text is stored hex-encoded (Common's WideToHex) so the
// ANSI list box can carry wide strings; DrawItem decodes it and draws with TextOutW.
// Retail: ctor .text:0040fad0, dtor 0040fe50, vtables .rdata:0041dbd8 (IFLListBox part, at
// offset 0) and 0041db00 (CListBox part, at +4), message map .rdata:0041daf4.
// Used by the accounts / chat / player-pick dialogs (member at +0x60 or +0x1a0).
#pragma once

#include <map>
#include "server_playerdb.h" // CiWString
#include "IFLListBox.h"      // the 10-slot base at offset 0 (MFC-free, see that header)

class CFLListBox : public IFLListBox, public CListBox
{
public:
	CFLListBox();
	virtual ~CFLListBox();

	virtual void AddItem(const wchar_t* Text, DWORD_PTR Data);
	virtual void GetItemText(int Index, CiWString& OutText);
	virtual int SelectItem(const wchar_t* Text);
	virtual void SetItemColor(DWORD_PTR Data, COLORREF Color);
	virtual void RemoveItemColor(DWORD_PTR Data);
	virtual COLORREF GetItemColor(DWORD_PTR Data);
	virtual void SetItemItalic(DWORD_PTR Data, bool bItalic);
	virtual bool IsItemItalic(DWORD_PTR Data);
	virtual void SetItemBold(DWORD_PTR Data, bool bBold);
	virtual bool IsItemBold(DWORD_PTR Data);

	// CListBox owner-draw overrides (CListBox vtable slots +0xc0 / +0xcc).
	virtual void DrawItem(LPDRAWITEMSTRUCT DrawInfo);
	virtual void DeleteItem(LPDELETEITEMSTRUCT DeleteInfo);

	DECLARE_MESSAGE_MAP()

public:
	// Retail offsets are from the start of the object (CListBox subobject at +4).
	// The accounts dialog measures its rows with RegularFont (.text:00412c99).
	CFont RegularFont;    // +0x44  weight 400
	CFont ItalicFont;     // +0x4c  weight 400, italic
	CFont BoldFont;       // +0x54  weight 700
	CFont BoldItalicFont; // +0x5c  weight 700, italic

protected:
	std::map<DWORD_PTR, COLORREF> ItemColors; // +0x64
	std::map<DWORD_PTR, bool> ItalicItems;    // +0x78
	std::map<DWORD_PTR, bool> BoldItems;      // +0x8c
};
