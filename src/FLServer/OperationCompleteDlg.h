// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// COperationCompleteDlg — "Operation Complete / The password has been set!" (dialog 0x90).
// Retail: ctor .text:00413d50, vtable .rdata:0041e1a8, message map .rdata:0041e1a0 (empty).
#pragma once

class COperationCompleteDlg : public CDialog
{
public:
	enum { IDD = 0x90 };

	explicit COperationCompleteDlg(CWnd* Parent = nullptr);

protected:
	virtual void DoDataExchange(CDataExchange* DX);

	DECLARE_MESSAGE_MAP()
};
