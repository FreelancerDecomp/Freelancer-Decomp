// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "FLServerView.h"
#include "FLServerDoc.h"
#include "FLServer.h"

#include "dacom.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

// .data:00427900 — guards the console view pointer, the FDUMP swap and the log buffer.
// Retail creates it from the static initializer at .text:0040ddc0 (the tail of the document
// TU's initializers); it is defined here next to its only users.
HANDLE GLogMutex = CreateMutexA(NULL, FALSE, NULL);

// .data:00427d2c — the view ServerLogf writes to (set by the view ctor, cleared by its dtor).
static CFLServerView* GLogView = NULL;
// .data:00427d28 — DACOM's FDUMP before the view replaced it.
static void(__cdecl* GPrevFdump)(int, const char*, ...) = NULL;
// .data:00427920 — the formatted line. Retail's buffer is 0x400 bytes and the '\n' append can
// store its terminator one byte past it (into .data:00427d20); the extra byte keeps that
// store inside this object.
static char GLogBuffer[0x400 + 1];
// .data:004258d4 — running line number of c:\fl-server-errors.log (starts at 1).
static int GErrorLogLine = 1;

// Text colours (.data:004258b8..004258d0, COLORREF 0x00bbggrr).
static const COLORREF GColorError = 0x000000ff;     // .data:004258b8 levels 0 and 1
static const COLORREF GColorDefault = 0x0000ff00;   // .data:004258bc
static const COLORREF GColorWarning = 0x0000ffff;   // .data:004258c0 level 2
static const COLORREF GColorPrompt = 0x00ffff00;    // .data:004258cc lines with '>' at [5]
static const COLORREF GColorText = 0x00ffffff;      // .data:004258d0 OnInitialUpdate

// The CHARFORMAT every call site builds on the stack: zeroed, cbSize = sizeof(CHARFORMATA)
// (0x3c), dwMask = CFM_COLOR, crTextColor = Color.
static void SetViewTextColor(CRichEditView* View, COLORREF Color)
{
	CHARFORMAT2 Format;
	memset(&Format, 0, sizeof(Format));
	Format.cbSize = sizeof(CHARFORMATA);
	Format.dwMask = CFM_COLOR;
	Format.crTextColor = Color;
	View->SetCharFormat(Format);
}

// IMPLEMENT_DYNCREATE(CFLServerView, CRichEditView): CRuntimeClass .rdata:0041d49c
// {"CFLServerView", 0x188, 0xffff, CreateObject 0040dde0, _GetBaseClass 0040de90};
// GetRuntimeClass 0040dea0.
IMPLEMENT_DYNCREATE(CFLServerView, CRichEditView)

// .rdata:0041d548 (GetMessageMap 0040dec0, base CRichEditView::messageMap 0040deb0),
// entries .rdata:0041d4b8.
BEGIN_MESSAGE_MAP(CFLServerView, CRichEditView)
	ON_WM_DESTROY()
	ON_WM_KEYDOWN()
	ON_COMMAND(ID_FILE_PRINT, &CRichEditView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CRichEditView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CRichEditView::OnFilePrintPreview)
END_MESSAGE_MAP()

// .text:0040ded0
CFLServerView::CFLServerView()
{
	LineCount = 0;
	WaitForSingleObject(GLogMutex, INFINITE);
	GLogView = this;
	GPrevFdump = FDUMP;
	FDUMP = (void(__cdecl*)(int, const char*, ...))ServerLogf;
	ReleaseMutex(GLogMutex);
}

// .text:0040df40
CFLServerView::~CFLServerView()
{
	WaitForSingleObject(GLogMutex, INFINITE);
	FDUMP = GPrevFdump;
	GLogView = NULL;
	ReleaseMutex(GLogMutex);
}

// .text:0040e040
BOOL CFLServerView::PreCreateWindow(CREATESTRUCT& Cs)
{
	return CRichEditView::PreCreateWindow(Cs);
}

// .text:0040e050
void CFLServerView::OnInitialUpdate()
{
	CRichEditView::OnInitialUpdate();
	SetViewTextColor(this, GColorText);
	::SendMessageA(m_hWnd, EM_SETREADONLY, TRUE, 0);
	::SendMessageA(m_hWnd, EM_SETBKGNDCOLOR, 0, 0);
	m_rectMargin.SetRect(720, 720, 720, 720);
	if (GApp != NULL)
	{
		CString Title;
		LoadStringChecked(Title, 0xf00d);
		GApp->GetMainWnd()->SetWindowText(Title);
	}
}

// .text:0040e160
BOOL CFLServerView::OnPreparePrinting(CPrintInfo* Info)
{
	return DoPreparePrinting(Info);
}

// .text:0040e170 — deactivate an in-place item that belongs to this view.
// Retail order: base OnDestroy first, then the document query (vtbl+0xb8). CRichEditView::
// OnDestroy Release()s m_lpRichEditOle without clearing it, and CRichEditDoc::
// GetInPlaceActiveItem then calls IRichEditOle::GetObject through that pointer. Under MFC 4.2
// with the 2003 riched20 the control still held the object, so the call was harmless; the
// MFC 14 / msftedit control frees it on that Release and the query jumped through a dead
// vtable. One extra reference across the retail-order query keeps the object alive exactly as
// the old control did (deviations.md: "FLServer: IRichEditOle reference across OnDestroy").
void CFLServerView::OnDestroy()
{
	IRichEditOle* const RichEditOle = m_lpRichEditOle;
	if (RichEditOle != NULL)
	{
		RichEditOle->AddRef();
	}
	CRichEditView::OnDestroy();
	COleClientItem* ActiveItem = GetDocument()->GetInPlaceActiveItem(this);
	if (ActiveItem != NULL && ActiveItem->GetActiveView() == this)
	{
		ActiveItem->Deactivate();
	}
	if (RichEditOle != NULL)
	{
		RichEditOle->Release();
	}
}

// .text:0040e680
void CFLServerView::OnKeyDown(UINT Char, UINT RepCnt, UINT Flags)
{
	CRichEditView::OnKeyDown(Char, RepCnt, Flags);
}

// .text:0040e280
bool CFLServerView::TrimLines()
{
	CRichEditCtrl& Ctrl = GetRichEditCtrl();
	int Chars = 0;
	for (unsigned int Line = 0; Line < GApp->ServerControl.ConsoleTrimLines; ++Line)
	{
		Chars += Ctrl.LineLength(Chars + 1) + 1;
	}
	if (Chars > 0)
	{
		Ctrl.SetSel(0, Chars);
		::SendMessageA(m_hWnd, EM_SETREADONLY, FALSE, 0);
		::SendMessageA(m_hWnd, WM_CLEAR, 0, 0);
		::SendMessageA(m_hWnd, EM_SETREADONLY, TRUE, 0);
		Ctrl.SetSel(-1, -1);
		int Lines = (int)::SendMessageA(m_hWnd, EM_GETLINECOUNT, 0, 0);
		Ctrl.LineScroll(Lines, 0);
	}
	return true;
}

// .text:0040e330
int __cdecl ServerLogf(ErrorCode Code, const char* Format, ...)
{
	WaitForSingleObject(GLogMutex, INFINITE);

	FServerControl& Control = GApp->ServerControl;
	int Level = ((int)(Code.Value << 28)) >> 28;
	if (Level > Control.LogLevel)
	{
		ReleaseMutex(GLogMutex);
		return 0;
	}
	if (Level != 0)
	{
		int Category = (int)Code.Value >> 4;
		if (std::find(Control.LogCategories.begin(), Control.LogCategories.end(), Category) == Control.LogCategories.end())
		{
			ReleaseMutex(GLogMutex);
			return 0;
		}
	}

	va_list Args;
	va_start(Args, Format);
	_vsnprintf(GLogBuffer, 0x400, Format, Args);
	va_end(Args);
	size_t Length = strlen(GLogBuffer);
	// Retail reads GLogBuffer[-1] (a zero padding byte) for an empty line.
	if ((Length == 0 || GLogBuffer[Length - 1] != '\n') && Length < 0x400)
	{
		GLogBuffer[Length] = '\n';
		++Length;
		GLogBuffer[Length] = '\0';
	}
	OutputDebugStringA(GLogBuffer);

	if (GLogView == NULL)
	{
		ReleaseMutex(GLogMutex);
		return 0;
	}
	SetViewTextColor(GLogView, GColorDefault);
	if (Level == 2)
	{
		SetViewTextColor(GLogView, GColorWarning);
	}
	if (Level == 1 || Level == 0)
	{
		SetViewTextColor(GLogView, GColorError);
	}
	if (GLogBuffer[5] == '>')
	{
		SetViewTextColor(GLogView, GColorPrompt);
	}
	GLogView->GetRichEditCtrl().SetSel(-1, -1);
	::SendMessageA(GLogView->m_hWnd, EM_REPLACESEL, 0, (LPARAM)GLogBuffer);

	++GLogView->LineCount;
	if (GLogView->LineCount > Control.MaxConsoleLines && GLogView->TrimLines())
	{
		GLogView->LineCount -= Control.ConsoleTrimLines;
	}
	if (Control.bLogOpen)
	{
		Control.WriteLog(GLogBuffer, (int)Length);
	}
	ReleaseMutex(GLogMutex);

	if (Level == 0 || Level == 1)
	{
		FILE* File = fopen("c:\\fl-server-errors.log", "a+");
		if (File != NULL)
		{
			if (GErrorLogLine == 1)
			{
				unsigned int VersionHigh = GServerVersion >> 16;
				fprintf(File, "****** New Log (Version %d.%d Build %d) ******\n",
					(VersionHigh >> 8) & 0xff, VersionHigh & 0xff, GServerVersion & 0xffff);
			}
			fprintf(File, "%6x: %s\n", GErrorLogLine++, GLogBuffer);
			fclose(File);
		}
	}
	return 0;
}
