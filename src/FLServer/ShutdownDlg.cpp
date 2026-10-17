// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "ShutdownDlg.h"
#include "FLServer.h"

namespace
{
	// SetTimer id used by OnInitDialog (.text:00415ef6) and tested by OnTimer (.text:00415f90).
	const UINT_PTR ShutdownPollTimerId = 0x3001;
}

// .text:00415e40
CShutdownDlg::CShutdownDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
	, WaitThread(WaitStartupThread)
{
}

// .text:00415ea0 — retail body is empty (CWnd::DoDataExchange is an empty inline).
void CShutdownDlg::DoDataExchange(CDataExchange* DX)
{
	CDialog::DoDataExchange(DX);
}

// Message map .rdata:0041e5d8: WM_TCARD -> 00415f80, WM_TIMER -> 00415f90.
BEGIN_MESSAGE_MAP(CShutdownDlg, CDialog)
	ON_WM_TCARD()
	ON_WM_TIMER()
END_MESSAGE_MAP()

// .text:00415ee0 — starts the one-second poll timer and centres the box over the main window.
BOOL CShutdownDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	::SetTimer(m_hWnd, ShutdownPollTimerId, 1000, nullptr);

	RECT MainRect;
	RECT DlgRect;
	::GetWindowRect(GApp->m_pMainWnd->m_hWnd, &MainRect);
	::GetWindowRect(m_hWnd, &DlgRect);
	const int Y = (MainRect.top + MainRect.bottom - (DlgRect.bottom - DlgRect.top)) / 2;
	const int X = (MainRect.left + MainRect.right - (DlgRect.right - DlgRect.left)) / 2;
	SetWindowPos(nullptr, X, Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	return TRUE;
}

// .text:00415f80 — swallows WM_TCARD (retail body is a bare RET 8).
void CShutdownDlg::OnTCard(UINT Action, DWORD ActionData)
{
}

// .text:00415f90
void CShutdownDlg::OnTimer(UINT_PTR IDEvent)
{
	if (IDEvent != ShutdownPollTimerId)
	{
		Default(); // CWnd::OnTimer inline
		return;
	}

	bool bDone;
	if (WaitThread == WaitStartupThread)
	{
		bDone = GApp->PollStartupThread();
	}
	else if (WaitThread == WaitShutdownThread)
	{
		bDone = GApp->PollShutdownThread();
	}
	else
	{
		return;
	}

	if (bDone)
	{
		EndDialog(0);
	}
}
