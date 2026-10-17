// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAreYouSureDlg -- dialog 142 "Are you sure?" (ServerResources.dll). Retail ctor
// .text:00402770, scalar deleting dtor .text:00402790, vtable .rdata:0041c0b8, empty message
// map .rdata:0041c094 (base CDialog). No members past CDialog.
#pragma once

class CAreYouSureDlg : public CDialog
{
public:
	enum { IDD = 142 };

	CAreYouSureDlg(CWnd* Parent = NULL);   // .text:00402770

protected:
	virtual void DoDataExchange(CDataExchange* DX);   // .text:004027c0 (empty: RET 4)

	DECLARE_MESSAGE_MAP()
};
