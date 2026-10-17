// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "OperationCompleteDlg.h"

// .text:00413d50
COperationCompleteDlg::COperationCompleteDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
}

// .text:00413da0 — retail body is RET 4.
void COperationCompleteDlg::DoDataExchange(CDataExchange* DX)
{
	CDialog::DoDataExchange(DX);
}

// .rdata:0041e188: no entries.
BEGIN_MESSAGE_MAP(COperationCompleteDlg, CDialog)
END_MESSAGE_MAP()
