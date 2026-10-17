// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CShutdownDlg — the modeless "Please wait while the server shuts down..." box (dialog 0xa0 in
// ServerResources.dll). Retail keeps one by value in CFLServerApp at +0xbac and polls a worker
// thread of the app once a second until it has finished, then closes itself.
// Retail: ctor .text:00415e40, vtable .rdata:0041e628, message map .rdata:0041e620.
#pragma once

class CShutdownDlg : public CDialog
{
public:
	enum { IDD = 0xa0 };

	// Which app worker thread the timer waits for (the dword at +0x60).
	enum EWaitThread
	{
		WaitStartupThread = 0,  // CFLServerApp::PollStartupThread  (.text:0040c100)
		WaitShutdownThread = 1, // CFLServerApp::PollShutdownThread (.text:0040b6a0)
	};

	explicit CShutdownDlg(CWnd* Parent = nullptr);

	// .text:00415ed0
	void SetWaitThread(int Thread) { WaitThread = Thread; }

protected:
	virtual void DoDataExchange(CDataExchange* DX);
	virtual BOOL OnInitDialog();

	afx_msg void OnTCard(UINT Action, DWORD ActionData);
	afx_msg void OnTimer(UINT_PTR IDEvent);

	DECLARE_MESSAGE_MAP()

	int WaitThread; // +0x60
};
