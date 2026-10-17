// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "SetPasswordDlg.h"
#include "OperationCompleteDlg.h"
#include "RichEditText.h"
#include "FLServer.h"
#include "server_playerdb.h"

// .text:00415af0
CSetPasswordDlg::CSetPasswordDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
	, Account(nullptr)
{
}

// .text:00415bd0
void CSetPasswordDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 1038, PasswordEdit);
}

// .rdata:0041e4d0: no entries.
BEGIN_MESSAGE_MAP(CSetPasswordDlg, CDialog)
END_MESSAGE_MAP()

// .text:00415c10
void CSetPasswordDlg::OnOK()
{
	wchar_t Text[0x400];
	GetRichEditText(PasswordEdit.m_hWnd, Text, sizeof(Text));
	LockServer();
	{
		CiWString Password(Text);
		Players.SetAccountPassword(Account->m_Name, Password);
	}
	UnlockServer();

	COperationCompleteDlg Done(nullptr);
	Done.DoModal();
	CDialog::OnOK();
}
