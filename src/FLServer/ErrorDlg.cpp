// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "ErrorDlg.h"

CErrorDlg::CErrorDlg(CWnd* Parent)
	: CDialog(CErrorDlg::IDD, Parent)
{
}

void CErrorDlg::DoDataExchange(CDataExchange* DX)
{
	CDialog::DoDataExchange(DX);
}

// .rdata:0041bc68: no entries.
BEGIN_MESSAGE_MAP(CErrorDlg, CDialog)
END_MESSAGE_MAP()
