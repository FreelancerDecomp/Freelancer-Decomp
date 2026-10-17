// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CCreateAccountDlg — "Create Account" (dialog 0x8a in ServerResources.dll): creates an account
// with a password through Players.CreateAccount / SetAccountPassword.
// Retail: ctor .text:00406fb0, dtor 00407090, scalar deleting dtor 00407070, vtable
// .rdata:0041c748, empty message map .rdata:0041c740 (entries 0041c728).
// Retail never constructs it: no CALL/JMP reaches the ctor (raw scan of .text), and neither
// the ctor nor the vtable address appears anywhere else in the image; the class is compiled
// in but unused. Ported for structure; nothing creates it here either.
// No RTTI or string names the class; the name follows the dialog caption.
#pragma once

class CAccount;

class CCreateAccountDlg : public CDialog
{
public:
	enum { IDD = 0x8a };

	explicit CCreateAccountDlg(CWnd* Parent = nullptr);   // .text:00406fb0

	CRichEditCtrl NameEdit;       // +0x60, control 1037
	CRichEditCtrl PasswordEdit;   // +0xa0, control 1038
	CAccount* CreatedAccount;     // +0xe0, the new account after a successful OnOK

protected:
	virtual void DoDataExchange(CDataExchange* DX);   // .text:004070f0
	virtual void OnOK();                              // .text:00407140

	DECLARE_MESSAGE_MAP()
};
