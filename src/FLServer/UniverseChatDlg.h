// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CUniverseChatDlg — the universe chat window (dialog 0x92), a single modeless instance
// (GUniverseChatDlg). Text typed into the input box goes out as universe chat; universe chat
// arrives through Server.dll's g_pUniverseChatHook while the window exists.
// Retail: TU .text:00417fb0..004185c0. Global .data:0042a6a8 (CRT initializer 00418090, atexit
// dtor 004180b0 -> 004180c0), ctor 00418120, scalar deleting dtor 004181a0, vtable
// .rdata:0041e8d8, message map 0041e8d0 (entries 0041e858). Opened by the app's universe chat
// command (FLServer.cpp).
#pragma once

class CUniverseChatDlg : public CDialog
{
public:
	enum { IDD = 0x92 };

	explicit CUniverseChatDlg(CWnd* Parent = nullptr);

protected:
	virtual void DoDataExchange(CDataExchange* DX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnMsgFilterInput(NMHDR* Header, LRESULT* Result);
	afx_msg void OnCloseButton();
	afx_msg void OnClose();
	afx_msg void OnDestroy();

	// .text:00418270 (cdecl) — Server.dll's g_pUniverseChatHook, installed by OnInitDialog and
	// cleared by OnDestroy: appends universe chat to GUniverseChatDlg's log.
	static void __cdecl OnUniverseChat(wchar_t* Text, int PlayerId);

	DECLARE_MESSAGE_MAP()

	CRichEditCtrl InputEdit;     // +0x60, control 1047
	CRichEditCtrl LogEdit;       // +0xa0, control 1048
};

// .data:0042a6a8
extern CUniverseChatDlg GUniverseChatDlg;
