// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include <wchar.h>

#include "CreateAccountDlg.h"
#include "ErrorDlg.h"
#include "FLServer.h"           // LockServer / UnlockServer
#include "RichEditText.h"
#include "server_playerdb.h"    // Players, CiWString

// .text:00406fb0
CCreateAccountDlg::CCreateAccountDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
	, CreatedAccount(nullptr)
{
}

// .text:004070f0
void CCreateAccountDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 1037, NameEdit);
	DDX_Control(DX, 1038, PasswordEdit);
}

// .rdata:0041c728: no entries.
BEGIN_MESSAGE_MAP(CCreateAccountDlg, CDialog)
END_MESSAGE_MAP()

// .text:00407140 — an empty name just closes the dialog. Otherwise the name is lower-cased and
// the account created under the server lock; on success the password is set and the dialog
// closes (still inside the lock), on failure CErrorDlg is shown and the dialog stays open.
void CCreateAccountDlg::OnOK()
{
	wchar_t NameText[0x40];
	wchar_t PasswordText[0x40];
	GetRichEditText(NameEdit.m_hWnd, NameText, sizeof(NameText));
	GetRichEditText(PasswordEdit.m_hWnd, PasswordText, sizeof(PasswordText));
	if (wcslen(NameText) == 0)
	{
		CDialog::OnOK();
		return;
	}
	_wcslwr(NameText);

	CiWString AccountName;
	AccountName.assign(NameText, wcslen(NameText));
	CiWString Password;
	Password.assign(PasswordText, wcslen(PasswordText));

	LockServer();
	const bool bCreated = Players.CreateAccount(AccountName);
	if (bCreated)
	{
		CreatedAccount = Players.FindAccountFromName(AccountName);
		Players.SetAccountPassword(AccountName, Password);
		CDialog::OnOK();
	}
	UnlockServer();

	if (!bCreated)
	{
		CErrorDlg Error(nullptr);
		Error.DoModal();
	}
}
