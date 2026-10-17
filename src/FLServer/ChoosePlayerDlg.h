// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CChoosePlayerDlg — "Choose A Player" (dialog 0x97 in ServerResources.dll). Lists the names
// of the characters logged in right now; OK (or a double click) opens the CChatAccountDlg of
// the selected character's account. Modeless: retail keeps one static instance (GChoosePlayerDlg)
// that CFLServerApp's Chat > Player command (.text:0040d1f0) creates or brings to front.
// Retail: ctor .text:00403850, dtor 00403800, scalar deleting dtor 004038b0, vtable
// .rdata:0041c3f8, message map .rdata:0041c1e8 (entries 0041c1a0). No RTTI or string names the
// class; the name follows the dialog caption.
#pragma once

#include "FLListBox.h"

class CChoosePlayerDlg : public CDialog
{
public:
	enum { IDD = 0x97 };

	explicit CChoosePlayerDlg(CWnd* Parent = nullptr);   // .text:00403850

protected:
	virtual void DoDataExchange(CDataExchange* DX);      // .text:004038d0
	virtual BOOL OnInitDialog();                         // .text:00403940
	virtual void OnOK();                                 // .text:004039a0
	virtual void OnCancel();                             // .text:00403990

	afx_msg void OnDblClkPlayerList();                                        // .text:00403930
	afx_msg void OnMeasureItem(int IDCtl, LPMEASUREITEMSTRUCT MeasureInfo);   // .text:00403b00

	DECLARE_MESSAGE_MAP()

	CFLListBox PlayerList;   // +0x60 (CListBox part at +0x64), control 1057
};

// .data:00426028 — constructed by the static initializer .text:004037d0 (CRT XC table
// .data:00425098), destroyed through the atexit thunk .text:004037f0.
extern CChoosePlayerDlg GChoosePlayerDlg;
