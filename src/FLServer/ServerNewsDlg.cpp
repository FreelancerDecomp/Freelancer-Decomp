// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "ServerNewsDlg.h"
#include "RichEditText.h"
#include "Motd.h"
#include "FLServer.h"
#include "server_playerdb.h"

namespace
{
	// EM_EXLIMITTEXT limit and EM_GETTEXTEX byte count used by the dialog (0x1400).
	const int NewsTextBytes = 0x1400;
}

// .text:00413560
CServerNewsDlg::CServerNewsDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
}

// .text:004135e0
void CServerNewsDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 1058, NewsEdit);
}

// .rdata:0041e078: no entries.
BEGIN_MESSAGE_MAP(CServerNewsDlg, CDialog)
END_MESSAGE_MAP()

// .text:00413730
BOOL CServerNewsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	LockServer();
	::SendMessageA(NewsEdit.m_hWnd, EM_EXLIMITTEXT, 0, NewsTextBytes);
	SetRichEditText(NewsEdit.m_hWnd, GMotd.c_str());
	UnlockServer();
	return TRUE;
}

// .text:00413620
void CServerNewsDlg::OnOK()
{
	wchar_t Text[NewsTextBytes];
	GetRichEditText(NewsEdit.m_hWnd, Text, NewsTextBytes);
	LockServer();
	GMotd = Text;
	SaveMotd();
	Players.SetMOTD(GMotd);
	UnlockServer();
	CDialog::OnOK();
}

// .text:00413780
void ApplyServerNews()
{
	LockServer();
	Players.SetMOTD(GMotd);
	UnlockServer();
}
