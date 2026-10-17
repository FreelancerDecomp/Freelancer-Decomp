// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CServerInfoDlg — "Server Information..." (dialog 0x83 in ServerResources.dll): a resizable
// window holding one tree view (control 1005) that hides itself on close.
// Retail: ctor .text:004074c0, dtor 00407560, scalar deleting dtor 00407540, vtable
// .rdata:0041c970, message map .rdata:0041c968 (entries 0041c8f0).
// Retail never constructs it: no CALL/JMP reaches the ctor (raw scan of .text), and neither
// the ctor nor the vtable address appears anywhere else in the image; the class is compiled
// in but unused. Its tree-refresh routine .text:00407600 (recursive, walks an object through
// vtable slots +0x0c/+0x14/+0x20 and prunes stale items through a local std::set) has no
// caller other than itself, so its argument type cannot be recovered; it is not ported
// (tracker: skipped, dead). Ported for structure; nothing creates the dialog here either.
// No RTTI or string names the class; the name follows the dialog caption.
#pragma once

class CServerInfoDlg : public CDialog
{
public:
	enum { IDD = 0x83 };

	explicit CServerInfoDlg(CWnd* Parent = nullptr);   // .text:004074c0

	CTreeCtrl InfoTree;   // +0x60, control 1005

protected:
	virtual void DoDataExchange(CDataExchange* DX);   // .text:004075b0

	afx_msg void OnClose();                                   // .text:004075f0
	afx_msg void OnTimer(UINT_PTR IDEvent);                   // .text:004079c0
	afx_msg int OnCreate(LPCREATESTRUCT CreateInfo);          // .text:004079d0
	afx_msg void OnSize(UINT Type, int Width, int Height);    // .text:00407a00

	DECLARE_MESSAGE_MAP()
};
