// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "MainFrm.h"
#include "FLServer.h"           // GApp
#include "ServerControl.h"      // LoadStringChecked

#include "remoteclient.h"       // GetNumClients, GetClientStats, FClientStats

#include <mmsystem.h>
#include <sys/timeb.h>
#include <string.h>

// CMainFrame TU, retail .text:00411ea0..00412a80.

namespace
{
	// .data:00425978 — the status bar panes: the stretching message pane and the uptime pane
	// (0xe707, string 59143 "Time: 00:00:00" in ServerResources.dll).
	const UINT GStatusIndicators[] = { ID_SEPARATOR, 0xe707 };

	// .data:00427d98 / .data:00428528 — RemoteClient's per-client counters at the last and the
	// previous 1 s sample (16 records of 0x78 bytes each); OnTimer zeroes both once, guarded by
	// .data:00428cb0.
	FClientStats GClientStats[0x10];
	FClientStats GPrevClientStats[0x10];
	bool GbClientStatsCleared = false;

	// .rdata:0041dea8 (1/1024: bytes to KB) and .rdata:0041deb0 (0.001: bits to kbit).
	const double GBytesToKb = 0.0009765625;
	const double GBitsToKbit = 0.001;
}

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

// .rdata:0041dda0 (entries .rdata:0041dd28).
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_TIMER()
	ON_WM_CLOSE()
	ON_UPDATE_COMMAND_UI(0xe707, &CMainFrame::OnUpdateUptime)
END_MESSAGE_MAP()

// .text:00411f40
CMainFrame::CMainFrame()
{
	TotalBytes = 0;
	BytesReceived = 0;
	BytesSent = 0;
	PrevTotalBytes = 0;
	ThroughputBits = 0;
	PacketsSent = 0;
	PacketsReceived = 0;
	PrevSampleTime = 0;
	SampleTime = 0;
	ClientCount = 0;
	// winbase.h #defines GetCurrentTime() as GetTickCount(), so VC6 compiled CTime::GetCurrentTime
	// into CTime::GetTickCount (the MFC42 import retail calls); ATL's CTime names it the same.
	StartTime = CTime::GetTickCount();
	BandwidthLog = NULL;
}

// .text:00412020
CMainFrame::~CMainFrame()
{
	if (BandwidthLog != NULL)
	{
		fclose(BandwidthLog);
		BandwidthLog = NULL;
	}
}

// .text:004120b0
BOOL CMainFrame::LoadFrame(UINT IdResource, DWORD DefaultStyle, CWnd* ParentWnd, CCreateContext* Context)
{
	DefaultStyle &= ~WS_THICKFRAME;
	return CFrameWnd::LoadFrame(IdResource, DefaultStyle, ParentWnd, Context);
}

// .text:004120d0
int CMainFrame::OnCreate(LPCREATESTRUCT CreateStruct)
{
	if (CFrameWnd::OnCreate(CreateStruct) == -1)
	{
		return -1;
	}
	if (!StatusBar.Create(this, 0x50008200, 0xe801))
	{
		return -1;
	}
	if (!StatusBar.SetIndicators(GStatusIndicators, 2))
	{
		return -1;
	}
	::SetTimer(m_hWnd, 0x111, 1000, NULL);
	::SetTimer(m_hWnd, 0x112, 11000, NULL);
	CMenu* Menu = CMenu::FromHandle(::GetMenu(m_hWnd));
	if (Menu != NULL)
	{
		::DeleteMenu(Menu->m_hMenu, 5, MF_BYPOSITION);
	}
	return 0;
}

// .text:00412180 — the base's answer normalised to 0/1.
BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& Cs)
{
	return CFrameWnd::PreCreateWindow(Cs) != 0 ? TRUE : FALSE;
}

// .text:004121a0 — "%.2f kbi, %.2f kbo" (0xf028).
void CMainFrame::OnUpdateTraffic(CCmdUI* CmdUI)
{
	CString Format;
	LoadStringChecked(Format, 0xf028);
	char Text[0x40];
	sprintf(Text, (LPCTSTR)Format, (double)BytesReceived * GBytesToKb, (double)BytesSent * GBytesToKb);
	CmdUI->SetText(Text);
}

// .text:00412270 — "Connects: %02d" (0xf029).
void CMainFrame::OnUpdateConnects(CCmdUI* CmdUI)
{
	CString Format;
	LoadStringChecked(Format, 0xf029);
	char Text[0x40];
	sprintf(Text, (LPCTSTR)Format, ClientCount);
	CmdUI->SetText(Text);
}

// .text:00412300 — "%03.2f kbp/s" (0xf02a).
void CMainFrame::OnUpdateThroughput(CCmdUI* CmdUI)
{
	CString Format;
	LoadStringChecked(Format, 0xf02a);
	char Text[0x40];
	sprintf(Text, (LPCTSTR)Format, (double)ThroughputBits * GBitsToKbit);
	CmdUI->SetText(Text);
}

// .text:004123a0 — "PktOut: %05d" (0xf02b).
void CMainFrame::OnUpdatePacketsOut(CCmdUI* CmdUI)
{
	CString Format;
	LoadStringChecked(Format, 0xf02b);
	char Text[0x40];
	sprintf(Text, (LPCTSTR)Format, PacketsSent);
	CmdUI->SetText(Text);
}

// .text:00412430 — "PktIn: %05d" (0xf02c).
void CMainFrame::OnUpdatePacketsIn(CCmdUI* CmdUI)
{
	CString Format;
	LoadStringChecked(Format, 0xf02c);
	char Text[0x40];
	sprintf(Text, (LPCTSTR)Format, PacketsReceived);
	CmdUI->SetText(Text);
}

// .text:004124c0 — the uptime pane: "%D:%H:%M:%S" since the frame was built. Retail measures
// the text in the status bar's font and widens pane 0 (not the uptime pane) to that width
// with SBPS_STRETCH added.
void CMainFrame::OnUpdateUptime(CCmdUI* CmdUI)
{
	CString Uptime("00:00:00:00");
	CString Text("");
	CTimeSpan Span = CTime::GetTickCount() - StartTime;   // GetCurrentTime macro, see the ctor
	Uptime = Span.Format("%D:%H:%M:%S");
	Text = Text + Uptime;

	HFONT Font = (HFONT)::SendMessageA(StatusBar.m_hWnd, WM_GETFONT, 0, 0);
	SIZE Extent;
	{
		CClientDC Dc(NULL);
		HGDIOBJ OldFont = NULL;
		if (Font != NULL)
		{
			OldFont = ::SelectObject(Dc.m_hDC, Font);
		}
		::GetTextExtentPoint32A(Dc.m_hDC, (LPCTSTR)Text, Text.GetLength(), &Extent);
		if (OldFont != NULL)
		{
			::SelectObject(Dc.m_hDC, OldFont);
		}
	}

	UINT Id;
	UINT Style;
	int Width;
	StatusBar.GetPaneInfo(0, Id, Style, Width);
	Style |= SBPS_STRETCH;
	StatusBar.SetPaneInfo(0, Id, Style, Extent.cx);
	CmdUI->SetText(Text);
	CmdUI->Enable(TRUE);
}

// .text:00412670 — timer 0x111 (1 s): samples RemoteClient's per-client counters, sums them,
// appends a CSV row to the bandwidth log and computes the throughput. With no client, or more
// than 16, the sample stops early and the message is not passed on.
void CMainFrame::OnTimer(UINT_PTR IdEvent)
{
	if (IdEvent == 0x111)
	{
		if (!GbClientStatsCleared)
		{
			memset(GClientStats, 0, sizeof(GClientStats));
			memset(GPrevClientStats, 0, sizeof(GPrevClientStats));
			GbClientStatsCleared = true;
		}
		PrevTotalBytes = TotalBytes;
		PrevSampleTime = SampleTime;
		SampleTime = timeGetTime();
		ClientCount = GetNumClients();
		if (ClientCount == 0 || ClientCount > 0x10)
		{
			return;
		}

		if (BandwidthLog != NULL)
		{
			// Retail's _timeb has a 32-bit time field (VC6 MSVCRT ftime).
			struct __timeb32 Now;
			_ftime32(&Now);
			fprintf(BandwidthLog, "%d.%04d, %d", (int)Now.time, (int)Now.millitm, ClientCount);
		}

		TotalBytes = 0;
		BytesReceived = 0;
		BytesSent = 0;
		PacketsReceived = 0;
		PacketsSent = 0;
		GetClientStats(GClientStats, &ClientCount);
		for (int Index = 0; Index < ClientCount; ++Index)
		{
			const FClientStats& Stats = GClientStats[Index];
			BytesSent += Stats.BytesSentGuaranteed;
			BytesReceived += Stats.BytesReceivedGuaranteed;
			TotalBytes += Stats.BytesSentGuaranteed;
			TotalBytes += Stats.BytesReceivedGuaranteed;
			PacketsReceived += Stats.PacketsReceivedGuaranteed;
			PacketsSent += Stats.PacketsSentGuaranteed;
		}

		if (BandwidthLog != NULL)
		{
			// Every one of the 16 slots, as the change since the previous sample.
			for (int Index = 0; Index < 0x10; ++Index)
			{
				const FClientStats& Stats = GClientStats[Index];
				const FClientStats& Prev = GPrevClientStats[Index];
				fprintf(BandwidthLog, ", %d, %d, %d, %d",
					(int)(Stats.BytesSentGuaranteed - Prev.BytesSentGuaranteed),
					(int)(Stats.BytesReceivedGuaranteed - Prev.BytesReceivedGuaranteed),
					(int)(Stats.PacketsSentGuaranteed - Prev.PacketsSentGuaranteed),
					(int)(Stats.PacketsReceivedGuaranteed - Prev.PacketsReceivedGuaranteed));
			}
			fprintf(BandwidthLog, "\n");
			fflush(BandwidthLog);
		}

		unsigned int Seconds = (SampleTime - PrevSampleTime) / 1000;
		if (Seconds == 0)
		{
			Seconds = 0xffffffff;
		}
		ThroughputBits = ((TotalBytes - PrevTotalBytes) * 8) / Seconds;
		memcpy(GPrevClientStats, GClientStats, sizeof(GClientStats));
	}
	Default();
}

// .text:004128b0
void CMainFrame::OpenBandwidthLog(const char* Path)
{
	if (BandwidthLog != NULL)
	{
		fclose(BandwidthLog);
		BandwidthLog = NULL;
	}
	if (Path[0] == '\0')
	{
		return;
	}
	BandwidthLog = fopen(Path, "wt");
	if (BandwidthLog == NULL)
	{
		return;
	}
	fprintf(BandwidthLog, "Timestamp,Connection Count");
	for (int Client = 1; Client <= 0x10; ++Client)
	{
		fprintf(BandwidthLog, ", Bytes Xmit %d, Bytes Recv %d, Packets Xmit %d, Packets Recv %d",
			Client, Client, Client, Client);
	}
	fprintf(BandwidthLog, "\n");
}

// .text:00412950
void CMainFrame::CloseBandwidthLog()
{
	if (BandwidthLog != NULL)
	{
		fclose(BandwidthLog);
		BandwidthLog = NULL;
	}
}

// .text:00412980
void CMainFrame::OnClose()
{
	GApp->WaitForStartupThread();
	::KillTimer(m_hWnd, 0x111);
	::KillTimer(m_hWnd, 0x112);
	GApp->BeginShutdown();
	CFrameWnd::OnClose();
}
