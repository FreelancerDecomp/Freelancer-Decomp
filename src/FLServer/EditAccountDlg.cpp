// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "EditAccountDlg.h"
#include "AreYouSureDlg.h"
#include "RichEditText.h"
#include "FLServer.h"
#include "server_playerdb.h"

// .text:00409200 — CButton +0x60, CFLListBox +0xa0 (0040fad0), CButton +0x140, CButton +0x180,
// CRichEditCtrl +0x1c0; Account (+0x200) is not written.
CEditAccountDlg::CEditAccountDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
}

// .text:00409390
void CEditAccountDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 1044, BannedCheck);
	DDX_Control(DX, 1040, CharacterList);
	DDX_Control(DX, 1041, DeleteCharacterButton);
	DDX_Control(DX, 1037, AccountNameEdit);
}

// Message map .rdata:0041cb70.
BEGIN_MESSAGE_MAP(CEditAccountDlg, CDialog)
	ON_LBN_SELCHANGE(1040, &CEditAccountDlg::OnSelChangeCharacterList) // 00409410
	ON_LBN_SELCANCEL(1040, &CEditAccountDlg::OnSelCancelCharacterList) // 00409420
	ON_BN_CLICKED(1041, &CEditAccountDlg::OnDeleteCharacter)           // 004094e0
	ON_WM_MEASUREITEM()                                                // 00409610
	ON_BN_CLICKED(1045, &CEditAccountDlg::OnKickAccount)               // 00409770
END_MESSAGE_MAP()

// .text:00409430
BOOL CEditAccountDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	LockServer();
	SetRichEditText(AccountNameEdit.m_hWnd, Account->m_Name.c_str());
	std::list<CiWString>::iterator Character = Account->m_Characters.begin();
	while (Character != Account->m_Characters.end())
	{
		CharacterList.AddItem(Character->c_str(), 0);
		++Character;
	}
	::SendMessageA(BannedCheck.m_hWnd, BM_SETCHECK, !Account->m_bNotBanned, 0);
	UnlockServer();
	return TRUE;
}

// .text:00409410
void CEditAccountDlg::OnSelChangeCharacterList()
{
	DeleteCharacterButton.EnableWindow(TRUE);
}

// .text:00409420
void CEditAccountDlg::OnSelCancelCharacterList()
{
	DeleteCharacterButton.EnableWindow(FALSE);
}

// .text:004094e0
void CEditAccountDlg::OnDeleteCharacter()
{
	CAreYouSureDlg Confirm(nullptr);
	if (Confirm.DoModal() != IDOK)
	{
		return;
	}
	const int Selection = (int)::SendMessageA(CharacterList.m_hWnd, LB_GETCURSEL, 0, 0);
	if (Selection <= -1)
	{
		return;
	}

	LockServer();
	CiWString CharacterName;
	CharacterList.GetItemText(Selection, CharacterName);
	Players.DeleteCharacterFromName(CharacterName);
	UnlockServer();
	::SendMessageA(CharacterList.m_hWnd, LB_DELETESTRING, Selection, 0);
	DeleteCharacterButton.EnableWindow(FALSE);
}

// .text:00409610 — rows are as tall as the list box's regular font.
void CEditAccountDlg::OnMeasureItem(int IDCtl, LPMEASUREITEMSTRUCT MeasureInfo)
{
	if (IDCtl != 1040)
	{
		CWnd::OnMeasureItem(IDCtl, MeasureInfo);
		return;
	}
	CDC MeasureDC;
	MeasureDC.Attach(::CreateCompatibleDC(nullptr));
	CFont* OldFont = MeasureDC.SelectObject(&CharacterList.RegularFont);
	TEXTMETRICA Metrics;
	::GetTextMetricsA(MeasureDC.m_hDC, &Metrics);
	MeasureDC.SelectObject(OldFont);
	MeasureInfo->itemHeight = Metrics.tmHeight;
}

// .text:004096e0 — the "Banned" box is applied only when it differs from the account's state
// (m_bNotBanned equal to the check state means they disagree).
void CEditAccountDlg::OnOK()
{
	const bool bBanned = ::SendMessageA(BannedCheck.m_hWnd, BM_GETCHECK, 0, 0) != 0;
	LockServer();
	if (Account->m_bNotBanned == bBanned)
	{
		Players.LockAccountAccess(Account->m_Name);
		Players.BanAccount(Account->m_Name, bBanned);
		Players.UnlockAccountAccess(Account->m_Name);
	}
	UnlockServer();
	CDialog::OnOK();
}

// .text:00409770 — "Kick Account From Server": applies the "Banned" box like OnOK, but with the
// account access lock taken unconditionally, then closes the dialog with IDCANCEL. Retail does
// nothing else here (no logout call of its own).
void CEditAccountDlg::OnKickAccount()
{
	LockServer();
	Players.LockAccountAccess(Account->m_Name);
	const bool bBanned = ::SendMessageA(BannedCheck.m_hWnd, BM_GETCHECK, 0, 0) != 0;
	if (Account->m_bNotBanned == bBanned)
	{
		Players.BanAccount(Account->m_Name, bBanned);
	}
	Players.UnlockAccountAccess(Account->m_Name);
	UnlockServer();
	EndDialog(IDCANCEL);
}
