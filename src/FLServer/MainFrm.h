// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CMainFrame — FLServer.exe's main frame window (retail CreateObject .text:00411ea0, ctor
// 00411f40, vtable .rdata:0041dda8, CRuntimeClass .rdata:0041dd0c, message map
// .rdata:0041dda0; TU 00411ea0..00412a80). Owns the status bar with the uptime pane, a 1 s
// timer that samples RemoteClient's per-client traffic counters and the optional bandwidth CSV
// log (+0x20c).
#pragma once

#include <stdio.h>

class CMainFrame : public CFrameWnd
{
protected:
	CMainFrame();                                         // .text:00411f40
	DECLARE_DYNCREATE(CMainFrame)

public:
	virtual ~CMainFrame();                                // .text:00412020

	// .text:004120b0 — clears WS_THICKFRAME (0x40000) from the default style, then the base.
	virtual BOOL LoadFrame(UINT IdResource, DWORD DefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
		CWnd* ParentWnd = NULL, CCreateContext* Context = NULL) override;
	// .text:00412180
	virtual BOOL PreCreateWindow(CREATESTRUCT& Cs) override;

	// .text:004128b0 — (re)opens the bandwidth CSV log; called by
	// CFLServerApp::OnServerStartupFinished (.text:0040c0e1).
	void OpenBandwidthLog(const char* Path);
	// .text:00412950 — closes the bandwidth log. No caller in retail (no call/jmp/abs ref).
	void CloseBandwidthLog();

	// CCmdUI handlers with no message-map entry and no caller in retail (strings 0xf028..0xf02c).
	void OnUpdateTraffic(CCmdUI* CmdUI);                  // .text:004121a0
	void OnUpdateConnects(CCmdUI* CmdUI);                 // .text:00412270
	void OnUpdateThroughput(CCmdUI* CmdUI);               // .text:00412300
	void OnUpdatePacketsOut(CCmdUI* CmdUI);               // .text:004123a0
	void OnUpdatePacketsIn(CCmdUI* CmdUI);                // .text:00412430

protected:
	afx_msg int OnCreate(LPCREATESTRUCT CreateStruct);    // .text:004120d0
	afx_msg void OnUpdateUptime(CCmdUI* CmdUI);           // .text:004124c0
	afx_msg void OnTimer(UINT_PTR IdEvent);               // .text:00412670
	afx_msg void OnClose();                               // .text:00412980

	DECLARE_MESSAGE_MAP()

public:
	CTime StartTime;                 // +0xc0: CTime::GetCurrentTime() in the ctor
	CStatusBar StatusBar;            // +0xc4
	CToolBar ToolBar;                // +0x144 (never created)
	// Traffic summed over every client at the last 1 s sample (OnTimer).
	unsigned int TotalBytes;         // +0x1e4 sent + received
	unsigned int BytesReceived;      // +0x1e8
	unsigned int BytesSent;          // +0x1ec
	unsigned int PrevTotalBytes;     // +0x1f0 TotalBytes of the previous sample
	unsigned int ThroughputBits;     // +0x1f4 bits per second between the last two samples
	unsigned int PacketsSent;        // +0x1f8
	unsigned int PacketsReceived;    // +0x1fc
	unsigned int PrevSampleTime;     // +0x200 timeGetTime of the previous sample
	unsigned int SampleTime;         // +0x204 timeGetTime of the last sample
	int ClientCount;                 // +0x208 GetNumClients at the last sample
	FILE* BandwidthLog;              // +0x20c
};
