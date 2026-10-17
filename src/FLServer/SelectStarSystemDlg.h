// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CSelectStarSystemDlg — "Select A Star System" (dialog 0x96 in ServerResources.dll). Lists
// the star systems that have at least one player in them; OK (or a double click) opens the
// CChatSystemDlg of the selected system. Modeless: retail keeps one static instance
// (GSelectStarSystemDlg) that CFLServerApp's Chat > Star System command (.text:0040d250)
// creates or brings to front.
// Retail: ctor .text:00406260, dtor 00406210, scalar deleting dtor 004062c0, vtable
// .rdata:0041c538, message map .rdata:0041c530 (entries 0041c4e8). No RTTI or string names the
// class; the name follows the dialog caption.
#pragma once

#include "FLListBox.h"

class CSelectStarSystemDlg : public CDialog
{
public:
	enum { IDD = 0x96 };

	explicit CSelectStarSystemDlg(CWnd* Parent = nullptr);   // .text:00406260

protected:
	virtual void DoDataExchange(CDataExchange* DX);          // .text:004062e0
	virtual BOOL OnInitDialog();                             // .text:00406340
	virtual void OnOK();                                     // .text:00406370
	virtual void OnCancel();                                 // .text:00406360

	afx_msg void OnDblClkSystemList();                                        // .text:00406450
	afx_msg void OnMeasureItem(int IDCtl, LPMEASUREITEMSTRUCT MeasureInfo);   // .text:00406460

	DECLARE_MESSAGE_MAP()

	CFLListBox SystemList;   // +0x60 (CListBox part at +0x64), control 1040; item data = system id
};

// .data:00426140 — constructed by the static initializer .text:004061e0 (CRT XC table
// .data:004250c0), destroyed through the atexit thunk .text:00406200.
extern CSelectStarSystemDlg GSelectStarSystemDlg;
