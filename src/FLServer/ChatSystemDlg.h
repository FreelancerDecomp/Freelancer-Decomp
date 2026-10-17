// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CChatSystemDlg — "Star System Chat" (dialog 0x95), one modeless window per star system,
// registered while it exists in a global map keyed by system id (.data:00429e34). Text typed
// into the input box goes out as system chat; the system's chat arrives through Server.dll's
// g_pSystemChatHook.
// Retail: TU .text:00415fe0..004172e0. ctor 004161d0 (0x124 bytes), dtor 00416290, scalar
// deleting dtor 00416270, vtable .rdata:0041e760, message map 0041e758 (entries 0041e710).
// Created by CSelectStarSystemDlg::OnOK (.text:00406370), which sets SystemId before Create.
#pragma once

class CChatSystemDlg : public CDialog
{
public:
	enum { IDD = 0x95 };

	explicit CChatSystemDlg(CWnd* Parent = nullptr);
	virtual ~CChatSystemDlg();

	// .text:00416620 — the open chat window of a system, or null.
	static CChatSystemDlg* FindBySystem(unsigned int SystemId);

	unsigned int SystemId;       // +0x60, 0xffffffff until the creator sets it

protected:
	virtual void DoDataExchange(CDataExchange* DX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnMsgFilterInput(NMHDR* Header, LRESULT* Result);
	afx_msg void OnCloseButton();

	// .text:00416670 (cdecl) — Server.dll's g_pSystemChatHook, installed by OnInitDialog:
	// appends a system's chat to the log of that system's chat window, if open.
	static void __cdecl OnSystemChat(wchar_t* Text, int SystemId);

	DECLARE_MESSAGE_MAP()

	CRichEditCtrl InputEdit;     // +0x64, control 1047
	CRichEditCtrl LogEdit;       // +0xa4, control 1048
	CRichEditCtrl TitleEdit;     // +0xe4, control 1049
};
