// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include <stdio.h>
#include <wchar.h>
#include <imm.h>

#include "ServerConfigDlg.h"
#include "FLServer.h"           // GApp, FServerControl
#include "RichEditText.h"
#include "msvcrt_compat.h"      // MsvcrtAtoi
#include "server_playerdb.h"    // Players

namespace
{
	// ServerResources.dll string table.
	const UINT IdsBadMaxPlayers = 0xf006;   // "...That number must be between 1 and 128."
	const UINT IdsNoServerName = 0xf007;    // "You must provide a name for your server."
	const UINT IdsLegalNotice = 0xf02d;

	// FLServer.cfg — written and read field by field in this order (binary, "wb" / "rb").
	// Version 2 files lack the trailing bAllowPvp byte. Retail keeps every field as a separate
	// local of OnInitDialog / OnOK.
	struct FServerConfigFile
	{
		int Version;                   // 3 when written
		wchar_t ServerName[0x21];      // 0x42 bytes
		wchar_t Description[0x81];     // 0x102 bytes
		wchar_t Password[0x11];        // 0x22 bytes
		bool bAllowNewPlayers;         // 1 byte
		bool bInternetAccessible;      // 1 byte
		int MaxPlayers;                // 4 bytes
		bool bAllowPvp;                // 1 byte, version 3 only
	};

	const char* const ConfigFileName = "FLServer.cfg";
}

CServerConfigDlg::CServerConfigDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
	// Retail's inline ctor (.text:0040bd6c) leaves the control pointers unset; OnInitDialog
	// assigns them before anything reads them.
}

// .text:00407040 — empty (shared, ICF-folded `RET 4`).
void CServerConfigDlg::DoDataExchange(CDataExchange* DX)
{
	CDialog::DoDataExchange(DX);
}

// .text:004067f0
BOOL CServerConfigDlg::OnInitDialog()
{
	MaxPlayersSpin = GetDlgItem(1011);
	MaxPlayersEdit = GetDlgItem(1012);
	AllowNewPlayersCheck = GetDlgItem(1010);
	PvpCheck = GetDlgItem(1059);
	InternetCheck = GetDlgItem(1014);
	ServerNameEdit = GetDlgItem(1006);
	DescriptionEdit = GetDlgItem(1007);
	PasswordEdit = GetDlgItem(1008);
	LegalNotice = GetDlgItem(1070);

	::SendMessage(MaxPlayersEdit->m_hWnd, EM_LIMITTEXT, 3, 0);
	::SendMessage(ServerNameEdit->m_hWnd, EM_EXLIMITTEXT, 0, 0x20);
	::SendMessage(DescriptionEdit->m_hWnd, EM_EXLIMITTEXT, 0, 0x80);
	::SendMessage(PasswordEdit->m_hWnd, EM_EXLIMITTEXT, 0, 0x10);

	FServerConfigFile Config = {};
	Config.Version = 3;
	Config.bAllowPvp = false;
	Config.bAllowNewPlayers = true;
	Config.bInternetAccessible = true;
	Config.MaxPlayers = 0x10;

	char Path[0x400] = "";
	Players.MakeLocalUserPath(Path, ConfigFileName);
	FILE* File = fopen(Path, "rb");
	if (File != nullptr)
	{
		fread(&Config.Version, 4, 1, File);
		if (Config.Version == 2 || Config.Version == 3)
		{
			fread(Config.ServerName, sizeof(Config.ServerName), 1, File);
			fread(Config.Description, sizeof(Config.Description), 1, File);
			fread(Config.Password, sizeof(Config.Password), 1, File);
			fread(&Config.bAllowNewPlayers, 1, 1, File);
			fread(&Config.bInternetAccessible, 1, 1, File);
			fread(&Config.MaxPlayers, 4, 1, File);
			if (Config.Version == 3)
			{
				fread(&Config.bAllowPvp, 1, 1, File);
			}
		}
		fclose(File);
	}

	::SendMessage(MaxPlayersSpin->m_hWnd, UDM_SETRANGE, 0, MAKELONG(0x80, 1));
	::SendMessage(MaxPlayersSpin->m_hWnd, UDM_SETPOS, 0, (WORD)Config.MaxPlayers);
	::SendMessage(AllowNewPlayersCheck->m_hWnd, BM_SETCHECK, (BYTE)Config.bAllowNewPlayers, 0);
	::SendMessage(InternetCheck->m_hWnd, BM_SETCHECK, (BYTE)Config.bInternetAccessible, 0);
	::SendMessage(PvpCheck->m_hWnd, BM_SETCHECK, (BYTE)Config.bAllowPvp, 0);
	SetRichEditText(ServerNameEdit->m_hWnd, Config.ServerName);
	SetRichEditText(DescriptionEdit->m_hWnd, Config.Description);
	SetRichEditText(PasswordEdit->m_hWnd, Config.Password);
	::ImmAssociateContext(PasswordEdit->m_hWnd, nullptr);

	{
		CString LegalText;
		LoadStringChecked(LegalText, IdsLegalNotice);
		LegalNotice->SetWindowText(LegalText);
	}

	// Command-line /C switch (FServerControl+0x9ae, .text:00406b3b): with a saved configuration,
	// accept it without showing the dialog. Retail tests the (already closed) FILE pointer for
	// non-null.
	if (GApp->ServerControl.bAutoAcceptConfig && File != nullptr)
	{
		OnOK();
	}
	return TRUE;
}

// .text:00406b70
void CServerConfigDlg::OnOK()
{
	FServerConfigFile Config;
	Config.Version = 3;

	char MaxPlayersText[4];
	MaxPlayersEdit->GetWindowText(MaxPlayersText, 4);
	Config.MaxPlayers = MsvcrtAtoi(MaxPlayersText);
	GetRichEditText(ServerNameEdit->m_hWnd, Config.ServerName, sizeof(Config.ServerName));
	GetRichEditText(DescriptionEdit->m_hWnd, Config.Description, sizeof(Config.Description));
	GetRichEditText(PasswordEdit->m_hWnd, Config.Password, sizeof(Config.Password));
	Config.bAllowNewPlayers = ::SendMessage(AllowNewPlayersCheck->m_hWnd, BM_GETCHECK, 0, 0) != 0;
	Config.bInternetAccessible = ::SendMessage(InternetCheck->m_hWnd, BM_GETCHECK, 0, 0) != 0;
	Config.bAllowPvp = ::SendMessage(PvpCheck->m_hWnd, BM_GETCHECK, 0, 0) != 0;

	if (Config.MaxPlayers < 1 || Config.MaxPlayers > 0x80)
	{
		AfxMessageBox(IdsBadMaxPlayers, 0, (UINT)-1);
		::SendMessage(m_hWnd, WM_NEXTDLGCTL, (WPARAM)MaxPlayersEdit->m_hWnd, 1);
		return;
	}
	if (wcslen(Config.ServerName) == 0)
	{
		AfxMessageBox(IdsNoServerName, 0, (UINT)-1);
		::SendMessage(m_hWnd, WM_NEXTDLGCTL, (WPARAM)ServerNameEdit->m_hWnd, 1);
		return;
	}

	FServerControl& Control = GApp->ServerControl;
	Control.SetMaxPlayers(Config.MaxPlayers);
	Control.SetServerDescription(Config.Description);
	Control.SetServerName(Config.ServerName);
	Control.SetAllowNewPlayers(Config.bAllowNewPlayers);    // SStartupInfo byte, .text:00402370
	Control.bListServerReporting = Config.bInternetAccessible;
	Control.ServerPassword.assign(Config.Password, wcslen(Config.Password));
	Control.SetAllowPvp(Config.bAllowPvp);                  // SStartupInfo byte, .text:00402390

	Players.BuildLocalUserDir();
	char Path[0x400] = "";
	Players.MakeLocalUserPath(Path, ConfigFileName);
	FILE* File = fopen(Path, "wb");
	if (File != nullptr)
	{
		fwrite(&Config.Version, 4, 1, File);
		fwrite(Config.ServerName, sizeof(Config.ServerName), 1, File);
		fwrite(Config.Description, sizeof(Config.Description), 1, File);
		fwrite(Config.Password, sizeof(Config.Password), 1, File);
		fwrite(&Config.bAllowNewPlayers, 1, 1, File);
		fwrite(&Config.bInternetAccessible, 1, 1, File);
		fwrite(&Config.MaxPlayers, 4, 1, File);
		fwrite(&Config.bAllowPvp, 1, 1, File);
		fclose(File);
	}
	CDialog::OnOK();
}
