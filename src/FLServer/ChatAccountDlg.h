// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CChatAccountDlg — "Chat With Account" (dialog 0x94), one modeless window per account,
// registered while it exists in a global map keyed by account name (.data:0042957c). Text typed
// into the input box is sent to the account's client as console chat; the account's private
// chat to the server arrives through Server.dll's g_pPrivateChatHook.
// Retail: TU .text:00413dd0..00415920. ctor 00413fd0 (0x130 bytes), dtor 004140a0, scalar
// deleting dtor 00414080, vtable .rdata:0041e2e0, message map 0041e2d8 (entries 0041e290).
// Created by CChoosePlayerDlg::OnOK (.text:004039a0), which sets AccountName before Create.
#pragma once

#include "server_playerdb.h" // CiWString

class CChatAccountDlg : public CDialog
{
public:
	enum { IDD = 0x94 };

	explicit CChatAccountDlg(CWnd* Parent = nullptr);
	virtual ~CChatAccountDlg();

	// .text:004141e0 — the open chat window of an account, or null.
	static CChatAccountDlg* FindByName(CiWString& Name);

	CiWString AccountName;       // +0x60

protected:
	virtual void DoDataExchange(CDataExchange* DX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnMsgFilterInput(NMHDR* Header, LRESULT* Result);
	afx_msg void OnCloseButton();

	// .text:004145a0 (cdecl) — Server.dll's g_pPrivateChatHook, installed by OnInitDialog:
	// appends a player's private chat to the log of that account's chat window, if open.
	static void __cdecl OnPrivateChat(wchar_t* Text, int ClientId);

	DECLARE_MESSAGE_MAP()

	CRichEditCtrl InputEdit;     // +0x70, control 1047
	CRichEditCtrl LogEdit;       // +0xb0, control 1048
	CRichEditCtrl TitleEdit;     // +0xf0, control 1049
};
