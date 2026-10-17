// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "AccountsDlg.h"
#include "AreYouSureDlg.h"
#include "EditAccountDlg.h"
#include "FLServer.h"
#include "server_playerdb.h"

namespace
{
	// Character rows store their distance below the account row (1, 2, ...); account rows
	// store the CAccount pointer. Retail tells them apart with an unsigned `<= 0x80`.
	const DWORD_PTR MaxCharacterRowData = 0x80;
}

// .text:00412aa0
CAccountsDlg::CAccountsDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
}

// .text:00412b90
void CAccountsDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 3, DeleteAccountButton);
	DDX_Control(DX, 2, DeleteCharacterButton);
	DDX_Control(DX, 1045, BannedLabel);
	DDX_Control(DX, 1029, EditAccountButton);
	DDX_Control(DX, 1040, AccountList);
}

// Message map .rdata:0041dec8.
BEGIN_MESSAGE_MAP(CAccountsDlg, CDialog)
	ON_WM_MEASUREITEM()                             // 00412c50
	ON_LBN_SELCHANGE(1040, &CAccountsDlg::OnSelChangeList) // 00412f00
	ON_BN_CLICKED(1029, &CAccountsDlg::OnEditAccount)      // 00413030
	ON_LBN_DBLCLK(1040, &CAccountsDlg::OnDblClkList)       // 00413180
	ON_BN_CLICKED(2, &CAccountsDlg::OnDeleteCharacter)     // 00413190
	ON_BN_CLICKED(3, &CAccountsDlg::OnDeleteAccount)       // 00413380
	ON_WM_CTLCOLOR()                                // 00413440
END_MESSAGE_MAP()

// .text:00412c30
BOOL CAccountsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	RefreshAccountList();
	return TRUE;
}

// .text:00412c50 — rows are as tall as the list box's regular font.
void CAccountsDlg::OnMeasureItem(int IDCtl, LPMEASUREITEMSTRUCT MeasureInfo)
{
	if (IDCtl != 1040)
	{
		CDialog::OnMeasureItem(IDCtl, MeasureInfo);
		return;
	}
	CDC MeasureDC;
	MeasureDC.Attach(::CreateCompatibleDC(nullptr));
	CFont* OldFont = MeasureDC.SelectObject(&AccountList.RegularFont);
	TEXTMETRICA Metrics;
	MeasureDC.GetTextMetrics(&Metrics);
	MeasureDC.SelectObject(OldFont);
	MeasureInfo->itemHeight = Metrics.tmHeight;
}

// .text:00412d20
void CAccountsDlg::UpdateAccountStyle(CAccount* Account)
{
	bool bBold;
	if (!Account->m_bNotBanned)
	{
		AccountList.SetItemColor((DWORD_PTR)Account, RGB(0xff, 0, 0));
		bBold = true;
	}
	else if (Account->m_bAdmin)
	{
		AccountList.SetItemColor((DWORD_PTR)Account, RGB(0, 0x80, 0));
		bBold = true;
	}
	else
	{
		AccountList.RemoveItemColor((DWORD_PTR)Account);
		bBold = false;
	}
	AccountList.SetItemBold((DWORD_PTR)Account, bBold);
	AccountList.SetItemItalic((DWORD_PTR)Account, Account->m_ClientID != 0);
	::InvalidateRect(m_hWnd, nullptr, TRUE);
}

// .text:00412d90
void CAccountsDlg::RefreshAccountList()
{
	const HWND List = AccountList.m_hWnd;
	const LRESULT Selection = ::SendMessageA(List, LB_GETCURSEL, 0, 0);
	::SendMessageA(List, LB_GETTOPINDEX, 0, 0); // result unused in retail
	::SendMessageA(List, LB_RESETCONTENT, 0, 0);
	LockServer();
	std::map<CiWString, CAccount*>::iterator It = Players.m_AccountNameMap.begin();
	while (It != Players.m_AccountNameMap.end())
	{
		CAccount* Account = It->second;
		AccountList.AddItem(Account->m_Name.c_str(), (DWORD_PTR)Account);
		DWORD_PTR Row = 1;
		std::list<CiWString>::iterator Character = Account->m_Characters.begin();
		while (Character != Account->m_Characters.end())
		{
			wchar_t Line[0x400] = L"    ";
			wcscat_s(Line, Character->c_str());
			AccountList.AddItem(Line, Row);
			++Character;
			++Row;
		}
		UpdateAccountStyle(Account);
		++It;
	}
	::SendMessageA(List, LB_SETTOPINDEX, (WPARAM)Selection, 0);
	::SendMessageA(List, LB_SETCURSEL, (WPARAM)Selection, 0);
	UnlockServer();
}

// .text:00412f00
void CAccountsDlg::OnSelChangeList()
{
	const HWND List = AccountList.m_hWnd;
	int Selection = (int)::SendMessageA(List, LB_GETCURSEL, 0, 0);
	if (Selection <= -1)
	{
		DeleteCharacterButton.EnableWindow(FALSE);
		EditAccountButton.EnableWindow(FALSE);
		DeleteAccountButton.EnableWindow(FALSE);
		return;
	}

	AccountList.SetFocus();
	const DWORD_PTR Data = (DWORD_PTR)::SendMessageA(List, LB_GETITEMDATA, Selection, 0);
	DWORD_PTR AccountData;
	if (Data <= MaxCharacterRowData)
	{
		AccountData = (DWORD_PTR)::SendMessageA(List, LB_GETITEMDATA, Selection - Data, 0);
	}
	else
	{
		AccountData = Data;
	}

	const int Last = (int)::SendMessageA(List, LB_GETCOUNT, 0, 0) - 1;
	if (Selection > Last)
	{
		Selection = Last;
	}
	const DWORD_PTR SelectedData = (DWORD_PTR)::SendMessageA(List, LB_GETITEMDATA, Selection, 0);
	if (Selection > 0 && SelectedData > MaxCharacterRowData && SelectedData != AccountData)
	{
		--Selection;
	}
	::SendMessageA(List, LB_SETCURSEL, Selection, 0);

	if (Data <= MaxCharacterRowData)
	{
		DeleteCharacterButton.EnableWindow(TRUE);
		EditAccountButton.EnableWindow(TRUE);
		DeleteAccountButton.EnableWindow(FALSE);
	}
	else
	{
		DeleteCharacterButton.EnableWindow(FALSE);
		EditAccountButton.EnableWindow(TRUE);
		DeleteAccountButton.EnableWindow(TRUE);
	}
}

// .text:00413030
void CAccountsDlg::OnEditAccount()
{
	const HWND List = AccountList.m_hWnd;
	const int Selection = (int)::SendMessageA(List, LB_GETCURSEL, 0, 0);
	if (Selection <= -1)
	{
		return;
	}
	const DWORD_PTR Data = (DWORD_PTR)::SendMessageA(List, LB_GETITEMDATA, Selection, 0);
	DWORD_PTR AccountData;
	if (Data <= MaxCharacterRowData)
	{
		AccountData = (DWORD_PTR)::SendMessageA(List, LB_GETITEMDATA, Selection - Data, 0);
	}
	else
	{
		AccountData = (DWORD_PTR)::SendMessageA(List, LB_GETITEMDATA, Selection, 0);
	}

	CEditAccountDlg Dlg(nullptr);
	Dlg.Account = (CAccount*)AccountData;
	Dlg.DoModal();
	RefreshAccountList();
	OnSelChangeList();
}

// .text:00413180 — JMP 00413030.
void CAccountsDlg::OnDblClkList()
{
	OnEditAccount();
}

// .text:00413190
void CAccountsDlg::OnDeleteCharacter()
{
	CAreYouSureDlg Confirm(nullptr);
	if (Confirm.DoModal() != IDOK)
	{
		return;
	}
	const int Selection = (int)::SendMessageA(AccountList.m_hWnd, LB_GETCURSEL, 0, 0);
	if (Selection <= -1)
	{
		return;
	}

	CiWString CharacterName;
	AccountList.GetItemText(Selection, CharacterName);
	// The row reads "    <name>": drop the indent.
	wchar_t Row[0x400];
	wcscpy_s(Row, CharacterName.c_str());
	const wchar_t* Name = Row + wcslen(L"    ");
	CharacterName.assign(Name, wcslen(Name));

	LockServer();
	CAccount* Account = Players.FindAccountFromCharacterName(CharacterName);
	Players.LockAccountAccess(Account->m_Name);
	Players.DeleteCharacterFromName(CharacterName);
	Players.UnlockAccountAccess(Account->m_Name);
	UnlockServer();

	::SendMessageA(AccountList.m_hWnd, LB_DELETESTRING, Selection, 0);
	DeleteCharacterButton.EnableWindow(FALSE);
	RefreshAccountList();
	OnSelChangeList();
}

// .text:00413380
void CAccountsDlg::OnDeleteAccount()
{
	CAreYouSureDlg Confirm(nullptr);
	if (Confirm.DoModal() != IDOK)
	{
		return;
	}
	const int Selection = (int)::SendMessageA(AccountList.m_hWnd, LB_GETCURSEL, 0, 0);
	if (Selection <= -1)
	{
		return;
	}
	LockServer();
	CAccount* Account = (CAccount*)::SendMessageA(AccountList.m_hWnd, LB_GETITEMDATA, Selection, 0);
	Players.DeleteAccount(Account->m_Name);
	UnlockServer();
	RefreshAccountList();
	OnSelChangeList();
}

// .text:00413440 — the "* Red accounts are banned" label is drawn red.
HBRUSH CAccountsDlg::OnCtlColor(CDC* DC, CWnd* Wnd, UINT CtlColor)
{
	HBRUSH Brush = CDialog::OnCtlColor(DC, Wnd, CtlColor);
	if (Wnd == &BannedLabel)
	{
		DC->SetTextColor(RGB(0xff, 0, 0));
	}
	return Brush;
}
