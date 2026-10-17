// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLSummaryForm — the status summary page (a CFormView on dialog 156 of ServerResources.dll),
// swapped with the console view by CFLServerApp::SwitchView. Retail: CRuntimeClass
// .rdata:0041d69c ("FLSummaryForm"), object size 0x228, vtable .rdata:0041d710, message map
// .rdata:0041d700, TU 0040e8b0..0040f9e0 plus static initializers 0040f9f0..0040fab0.
//
// A rich-edit banner scrolls the "LOADING..." marquee while the server starts, then shows the
// server name; a progress bar and a static show the document's average frame load; a list
// shows six ServerStats counters. Timers: 0x2002 (100 ms marquee) and 0x2001 (1 s refresh).
#pragma once

#include <string>

#include "common_server_types.h"   // ServerStats

class CFLSummaryForm : public CFormView
{
protected:
	DECLARE_DYNCREATE(CFLSummaryForm)

public:
	enum { IDD = 156 };

	CFLSummaryForm();                                   // .text:0040e930
	virtual ~CFLSummaryForm();                          // .text:0040ec60

protected:
	virtual void DoDataExchange(CDataExchange* DX) override;   // .text:0040edc0 (vtbl+0x8c)
	virtual void OnInitialUpdate() override;                   // .text:0040f3d0 (vtbl+0xe8)
	virtual void OnUpdate(CView* Sender, LPARAM Hint, CObject* HintObject) override; // 0040f980

	// .text:0040f030 — timer 0x2001 / OnUpdate: server name, load bar, statistics list.
	void Refresh();
	// .text:0040ee50 — timer 0x2002: scrolls the marquee while the startup is pending, else
	// swaps to the 1 s refresh timer.
	void ScrollMarquee();

	afx_msg void OnTimer(UINT_PTR IdEvent);                     // .text:0040f950
	afx_msg HBRUSH OnCtlColor(CDC* Dc, CWnd* Wnd, UINT CtlColor); // .text:0040f990

	DECLARE_MESSAGE_MAP()

public:
	CRichEditCtrl Banner;            // +0x80  control 0x42b
	CStatic Title;                   // +0xc0  control 0x428 ("Agency FB" 48 pt)
	CListCtrl StatsList;             // +0x100 control 0x410
	CStatic LoadText;                // +0x140 control 0x427 ("%d msec")
	CProgressCtrl LoadBar;           // +0x180 control 0x425
	ServerStats ShownStats;          // +0x1c0 the snapshot the list shows (0xffffffff = none)
	bool bInitialized;               // +0x1ec set at the end of OnInitialUpdate
	CBrush BackgroundBrush;          // +0x1f0 DIB pattern brush of bitmap 0x9f
	CFont TitleFont;                 // +0x1f8
	HGLOBAL BackgroundResource;      // +0x200 LoadResource of bitmap 0x9f
	void* BackgroundBits;            // +0x204 LockResource of it
	std::wstring ShownServerName;    // +0x208 the server name the banner shows
	std::wstring Marquee;            // +0x218 string 0xf01e, rotated by ScrollMarquee
};
