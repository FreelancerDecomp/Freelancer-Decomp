// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CErrorDlg -- dialog 145 "Error" (ServerResources.dll). Retail ctor .text:00401290 (called
// from .text:0040728d), scalar deleting dtor .text:00401300, vtable .rdata:0041bf88, empty
// message map .rdata:0041bc68 (base CDialog). No members past CDialog.
#pragma once

class CErrorDlg : public CDialog
{
public:
	enum { IDD = 145 };

	CErrorDlg(CWnd* Parent = NULL);   // .text:00401290

protected:
	virtual void DoDataExchange(CDataExchange* DX);   // .text:00401330 (empty: RET 4)

	DECLARE_MESSAGE_MAP()
};
