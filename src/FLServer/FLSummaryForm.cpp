// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "FLSummaryForm.h"
#include "FLServer.h"           // GApp
#include "FLServerDoc.h"        // CFLServerDoc::GetAverageLoad / GetStats
#include "ResourceStrings.h"    // LoadResourceString
#include "RichEditText.h"       // SetRichEditText
#include "ServerControl.h"      // LoadStringChecked

#include <stdio.h>
#include <string.h>

// CFLSummaryForm TU, retail .text:0040e8b0..0040f9e9.

namespace
{
	// The banner's text colour (.text:0040f820, 0040f9b0).
	const COLORREF GBannerTextColor = 0xe0c387;

	// Inlined twice in retail (.text:0040ef56..0040efb7 and 0040f103..0040f164): puts Text
	// into the banner, then invalidates the banner's rectangle in the form and the banner
	// itself and updates both.
	void ShowBannerText(CWnd* Form, CRichEditCtrl& Banner, const wchar_t* Text)
	{
		SetRichEditText(Banner.m_hWnd, Text);
		RECT Rect;
		::GetWindowRect(Banner.m_hWnd, &Rect);
		Form->ScreenToClient(&Rect);
		::InvalidateRect(Form->m_hWnd, &Rect, TRUE);
		::InvalidateRect(Banner.m_hWnd, NULL, TRUE);
		::UpdateWindow(Form->m_hWnd);
		::UpdateWindow(Banner.m_hWnd);
	}
}

// IMPLEMENT_DYNCREATE(CFLSummaryForm, CFormView) spelled out (MFC 14 afx.h IMPLEMENT_DYNCREATE
// -> IMPLEMENT_RUNTIMECLASS, _AFXDLL branch) with the one difference that the runtime class name
// is "FLSummaryForm": retail's CRuntimeClass .rdata:0041d69c points at .data:00425944
// "FLSummaryForm" (no C), schema 0xFFFF, CreateObject .text:0040e8b0, _GetBaseClass .text:0040e910.
CObject* PASCAL CFLSummaryForm::CreateObject()
{
	return new CFLSummaryForm;
}

#ifdef _AFXDLL
CRuntimeClass* PASCAL CFLSummaryForm::_GetBaseClass()
{
	return RUNTIME_CLASS(CFormView);
}

AFX_COMDAT const CRuntimeClass CFLSummaryForm::classCFLSummaryForm = {
	"FLSummaryForm", sizeof(class CFLSummaryForm), 0xFFFF, CFLSummaryForm::CreateObject,
	&CFLSummaryForm::_GetBaseClass, NULL, NULL };

CRuntimeClass* PASCAL CFLSummaryForm::GetThisClass()
{
	return _RUNTIME_CLASS(CFLSummaryForm);
}

CRuntimeClass* CFLSummaryForm::GetRuntimeClass() const
{
	return _RUNTIME_CLASS(CFLSummaryForm);
}
#else
AFX_COMDAT const CRuntimeClass CFLSummaryForm::classCFLSummaryForm = {
	"FLSummaryForm", sizeof(class CFLSummaryForm), 0xFFFF, CFLSummaryForm::CreateObject,
	RUNTIME_CLASS(CFormView), NULL, NULL };

CRuntimeClass* CFLSummaryForm::GetRuntimeClass() const
{
	return RUNTIME_CLASS(CFLSummaryForm);
}
#endif

// .rdata:0041d700 (entries .rdata:0041d6b8).
BEGIN_MESSAGE_MAP(CFLSummaryForm, CFormView)
	ON_WM_TIMER()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

// .text:0040e930. Retail leaves ShownStats, bInitialized and the two resource handles
// unwritten until OnInitialUpdate; the port clears bInitialized here so the timer and OnUpdate
// paths never read an indeterminate bool.
CFLSummaryForm::CFLSummaryForm()
	: CFormView(IDD)
{
	bInitialized = false;
}

// .text:0040ec60 — the strings, the font and the brush, then the controls, are released by
// their destructors in reverse declaration order, as retail does explicitly.
CFLSummaryForm::~CFLSummaryForm()
{
}

// .text:0040edc0
void CFLSummaryForm::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 0x42b, Banner);
	DDX_Control(DX, 0x428, Title);
	DDX_Control(DX, 0x410, StatsList);
	DDX_Control(DX, 0x427, LoadText);
	DDX_Control(DX, 0x425, LoadBar);
}

// .text:0040ee50
void CFLSummaryForm::ScrollMarquee()
{
	if (!bInitialized || GApp == NULL)
	{
		return;
	}
	if (!GApp->IsStartupPending())
	{
		::KillTimer(m_hWnd, 0x2002);
		::SetTimer(m_hWnd, 0x2001, 1000, NULL);
		return;
	}
	// Rotate left by one character.
	wchar_t First = Marquee.at(0);
	Marquee.erase(0, 1);
	Marquee.append(1, First);
	ShowBannerText(this, Banner, Marquee.c_str());
}

// .text:0040f030
void CFLSummaryForm::Refresh()
{
	if (!bInitialized || GApp == NULL || GApp->IsShuttingDown())
	{
		return;
	}

	const wchar_t* Name = GApp->ServerControl.ServerName.c_str();
	if (ShownServerName.compare(0, ShownServerName.size(), Name, wcslen(Name)) != 0)
	{
		ShownServerName.assign(Name, wcslen(Name));
		ShowBannerText(this, Banner, ShownServerName.c_str());
	}

	CFLServerDoc* Document = (CFLServerDoc*)m_pDocument;
	if (Document == NULL)
	{
		return;
	}

	int Load = Document->GetAverageLoad();
	char Text[0x50];
	if ((int)::SendMessageA(LoadBar.m_hWnd, PBM_GETPOS, 0, 0) != Load)
	{
		::SendMessageA(LoadBar.m_hWnd, PBM_SETPOS, Load, 0);
		COLORREF Color;
		if ((unsigned int)Load <= 0x21)
		{
			Color = 0x1dbf3b;
		}
		else if ((unsigned int)Load <= 0x31)
		{
			Color = 0x52eaf5;
		}
		else
		{
			Color = 0x1d1dbf;
		}
		::SendMessageA(LoadBar.m_hWnd, PBM_SETBARCOLOR, 0, Color);
		CString Format;
		LoadStringChecked(Format, 0xf012);
		sprintf(Text, (LPCTSTR)Format, Load);
		LoadText.SetWindowText(Text);
	}

	ServerStats Stats;
	Document->GetStats(&Stats);
	// Row order of the list (OnInitialUpdate): players, logins, logouts, failed logins,
	// aborted missions, player deaths.
	if (Stats.PlayerCount != ShownStats.PlayerCount)
	{
		sprintf(Text, "%d", Stats.PlayerCount);
		StatsList.SetItem(0, 1, LVIF_TEXT, Text, 0, 0, 0, 0);
	}
	if (Stats.LoginSuccessCount != ShownStats.LoginSuccessCount)
	{
		sprintf(Text, "%d", Stats.LoginSuccessCount);
		StatsList.SetItem(1, 1, LVIF_TEXT, Text, 0, 0, 0, 0);
	}
	if (Stats.LogoutCount != ShownStats.LogoutCount)
	{
		sprintf(Text, "%d", Stats.LogoutCount);
		StatsList.SetItem(2, 1, LVIF_TEXT, Text, 0, 0, 0, 0);
	}
	if (Stats.LoginFailCount != ShownStats.LoginFailCount)
	{
		sprintf(Text, "%d", Stats.LoginFailCount);
		StatsList.SetItem(3, 1, LVIF_TEXT, Text, 0, 0, 0, 0);
	}
	if (Stats.AbortMissionCount != ShownStats.AbortMissionCount)
	{
		sprintf(Text, "%d", Stats.AbortMissionCount);
		StatsList.SetItem(4, 1, LVIF_TEXT, Text, 0, 0, 0, 0);
	}
	if (Stats.Field_0x10 != ShownStats.Field_0x10)
	{
		sprintf(Text, "%d", Stats.Field_0x10);
		StatsList.SetItem(5, 1, LVIF_TEXT, Text, 0, 0, 0, 0);
	}
	ShownStats = Stats;
}

// .text:0040f3d0
void CFLSummaryForm::OnInitialUpdate()
{
	bInitialized = false;
	CFormView::OnInitialUpdate();

	// Background: a DIB pattern brush of bitmap 0x9f; a solid brush only when attaching the
	// pattern brush fails.
	HRSRC Resource = ::FindResourceA(AfxGetResourceHandle(), MAKEINTRESOURCEA(0x9f), RT_BITMAP);
	if (Resource != NULL)
	{
		BackgroundResource = ::LoadResource(AfxGetResourceHandle(), Resource);
		if (BackgroundResource != NULL)
		{
			BackgroundBits = ::LockResource(BackgroundResource);
			if (BackgroundBits != NULL)
			{
				HBRUSH Brush = ::CreateDIBPatternBrushPt(BackgroundBits, DIB_RGB_COLORS);
				if (Brush != NULL)
				{
					if (!BackgroundBrush.Attach(Brush))
					{
						BackgroundBrush.Attach(::CreateSolidBrush(0x640a00));
					}
				}
			}
		}
	}

	{
		CClientDC Dc(this);
		if (TitleFont.CreatePointFont(0x1e0, "Agency FB", &Dc))
		{
			::SendMessageA(Title.m_hWnd, WM_SETFONT, (WPARAM)(HFONT)TitleFont, TRUE);
		}
		Title.ShowWindow(SW_SHOW);
	}

	::SendMessageA(LoadBar.m_hWnd, PBM_SETRANGE, 0, MAKELPARAM(0, 0x42));
	::SendMessageA(LoadBar.m_hWnd, PBM_SETPOS, 0, 0);

	RECT ListRect;
	::GetWindowRect(StatsList.m_hWnd, &ListRect);
	{
		CString Heading;
		LoadStringChecked(Heading, 0xf013);
		StatsList.InsertColumn(0, Heading, LVCFMT_LEFT, (ListRect.right - ListRect.left) / 2 - 4, 0);
	}
	{
		CString Heading;
		LoadStringChecked(Heading, 0xf014);
		StatsList.InsertColumn(1, Heading, LVCFMT_LEFT, (ListRect.right - ListRect.left) / 2 - 4, 1);
	}
	// Rows 0..5: strings 0xf015..0xf01a.
	for (int Row = 0; Row < 6; ++Row)
	{
		CString Label;
		LoadStringChecked(Label, 0xf015 + Row);
		StatsList.InsertItem(LVIF_TEXT, Row, Label, 0, 0, 0, 0);
	}
	::SendMessageA(StatsList.m_hWnd, LVM_SETCOLUMNWIDTH, 0, 0xffff);
	::SendMessageA(StatsList.m_hWnd, LVM_SETCOLUMNWIDTH, 1, 0xfffe);
	LoadText.SetWindowText("");

	memset(&ShownStats, 0xff, sizeof(ShownStats));

	// The default format is a CHARFORMAT2 (cbSize 0x54) carrying only the text colour.
	CHARFORMAT2A Format;
	memset(&Format, 0, sizeof(Format));
	Format.cbSize = 0x54;
	Format.dwMask = CFM_COLOR;
	Format.crTextColor = GBannerTextColor;
	Banner.SetDefaultCharFormat(Format);
	::SendMessageA(Banner.m_hWnd, EM_SETBKGNDCOLOR, 0, 0xff);

	wchar_t Text[0x100];
	LoadResourceString(AfxGetResourceHandle(), 0xf01e, Text, 0x100);
	Marquee.assign(Text, wcslen(Text));

	::SetTimer(m_hWnd, 0x2002, 100, NULL);
	bInitialized = true;
	Refresh();
}

// .text:0040f950
void CFLSummaryForm::OnTimer(UINT_PTR IdEvent)
{
	if (IdEvent == 0x2001)
	{
		Refresh();
		return;
	}
	if (IdEvent == 0x2002)
	{
		ScrollMarquee();
		return;
	}
	Default();
}

// .text:0040f980
void CFLSummaryForm::OnUpdate(CView* Sender, LPARAM Hint, CObject* HintObject)
{
	Refresh();
}

// .text:0040f990 — statics get the banner colour on a transparent background; statics, the
// dialog, edits and message boxes paint with the background brush; everything else keeps the
// base brush.
HBRUSH CFLSummaryForm::OnCtlColor(CDC* Dc, CWnd* Wnd, UINT CtlColor)
{
	HBRUSH Result = CWnd::OnCtlColor(Dc, Wnd, CtlColor);
	if (CtlColor == CTLCOLOR_STATIC)
	{
		Dc->SetTextColor(GBannerTextColor);
		Dc->SetBkMode(TRANSPARENT);
		return (HBRUSH)BackgroundBrush.m_hObject;
	}
	if (CtlColor == CTLCOLOR_DLG || CtlColor == CTLCOLOR_EDIT || CtlColor == CTLCOLOR_MSGBOX)
	{
		return (HBRUSH)BackgroundBrush.m_hObject;
	}
	return Result;
}
